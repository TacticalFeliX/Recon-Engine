#include "reconciliation/HashReconciler.hpp"
#include "reconciliation/MatchClassifier.hpp"

#include <unordered_map>
#include <unordered_set>

namespace reconciliation::engine {

using models::Transaction;

std::vector<ReconciliationResult> HashReconciler::reconcile(
        const std::vector<Transaction>& internalTransactions,
        const std::vector<Transaction>& externalTransactions,
        const MatchingConfig& config) const {

    // Building these maps is the O(N) / O(M) indexing pass. Using
    // vector<const Transaction*> per bucket (rather than a single
    // pointer) is what lets a key legitimately hold more than one
    // record — necessary to detect duplicates, not just first-match.
    std::unordered_map<std::string, std::vector<const Transaction*>> internalByKey;
    internalByKey.reserve(internalTransactions.size());
    for (const auto& t : internalTransactions) {
        internalByKey[MatchClassifier::buildKey(t, config.strategy)].push_back(&t);
    }

    std::unordered_map<std::string, std::vector<const Transaction*>> externalByKey;
    externalByKey.reserve(externalTransactions.size());
    for (const auto& t : externalTransactions) {
        externalByKey[MatchClassifier::buildKey(t, config.strategy)].push_back(&t);
    }

    // Union of keys from both maps, visited once each — this is the
    // O(N + M) lookup pass (each unordered_map::find/insert is O(1)
    // average).
    std::unordered_set<std::string> allKeys;
    allKeys.reserve(internalByKey.size() + externalByKey.size());
    for (const auto& [key, _] : internalByKey) allKeys.insert(key);
    for (const auto& [key, _] : externalByKey) allKeys.insert(key);

    std::vector<ReconciliationResult> results;
    results.reserve(internalTransactions.size() + externalTransactions.size());

    for (const auto& key : allKeys) {
        auto internalIt = internalByKey.find(key);
        auto externalIt = externalByKey.find(key);
        bool hasInternal = internalIt != internalByKey.end();
        bool hasExternal = externalIt != externalByKey.end();

        std::size_t internalCount = hasInternal ? internalIt->second.size() : 0;
        std::size_t externalCount = hasExternal ? externalIt->second.size() : 0;

        if (internalCount > 1 || externalCount > 1) {
            // Per the duplicate policy (see MatchClassifier.hpp): once a
            // key is ambiguous on EITHER side, every record under that key
            // on BOTH sides is reported as duplicate — including a side
            // that only has one record under this key, since it can no
            // longer be matched unambiguously against an ambiguous
            // opposite side. Reporting only the >1 side would silently
            // drop the other side's record entirely, which is a data-loss
            // bug, not a simplification.
            if (hasInternal) {
                for (const Transaction* t : internalIt->second) {
                    results.push_back(MatchClassifier::duplicate(*t));
                }
            }
            if (hasExternal) {
                for (const Transaction* t : externalIt->second) {
                    results.push_back(MatchClassifier::duplicate(*t));
                }
            }
        } else if (hasInternal && hasExternal) {
            results.push_back(MatchClassifier::classifyMatch(
                *internalIt->second.front(), *externalIt->second.front(), config));
        } else if (hasInternal) {
            results.push_back(MatchClassifier::missingExternal(*internalIt->second.front()));
        } else { // hasExternal only
            results.push_back(MatchClassifier::missingInternal(*externalIt->second.front()));
        }
    }

    return results;
}

} // namespace reconciliation::engine
