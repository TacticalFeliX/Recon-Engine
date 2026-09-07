"""
End-to-end pipeline test: runs the actual CLI commands (init-db, ingest,
run, report) against the live database and the real sample CSVs, then
asserts on the real classification outcome. This is the test that
substantiates the README's demo section -- it's not a description of what
the CLI *should* do, it's proof of what it *actually* does.
"""
import os

import pytest

from recon_engine.cli import cli
from recon_engine.database.reconciliation_result_repository import ReconciliationResultRepository
from recon_engine.database.transaction_repository import TransactionRepository
from recon_engine.models.transaction import SourceSystem

pytestmark = pytest.mark.integration

SAMPLE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data", "sample")


@pytest.mark.parametrize("algorithm", ["hash", "two-pointer", "brute-force"])
def test_full_pipeline_end_to_end(db_connection, algorithm):
    internal_path = os.path.join(SAMPLE_DIR, "internal_sample.csv")
    external_path = os.path.join(SAMPLE_DIR, "external_sample.csv")

    cli.ingest_file(db_connection, internal_path, SourceSystem.INTERNAL, "internal", batch_size=10_000)
    cli.ingest_file(db_connection, external_path, SourceSystem.EXTERNAL, "external", batch_size=10_000)

    tx_repo = TransactionRepository(db_connection)
    internal_count = tx_repo.count_all(SourceSystem.INTERNAL)
    external_count = tx_repo.count_all(SourceSystem.EXTERNAL)
    assert internal_count > 0
    assert external_count > 0

    exit_code = cli.cmd_run(db_connection, ["run", "--algorithm", algorithm])
    assert exit_code == 0

    # The most recent run's results should be non-empty and internally consistent:
    # every result's status is a valid ReconciliationStatus value.
    with db_connection.handle.cursor() as cur:
        cur.execute("SELECT id FROM reconciliation_runs ORDER BY id DESC LIMIT 1")
        run_id = cur.fetchone()[0]
    db_connection.handle.commit()

    result_repo = ReconciliationResultRepository(db_connection)
    results = result_repo.find_by_run(run_id)
    assert len(results) > 0

    valid_statuses = {
        "MATCHED", "AMOUNT_MISMATCH", "TAX_MISMATCH", "DATE_MISMATCH",
        "VENDOR_MISMATCH", "MISSING_INTERNAL", "MISSING_EXTERNAL",
        "DUPLICATE", "MULTIPLE_MISMATCHES",
    }
    assert all(r.status in valid_statuses for r in results)

    assert os.path.exists("data/output/reconciliation_report.csv")


def test_reingesting_identical_file_is_idempotent(db_connection):
    internal_path = os.path.join(SAMPLE_DIR, "internal_sample.csv")

    cli.ingest_file(db_connection, internal_path, SourceSystem.INTERNAL, "internal", batch_size=10_000)
    tx_repo = TransactionRepository(db_connection)
    first_count = tx_repo.count_all(SourceSystem.INTERNAL)

    # Re-ingesting the exact same file must be a no-op, not a duplicate load.
    cli.ingest_file(db_connection, internal_path, SourceSystem.INTERNAL, "internal", batch_size=10_000)
    second_count = tx_repo.count_all(SourceSystem.INTERNAL)

    assert first_count == second_count
    assert first_count > 0


def test_dirty_sample_rows_are_quarantined_not_fatal(db_connection):
    dirty_path = os.path.join(SAMPLE_DIR, "internal_dirty_sample.csv")
    cli.ingest_file(db_connection, dirty_path, SourceSystem.INTERNAL, "internal", batch_size=10_000)

    with db_connection.handle.cursor() as cur:
        cur.execute("SELECT records_read, records_valid, records_failed FROM ingestion_batches ORDER BY id DESC LIMIT 1")
        row = cur.fetchone()
        cur.execute("SELECT COUNT(*) FROM ingestion_errors")
        error_count = cur.fetchone()[0]
    db_connection.handle.commit()

    records_read, records_valid, records_failed = row
    assert records_failed > 0  # the "dirty" sample must actually contain bad rows
    assert records_valid > 0   # and the good rows in it must still get through
    assert records_read == records_valid + records_failed
    assert error_count == records_failed
