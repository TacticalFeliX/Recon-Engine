#include "ingestion/StreamingCsvIngestor.hpp"
#include "ingestion/CsvParser.hpp"

#include <fstream>
#include <stdexcept>

namespace reconciliation::ingestion {

StreamingCsvIngestor::StreamingCsvIngestor(RowValidator validator, std::size_t batchSize)
    : validator_(std::move(validator)), batchSize_(batchSize) {
    if (batchSize_ == 0) {
        throw std::invalid_argument("StreamingCsvIngestor: batchSize must be > 0");
    }
}

StreamingCsvIngestor::IngestionSummary StreamingCsvIngestor::ingest(
        const std::string& filePath,
        models::SourceSystem source,
        const BatchCallback& onBatch) {

    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("StreamingCsvIngestor: could not open file: " + filePath);
    }

    IngestionSummary summary;
    std::vector<models::Transaction> currentBatch;
    currentBatch.reserve(batchSize_);

    std::string line;
    std::getline(file, line); // header row, discarded (not counted in rowsRead)

    std::size_t rowNumber = 1; // header is row 1
    while (std::getline(file, line)) {
        rowNumber++;
        if (line.empty()) {
            continue; // trailing blank lines are common in hand-edited CSVs; not an error
        }

        summary.rowsRead++;
        auto fields = CsvParser::splitLine(line);
        RowValidationResult validated = validator_.validateRow(fields, rowNumber, source);

        if (validated.isValid) {
            summary.rowsValid++;
            currentBatch.push_back(std::move(validated.transaction));
            if (currentBatch.size() >= batchSize_) {
                onBatch(currentBatch);
                currentBatch.clear();
                currentBatch.reserve(batchSize_);
            }
        } else {
            summary.rowsFailed++;
            summary.errors.push_back(ValidationError{rowNumber, line, validated.errorMessage});
        }
    }

    // Flush the final partial batch, if any.
    if (!currentBatch.empty()) {
        onBatch(currentBatch);
    }

    return summary;
}

} // namespace reconciliation::ingestion
