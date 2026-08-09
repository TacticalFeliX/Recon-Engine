#pragma once

#include <string>
#include <vector>
#include <memory>
#include "database/DatabaseConnection.hpp"
#include "reconciliation/IReconciler.hpp"
#include "reconciliation/MatchingConfig.hpp"
#include "models/Transaction.hpp"

namespace reconciliation::cli {

// ---------------------------------------------------------------------------
// Cli
//
// Implements the command surface from spec Section 18:
//   reconcile init-db
//   reconcile ingest internal|external <path>
//   reconcile run [--algorithm hash|two-pointer|brute-force] [--strategy exact-id|composite-key] [--tolerance-cents N]
//   reconcile report --run <id>
//   reconcile --help
//
// This class is intentionally a thin dispatcher: every command handler
// below is a few lines that parse arguments and then call into the
// existing, already-tested repository/ingestion/reconciliation classes
// from Modules 2-5. No new business logic lives here — the CLI's only
// job is argument parsing and wiring, which is exactly what makes it
// straightforward to demo live in an interview (spec Section 49).
// ---------------------------------------------------------------------------
class Cli {
public:
    // Returns the process exit code (0 = success), matching main()'s
    // contract directly.
    int run(int argc, char** argv);

private:
    void printHelp() const;

    int cmdInitDb(database::DatabaseConnection& conn);
    int cmdIngest(database::DatabaseConnection& conn, const std::vector<std::string>& args);
    int cmdRun(database::DatabaseConnection& conn, const std::vector<std::string>& args);
    int cmdReport(database::DatabaseConnection& conn, const std::vector<std::string>& args);

    // Shared by cmdIngest (and reused conceptually by Module 5's original
    // main.cpp logic, now consolidated here as the one place ingestion is
    // driven from).
    void ingestFile(database::DatabaseConnection& conn,
                     const std::string& path,
                     models::SourceSystem source,
                     const std::string& sourceLabel,
                     std::size_t batchSize);

    // Maps a CLI --algorithm value to a concrete reconciler instance.
    // Accepts both hyphen and underscore forms ("two-pointer" and
    // "two_pointer") since spec Section 18's examples use hyphens but the
    // SQL enum and IReconciler::name() use underscores — rather than
    // forcing the user to remember which context wants which, both are
    // accepted here.
    std::unique_ptr<engine::IReconciler> buildReconciler(const std::string& algorithmArg) const;

    // Extracts a "--flag value" pair from args; returns defaultValue if
    // the flag isn't present. Simple linear scan — the CLI never has more
    // than a handful of flags, so this doesn't need to be more clever.
    static std::string getFlag(const std::vector<std::string>& args,
                                const std::string& flag,
                                const std::string& defaultValue);
};

} // namespace reconciliation::cli
