#pragma once

#include <vector>
#include <cstdint>
#include "database/DatabaseConnection.hpp"
#include "models/Transaction.hpp"

namespace reconciliation::database {

// ---------------------------------------------------------------------------
// TransactionRepository
//
// Owns all SQL touching internal_transactions / external_transactions.
// Module 2 implements straightforward per-row prepared-statement inserts
// wrapped in a single transaction — correct and atomic, but not yet the
// fastest option. Module 5 ("Bulk Insert Performance", spec §10) adds a
// COPY-based bulk loader and benchmarks it against this method directly,
// which is the point: we want a correct baseline before optimizing it.
//
// ATOMICITY: insertBatch() wraps the entire batch in one pqxx::work. If
// any row fails (e.g. a check constraint violation that somehow slipped
// past application-level validation), the whole transaction rolls back —
// per spec §45, a batch must never leave the database in a partial state
// where "25,000 of 50,000 records" silently exist with no record of the
// failure.
// ---------------------------------------------------------------------------
class TransactionRepository {
public:
    explicit TransactionRepository(DatabaseConnection& connection);

    // Inserts every transaction in `transactions` as belonging to
    // `batchId`, into internal_transactions or external_transactions
    // depending on `source`. All-or-nothing: throws DatabaseException
    // (and inserts nothing) if any row fails.
    // Returns the number of rows inserted (== transactions.size() on
    // success, since this method does not partially succeed).
    std::size_t insertBatch(models::SourceSystem source,
                             const std::vector<models::Transaction>& transactions,
                             int64_t batchId);

    // Fetches every transaction for a source, across all batches. Used by
    // the C++-side reconciliation algorithms (Module 4), which load both
    // full tables into memory and reconcile with sorting/hashing rather
    // than doing the join in SQL.
    std::vector<models::Transaction> findAll(models::SourceSystem source);

    // Total row count for a source — used by CLI status output and tests.
    std::size_t countAll(models::SourceSystem source);

private:
    DatabaseConnection& connection_;

    static std::string tableFor(models::SourceSystem source);
};

} // namespace reconciliation::database
