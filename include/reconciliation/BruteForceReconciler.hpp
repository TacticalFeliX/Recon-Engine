#pragma once

#include "reconciliation/IReconciler.hpp"

namespace reconciliation::engine {

// ---------------------------------------------------------------------------
// BruteForceReconciler — Algorithm 1 (spec Section 11)
//
// Deliberately the "do the simplest possible thing" baseline: no sorting,
// no hashing. For every internal record, linearly scan the external
// records for a key match (and vice versa for duplicate detection within
// a single side). This makes its correctness easy to eyeball-verify and
// gives the other two algorithms something concrete to be benchmarked
// against.
//
// COMPLEXITY: O(N * M) for the internal-vs-external matching pass, plus
// O(N^2) and O(M^2) for the within-side duplicate-detection passes (also
// done by naive nested scanning, in keeping with "brute force" meaning
// no hashing/sorting anywhere in this class). At 100k+ records per side
// this becomes impractically slow — that's the point being demonstrated,
// not a flaw to fix here.
// ---------------------------------------------------------------------------
class BruteForceReconciler : public IReconciler {
public:
    std::vector<ReconciliationResult> reconcile(
        const std::vector<models::Transaction>& internalTransactions,
        const std::vector<models::Transaction>& externalTransactions,
        const MatchingConfig& config) const override;

    std::string name() const override { return "brute_force"; }
};

} // namespace reconciliation::engine
