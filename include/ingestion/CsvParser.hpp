#pragma once

#include <string>
#include <vector>

namespace reconciliation::ingestion {

// ---------------------------------------------------------------------------
// CsvParser
//
// MODULE 1 SCOPE: this is intentionally a minimal skeleton. It only proves
// out the header/source split and CMake wiring. The real implementation —
// streaming line-by-line reads, batch parsing, malformed-row tracking,
// configurable batch size — is built in Module 3 (Ingestion), per the
// phased build plan.
//
// splitLine() is implemented now (not a placeholder) because it's a small,
// self-contained utility that later parsing logic will depend on, and it's
// easy to unit test in isolation.
// ---------------------------------------------------------------------------
class CsvParser {
public:
    // Splits a single CSV line into fields. Handles simple comma-separated
    // values. Quoted-field handling (commas inside quotes, escaped quotes)
    // is added in Module 3 when we ingest realistic CSV data that may
    // contain them (e.g. vendor names with commas).
    static std::vector<std::string> splitLine(const std::string& line);
};

} // namespace reconciliation::ingestion
