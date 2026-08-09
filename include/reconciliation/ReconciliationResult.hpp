#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include "models/Transaction.hpp"
#include "reconciliation/ReconciliationStatus.hpp"

namespace reconciliation::engine {

// ---------------------------------------------------------------------------
// ReconciliationResult
//
// One row per outcome. Both transaction fields are std::optional because
// MISSING_EXTERNAL results have no external record and MISSING_INTERNAL
// results have no internal record (mirrors the nullable FKs on
// reconciliation_results in sql/schema.sql).
//
// Storing full Transaction copies here (not pointers/references into the
// original vectors) is a deliberate simplicity-over-micro-optimization
// choice: results typically number in the same order of magnitude as the
// input data, get written to the database and a CSV report, and then go
// out of scope — holding a reference into vectors that might be reordered
// or reallocated by different algorithms (e.g. TwoPointerReconciler sorts
// its working copies) would be a dangling-reference risk for a marginal
// memory saving. See docs/PERFORMANCE.md (added later) for the measured
// cost of this choice at scale.
// ---------------------------------------------------------------------------
struct ReconciliationResult {
    std::string transactionId; // business key, always populated from whichever side exists
    std::optional<models::Transaction> internalTransaction;
    std::optional<models::Transaction> externalTransaction;
    ReconciliationStatus status = ReconciliationStatus::Matched;
    int64_t amountDifferenceCents = 0;
    int64_t taxDifferenceCents = 0;
    std::string mismatchDetails; // human-readable, e.g. "amount differs by 500 cents; date differs (2026-07-01 vs 2026-07-02)"
};

} // namespace reconciliation::engine
