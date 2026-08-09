#include "ingestion/RowValidator.hpp"
#include "models/Money.hpp"
#include "models/Date.hpp"

namespace reconciliation::ingestion {

using models::Transaction;
using models::Money;
using models::Date;
using models::SourceSystem;

RowValidator::RowValidator(std::unordered_set<std::string> supportedCurrencies)
    : supportedCurrencies_(std::move(supportedCurrencies)) {}

RowValidationResult RowValidator::validateRow(const std::vector<std::string>& fields,
                                               std::size_t rowNumber,
                                               SourceSystem source) const {
    RowValidationResult result;

    if (fields.size() != EXPECTED_FIELD_COUNT) {
        result.errorMessage = "expected " + std::to_string(EXPECTED_FIELD_COUNT) +
                               " columns, got " + std::to_string(fields.size());
        return result;
    }

    const std::string& transactionId = fields[0];
    const std::string& invoiceNumber = fields[1];
    const std::string& vendorId      = fields[2];
    const std::string& dateField     = fields[3];
    const std::string& amountField   = fields[4];
    const std::string& taxField      = fields[5];
    const std::string& currency      = fields[6];

    if (transactionId.empty()) {
        result.errorMessage = "missing transaction_id";
        return result;
    }
    if (invoiceNumber.empty()) {
        result.errorMessage = "missing invoice_number";
        return result;
    }
    if (vendorId.empty()) {
        result.errorMessage = "missing vendor_id";
        return result;
    }

    Date date;
    try {
        date = Date::parse(dateField);
    } catch (const std::invalid_argument& e) {
        result.errorMessage = std::string("invalid transaction_date: ") + e.what();
        return result;
    }

    Money amount;
    try {
        amount = Money::fromDecimalString(amountField);
    } catch (const std::invalid_argument& e) {
        result.errorMessage = std::string("malformed amount: ") + e.what();
        return result;
    }
    if (amount.cents() < 0) {
        // Business rule for this project: a transaction/invoice amount
        // must be non-negative. A real accounts-payable system might
        // legitimately have negative amounts for credit notes/refunds —
        // if this project needed to support that, the rule would move to
        // a per-currency or per-transaction-type policy rather than a
        // blanket rejection. Documented here since it's a deliberate
        // scope decision, not an oversight.
        result.errorMessage = "negative amount not allowed: " + amount.toString();
        return result;
    }

    Money tax;
    try {
        tax = Money::fromDecimalString(taxField);
    } catch (const std::invalid_argument& e) {
        result.errorMessage = std::string("malformed tax_amount: ") + e.what();
        return result;
    }
    if (tax.cents() < 0) {
        result.errorMessage = "negative tax_amount not allowed: " + tax.toString();
        return result;
    }

    if (supportedCurrencies_.find(currency) == supportedCurrencies_.end()) {
        result.errorMessage = "unsupported currency: '" + currency + "'";
        return result;
    }

    Transaction t;
    t.transactionId = transactionId;
    t.invoiceNumber = invoiceNumber;
    t.vendorId = vendorId;
    t.transactionDate = date;
    t.amount = amount;
    t.taxAmount = tax;
    t.currency = currency;
    t.source = source;
    t.sourceRowNumber = rowNumber;

    result.isValid = true;
    result.transaction = std::move(t);
    return result;
}

} // namespace reconciliation::ingestion
