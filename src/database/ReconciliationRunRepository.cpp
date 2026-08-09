#include "database/ReconciliationRunRepository.hpp"
#include "database/DatabaseException.hpp"

#include <pqxx/pqxx>

namespace reconciliation::database {

ReconciliationRunRepository::ReconciliationRunRepository(DatabaseConnection& connection)
    : connection_(connection) {}

int64_t ReconciliationRunRepository::createRun(const std::string& algorithm,
                                                const std::string& matchingStrategy,
                                                int64_t amountToleranceCents) {
    try {
        pqxx::work txn(connection_.handle());
        pqxx::result r = txn.exec_params(
            "INSERT INTO reconciliation_runs (algorithm, matching_strategy, amount_tolerance_cents) "
            "VALUES ($1, $2, $3) RETURNING id",
            algorithm, matchingStrategy, amountToleranceCents
        );
        txn.commit();
        return r[0][0].as<int64_t>();
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("createRun failed: ") + e.what());
    }
}

void ReconciliationRunRepository::completeRun(int64_t runId, const RunStats& stats, int64_t durationMs) {
    try {
        pqxx::work txn(connection_.handle());
        txn.exec_params(
            "UPDATE reconciliation_runs SET "
            "  records_internal = $1, records_external = $2, matched_count = $3, "
            "  mismatched_count = $4, missing_internal_count = $5, missing_external_count = $6, "
            "  duplicate_count = $7, duration_ms = $8, completed_at = now() "
            "WHERE id = $9",
            stats.recordsInternal, stats.recordsExternal, stats.matchedCount,
            stats.mismatchedCount, stats.missingInternalCount, stats.missingExternalCount,
            stats.duplicateCount, durationMs, runId
        );
        txn.commit();
    } catch (const pqxx::sql_error& e) {
        throw DatabaseException(std::string("completeRun failed: ") + e.what());
    }
}

} // namespace reconciliation::database
