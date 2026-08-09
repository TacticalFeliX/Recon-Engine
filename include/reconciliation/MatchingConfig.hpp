#pragma once

#include <cstdint>

namespace reconciliation::engine {

// ---------------------------------------------------------------------------
// MatchingStrategy / MatchingConfig
//
// Spec Section 12 asks for configurable matching rules rather than a single
// hard-coded key. Two strategies are supported:
//
//   ExactId       — key = transaction_id. Simple, and correct when both
//                   source systems agree on a shared transaction
//                   identifier (the common case this project models).
//
//   CompositeKey  — key = vendor_id + invoice_number + transaction_date.
//                   Useful when the two systems DON'T share a common
//                   transaction_id (e.g. one side is a bank statement with
//                   its own reference numbers) but do agree on business
//                   facts like "which vendor, which invoice, which date".
//
// amountToleranceCents implements the tolerance-based comparison from
// spec Section 12 (e.g. treat a 1-2 cent difference as immaterial rather than
// flagging AMOUNT_MISMATCH for what's likely a rounding artifact upstream).
// Applied identically to both amount and tax comparisons.
// ---------------------------------------------------------------------------
enum class MatchingStrategy {
    ExactId,
    CompositeKey
};

struct MatchingConfig {
    MatchingStrategy strategy = MatchingStrategy::ExactId;
    int64_t amountToleranceCents = 0;
};

} // namespace reconciliation::engine
