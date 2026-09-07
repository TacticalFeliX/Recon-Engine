"""
DatabaseConnection
--------------------
Thin wrapper around a psycopg2 connection. Kept as its own class (rather
than passing raw psycopg2 connections around) so repositories depend on a
small interface, and so connection setup (autocommit off, so callers
control transaction boundaries explicitly) lives in exactly one place.
"""
from __future__ import annotations

import psycopg2

from .config import DatabaseConfig


class DatabaseException(Exception):
    pass


class DuplicateIngestionException(DatabaseException):
    def __init__(self, file_hash: str):
        super().__init__(f"a batch with file_hash '{file_hash}' already exists (race with findExistingBatch)")
        self.file_hash = file_hash


class DatabaseConnection:
    def __init__(self, config: DatabaseConfig):
        self._config = config
        try:
            self._conn = psycopg2.connect(
                host=config.host,
                port=config.port,
                dbname=config.database,
                user=config.user,
                password=config.password,
            )
            self._conn.autocommit = False
        except psycopg2.OperationalError as e:
            raise DatabaseException(f"could not connect to database: {e}") from e

    @property
    def handle(self):
        """Raw psycopg2 connection, for repositories to open cursors on."""
        return self._conn

    def close(self):
        self._conn.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
