#pragma once

#include "reconciliation/IReconciler.hpp"

namespace reconciliation::engine {

// ---------------------------------------------------------------------------
// TwoPointerReconciler — Algorithm 2 (spec Section 11)
//
// Sorts both sides by matching key, then walks both sorted sequences with
// two advancing pointers, comparing keys at each step (like the merge
// step of merge-sort). Records with equal keys form a "group"; a group of
// size 1 on both sides is a candidate match, a group of size >1 on either
// side is a duplicate, and a group present on only one side is missing
// from the other.
//
// COMPLEXITY: O(N log N + M log M) for the two sorts, then O(N + M) for
// the single linear merge-walk — the sort dominates. This is the
// "in-between" option: no hash table overhead (see HashReconciler for
// that tradeoff discussion), but strictly better than brute force's
// O(N * M) for any reasonably-sized dataset. It's also the natural choice
// when the data needs to be sorted for other reasons anyway (e.g.
// producing an ordered report), since the sort isn't "wasted" work.
// ---------------------------------------------------------------------------
class TwoPointerReconciler : public IReconciler {
public:
    std::vector<ReconciliationResult> reconcile(
        const std::vector<models::Transaction>& internalTransactions,
        const std::vector<models::Transaction>& externalTransactions,
        const MatchingConfig& config) const override;

    std::string name() const override { return "two_pointer"; }
};

} // namespace reconciliation::engine
