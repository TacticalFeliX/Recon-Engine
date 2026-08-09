#pragma once

#include <vector>
#include <cstdint>
#include "database/DatabaseConnection.hpp"
#include "ingestion/ValidationError.hpp"

namespace reconciliation::database {

// Owns all SQL touching ingestion_errors. Kept separate from
// TransactionRepository since it writes a different table for a different
// purpose (audit/diagnostics vs actual business data) — same
// single-responsibility reasoning as IngestionBatchRepository.
class IngestionErrorRepository {
public:
    explicit IngestionErrorRepository(DatabaseConnection& connection);

    // Persists every error under the given batch, in one transaction.
    // No-op if `errors` is empty (a clean file shouldn't touch this table
    // at all).
    void insertErrors(int64_t batchId, const std::vector<ingestion::ValidationError>& errors);

private:
    DatabaseConnection& connection_;
};

} // namespace reconciliation::database
