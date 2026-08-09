#include "database/IngestionBatchRepository.hpp"
#include "database/DatabaseException.hpp"

#include <pqxx/pqxx>

namespace reconciliation::database {

std::string IngestionBatchRepository::sourceToString(models::SourceSystem source) {
    return source == models::SourceSystem::Internal ? "INTERNAL" : "EXTERNAL";
}

IngestionBatchRepository::IngestionBatchRepository(DatabaseConnection& connection)
    : connection_(connection) {}

std::optional<int64_t> IngestionBatchRepository::findExistingBatch(models::SourceSystem source,
                                                                     const std::string& fileHash) {
    try {
        pqxx::work txn(connection_.handle());
        pqxx::result r = txn.exec_params(
            "SELECT id FROM ingestion_batches WHERE source_system = $1 AND file_hash = $2",
            sourceToString(source), fileHash
        );
        txn.commit();

        if (r.empty()) {
            return std::nullopt;
        }
        return r[0][0].as<int64_t>();
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("findExistingBatch failed: ") + e.what());
    }
}

int64_t IngestionBatchRepository::createBatch(models::SourceSystem source,
                                               const std::string& fileName,
                                               const std::string& fileHash) {
    try {
        pqxx::work txn(connection_.handle());
        pqxx::result r = txn.exec_params(
            "INSERT INTO ingestion_batches (source_system, file_name, file_hash) "
            "VALUES ($1, $2, $3) RETURNING id",
            sourceToString(source), fileName, fileHash
        );
        txn.commit();
        return r[0][0].as<int64_t>();
    } catch (const pqxx::unique_violation&) {
        // Defensive: this fires if two ingestion processes race between
        // findExistingBatch() and createBatch() for the same file. The
        // caller asked us to create a batch that already exists.
        throw DuplicateIngestionException(fileHash);
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("createBatch failed: ") + e.what());
    }
}

void IngestionBatchRepository::completeBatch(int64_t batchId, int recordsRead,
                                              int recordsValid, int recordsFailed) {
    try {
        pqxx::work txn(connection_.handle());
        txn.exec_params(
            "UPDATE ingestion_batches "
            "SET records_read = $1, records_valid = $2, records_failed = $3, completed_at = now() "
            "WHERE id = $4",
            recordsRead, recordsValid, recordsFailed, batchId
        );
        txn.commit();
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("completeBatch failed: ") + e.what());
    }
}

} // namespace reconciliation::database
