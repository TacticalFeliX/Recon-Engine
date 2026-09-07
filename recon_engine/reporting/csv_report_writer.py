"""
CsvReportWriter
------------------
Minimal CSV field escaping: if a field contains a comma, quote, or newline,
wrap it in quotes and double any internal quotes. None of our current
fields (transaction IDs, status names, decimal amounts) actually need this,
but mismatch_details is free text and could theoretically contain a comma —
so this is applied defensively rather than assumed unnecessary.
"""
from __future__ import annotations

import os
from typing import List

from ..reconciliation.config_and_result import ReconciliationResult


def _csv_escape(field: str) -> str:
    needs_quoting = any(c in field for c in (",", '"', "\n"))
    if not needs_quoting:
        return field
    return '"' + field.replace('"', '""') + '"'


class CsvReportWriter:
    @staticmethod
    def write(path: str, results: List[ReconciliationResult]) -> None:
        directory = os.path.dirname(path)
        if directory:
            os.makedirs(directory, exist_ok=True)

        with open(path, "w", newline="") as f:
            f.write("transaction_id,status,internal_amount,external_amount,difference\n")
            for r in results:
                internal_amount = r.internal_transaction.amount.to_string() if r.internal_transaction else ""
                external_amount = r.external_transaction.amount.to_string() if r.external_transaction else ""
                f.write(
                    f"{_csv_escape(r.transaction_id)},{r.status.value},"
                    f"{internal_amount},{external_amount},{r.amount_difference_cents}\n"
                )
