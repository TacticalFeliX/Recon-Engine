#include "reconciliation/BruteForceReconciler.hpp"
#include "reconciliation/MatchClassifier.hpp"

namespace reconciliation::engine {

using models::Transaction;

std::vector<ReconciliationResult> BruteForceReconciler::reconcile(
        const std::vector<Transaction>& internalTransactions,
        const std::vector<Transaction>& externalTransactions,
        const MatchingConfig& config) const {

    const std::size_t n = internalTransactions.size();
    const std::size_t m = externalTransactions.size();

    std::vector<std::string> internalKeys(n);
    std::vector<std::string> externalKeys(m);
    for (std::size_t i = 0; i < n; ++i) {
        internalKeys[i] = MatchClassifier::buildKey(internalTransactions[i], config.strategy);
    }
    for (std::size_t j = 0; j < m; ++j) {
        externalKeys[j] = MatchClassifier::buildKey(externalTransactions[j], config.strategy);
    }

    // For every internal record, count how many internal records (self
    // included) share its key, and how many external records share its
    // key -- both via naive linear scans. This nested-loop cross-counting,
    // repeated for every record on both sides, is what makes this class
    // O(n^2 + m^2 + n*m): no hash map, no sort, anywhere in this file.
    std::vector<std::size_t> internalOwnCount(n, 0);
    std::vector<std::size_t> internalCrossCount(n, 0); // matching external records
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < n; ++k) {
            if (internalKeys[k] == internalKeys[i]) internalOwnCount[i]++;
        }
        for (std::size_t j = 0; j < m; ++j) {
            if (externalKeys[j] == internalKeys[i]) internalCrossCount[i]++;
        }
    }

    std::vector<std::size_t> externalOwnCount(m, 0);
    std::vector<std::size_t> externalCrossCount(m, 0); // matching internal records
    for (std::size_t j = 0; j < m; ++j) {
        for (std::size_t k = 0; k < m; ++k) {
            if (externalKeys[k] == externalKeys[j]) externalOwnCount[j]++;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (internalKeys[i] == externalKeys[j]) externalCrossCount[j]++;
        }
    }

    std::vector<ReconciliationResult> results;
    results.reserve(n + m);

    // Internal side: duplicate (ambiguous key on either side), matched
    // (exactly one record on each side), or missing-external (no external
    // record shares this key at all).
    for (std::size_t i = 0; i < n; ++i) {
        if (internalOwnCount[i] > 1 || internalCrossCount[i] > 1) {
            results.push_back(MatchClassifier::duplicate(internalTransactions[i]));
        } else if (internalCrossCount[i] == 1) {
            // Exactly one external record shares this key -- find it via
            // linear scan (this scan, repeated per internal record, is
            // the O(n*m) matching pass).
            for (std::size_t j = 0; j < m; ++j) {
                if (externalKeys[j] == internalKeys[i]) {
                    results.push_back(MatchClassifier::classifyMatch(
                        internalTransactions[i], externalTransactions[j], config));
                    break;
                }
            }
        } else {
            results.push_back(MatchClassifier::missingExternal(internalTransactions[i]));
        }
    }

    // External side: only records whose key never appeared on the
    // internal side (missing-internal), or whose key is ambiguous on
    // either side (duplicate) -- the 1-to-1 matched case was already
    // emitted by the internal-side loop above, so it's intentionally not
    // repeated here.
    for (std::size_t j = 0; j < m; ++j) {
        if (externalOwnCount[j] > 1 || externalCrossCount[j] > 1) {
            results.push_back(MatchClassifier::duplicate(externalTransactions[j]));
        } else if (externalCrossCount[j] == 0) {
            results.push_back(MatchClassifier::missingInternal(externalTransactions[j]));
        }
        // externalCrossCount[j] == 1 and externalOwnCount[j] == 1: this is
        // the clean 1-to-1 match, already reported from the internal loop.
    }

    return results;
}

} // namespace reconciliation::engine
