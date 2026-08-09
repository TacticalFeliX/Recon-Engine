#pragma once

#include "reconciliation/IReconciler.hpp"

namespace reconciliation::engine {

// ---------------------------------------------------------------------------
// HashReconciler — Algorithm 3 (spec Section 11)
//
// Builds an unordered_map<key, vector<const Transaction*>> for each side
// in a single O(N) / O(M) pass, then iterates the union of keys once,
// looking each up in O(1) average time. Total expected complexity
// O(N + M).
//
// WHY THIS ISN'T ALWAYS THE FASTEST CHOICE DESPITE THE BEST BIG-O:
//   - Every insertion into unordered_map does a hash computation plus a
//     bucket lookup; for small-to-medium datasets, the constant-factor
//     overhead of hashing can exceed std::sort's constant factor for the
//     same N, even though std::sort is asymptotically worse.
//   - unordered_map's per-entry memory overhead (bucket pointers, load
//     factor headroom) is larger than the contiguous storage
//     TwoPointerReconciler sorts in place, which matters for cache
//     locality at scale.
//   - Worst case (pathological hash collisions) degrades to O(N) per
//     lookup, i.e. O(N*M) overall — the same worst case as brute force,
//     though std::hash<std::string> makes this vanishingly unlikely in
//     practice for this project's key format.
// These tradeoffs are worth measuring directly (e.g. with a small
// std::chrono-based harness) rather than assumed from Big-O alone.
// ---------------------------------------------------------------------------
class HashReconciler : public IReconciler {
public:
    std::vector<ReconciliationResult> reconcile(
        const std::vector<models::Transaction>& internalTransactions,
        const std::vector<models::Transaction>& externalTransactions,
        const MatchingConfig& config) const override;

    std::string name() const override { return "hash"; }
};

} // namespace reconciliation::engine
