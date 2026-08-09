#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "database/DatabaseConnection.hpp"
#include "reconciliation/ReconciliationResult.hpp"

namespace reconciliation::database {

// Flat, read-only projection of one reconciliation_results row, used by
// the `reconcile report` CLI command. Deliberately not the full
// engine::ReconciliationResult (which carries full Transaction copies) —
// the report command only needs to display these fields, and
// reconstructing full Transaction objects from a report query would be
// wasted work.
struct ResultRow {
    std::string transactionId;
    std::string status;
    int64_t amountDifferenceCents = 0;
    int64_t taxDifferenceCents = 0;
    std::string mismatchDetails;
};

// ---------------------------------------------------------------------------
// ReconciliationResultRepository
//
// Owns all SQL touching reconciliation_results. Each ReconciliationResult
// produced by any IReconciler implementation (Module 4) becomes one row
// here, scoped to the run that produced it.
//
// FOREIGN KEYS: internal_transaction_id / external_transaction_id are
// populated from Transaction::dbId (see models/Transaction.hpp) — which
// requires that the Transaction objects passed into reconciliation came
// from TransactionRepository::findAll() (which populates dbId), not
// freshly-parsed CSV rows. This is enforced by convention (documented
// here and at the main.cpp call site) rather than by the type system;
// making it a compile-time guarantee would need a separate
// "PersistedTransaction" type, which was judged not worth the added
// type-surface for this project's scope.
// ---------------------------------------------------------------------------
class ReconciliationResultRepository {
public:
    explicit ReconciliationResultRepository(DatabaseConnection& connection);

    // Inserts every result under the given run, in one transaction —
    // same atomicity reasoning as TransactionRepository::insertBatch:
    // a reconciliation run's results should never be partially visible.
    void insertResults(int64_t runId, const std::vector<engine::ReconciliationResult>& results);

    // Reads back every result row for a run, ordered by transaction_id —
    // used by `reconcile report --run N`.
    std::vector<ResultRow> findByRun(int64_t runId);

private:
    DatabaseConnection& connection_;
};

} // namespace reconciliation::database
