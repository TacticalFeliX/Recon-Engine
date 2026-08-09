#pragma once

#include <string>
#include <cstdint>
#include <optional>
#include "database/DatabaseConnection.hpp"
#include "models/Transaction.hpp"

namespace reconciliation::database {

// ---------------------------------------------------------------------------
// IngestionBatchRepository
//
// Owns all SQL touching ingestion_batches. This table is the mechanism
// behind two spec requirements that are easy to bolt on later but much
// cleaner to design in from the start:
//
//   - Idempotency (§46): findExistingBatch() lets the ingestion CLI command
//     check "have I already ingested a file with this exact hash, from
//     this source?" before doing any work, and refuse (or skip) instead of
//     silently double-inserting every row.
//   - Auditability (§47): every transaction row's ingestion_batch_id
//     traces back to exactly which file, ingested when, produced it.
// ---------------------------------------------------------------------------
class IngestionBatchRepository {
public:
    explicit IngestionBatchRepository(DatabaseConnection& connection);

    // Returns the existing batch id if a batch with this (source, fileHash)
    // was already ingested, or std::nullopt if this file is new. Callers
    // use this BEFORE inserting any transaction rows.
    std::optional<int64_t> findExistingBatch(models::SourceSystem source, const std::string& fileHash);

    // Creates a new batch row (started_at = now()) and returns its id.
    // Does NOT check for duplicates itself — callers must call
    // findExistingBatch() first, per the pattern demonstrated in
    // src/main.cpp. Keeping the check and the create as two explicit
    // steps (rather than one "createOrGetBatch") makes the idempotency
    // decision visible at the call site instead of hidden inside the
    // repository.
    int64_t createBatch(models::SourceSystem source, const std::string& fileName, const std::string& fileHash);

    // Marks a batch as finished and records final counts. Called after
    // all rows from the file have been processed (successfully or not).
    void completeBatch(int64_t batchId, int recordsRead, int recordsValid, int recordsFailed);

private:
    DatabaseConnection& connection_;

    static std::string sourceToString(models::SourceSystem source);
};

} // namespace reconciliation::database
