"""
ReconciliationStatus
---------------------
The `.value` of each member is the EXACT label of the PostgreSQL enum
`reconciliation_status` defined in sql/schema.sql. This isn't a coincidence
to maintain by hand — the reporting layer inserts these strings directly
into reconciliation_results.status, so keeping this Python enum and the SQL
enum in lockstep (same names, same literal text) means that insert is a
direct string pass-through with no translation table to keep in sync.
"""
from enum import Enum


class ReconciliationStatus(Enum):
    MATCHED = "MATCHED"
    AMOUNT_MISMATCH = "AMOUNT_MISMATCH"
    TAX_MISMATCH = "TAX_MISMATCH"
    DATE_MISMATCH = "DATE_MISMATCH"
    VENDOR_MISMATCH = "VENDOR_MISMATCH"
    MISSING_INTERNAL = "MISSING_INTERNAL"
    MISSING_EXTERNAL = "MISSING_EXTERNAL"
    DUPLICATE = "DUPLICATE"
    MULTIPLE_MISMATCHES = "MULTIPLE_MISMATCHES"
