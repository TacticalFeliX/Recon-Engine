#include "reconciliation/MatchClassifier.hpp"

#include <sstream>

namespace reconciliation::engine {

using models::Transaction;

std::string MatchClassifier::buildKey(const Transaction& t, MatchingStrategy strategy) {
    if (strategy == MatchingStrategy::ExactId) {
        return t.transactionId;
    }
    // CompositeKey: vendor_id + invoice_number + transaction_date, '|'-separated.
    // '|' is not a valid character in any of these three source fields
    // under this project's CSV format, so it's a safe, simple delimiter —
    // no escaping needed, unlike e.g. joining on a character that could
    // legitimately appear in a vendor ID.
    return t.vendorId + "|" + t.invoiceNumber + "|" + t.transactionDate.toString();
}

ReconciliationResult MatchClassifier::classifyMatch(const Transaction& internalTx,
                                                      const Transaction& externalTx,
                                                      const MatchingConfig& config) {
    ReconciliationResult result;
    result.transactionId = internalTx.transactionId;
    result.internalTransaction = internalTx;
    result.externalTransaction = externalTx;

    int64_t amountDiff = internalTx.amount.absDifferenceCents(externalTx.amount);
    int64_t taxDiff = internalTx.taxAmount.absDifferenceCents(externalTx.taxAmount);
    result.amountDifferenceCents = amountDiff;
    result.taxDifferenceCents = taxDiff;

    bool amountMismatch = amountDiff > config.amountToleranceCents;
    bool taxMismatch = taxDiff > config.amountToleranceCents;
    bool dateMismatch = internalTx.transactionDate != externalTx.transactionDate;
    bool vendorMismatch = internalTx.vendorId != externalTx.vendorId;

    int mismatchCount = (amountMismatch ? 1 : 0) + (taxMismatch ? 1 : 0) +
                         (dateMismatch ? 1 : 0) + (vendorMismatch ? 1 : 0);

    std::ostringstream details;
    if (amountMismatch) {
        details << "amount differs by " << amountDiff << " cents "
                << "(internal=" << internalTx.amount.toString()
                << ", external=" << externalTx.amount.toString() << "); ";
    }
    if (taxMismatch) {
        details << "tax differs by " << taxDiff << " cents "
                << "(internal=" << internalTx.taxAmount.toString()
                << ", external=" << externalTx.taxAmount.toString() << "); ";
    }
    if (dateMismatch) {
        details << "date differs (internal=" << internalTx.transactionDate.toString()
                << ", external=" << externalTx.transactionDate.toString() << "); ";
    }
    if (vendorMismatch) {
        details << "vendor differs (internal=" << internalTx.vendorId
                << ", external=" << externalTx.vendorId << "); ";
    }

    if (mismatchCount == 0) {
        result.status = ReconciliationStatus::Matched;
    } else if (mismatchCount > 1) {
        result.status = ReconciliationStatus::MultipleMismatches;
        result.mismatchDetails = details.str();
    } else if (amountMismatch) {
        result.status = ReconciliationStatus::AmountMismatch;
        result.mismatchDetails = details.str();
    } else if (taxMismatch) {
        result.status = ReconciliationStatus::TaxMismatch;
        result.mismatchDetails = details.str();
    } else if (dateMismatch) {
        result.status = ReconciliationStatus::DateMismatch;
        result.mismatchDetails = details.str();
    } else { // vendorMismatch
        result.status = ReconciliationStatus::VendorMismatch;
        result.mismatchDetails = details.str();
    }

    return result;
}

ReconciliationResult MatchClassifier::missingExternal(const Transaction& internalTx) {
    ReconciliationResult result;
    result.transactionId = internalTx.transactionId;
    result.internalTransaction = internalTx;
    result.status = ReconciliationStatus::MissingExternal;
    result.amountDifferenceCents = internalTx.amount.cents();
    result.mismatchDetails = "present in internal source only";
    return result;
}

ReconciliationResult MatchClassifier::missingInternal(const Transaction& externalTx) {
    ReconciliationResult result;
    result.transactionId = externalTx.transactionId;
    result.externalTransaction = externalTx;
    result.status = ReconciliationStatus::MissingInternal;
    result.amountDifferenceCents = externalTx.amount.cents();
    result.mismatchDetails = "present in external source only";
    return result;
}

ReconciliationResult MatchClassifier::duplicate(const Transaction& t) {
    ReconciliationResult result;
    result.transactionId = t.transactionId;
    if (t.source == models::SourceSystem::Internal) {
        result.internalTransaction = t;
    } else {
        result.externalTransaction = t;
    }
    result.status = ReconciliationStatus::Duplicate;
    result.mismatchDetails = "duplicate key within " +
        std::string(t.source == models::SourceSystem::Internal ? "internal" : "external") + " source";
    return result;
}

} // namespace reconciliation::engine
