#pragma once

#include <string>
#include <stdexcept>

namespace reconciliation::engine {

// ---------------------------------------------------------------------------
// ReconciliationStatus
//
// The string values returned by toString() are the EXACT labels of the
// PostgreSQL enum `reconciliation_status` defined in sql/schema.sql. This
// isn't a coincidence to maintain by hand — the reporting layer
// inserts these strings directly into reconciliation_results.status, so
// keeping this C++ enum and the SQL enum in lockstep (same names, same
// literal text) means that insert is a direct string pass-through with no
// translation table to keep in sync.
// ---------------------------------------------------------------------------
enum class ReconciliationStatus {
    Matched,
    AmountMismatch,
    TaxMismatch,
    DateMismatch,
    VendorMismatch,
    MissingInternal,
    MissingExternal,
    Duplicate,
    MultipleMismatches
};

inline std::string toString(ReconciliationStatus status) {
    switch (status) {
        case ReconciliationStatus::Matched:             return "MATCHED";
        case ReconciliationStatus::AmountMismatch:       return "AMOUNT_MISMATCH";
        case ReconciliationStatus::TaxMismatch:          return "TAX_MISMATCH";
        case ReconciliationStatus::DateMismatch:         return "DATE_MISMATCH";
        case ReconciliationStatus::VendorMismatch:       return "VENDOR_MISMATCH";
        case ReconciliationStatus::MissingInternal:      return "MISSING_INTERNAL";
        case ReconciliationStatus::MissingExternal:      return "MISSING_EXTERNAL";
        case ReconciliationStatus::Duplicate:            return "DUPLICATE";
        case ReconciliationStatus::MultipleMismatches:   return "MULTIPLE_MISMATCHES";
    }
    throw std::invalid_argument("toString(ReconciliationStatus): unknown enum value");
}

} // namespace reconciliation::engine
