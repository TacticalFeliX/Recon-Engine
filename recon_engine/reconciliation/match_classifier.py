"""
MatchClassifier
----------------
Pure classification logic shared by all three reconciliation algorithms
(brute force, two-pointer, hash). This is what makes the three algorithms
*provably* consistent with each other: they differ only in HOW they find
candidate (internal, external) pairs / orphans / duplicate groups, never in
HOW a given pair is judged matched vs. mismatched vs. duplicate. Running the
same dataset through all three should always yield identical
matched/mismatched/duplicate counts — the algorithms only change the CPU
complexity of getting there.
"""
from __future__ import annotations

from ..models.transaction import SourceSystem, Transaction
from .config_and_result import MatchingConfig, MatchingStrategy, ReconciliationResult
from .status import ReconciliationStatus


def build_key(t: Transaction, strategy: MatchingStrategy) -> str:
    if strategy == MatchingStrategy.EXACT_ID:
        return t.transaction_id
    # COMPOSITE_KEY: vendor_id + invoice_number + transaction_date, '|'-separated.
    # '|' is not a valid character in any of these three source fields under
    # this project's CSV format, so it's a safe, simple delimiter — no
    # escaping needed, unlike e.g. joining on a character that could
    # legitimately appear in a vendor ID.
    return f"{t.vendor_id}|{t.invoice_number}|{t.transaction_date.to_string()}"


def classify_match(internal_tx: Transaction, external_tx: Transaction, config: MatchingConfig) -> ReconciliationResult:
    amount_diff = internal_tx.amount.abs_difference_cents(external_tx.amount)
    tax_diff = internal_tx.tax_amount.abs_difference_cents(external_tx.tax_amount)

    amount_mismatch = amount_diff > config.amount_tolerance_cents
    tax_mismatch = tax_diff > config.amount_tolerance_cents
    date_mismatch = internal_tx.transaction_date != external_tx.transaction_date
    vendor_mismatch = internal_tx.vendor_id != external_tx.vendor_id

    mismatch_count = sum([amount_mismatch, tax_mismatch, date_mismatch, vendor_mismatch])

    details_parts = []
    if amount_mismatch:
        details_parts.append(
            f"amount differs by {amount_diff} cents "
            f"(internal={internal_tx.amount.to_string()}, external={external_tx.amount.to_string()}); "
        )
    if tax_mismatch:
        details_parts.append(
            f"tax differs by {tax_diff} cents "
            f"(internal={internal_tx.tax_amount.to_string()}, external={external_tx.tax_amount.to_string()}); "
        )
    if date_mismatch:
        details_parts.append(
            f"date differs (internal={internal_tx.transaction_date.to_string()}, "
            f"external={external_tx.transaction_date.to_string()}); "
        )
    if vendor_mismatch:
        details_parts.append(
            f"vendor differs (internal={internal_tx.vendor_id}, external={external_tx.vendor_id}); "
        )
    details = "".join(details_parts)

    if mismatch_count == 0:
        status = ReconciliationStatus.MATCHED
        details = ""
    elif mismatch_count > 1:
        status = ReconciliationStatus.MULTIPLE_MISMATCHES
    elif amount_mismatch:
        status = ReconciliationStatus.AMOUNT_MISMATCH
    elif tax_mismatch:
        status = ReconciliationStatus.TAX_MISMATCH
    elif date_mismatch:
        status = ReconciliationStatus.DATE_MISMATCH
    else:  # vendor_mismatch
        status = ReconciliationStatus.VENDOR_MISMATCH

    return ReconciliationResult(
        transaction_id=internal_tx.transaction_id,
        internal_transaction=internal_tx,
        external_transaction=external_tx,
        status=status,
        amount_difference_cents=amount_diff,
        tax_difference_cents=tax_diff,
        mismatch_details=details,
    )


def missing_external(internal_tx: Transaction) -> ReconciliationResult:
    return ReconciliationResult(
        transaction_id=internal_tx.transaction_id,
        internal_transaction=internal_tx,
        status=ReconciliationStatus.MISSING_EXTERNAL,
        amount_difference_cents=internal_tx.amount.cents(),
        mismatch_details="present in internal source only",
    )


def missing_internal(external_tx: Transaction) -> ReconciliationResult:
    return ReconciliationResult(
        transaction_id=external_tx.transaction_id,
        external_transaction=external_tx,
        status=ReconciliationStatus.MISSING_INTERNAL,
        amount_difference_cents=external_tx.amount.cents(),
        mismatch_details="present in external source only",
    )


def duplicate(t: Transaction) -> ReconciliationResult:
    result = ReconciliationResult(
        transaction_id=t.transaction_id,
        status=ReconciliationStatus.DUPLICATE,
        mismatch_details=(
            "duplicate key within "
            + ("internal" if t.source == SourceSystem.INTERNAL else "external")
            + " source"
        ),
    )
    if t.source == SourceSystem.INTERNAL:
        result.internal_transaction = t
    else:
        result.external_transaction = t
    return result
