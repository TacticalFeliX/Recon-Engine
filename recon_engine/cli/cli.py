"""
Cli
----
Usage:
    reconcile init-db
    reconcile ingest internal <path.csv>
    reconcile ingest external <path.csv>
    reconcile run [--algorithm hash|two-pointer|brute-force]
                  [--strategy exact-id|composite-key] [--tolerance-cents N]
    reconcile report --run <id>
    reconcile --help

Mirrors the original C++ CLI's command set and flags exactly, so anyone who
used the C++ tool (or read its README) can use this one without relearning
anything. `--help` is the only command that doesn't need a database
connection; everything else establishes one connection up front rather than
duplicating that logic in each handler.
"""
from __future__ import annotations

import sys
import time
from typing import List, Optional

from ..database.config import ConfigLoader, DatabaseConfig
from ..database.connection import DatabaseConnection, DatabaseException
from ..database.ingestion_batch_repository import IngestionBatchRepository
from ..database.ingestion_error_repository import IngestionErrorRepository
from ..database.reconciliation_result_repository import ReconciliationResultRepository
from ..database.reconciliation_run_repository import ReconciliationRunRepository, RunStats
from ..database.schema_manager import SchemaManager
from ..database.transaction_repository import TransactionRepository
from ..ingestion.row_validator import RowValidator
from ..ingestion.streaming_csv_ingestor import StreamingCsvIngestor
from ..models.transaction import SourceSystem
from ..reconciliation.base import IReconciler
from ..reconciliation.brute_force_reconciler import BruteForceReconciler
from ..reconciliation.config_and_result import MatchingConfig, MatchingStrategy
from ..reconciliation.hash_reconciler import HashReconciler
from ..reconciliation.status import ReconciliationStatus
from ..reconciliation.two_pointer_reconciler import TwoPointerReconciler
from ..reporting.csv_report_writer import CsvReportWriter
from ..utils.logger import Logger
from ..utils.sha256_util import hash_file_contents

HELP_TEXT = """High-Speed Financial Reconciliation Engine (Python port)

Usage:
  reconcile init-db
  reconcile ingest internal <path.csv>
  reconcile ingest external <path.csv>
  reconcile run [--algorithm hash|two-pointer|brute-force] [--strategy exact-id|composite-key] [--tolerance-cents N]
  reconcile report --run <id>
  reconcile --help

Examples:
  reconcile init-db
  reconcile ingest internal data/sample/internal_sample.csv
  reconcile ingest external data/sample/external_sample.csv
  reconcile run --algorithm hash
  reconcile report --run 1
"""


def get_flag(args: List[str], flag: str, default: str) -> str:
    for i, a in enumerate(args):
        if a == flag and i + 1 < len(args):
            return args[i + 1]
    return default


def build_reconciler(algorithm_arg: str) -> IReconciler:
    normalized = algorithm_arg.lower().replace("-", "_")
    if normalized == "hash":
        return HashReconciler()
    if normalized == "two_pointer":
        return TwoPointerReconciler()
    if normalized == "brute_force":
        return BruteForceReconciler()
    raise ValueError(f"Unknown --algorithm '{algorithm_arg}'. Expected hash, two-pointer, or brute-force.")


def _algorithm_name_to_sql_enum(name: str) -> str:
    # Converts an IReconciler.name value ("two_pointer") into the
    # reconciliation_algorithm SQL enum label ("TWO_POINTER") — the two
    # only differ by case, since both were deliberately chosen with
    # matching underscore-separated words.
    return name.upper()


def ingest_file(conn: DatabaseConnection, path: str, source: SourceSystem, source_label: str, batch_size: int) -> None:
    batch_repo = IngestionBatchRepository(conn)
    tx_repo = TransactionRepository(conn)
    error_repo = IngestionErrorRepository(conn)
    validator = RowValidator()

    with open(path, "rb") as f:
        file_hash = hash_file_contents(f.read())

    existing = batch_repo.find_existing_batch(source, file_hash)
    if existing is not None:
        Logger.info(f"{source_label}: '{path}' already ingested as batch {existing} (hash match) -- skipping")
        return

    batch_id = batch_repo.create_batch(source, path, file_hash)
    ingestor = StreamingCsvIngestor(validator, batch_size)

    total_inserted = 0

    def on_batch(batch):
        nonlocal total_inserted
        total_inserted += tx_repo.insert_batch(source, batch, batch_id)

    summary = ingestor.ingest(path, source, on_batch)

    if summary.errors:
        error_repo.insert_errors(batch_id, summary.errors)
        for err in summary.errors:
            Logger.warn(f"{source_label}: row {err.row_number} rejected: {err.message}")

    batch_repo.complete_batch(batch_id, summary.rows_read, summary.rows_valid, summary.rows_failed)

    Logger.info(
        f"{source_label}: {summary.rows_read} rows read, {summary.rows_valid} valid, "
        f"{summary.rows_failed} rejected (batch {batch_id}, inserted {total_inserted})"
    )


def cmd_init_db(conn: DatabaseConnection) -> int:
    Logger.info("Applying sql/schema.sql and sql/indexes.sql...")
    SchemaManager.initialize_schema(conn)
    Logger.info("Database schema initialized successfully.")
    return 0


def cmd_ingest(conn: DatabaseConnection, args: List[str]) -> int:
    # args[0] == "ingest"; args[1] should be internal/external; args[2] is the path.
    if len(args) < 3:
        print("Usage: reconcile ingest internal|external <path.csv>", file=sys.stderr)
        return 1

    source_arg = args[1].lower()
    if source_arg == "internal":
        source = SourceSystem.INTERNAL
    elif source_arg == "external":
        source = SourceSystem.EXTERNAL
    else:
        print(f"Unknown source '{args[1]}'. Expected 'internal' or 'external'.", file=sys.stderr)
        return 1

    path = args[2]
    app_config = ConfigLoader.load_from_file("config/config.ini")
    batch_size = int(app_config.get("batch_size", "10000"))

    ingest_file(conn, path, source, source_arg, batch_size)
    return 0


def cmd_run(conn: DatabaseConnection, args: List[str]) -> int:
    algorithm_arg = get_flag(args, "--algorithm", "hash")
    strategy_arg = get_flag(args, "--strategy", "exact-id")
    tolerance_arg = get_flag(args, "--tolerance-cents", "0")

    reconciler = build_reconciler(algorithm_arg)

    match_config = MatchingConfig(
        strategy=MatchingStrategy.COMPOSITE_KEY
        if strategy_arg.lower().replace("-", "_") == "composite_key"
        else MatchingStrategy.EXACT_ID,
        amount_tolerance_cents=int(tolerance_arg),
    )

    tx_repo = TransactionRepository(conn)
    internal_tx = tx_repo.find_all(SourceSystem.INTERNAL)
    external_tx = tx_repo.find_all(SourceSystem.EXTERNAL)

    Logger.info(
        f"Loaded {len(internal_tx)} internal, {len(external_tx)} external records "
        f"(algorithm={reconciler.name})"
    )

    run_repo = ReconciliationRunRepository(conn)
    run_id = run_repo.create_run(
        _algorithm_name_to_sql_enum(reconciler.name), strategy_arg, match_config.amount_tolerance_cents
    )

    start = time.monotonic()
    results = reconciler.reconcile(internal_tx, external_tx, match_config)
    duration_ms = int((time.monotonic() - start) * 1000)

    result_repo = ReconciliationResultRepository(conn)
    result_repo.insert_results(run_id, results)

    stats = RunStats(records_internal=len(internal_tx), records_external=len(external_tx))
    for r in results:
        if r.status == ReconciliationStatus.MATCHED:
            stats.matched_count += 1
        elif r.status == ReconciliationStatus.MISSING_INTERNAL:
            stats.missing_internal_count += 1
        elif r.status == ReconciliationStatus.MISSING_EXTERNAL:
            stats.missing_external_count += 1
        elif r.status == ReconciliationStatus.DUPLICATE:
            stats.duplicate_count += 1
        else:
            stats.mismatched_count += 1

    run_repo.complete_run(run_id, stats, duration_ms)

    CsvReportWriter.write("data/output/reconciliation_report.csv", results)

    print(f"Run {run_id} complete ({reconciler.name}) in {duration_ms} ms")
    print(f"  matched:           {stats.matched_count}")
    print(f"  mismatched:        {stats.mismatched_count}")
    print(f"  missing_internal:  {stats.missing_internal_count}")
    print(f"  missing_external:  {stats.missing_external_count}")
    print(f"  duplicate:         {stats.duplicate_count}")
    print("Report: data/output/reconciliation_report.csv")

    return 0


def cmd_report(conn: DatabaseConnection, args: List[str]) -> int:
    run_id_arg = get_flag(args, "--run", "")
    if not run_id_arg:
        print("Usage: reconcile report --run <id>", file=sys.stderr)
        return 1

    run_id = int(run_id_arg)
    result_repo = ReconciliationResultRepository(conn)
    rows = result_repo.find_by_run(run_id)

    if not rows:
        print(f"No results found for run {run_id} (either the run doesn't exist, or it produced zero results)")
        return 0

    print(f"Run {run_id} -- {len(rows)} results:\n")
    for row in rows:
        line = f"{row.transaction_id}\t{row.status}"
        if row.status != "MATCHED":
            line += f"\t{row.mismatch_details}"
        print(line)
    return 0


def run(argv: Optional[List[str]] = None) -> int:
    all_args = list(argv if argv is not None else sys.argv[1:])

    if not all_args or all_args[0] in ("--help", "-h", "help"):
        print(HELP_TEXT)
        return 0

    command = all_args[0]

    try:
        db_env = ConfigLoader.load_from_file(".env")
        db_config = DatabaseConfig.from_config_loader(db_env)
        conn = DatabaseConnection(db_config)
        try:
            if command == "init-db":
                return cmd_init_db(conn)
            if command == "ingest":
                return cmd_ingest(conn, all_args)
            if command == "run":
                return cmd_run(conn, all_args)
            if command == "report":
                return cmd_report(conn, all_args)

            print(f"Unknown command '{command}'\n", file=sys.stderr)
            print(HELP_TEXT)
            return 1
        finally:
            conn.close()

    except DatabaseException as e:
        Logger.error(f"Database error: {e}")
        return 1
    except Exception as e:
        Logger.error(f"Error: {e}")
        return 1


def main() -> None:
    sys.exit(run())


if __name__ == "__main__":
    main()
