#pragma once

#include <string>
#include <vector>
#include "reconciliation/ReconciliationResult.hpp"

namespace reconciliation::reporting {

// ---------------------------------------------------------------------------
// CsvReportWriter
//
// Produces the human-readable CSV report from spec Section 16:
//   transaction_id,status,internal_amount,external_amount,difference
//
// COLUMN INTERPRETATION (a deliberate choice, documented since the spec's
// example used bare numbers without specifying units):
//   internal_amount / external_amount — decimal string (e.g. "125.50"),
//     matching Money::toString(), blank if that side doesn't exist for
//     this result (e.g. MISSING_EXTERNAL has no external_amount).
//   difference — signed difference in CENTS (internal - external cents
//     for the amount field specifically), matching amountDifferenceCents
//     from ReconciliationResult, so a reader can sum/filter numerically
//     without parsing decimals.
// This is a reporting artifact for humans and spreadsheets — the
// authoritative, fully-detailed record lives in reconciliation_results
// (SQL), including tax differences and free-text mismatch details, which
// this CSV intentionally omits to stay close to the spec's example shape.
// ---------------------------------------------------------------------------
class CsvReportWriter {
public:
    static void write(const std::string& path, const std::vector<engine::ReconciliationResult>& results);
};

} // namespace reconciliation::reporting
