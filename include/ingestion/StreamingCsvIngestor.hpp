#pragma once

#include <string>
#include <vector>
#include <functional>
#include "ingestion/RowValidator.hpp"
#include "ingestion/ValidationError.hpp"
#include "models/Transaction.hpp"

namespace reconciliation::ingestion {

// ---------------------------------------------------------------------------
// StreamingCsvIngestor
//
// This is "Approach B" from the project's original design comparison:
//
//     stream file -> parse batches -> bulk insert (via callback) -> release memory
//
// as opposed to "Approach A" (read entire CSV into one big vector<Transaction>
// up front). Approach A is simpler but its peak memory usage is O(file size)
// — a 10 GB CSV needs ~10 GB (plus per-Transaction overhead) of RAM before a
// single row reaches the database. Approach B's peak memory usage is
// O(batchSize), independent of file size, because completed batches are
// handed to the caller's callback (which typically inserts them into
// Postgres) and then cleared before the next batch is read. That's the
// scalability property this class exists to provide.
//
// The tradeoff: Approach B is a little more code (batching + callback
// plumbing) and, if the caller's callback is slow (e.g. an unbatched
// database round-trip per row), the overall ingestion is only as fast as
// the slowest stage — which is exactly why Module 5 revisits the *insert*
// side specifically (batched multi-row INSERT vs Postgres COPY) rather
// than assuming "streaming" alone solves performance.
//
// WHY A CALLBACK INSTEAD OF RETURNING vector<vector<Transaction>>?
//   Returning all batches would just reintroduce Approach A's memory
//   problem one level up. The callback lets the caller (typically
//   TransactionRepository::insertBatch) consume and discard each batch
//   before this class reads the next one.
// ---------------------------------------------------------------------------
class StreamingCsvIngestor {
public:
    struct IngestionSummary {
        std::size_t rowsRead = 0;    // data rows read, excluding header
        std::size_t rowsValid = 0;
        std::size_t rowsFailed = 0;
        std::vector<ValidationError> errors;
    };

    using BatchCallback = std::function<void(const std::vector<models::Transaction>&)>;

    explicit StreamingCsvIngestor(RowValidator validator, std::size_t batchSize = 10000);

    // Streams `filePath`, validating each row with the configured
    // RowValidator, and invokes `onBatch` once per full batch (and once
    // more at the end for any remaining partial batch). Throws
    // std::runtime_error if the file cannot be opened — that's a setup
    // failure (bad path/permissions), not a row-level data-quality issue,
    // so it's treated differently from validation errors.
    IngestionSummary ingest(const std::string& filePath,
                             models::SourceSystem source,
                             const BatchCallback& onBatch);

private:
    RowValidator validator_;
    std::size_t batchSize_;
};

} // namespace reconciliation::ingestion
