from __future__ import annotations

from dataclasses import dataclass

import psycopg2

from .connection import DatabaseConnection, DatabaseException


@dataclass
class RunStats:
    records_internal: int = 0
    records_external: int = 0
    matched_count: int = 0
    mismatched_count: int = 0  # sum of AMOUNT/TAX/DATE/VENDOR/MULTIPLE mismatch statuses
    missing_internal_count: int = 0
    missing_external_count: int = 0
    duplicate_count: int = 0


class ReconciliationRunRepository:
    def __init__(self, connection: DatabaseConnection):
        self._connection = connection

    def create_run(self, algorithm: str, matching_strategy: str, amount_tolerance_cents: int) -> int:
        try:
            with self._connection.handle.cursor() as cur:
                cur.execute(
                    "INSERT INTO reconciliation_runs (algorithm, matching_strategy, amount_tolerance_cents) "
                    "VALUES (%s, %s, %s) RETURNING id",
                    (algorithm, matching_strategy, amount_tolerance_cents),
                )
                run_id = cur.fetchone()[0]
            self._connection.handle.commit()
            return run_id
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"createRun failed: {e}") from e

    def complete_run(self, run_id: int, stats: RunStats, duration_ms: int) -> None:
        try:
            with self._connection.handle.cursor() as cur:
                cur.execute(
                    "UPDATE reconciliation_runs SET "
                    "  records_internal = %s, records_external = %s, matched_count = %s, "
                    "  mismatched_count = %s, missing_internal_count = %s, missing_external_count = %s, "
                    "  duplicate_count = %s, duration_ms = %s, completed_at = now() "
                    "WHERE id = %s",
                    (
                        stats.records_internal, stats.records_external, stats.matched_count,
                        stats.mismatched_count, stats.missing_internal_count, stats.missing_external_count,
                        stats.duplicate_count, duration_ms, run_id,
                    ),
                )
            self._connection.handle.commit()
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"completeRun failed: {e}") from e
