#pragma once

#include <vector>
#include <string>
#include "models/Transaction.hpp"
#include "reconciliation/ReconciliationResult.hpp"
#include "reconciliation/MatchingConfig.hpp"

namespace reconciliation::engine {

// ---------------------------------------------------------------------------
// IReconciler
//
// Common interface for all three algorithms (spec Section 11). This is what
// lets Module 6's CLI do `reconcile run --algorithm hash` vs
// `--algorithm two-pointer` by swapping which concrete class it
// instantiates, and what makes it possible to run the exact same input
// through all three and compare wall-clock time directly.
// ---------------------------------------------------------------------------
class IReconciler {
public:
    virtual ~IReconciler() = default;

    virtual std::vector<ReconciliationResult> reconcile(
        const std::vector<models::Transaction>& internalTransactions,
        const std::vector<models::Transaction>& externalTransactions,
        const MatchingConfig& config) const = 0;

    // Short machine-readable name, used for logging and as the CLI's
    // --algorithm value (and matches the reconciliation_algorithm SQL enum
    // in sql/schema.sql).
    virtual std::string name() const = 0;
};

} // namespace reconciliation::engine
