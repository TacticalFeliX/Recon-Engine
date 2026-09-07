from __future__ import annotations

from typing import Optional

import psycopg2

from ..models.transaction import SourceSystem
from .connection import DatabaseConnection, DatabaseException, DuplicateIngestionException


class IngestionBatchRepository:
    def __init__(self, connection: DatabaseConnection):
        self._connection = connection

    @staticmethod
    def _source_to_string(source: SourceSystem) -> str:
        return source.value

    def find_existing_batch(self, source: SourceSystem, file_hash: str) -> Optional[int]:
        try:
            with self._connection.handle.cursor() as cur:
                cur.execute(
                    "SELECT id FROM ingestion_batches WHERE source_system = %s AND file_hash = %s",
                    (self._source_to_string(source), file_hash),
                )
                row = cur.fetchone()
            self._connection.handle.commit()
            return row[0] if row else None
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"findExistingBatch failed: {e}") from e

    def create_batch(self, source: SourceSystem, file_name: str, file_hash: str) -> int:
        try:
            with self._connection.handle.cursor() as cur:
                cur.execute(
                    "INSERT INTO ingestion_batches (source_system, file_name, file_hash) "
                    "VALUES (%s, %s, %s) RETURNING id",
                    (self._source_to_string(source), file_name, file_hash),
                )
                batch_id = cur.fetchone()[0]
            self._connection.handle.commit()
            return batch_id
        except psycopg2.errors.UniqueViolation:
            # Defensive: fires if two ingestion processes race between
            # find_existing_batch() and create_batch() for the same file.
            self._connection.handle.rollback()
            raise DuplicateIngestionException(file_hash)
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"createBatch failed: {e}") from e

    def complete_batch(self, batch_id: int, records_read: int, records_valid: int, records_failed: int) -> None:
        try:
            with self._connection.handle.cursor() as cur:
                cur.execute(
                    "UPDATE ingestion_batches "
                    "SET records_read = %s, records_valid = %s, records_failed = %s, completed_at = now() "
                    "WHERE id = %s",
                    (records_read, records_valid, records_failed, batch_id),
                )
            self._connection.handle.commit()
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"completeBatch failed: {e}") from e
