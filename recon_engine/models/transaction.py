"""
Transaction
-----------
In-memory representation of a single row from either CSV source, after
parsing and type conversion but before validation has necessarily confirmed
every field is *business*-valid. Validation (row_validator.py) produces a
RowValidationResult alongside a Transaction rather than raising, because a
single malformed row must not abort processing of the other 999,999 rows in
a large file.

Field type choices (locked in for the whole project, mirroring the original
C++ design):
    transaction_id, invoice_number, vendor_id, currency -> str
        Real-world alphanumeric business identifiers, not numbers.
    amount, tax_amount -> Money (int cents wrapper)
        Never float. See money.py for the full rationale.
    transaction_date -> Date
        Validated, comparable, compact. See date_value.py for rationale.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Optional

from .date_value import Date
from .money import Money


class SourceSystem(Enum):
    INTERNAL = "INTERNAL"
    EXTERNAL = "EXTERNAL"


@dataclass
class Transaction:
    transaction_id: str
    invoice_number: str
    vendor_id: str
    transaction_date: Date
    amount: Money
    tax_amount: Money
    currency: str
    source: SourceSystem = SourceSystem.INTERNAL

    # Which source row (1-based, within its file) this came from. Used for
    # error messages and audit trails ("row 4821 of internal.csv failed
    # validation: ...").
    source_row_number: int = 0

    # The database surrogate primary key (internal_transactions.id or
    # external_transactions.id) once this Transaction has been read back
    # from Postgres via TransactionRepository.find_all(). None for a
    # Transaction that only exists in memory (e.g. freshly parsed from a CSV
    # row, not yet inserted). Needed by ReconciliationResultRepository to
    # populate the nullable internal_transaction_id / external_transaction_id
    # foreign keys on reconciliation_results — those FKs reference the
    # surrogate id, not the business transaction_id (see sql/schema.sql).
    db_id: Optional[int] = None
