#include "reconciliation/TwoPointerReconciler.hpp"
#include "reconciliation/MatchClassifier.hpp"

#include <algorithm>

namespace reconciliation::engine {

using models::Transaction;

namespace {

struct KeyedRef {
    std::string key;
    const Transaction* tx;
};

// Returns the index one past the last element sharing keyed[start].key,
// i.e. [start, end) is the run of equal-key elements. Requires `keyed` to
// already be sorted by key.
std::size_t groupEnd(const std::vector<KeyedRef>& keyed, std::size_t start) {
    std::size_t end = start + 1;
    while (end < keyed.size() && keyed[end].key == keyed[start].key) {
        end++;
    }
    return end;
}

} // namespace

std::vector<ReconciliationResult> TwoPointerReconciler::reconcile(
        const std::vector<Transaction>& internalTransactions,
        const std::vector<Transaction>& externalTransactions,
        const MatchingConfig& config) const {

    std::vector<KeyedRef> internalKeyed;
    internalKeyed.reserve(internalTransactions.size());
    for (const auto& t : internalTransactions) {
        internalKeyed.push_back({MatchClassifier::buildKey(t, config.strategy), &t});
    }
    std::vector<KeyedRef> externalKeyed;
    externalKeyed.reserve(externalTransactions.size());
    for (const auto& t : externalTransactions) {
        externalKeyed.push_back({MatchClassifier::buildKey(t, config.strategy), &t});
    }

    // The O(N log N + M log M) step. Sorting by key is what makes the
    // subsequent merge-walk a single O(N + M) linear pass instead of
    // needing to search.
    auto byKey = [](const KeyedRef& a, const KeyedRef& b) { return a.key < b.key; };
    std::sort(internalKeyed.begin(), internalKeyed.end(), byKey);
    std::sort(externalKeyed.begin(), externalKeyed.end(), byKey);

    std::vector<ReconciliationResult> results;
    results.reserve(internalKeyed.size() + externalKeyed.size());

    std::size_t i = 0, j = 0;
    const std::size_t n = internalKeyed.size(), m = externalKeyed.size();

    while (i < n || j < m) {
        bool haveInternal = i < n;
        bool haveExternal = j < m;

        if (haveInternal && (!haveExternal || internalKeyed[i].key < externalKeyed[j].key)) {
            // Internal-only key at this point in the merge.
            std::size_t end = groupEnd(internalKeyed, i);
            std::size_t groupSize = end - i;
            for (std::size_t k = i; k < end; ++k) {
                results.push_back(groupSize > 1
                    ? MatchClassifier::duplicate(*internalKeyed[k].tx)
                    : MatchClassifier::missingExternal(*internalKeyed[k].tx));
            }
            i = end;

        } else if (haveExternal && (!haveInternal || externalKeyed[j].key < internalKeyed[i].key)) {
            // External-only key.
            std::size_t end = groupEnd(externalKeyed, j);
            std::size_t groupSize = end - j;
            for (std::size_t k = j; k < end; ++k) {
                results.push_back(groupSize > 1
                    ? MatchClassifier::duplicate(*externalKeyed[k].tx)
                    : MatchClassifier::missingInternal(*externalKeyed[k].tx));
            }
            j = end;

        } else {
            // Equal keys on both sides.
            std::size_t iEnd = groupEnd(internalKeyed, i);
            std::size_t jEnd = groupEnd(externalKeyed, j);
            std::size_t internalGroupSize = iEnd - i;
            std::size_t externalGroupSize = jEnd - j;

            if (internalGroupSize > 1 || externalGroupSize > 1) {
                // See HashReconciler.cpp for the full rationale: both
                // sides' records under this key are reported as duplicate
                // once EITHER side is ambiguous, so a size-1 side under an
                // ambiguous key is never silently dropped.
                for (std::size_t k = i; k < iEnd; ++k) {
                    results.push_back(MatchClassifier::duplicate(*internalKeyed[k].tx));
                }
                for (std::size_t k = j; k < jEnd; ++k) {
                    results.push_back(MatchClassifier::duplicate(*externalKeyed[k].tx));
                }
            } else {
                results.push_back(MatchClassifier::classifyMatch(
                    *internalKeyed[i].tx, *externalKeyed[j].tx, config));
            }

            i = iEnd;
            j = jEnd;
        }
    }

    return results;
}

} // namespace reconciliation::engine
