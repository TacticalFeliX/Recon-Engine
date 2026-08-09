#include "database/IngestionErrorRepository.hpp"
#include "database/DatabaseException.hpp"

#include <pqxx/pqxx>

namespace reconciliation::database {

IngestionErrorRepository::IngestionErrorRepository(DatabaseConnection& connection)
    : connection_(connection) {}

void IngestionErrorRepository::insertErrors(int64_t batchId,
                                             const std::vector<ingestion::ValidationError>& errors) {
    if (errors.empty()) {
        return;
    }

    const std::string sql =
        "INSERT INTO ingestion_errors (ingestion_batch_id, row_number, raw_line, error_message) "
        "VALUES ($1, $2, $3, $4)";

    try {
        pqxx::work txn(connection_.handle());
        for (const auto& e : errors) {
            txn.exec_params(sql, batchId, static_cast<int64_t>(e.rowNumber), e.rawLine, e.message);
        }
        txn.commit();
    } catch (const pqxx::sql_error& ex) {
        throw DatabaseException(std::string("insertErrors failed: ") + ex.what());
    }
}

} // namespace reconciliation::database
