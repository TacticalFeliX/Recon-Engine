from __future__ import annotations

from dataclasses import dataclass
from typing import List

import psycopg2
import psycopg2.extras

from ..reconciliation.config_and_result import ReconciliationResult
from .connection import DatabaseConnection, DatabaseException


@dataclass
class ResultRow:
    transaction_id: str
    status: str
    amount_difference_cents: int
    tax_difference_cents: int
    mismatch_details: str


class ReconciliationResultRepository:
    def __init__(self, connection: DatabaseConnection):
        self._connection = connection

    def insert_results(self, run_id: int, results: List[ReconciliationResult]) -> None:
        if not results:
            return

        sql = (
            "INSERT INTO reconciliation_results "
            "  (run_id, transaction_id, internal_transaction_id, external_transaction_id, "
            "   status, amount_difference_cents, tax_difference_cents, mismatch_details) "
            "VALUES %s"
        )

        rows = []
        for r in results:
            # None (Python's "no value") maps directly to SQL NULL for the
            # FK column when the corresponding side of the result is absent
            # (e.g. MISSING_EXTERNAL has no external_transaction at all).
            internal_id = r.internal_transaction.db_id if r.internal_transaction else None
            external_id = r.external_transaction.db_id if r.external_transaction else None
            rows.append((
                run_id, r.transaction_id, internal_id, external_id,
                r.status.value, r.amount_difference_cents, r.tax_difference_cents, r.mismatch_details,
            ))

        try:
            with self._connection.handle.cursor() as cur:
                psycopg2.extras.execute_values(cur, sql, rows)
            self._connection.handle.commit()
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"insertResults failed: {e}") from e

    def find_by_run(self, run_id: int) -> List[ResultRow]:
        sql = (
            "SELECT transaction_id, status, amount_difference_cents, tax_difference_cents, mismatch_details "
            "FROM reconciliation_results WHERE run_id = %s ORDER BY transaction_id"
        )
        try:
            with self._connection.handle.cursor() as cur:
                cur.execute(sql, (run_id,))
                rows = cur.fetchall()
            self._connection.handle.commit()
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"findByRun failed: {e}") from e

        return [
            ResultRow(
                transaction_id=row[0],
                status=row[1],
                amount_difference_cents=row[2] or 0,
                tax_difference_cents=row[3] or 0,
                mismatch_details=row[4] or "",
            )
            for row in rows
        ]
