"""
MatchingConfig / ReconciliationResult
--------------------------------------
MatchingStrategy — two supported strategies:

    EXACT_ID      — key = transaction_id. Simple, and correct when both
                    source systems agree on a shared transaction
                    identifier (the common case this project models).

    COMPOSITE_KEY — key = vendor_id + invoice_number + transaction_date.
                    Useful when the two systems DON'T share a common
                    transaction_id (e.g. one side is a bank statement with
                    its own reference numbers) but do agree on business
                    facts like "which vendor, which invoice, which date".

amount_tolerance_cents implements tolerance-based comparison (e.g. treat a
1-2 cent difference as immaterial rather than flagging AMOUNT_MISMATCH for
what's likely a rounding artifact upstream). Applied identically to both
amount and tax comparisons.

ReconciliationResult stores full Transaction copies (not references into the
original lists) — a deliberate simplicity-over-micro-optimization choice:
results typically number in the same order of magnitude as the input data,
get written to the database and a CSV report, and then go out of scope.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Optional

from ..models.transaction import Transaction
from .status import ReconciliationStatus


class MatchingStrategy(Enum):
    EXACT_ID = "exact_id"
    COMPOSITE_KEY = "composite_key"


@dataclass
class MatchingConfig:
    strategy: MatchingStrategy = MatchingStrategy.EXACT_ID
    amount_tolerance_cents: int = 0


@dataclass
class ReconciliationResult:
    transaction_id: str  # business key, always populated from whichever side exists
    internal_transaction: Optional[Transaction] = None
    external_transaction: Optional[Transaction] = None
    status: ReconciliationStatus = ReconciliationStatus.MATCHED
    amount_difference_cents: int = 0
    tax_difference_cents: int = 0
    mismatch_details: str = ""  # human-readable, e.g. "amount differs by 500 cents; ..."
