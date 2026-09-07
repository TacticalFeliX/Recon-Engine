"""
RowValidator
------------
WHY separate from the ingestor?
    This class touches no files, no database, no batching — it's a pure
    function of (fields in) -> (Transaction or error out). That makes it
    trivial to unit test with hand-written field lists, without needing a
    real CSV file on disk or a database connection.

DESIGN DECISION — duplicate transaction_id is NOT a validation error.
    Duplicates are a real-world data condition the reconciliation engine
    must classify (status DUPLICATE), not data to discard at ingestion.
    Rejecting duplicates here would destroy the exact signal reconciliation
    needs to detect. So RowValidator validates each row independently;
    duplicate detection happens later, during reconciliation itself.

DESIGN DECISION — supported currencies is a fixed, configurable set.
    A real system would likely load this from a currency reference table.
    For this project's scope, a fixed set passed into the constructor is
    sufficient to demonstrate the validation pattern, and keeps this class
    free of a database dependency (it stays pure).
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import FrozenSet, List, Optional

from ..models.date_value import Date, DateParseError
from ..models.money import Money
from ..models.transaction import SourceSystem, Transaction

DEFAULT_CURRENCIES: FrozenSet[str] = frozenset({"INR", "USD", "EUR", "GBP"})
EXPECTED_FIELD_COUNT = 7


@dataclass
class RowValidationResult:
    is_valid: bool = False
    transaction: Optional[Transaction] = None
    error_message: str = ""


class RowValidator:
    def __init__(self, supported_currencies: FrozenSet[str] = DEFAULT_CURRENCIES):
        self._supported_currencies = supported_currencies

    def validate_row(self, fields: List[str], row_number: int, source: SourceSystem) -> RowValidationResult:
        if len(fields) != EXPECTED_FIELD_COUNT:
            return RowValidationResult(
                error_message=f"expected {EXPECTED_FIELD_COUNT} columns, got {len(fields)}"
            )

        transaction_id, invoice_number, vendor_id, date_field, amount_field, tax_field, currency = fields

        if not transaction_id:
            return RowValidationResult(error_message="missing transaction_id")
        if not invoice_number:
            return RowValidationResult(error_message="missing invoice_number")
        if not vendor_id:
            return RowValidationResult(error_message="missing vendor_id")

        try:
            date = Date.parse(date_field)
        except DateParseError as e:
            return RowValidationResult(error_message=f"invalid transaction_date: {e}")

        try:
            amount = Money.from_decimal_string(amount_field)
        except ValueError as e:
            return RowValidationResult(error_message=f"malformed amount: {e}")
        if amount.cents() < 0:
            # Business rule for this project: a transaction/invoice amount
            # must be non-negative. A real accounts-payable system might
            # legitimately have negative amounts for credit notes/refunds —
            # if this project needed to support that, the rule would move
            # to a per-currency or per-transaction-type policy rather than
            # a blanket rejection. Documented as a deliberate scope
            # decision, not an oversight.
            return RowValidationResult(error_message=f"negative amount not allowed: {amount.to_string()}")

        try:
            tax = Money.from_decimal_string(tax_field)
        except ValueError as e:
            return RowValidationResult(error_message=f"malformed tax_amount: {e}")
        if tax.cents() < 0:
            return RowValidationResult(error_message=f"negative tax_amount not allowed: {tax.to_string()}")

        if currency not in self._supported_currencies:
            return RowValidationResult(error_message=f"unsupported currency: '{currency}'")

        transaction = Transaction(
            transaction_id=transaction_id,
            invoice_number=invoice_number,
            vendor_id=vendor_id,
            transaction_date=date,
            amount=amount,
            tax_amount=tax,
            currency=currency,
            source=source,
            source_row_number=row_number,
        )
        return RowValidationResult(is_valid=True, transaction=transaction)
