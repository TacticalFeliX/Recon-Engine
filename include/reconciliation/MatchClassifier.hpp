#pragma once

#include <string>
#include "models/Transaction.hpp"
#include "reconciliation/ReconciliationResult.hpp"
#include "reconciliation/MatchingConfig.hpp"

namespace reconciliation::engine {

// ---------------------------------------------------------------------------
// MatchClassifier
//
// This is the single place that decides "given these facts about a key,
// what's the outcome?" — deliberately factored out of all three
// algorithms (spec Section 11) so that:
//
//   1. Brute force, two-pointer, and hash matching are GUARANTEED to
//      produce identical classifications for the same input, because
//      they all call through here rather than each re-implementing the
//      mismatch rules. Any classification bug is fixed once, not three
//      times in three slightly-diverging copies.
//   2. This class alone is what a unit test needs to exercise the actual
//      business rules — it takes plain Transaction values in and returns
//      a ReconciliationResult, no algorithm, no database, no file I/O.
//
// DUPLICATE HANDLING POLICY (spec Section 14):
// When a matching key has more than one record on either side, this
// project does NOT attempt to guess which specific pair should be
// compared — picking "the first one" by list order would silently hide
// the fact that the data has a duplication problem, and picking based on
// closest-amount-match would be inventing a rule the source systems never
// agreed to. Instead, EVERY record under that key, on BOTH sides, is
// reported with status DUPLICATE — including a side that only has a
// single record under the key, since a single record can no longer be
// matched unambiguously against an ambiguous opposite side. (An earlier
// version of this policy only flagged the over-populated side and quietly
// dropped the single-record side entirely; that was caught as a genuine
// bug by cross-comparing all three algorithms' output against each other
// on the same input during development — and is exactly why all three
// algorithms route through this one class instead
// of each re-implementing the rule.)
// ---------------------------------------------------------------------------
class MatchClassifier {
public:
    // Builds the matching key for a transaction under the given strategy.
    // Used by all three algorithms to group records before classification.
    static std::string buildKey(const models::Transaction& t, MatchingStrategy strategy);

    // Classifies a single matched pair (exactly one internal + one
    // external record sharing a key). Compares amount (tolerance-aware),
    // tax (tolerance-aware), transaction date, and vendor_id; if more than
    // one field differs, status is MULTIPLE_MISMATCHES and mismatchDetails
    // lists every differing field — spec Section 13 explicitly requires not
    // losing information by reporting only the first mismatch found.
    static ReconciliationResult classifyMatch(const models::Transaction& internalTx,
                                               const models::Transaction& externalTx,
                                               const MatchingConfig& config);

    // A key present only on the internal side.
    static ReconciliationResult missingExternal(const models::Transaction& internalTx);

    // A key present only on the external side.
    static ReconciliationResult missingInternal(const models::Transaction& externalTx);

    // One record from a key that has more than one record on its own side.
    static ReconciliationResult duplicate(const models::Transaction& t);
};

} // namespace reconciliation::engine
