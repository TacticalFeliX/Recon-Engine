#pragma once

#include <string>
#include <cstdint>
#include "database/DatabaseConnection.hpp"

namespace reconciliation::database {

// Statistics captured for a single reconciliation run. Field names mirror
// reconciliation_runs columns directly (see sql/schema.sql) — this struct
// exists so callers build up counts in one place and hand them to
// completeRun() atomically, rather than making several small UPDATE calls.
struct RunStats {
    int recordsInternal = 0;
    int recordsExternal = 0;
    int matchedCount = 0;
    int mismatchedCount = 0;      // sum of AMOUNT/TAX/DATE/VENDOR/MULTIPLE mismatch statuses
    int missingInternalCount = 0;
    int missingExternalCount = 0;
    int duplicateCount = 0;
};

// ---------------------------------------------------------------------------
// ReconciliationRunRepository
//
// Owns all SQL touching reconciliation_runs. Every invocation of the
// reconcile algorithm gets its own row here (spec Section 17), which is what
// lets `reconcile report --run 12` (Module 6's CLI) look back at any past
// run, and what lets you compare algorithms by querying
// duration_ms across runs with different `algorithm` values on the same
// data.
// ---------------------------------------------------------------------------
class ReconciliationRunRepository {
public:
    explicit ReconciliationRunRepository(DatabaseConnection& connection);

    // Creates a run row with started_at = now() and returns its id.
    // algorithm must be one of the reconciliation_algorithm SQL enum
    // labels ("BRUTE_FORCE", "TWO_POINTER", "HASH", "SQL_JOIN").
    int64_t createRun(const std::string& algorithm,
                       const std::string& matchingStrategy,
                       int64_t amountToleranceCents);

    // Marks a run complete: sets completed_at = now(), records final
    // stats and duration, computed by the caller (main.cpp / CLI) using
    // std::chrono around the actual reconcile() call — this repository
    // does not measure time itself, keeping timing logic in one place at
    // the call site rather than duplicated across repositories.
    void completeRun(int64_t runId, const RunStats& stats, int64_t durationMs);

private:
    DatabaseConnection& connection_;
};

} // namespace reconciliation::database
