#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include "models/Money.hpp"
#include "models/Date.hpp"

namespace reconciliation::models {

// Which of the two data sources a Transaction came from. Stored alongside
// every record so downstream code (reconciliation, reporting, auditing)
// never has to guess based on which table it was queried from.
enum class SourceSystem {
    Internal,
    External
};

// ---------------------------------------------------------------------------
// Transaction
//
// This is the in-memory representation of a single row from either CSV
// source, AFTER parsing and type conversion but BEFORE validation has
// necessarily confirmed every field. Validation (Module 3) produces a
// ValidationResult alongside a Transaction rather than throwing, because a
// single malformed row must not abort processing of the other 999,999
// rows in a large file (see §7 of the spec).
//
// Field type choices (locked in for the whole project):
//   transactionId, invoiceNumber, vendorId, currency -> std::string
//     These are real-world alphanumeric business identifiers, not numbers.
//   amount, taxAmount -> Money (int64_t cents wrapper)
//     Never float. See Money.hpp for the full rationale.
//   transactionDate -> Date
//     Validated, comparable, compact. See Date.hpp for rationale.
// ---------------------------------------------------------------------------
struct Transaction {
    std::string transactionId;
    std::string invoiceNumber;
    std::string vendorId;
    Date transactionDate;
    Money amount;
    Money taxAmount;
    std::string currency;
    SourceSystem source = SourceSystem::Internal;

    // Which source row (1-based, within its file) this came from. Used for
    // error messages and audit trails ("row 4821 of internal.csv failed
    // validation: ...").
    std::size_t sourceRowNumber = 0;

    // The database surrogate primary key (internal_transactions.id or
    // external_transactions.id) once this Transaction has been read back
    // from Postgres via TransactionRepository::findAll(). std::nullopt for
    // a Transaction that only exists in memory (e.g. freshly parsed from
    // a CSV row, not yet inserted). This is needed by
    // ReconciliationResultRepository (Module 5) to populate the nullable
    // internal_transaction_id / external_transaction_id foreign keys on
    // reconciliation_results — those FKs reference the surrogate id, not
    // the business transaction_id, per the schema design rationale in
    // sql/schema.sql.
    std::optional<int64_t> dbId;
};

} // namespace reconciliation::models
