#pragma once

#include <string>
#include <cstddef>

namespace reconciliation::ingestion {

// A single row that failed validation. Kept deliberately flat and simple —
// this struct's only job is to carry enough context (which row, what the
// raw text was, why it failed) that a human can go fix the source CSV.
struct ValidationError {
    std::size_t rowNumber;
    std::string rawLine;
    std::string message;
};

} // namespace reconciliation::ingestion
