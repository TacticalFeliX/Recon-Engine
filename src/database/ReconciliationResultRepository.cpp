#include "database/ReconciliationResultRepository.hpp"
#include "database/DatabaseException.hpp"
#include "reconciliation/ReconciliationStatus.hpp"

#include <pqxx/pqxx>

namespace reconciliation::database {

ReconciliationResultRepository::ReconciliationResultRepository(DatabaseConnection& connection)
    : connection_(connection) {}

void ReconciliationResultRepository::insertResults(int64_t runId,
                                                     const std::vector<engine::ReconciliationResult>& results) {
    if (results.empty()) {
        return;
    }

    const std::string sql =
        "INSERT INTO reconciliation_results "
        "  (run_id, transaction_id, internal_transaction_id, external_transaction_id, "
        "   status, amount_difference_cents, tax_difference_cents, mismatch_details) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)";

    try {
        pqxx::work txn(connection_.handle());

        for (const auto& result : results) {
            // pqxx represents "no value" for a nullable parameter as
            // std::optional<T>{} — passing a populated dbId or an empty
            // optional here maps directly to a SQL NULL for the FK column
            // when the corresponding side of the result is absent (e.g.
            // MISSING_EXTERNAL has no externalTransaction at all).
            std::optional<int64_t> internalId = result.internalTransaction
                ? result.internalTransaction->dbId : std::nullopt;
            std::optional<int64_t> externalId = result.externalTransaction
                ? result.externalTransaction->dbId : std::nullopt;

            txn.exec_params(sql,
                runId,
                result.transactionId,
                internalId,
                externalId,
                engine::toString(result.status),
                result.amountDifferenceCents,
                result.taxDifferenceCents,
                result.mismatchDetails
            );
        }

        txn.commit();
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("insertResults failed: ") + e.what());
    }
}

std::vector<ResultRow> ReconciliationResultRepository::findByRun(int64_t runId) {
    const std::string sql =
        "SELECT transaction_id, status, amount_difference_cents, tax_difference_cents, mismatch_details "
        "FROM reconciliation_results WHERE run_id = $1 ORDER BY transaction_id";

    std::vector<ResultRow> rows;
    try {
        pqxx::work txn(connection_.handle());
        pqxx::result r = txn.exec_params(sql, runId);
        txn.commit();

        rows.reserve(r.size());
        for (const auto& row : r) {
            ResultRow rr;
            rr.transactionId = row[0].as<std::string>();
            rr.status = row[1].as<std::string>();
            rr.amountDifferenceCents = row[2].is_null() ? 0 : row[2].as<int64_t>();
            rr.taxDifferenceCents = row[3].is_null() ? 0 : row[3].as<int64_t>();
            rr.mismatchDetails = row[4].is_null() ? "" : row[4].as<std::string>();
            rows.push_back(std::move(rr));
        }
        return rows;
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("findByRun failed: ") + e.what());
    }
}

} // namespace reconciliation::database
