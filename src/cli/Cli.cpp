#include "cli/Cli.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cctype>

#include "database/ConfigLoader.hpp"
#include "database/DatabaseConfig.hpp"
#include "database/DatabaseException.hpp"
#include "database/SchemaManager.hpp"
#include "database/IngestionBatchRepository.hpp"
#include "database/IngestionErrorRepository.hpp"
#include "database/TransactionRepository.hpp"
#include "database/ReconciliationRunRepository.hpp"
#include "database/ReconciliationResultRepository.hpp"
#include "ingestion/RowValidator.hpp"
#include "ingestion/StreamingCsvIngestor.hpp"
#include "reconciliation/BruteForceReconciler.hpp"
#include "reconciliation/TwoPointerReconciler.hpp"
#include "reconciliation/HashReconciler.hpp"
#include "reconciliation/ReconciliationStatus.hpp"
#include "reporting/CsvReportWriter.hpp"
#include "utils/Logger.hpp"
#include "utils/Sha256.hpp"

namespace reconciliation::cli {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string readWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open file: " + path);
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

// Converts an IReconciler::name() value ("two_pointer") into the
// reconciliation_algorithm SQL enum label ("TWO_POINTER") — the two only
// differ by case, since both were deliberately chosen with matching
// underscore-separated words (see reconciliation/IReconciler.hpp).
std::string algorithmNameToSqlEnum(const std::string& name) {
    std::string result = name;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

} // namespace

std::string Cli::getFlag(const std::vector<std::string>& args, const std::string& flag,
                          const std::string& defaultValue) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == flag && i + 1 < args.size()) {
            return args[i + 1];
        }
    }
    return defaultValue;
}

void Cli::printHelp() const {
    std::cout <<
        "High-Speed Financial Reconciliation Engine\n\n"
        "Usage:\n"
        "  reconcile init-db\n"
        "  reconcile ingest internal <path.csv>\n"
        "  reconcile ingest external <path.csv>\n"
        "  reconcile run [--algorithm hash|two-pointer|brute-force] [--strategy exact-id|composite-key] [--tolerance-cents N]\n"
        "  reconcile report --run <id>\n"
        "  reconcile --help\n\n"
        "Examples:\n"
        "  reconcile init-db\n"
        "  reconcile ingest internal data/sample/internal_sample.csv\n"
        "  reconcile ingest external data/sample/external_sample.csv\n"
        "  reconcile run --algorithm hash\n"
        "  reconcile report --run 1\n";
}

std::unique_ptr<engine::IReconciler> Cli::buildReconciler(const std::string& algorithmArg) const {
    std::string normalized = toLower(algorithmArg);
    std::replace(normalized.begin(), normalized.end(), '-', '_');

    if (normalized == "hash") return std::make_unique<engine::HashReconciler>();
    if (normalized == "two_pointer") return std::make_unique<engine::TwoPointerReconciler>();
    if (normalized == "brute_force") return std::make_unique<engine::BruteForceReconciler>();

    throw std::invalid_argument("Unknown --algorithm '" + algorithmArg +
                                 "'. Expected hash, two-pointer, or brute-force.");
}

void Cli::ingestFile(database::DatabaseConnection& conn, const std::string& path,
                      models::SourceSystem source, const std::string& sourceLabel,
                      std::size_t batchSize) {
    database::IngestionBatchRepository batchRepo(conn);
    database::TransactionRepository txRepo(conn);
    database::IngestionErrorRepository errorRepo(conn);
    ingestion::RowValidator validator;

    std::string fileHash = utils::Sha256::hash(readWholeFile(path));

    if (auto existing = batchRepo.findExistingBatch(source, fileHash)) {
        utils::Logger::info(sourceLabel + ": '" + path + "' already ingested as batch " +
                             std::to_string(*existing) + " (hash match) -- skipping");
        return;
    }

    int64_t batchId = batchRepo.createBatch(source, path, fileHash);
    ingestion::StreamingCsvIngestor ingestor(validator, batchSize);

    std::size_t totalInserted = 0;
    auto onBatch = [&](const std::vector<models::Transaction>& batch) {
        totalInserted += txRepo.insertBatch(source, batch, batchId);
    };

    auto summary = ingestor.ingest(path, source, onBatch);
    if (!summary.errors.empty()) {
        errorRepo.insertErrors(batchId, summary.errors);
        for (const auto& err : summary.errors) {
            utils::Logger::warn(sourceLabel + ": row " + std::to_string(err.rowNumber) +
                                 " rejected: " + err.message);
        }
    }

    batchRepo.completeBatch(batchId, static_cast<int>(summary.rowsRead),
                             static_cast<int>(summary.rowsValid),
                             static_cast<int>(summary.rowsFailed));

    utils::Logger::info(sourceLabel + ": " + std::to_string(summary.rowsRead) + " rows read, " +
                         std::to_string(summary.rowsValid) + " valid, " +
                         std::to_string(summary.rowsFailed) + " rejected (batch " +
                         std::to_string(batchId) + ", inserted " + std::to_string(totalInserted) + ")");
}

int Cli::cmdInitDb(database::DatabaseConnection& conn) {
    utils::Logger::info("Applying sql/schema.sql and sql/indexes.sql...");
    database::SchemaManager::initializeSchema(conn);
    utils::Logger::info("Database schema initialized successfully.");
    return 0;
}

int Cli::cmdIngest(database::DatabaseConnection& conn, const std::vector<std::string>& args) {
    // args[0] == "ingest"; args[1] should be internal/external; args[2] is the path.
    if (args.size() < 3) {
        std::cerr << "Usage: reconcile ingest internal|external <path.csv>\n";
        return 1;
    }

    std::string sourceArg = toLower(args[1]);
    models::SourceSystem source;
    if (sourceArg == "internal") source = models::SourceSystem::Internal;
    else if (sourceArg == "external") source = models::SourceSystem::External;
    else {
        std::cerr << "Unknown source '" << args[1] << "'. Expected 'internal' or 'external'.\n";
        return 1;
    }

    std::string path = args[2];

    database::ConfigLoader appConfig = database::ConfigLoader::loadFromFile("config/config.ini");
    std::size_t batchSize = static_cast<std::size_t>(std::stoul(appConfig.get("batch_size", "10000")));

    ingestFile(conn, path, source, sourceArg, batchSize);
    return 0;
}

int Cli::cmdRun(database::DatabaseConnection& conn, const std::vector<std::string>& args) {
    std::string algorithmArg = getFlag(args, "--algorithm", "hash");
    std::string strategyArg = getFlag(args, "--strategy", "exact-id");
    std::string toleranceArg = getFlag(args, "--tolerance-cents", "0");

    auto reconciler = buildReconciler(algorithmArg);

    engine::MatchingConfig matchConfig;
    matchConfig.strategy = (toLower(strategyArg) == "composite-key" || toLower(strategyArg) == "composite_key")
        ? engine::MatchingStrategy::CompositeKey
        : engine::MatchingStrategy::ExactId;
    matchConfig.amountToleranceCents = std::stoll(toleranceArg);

    database::TransactionRepository txRepo(conn);
    std::vector<models::Transaction> internalTx = txRepo.findAll(models::SourceSystem::Internal);
    std::vector<models::Transaction> externalTx = txRepo.findAll(models::SourceSystem::External);

    utils::Logger::info("Loaded " + std::to_string(internalTx.size()) + " internal, " +
                         std::to_string(externalTx.size()) + " external records " +
                         "(algorithm=" + reconciler->name() + ")");

    database::ReconciliationRunRepository runRepo(conn);
    int64_t runId = runRepo.createRun(algorithmNameToSqlEnum(reconciler->name()),
                                       strategyArg, matchConfig.amountToleranceCents);

    auto startTime = std::chrono::steady_clock::now();
    std::vector<engine::ReconciliationResult> results = reconciler->reconcile(internalTx, externalTx, matchConfig);
    auto endTime = std::chrono::steady_clock::now();
    int64_t durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    database::ReconciliationResultRepository resultRepo(conn);
    resultRepo.insertResults(runId, results);

    database::RunStats stats;
    stats.recordsInternal = static_cast<int>(internalTx.size());
    stats.recordsExternal = static_cast<int>(externalTx.size());
    for (const auto& r : results) {
        switch (r.status) {
            case engine::ReconciliationStatus::Matched: stats.matchedCount++; break;
            case engine::ReconciliationStatus::MissingInternal: stats.missingInternalCount++; break;
            case engine::ReconciliationStatus::MissingExternal: stats.missingExternalCount++; break;
            case engine::ReconciliationStatus::Duplicate: stats.duplicateCount++; break;
            default: stats.mismatchedCount++; break;
        }
    }
    runRepo.completeRun(runId, stats, durationMs);

    reporting::CsvReportWriter::write("data/output/reconciliation_report.csv", results);

    std::cout << "Run " << runId << " complete (" << reconciler->name() << ") in " << durationMs << " ms\n"
              << "  matched:           " << stats.matchedCount << "\n"
              << "  mismatched:        " << stats.mismatchedCount << "\n"
              << "  missing_internal:  " << stats.missingInternalCount << "\n"
              << "  missing_external:  " << stats.missingExternalCount << "\n"
              << "  duplicate:         " << stats.duplicateCount << "\n"
              << "Report: data/output/reconciliation_report.csv\n";

    return 0;
}

int Cli::cmdReport(database::DatabaseConnection& conn, const std::vector<std::string>& args) {
    std::string runIdArg = getFlag(args, "--run", "");
    if (runIdArg.empty()) {
        std::cerr << "Usage: reconcile report --run <id>\n";
        return 1;
    }

    int64_t runId = std::stoll(runIdArg);
    database::ReconciliationResultRepository resultRepo(conn);
    std::vector<database::ResultRow> rows = resultRepo.findByRun(runId);

    if (rows.empty()) {
        std::cout << "No results found for run " << runId
                  << " (either the run doesn't exist, or it produced zero results)\n";
        return 0;
    }

    std::cout << "Run " << runId << " -- " << rows.size() << " results:\n\n";
    for (const auto& row : rows) {
        std::cout << row.transactionId << "\t" << row.status;
        if (row.status != "MATCHED") {
            std::cout << "\t" << row.mismatchDetails;
        }
        std::cout << "\n";
    }
    return 0;
}

int Cli::run(int argc, char** argv) {
    std::vector<std::string> allArgs(argv + 1, argv + argc);

    if (allArgs.empty() || allArgs[0] == "--help" || allArgs[0] == "-h" || allArgs[0] == "help") {
        printHelp();
        return 0;
    }

    const std::string& command = allArgs[0];

    // `--help` is the only command that doesn't need a database
    // connection -- everything else does, so the connection is
    // established once here rather than duplicated in each handler.
    try {
        database::ConfigLoader dbEnv = database::ConfigLoader::loadFromFile(".env");
        database::DatabaseConfig dbConfig = database::DatabaseConfig::fromConfigLoader(dbEnv);
        database::DatabaseConnection conn(dbConfig);

        if (command == "init-db") return cmdInitDb(conn);
        if (command == "ingest") return cmdIngest(conn, allArgs);
        if (command == "run") return cmdRun(conn, allArgs);
        if (command == "report") return cmdReport(conn, allArgs);

        std::cerr << "Unknown command '" << command << "'\n\n";
        printHelp();
        return 1;

    } catch (const database::DatabaseException& e) {
        utils::Logger::error(std::string("Database error: ") + e.what());
        return 1;
    } catch (const std::exception& e) {
        utils::Logger::error(std::string("Error: ") + e.what());
        return 1;
    }
}

} // namespace reconciliation::cli
