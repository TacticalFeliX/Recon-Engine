from __future__ import annotations

import os

import psycopg2

from .connection import DatabaseConnection, DatabaseException

_SQL_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "sql")


class SchemaManager:
    @staticmethod
    def initialize_schema(conn: DatabaseConnection) -> None:
        schema_path = os.path.join(_SQL_DIR, "schema.sql")
        indexes_path = os.path.join(_SQL_DIR, "indexes.sql")
        try:
            with open(schema_path) as f:
                schema_sql = f.read()
            with open(indexes_path) as f:
                indexes_sql = f.read()

            with conn.handle.cursor() as cur:
                cur.execute(schema_sql)
                cur.execute(indexes_sql)
            conn.handle.commit()
        except (psycopg2.Error, OSError) as e:
            conn.handle.rollback()
            raise DatabaseException(f"initializeSchema failed: {e}") from e
