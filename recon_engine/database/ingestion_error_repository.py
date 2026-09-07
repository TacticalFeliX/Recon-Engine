from __future__ import annotations

from typing import List

import psycopg2

from ..ingestion.validation_error import ValidationError
from .connection import DatabaseConnection, DatabaseException


class IngestionErrorRepository:
    def __init__(self, connection: DatabaseConnection):
        self._connection = connection

    def insert_errors(self, batch_id: int, errors: List[ValidationError]) -> None:
        if not errors:
            return
        sql = (
            "INSERT INTO ingestion_errors (ingestion_batch_id, row_number, raw_line, error_message) "
            "VALUES (%s, %s, %s, %s)"
        )
        try:
            with self._connection.handle.cursor() as cur:
                cur.executemany(
                    sql,
                    [(batch_id, e.row_number, e.raw_line, e.message) for e in errors],
                )
            self._connection.handle.commit()
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"insertErrors failed: {e}") from e
