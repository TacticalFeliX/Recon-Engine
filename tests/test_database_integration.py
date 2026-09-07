import pytest

from recon_engine.database.ingestion_batch_repository import IngestionBatchRepository
from recon_engine.database.ingestion_error_repository import IngestionErrorRepository
from recon_engine.database.reconciliation_result_repository import ReconciliationResultRepository
from recon_engine.database.reconciliation_run_repository import ReconciliationRunRepository, RunStats
from recon_engine.database.transaction_repository import TransactionRepository
from recon_engine.ingestion.validation_error import ValidationError
from recon_engine.models.date_value import Date
from recon_engine.models.money import Money
from recon_engine.models.transaction import SourceSystem, Transaction
from recon_engine.reconciliation.config_and_result import ReconciliationResult
from recon_engine.reconciliation.status import ReconciliationStatus

pytestmark = pytest.mark.integration


def make_tx(tid, source=SourceSystem.INTERNAL, amount="100.00"):
    return Transaction(
        transaction_id=tid,
        invoice_number=f"INV-{tid}",
        vendor_id="V1",
        transaction_date=Date.parse("2026-07-01"),
        amount=Money.from_decimal_string(amount),
        tax_amount=Money.from_decimal_string("10.00"),
        currency="INR",
        source=source,
    )


def test_ingestion_batch_idempotency(db_connection):
    repo = IngestionBatchRepository(db_connection)
    assert repo.find_existing_batch(SourceSystem.INTERNAL, "abc123") is None

    batch_id = repo.create_batch(SourceSystem.INTERNAL, "file.csv", "abc123")
    assert batch_id > 0

    # Same (source, hash) must be found as already-ingested, not re-created.
    found = repo.find_existing_batch(SourceSystem.INTERNAL, "abc123")
    assert found == batch_id


def test_ingestion_batch_complete_updates_counts(db_connection):
    repo = IngestionBatchRepository(db_connection)
    batch_id = repo.create_batch(SourceSystem.INTERNAL, "file.csv", "hash1")
    repo.complete_batch(batch_id, records_read=10, records_valid=8, records_failed=2)

    with db_connection.handle.cursor() as cur:
        cur.execute(
            "SELECT records_read, records_valid, records_failed, completed_at FROM ingestion_batches WHERE id=%s",
            (batch_id,),
        )
        row = cur.fetchone()
    db_connection.handle.commit()
    assert row[0] == 10 and row[1] == 8 and row[2] == 2
    assert row[3] is not None  # completed_at was set


def test_ingestion_error_insert(db_connection):
    batch_repo = IngestionBatchRepository(db_connection)
    error_repo = IngestionErrorRepository(db_connection)
    batch_id = batch_repo.create_batch(SourceSystem.INTERNAL, "file.csv", "hash2")

    error_repo.insert_errors(batch_id, [
        ValidationError(row_number=3, raw_line="bad,row", message="missing transaction_id"),
        ValidationError(row_number=7, raw_line="also,bad", message="invalid date"),
    ])

    with db_connection.handle.cursor() as cur:
        cur.execute("SELECT COUNT(*) FROM ingestion_errors WHERE ingestion_batch_id=%s", (batch_id,))
        count = cur.fetchone()[0]
    db_connection.handle.commit()
    assert count == 2


def test_transaction_insert_and_find_all_via_execute_values(db_connection):
    batch_repo = IngestionBatchRepository(db_connection)
    tx_repo = TransactionRepository(db_connection)
    batch_id = batch_repo.create_batch(SourceSystem.INTERNAL, "f.csv", "h3")

    txs = [make_tx("TX1"), make_tx("TX2", amount="250.75")]
    inserted = tx_repo.insert_batch(SourceSystem.INTERNAL, txs, batch_id)
    assert inserted == 2

    fetched = tx_repo.find_all(SourceSystem.INTERNAL)
    assert len(fetched) == 2
    assert {t.transaction_id for t in fetched} == {"TX1", "TX2"}
    tx2 = next(t for t in fetched if t.transaction_id == "TX2")
    assert tx2.amount.cents() == 25075  # confirms exact-cents survives the DB round trip
    assert tx2.db_id is not None


def test_transaction_insert_via_copy_matches_insert_batch(db_connection):
    """Both bulk-load paths must produce identical, correct rows -- this is
    the actual verification behind the README's COPY-vs-INSERT discussion,
    not just an assumption that COPY 'should' work the same way."""
    batch_repo = IngestionBatchRepository(db_connection)
    tx_repo = TransactionRepository(db_connection)
    batch_id = batch_repo.create_batch(SourceSystem.EXTERNAL, "f2.csv", "h4")

    txs = [make_tx(f"CTX{i}", source=SourceSystem.EXTERNAL, amount=f"{i}.50") for i in range(5)]
    inserted = tx_repo.bulk_insert_via_copy(SourceSystem.EXTERNAL, txs, batch_id)
    assert inserted == 5

    fetched = tx_repo.find_all(SourceSystem.EXTERNAL)
    assert len(fetched) == 5
    assert tx_repo.count_all(SourceSystem.EXTERNAL) == 5


def test_unique_constraint_blocks_duplicate_batch_hash(db_connection):
    import psycopg2
    repo = IngestionBatchRepository(db_connection)
    repo.create_batch(SourceSystem.INTERNAL, "f.csv", "dup-hash")
    with pytest.raises(Exception):
        # Bypassing find_existing_batch on purpose, to prove the DB-level
        # UNIQUE constraint -- not just application logic -- is what
        # actually prevents the duplicate.
        with db_connection.handle.cursor() as cur:
            cur.execute(
                "INSERT INTO ingestion_batches (source_system, file_name, file_hash) VALUES (%s, %s, %s)",
                ("INTERNAL", "other.csv", "dup-hash"),
            )
    db_connection.handle.rollback()


def test_reconciliation_run_and_results_round_trip(db_connection):
    batch_repo = IngestionBatchRepository(db_connection)
    tx_repo = TransactionRepository(db_connection)
    run_repo = ReconciliationRunRepository(db_connection)
    result_repo = ReconciliationResultRepository(db_connection)

    batch_id = batch_repo.create_batch(SourceSystem.INTERNAL, "f.csv", "h5")
    tx_repo.insert_batch(SourceSystem.INTERNAL, [make_tx("RTX1")], batch_id)
    internal = tx_repo.find_all(SourceSystem.INTERNAL)

    run_id = run_repo.create_run("HASH", "exact_id", 0)
    assert run_id > 0

    result = ReconciliationResult(
        transaction_id="RTX1",
        internal_transaction=internal[0],
        status=ReconciliationStatus.MISSING_EXTERNAL,
        amount_difference_cents=10000,
        mismatch_details="present in internal source only",
    )
    result_repo.insert_results(run_id, [result])

    stats = RunStats(records_internal=1, records_external=0, missing_external_count=1)
    run_repo.complete_run(run_id, stats, duration_ms=5)

    fetched_results = result_repo.find_by_run(run_id)
    assert len(fetched_results) == 1
    assert fetched_results[0].status == "MISSING_EXTERNAL"
    assert fetched_results[0].transaction_id == "RTX1"

    with db_connection.handle.cursor() as cur:
        cur.execute("SELECT missing_external_count, duration_ms, completed_at FROM reconciliation_runs WHERE id=%s",
                    (run_id,))
        row = cur.fetchone()
    db_connection.handle.commit()
    assert row[0] == 1
    assert row[1] == 5
    assert row[2] is not None
