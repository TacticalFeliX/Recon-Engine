#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include "models/Transaction.hpp"

namespace reconciliation::ingestion {

// ---------------------------------------------------------------------------
// RowValidator
//
// WHY separate from StreamingCsvIngestor?
//   This class touches no files, no database, no batching — it's a pure
//   function of (fields in) -> (Transaction or error out). That makes it
//   trivial to unit test with hand-written field vectors, without needing
//   a real CSV file on disk or a database connection. StreamingCsvIngestor is the I/O-heavy
//   orchestrator that calls this repeatedly; this class has zero
//   knowledge that a "stream" or "batch" exists.
//
// DESIGN DECISION — duplicate transaction_id is NOT a validation error.
//   Spec §7 lists "duplicate IDs" under things to detect, but §14 makes
//   clear duplicates are a real-world data condition the reconciliation
//   engine must classify (status DUPLICATE), not data to discard at
//   ingestion. Rejecting duplicates here would destroy the exact signal
//   Rejecting duplicates here would destroy the exact signal
//   reconciliation needs to detect. So RowValidator validates each row
//   independently and duplicate detection happens later, at query time
//   (`GROUP BY transaction_id HAVING COUNT(*) > 1`, already demonstrated
//   against the live database in Module 2) or during reconciliation
//   itself (Module 4).
//
// DESIGN DECISION — supported currencies is a fixed, configurable set.
//   A real system would likely load this from a currency reference table.
//   For this project's scope, a fixed set passed into the constructor is
//   sufficient to demonstrate the validation pattern, and keeps this
//   class free of a database dependency (it stays pure).
// ---------------------------------------------------------------------------
struct RowValidationResult {
    bool isValid = false;
    models::Transaction transaction;   // populated only if isValid
    std::string errorMessage;          // populated only if !isValid
};

class RowValidator {
public:
    explicit RowValidator(std::unordered_set<std::string> supportedCurrencies = {"INR", "USD", "EUR", "GBP"});

    static constexpr std::size_t EXPECTED_FIELD_COUNT = 7;

    // Validates one already-split CSV row. rowNumber is 1-based and
    // includes the header row (so the first data row is row 2) — this
    // matches what a human sees when opening the CSV in a text editor,
    // which matters when error messages reference it.
    RowValidationResult validateRow(const std::vector<std::string>& fields,
                                     std::size_t rowNumber,
                                     models::SourceSystem source) const;

private:
    std::unordered_set<std::string> supportedCurrencies_;
};

} // namespace reconciliation::ingestion
