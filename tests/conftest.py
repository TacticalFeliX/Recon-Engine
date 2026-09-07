"""
Integration-test fixtures. These require a real PostgreSQL connection —
tests using the `db_connection` fixture are marked `@pytest.mark.integration`
so they can be excluded with `pytest -m "not integration"` in environments
without a database (e.g. a lightweight CI stage that only runs unit tests).

Connection settings come from the same RECON_DB_* environment variables /
.env file the CLI itself reads (see recon_engine/database/config.py) — no
separate "test config" format to keep in sync.
"""
import pytest

from recon_engine.database.config import ConfigLoader, DatabaseConfig
from recon_engine.database.connection import DatabaseConnection
from recon_engine.database.schema_manager import SchemaManager


@pytest.fixture(scope="session")
def db_config():
    loader = ConfigLoader.load_from_file(".env")
    return DatabaseConfig.from_config_loader(loader)


@pytest.fixture(scope="session")
def _schema_initialized(db_config):
    conn = DatabaseConnection(db_config)
    SchemaManager.initialize_schema(conn)
    conn.close()


@pytest.fixture
def db_connection(db_config, _schema_initialized):
    """A fresh connection per test, with all reconciliation-relevant tables
    truncated before the test runs, so tests don't see leftover rows from a
    previous test run or a previous CLI demo session."""
    conn = DatabaseConnection(db_config)
    with conn.handle.cursor() as cur:
        cur.execute(
            "TRUNCATE reconciliation_results, reconciliation_runs, "
            "ingestion_errors, internal_transactions, external_transactions, "
            "ingestion_batches RESTART IDENTITY CASCADE"
        )
    conn.handle.commit()
    yield conn
    conn.close()
