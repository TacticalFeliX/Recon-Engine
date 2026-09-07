# Recon Engine — High-Speed Financial Reconciliation Engine

A Python backend that reconciles financial transactions between two
independent data sources (e.g. internal ledger vs. a bank/payment-processor
export), backed by PostgreSQL. Built as a portfolio project to demonstrate
Python, SQL, algorithms, database design, and backend system design.

## Problem it solves

Two systems record the same transactions but rarely agree perfectly: records
go missing on one side, amounts drift by a few cents, duplicates creep in.
This engine ingests both sources, detects and classifies every discrepancy,
and produces both a SQL audit trail and a CSV report.

## Why three reconciliation algorithms

The reconciliation step is the actual computational core — for two datasets
of size N and M, a naive comparison is O(N·M). This project implements three
approaches (brute force, sort + two-pointer, hash-based) so that complexity
difference is something you can *measure* on the same dataset, not just
assert. All three share one classification function (`match_classifier.py`),
so they are provably consistent with each other — running the same data
through all three always yields identical matched/mismatched/duplicate
counts.

## Architecture

```
CSV A (internal) ──┐
                    ├──> Streaming CSV Ingestion + Validation
CSV B (external) ──┘              │
                                   ▼
                      Batched inserts (psycopg2)
                                   │
                                   ▼
                              PostgreSQL
                    (internal_transactions, external_transactions)
                                   │
                                   ▼
                     Reconciliation Engine (pluggable algorithm)
              brute-force  |  sort + two-pointer  |  hash-based
                                   │
                                   ▼
                          ReconciliationResult
                          │                    │
                          ▼                    ▼
              reconciliation_results   reconciliation_report.csv
                    (PostgreSQL)           (data/output/)
```

## Tech stack

- **Python 3.12**, `psycopg2` (PostgreSQL client), stdlib only otherwise (no
  pandas/ORM in the core engine — see "Design decisions")
- **PostgreSQL 16**, with a hand-written schema (no ORM)
- **pytest** + `pytest-cov` for testing

## Repository structure

```
├── recon_engine/
│   ├── models/          # Money (exact-cents), Date (strict ISO-8601), Transaction
│   ├── ingestion/       # csv_parser, RowValidator, StreamingCsvIngestor
│   ├── database/        # config, connection, schema_manager, repositories
│   ├── reconciliation/  # MatchClassifier + brute-force/two-pointer/hash reconcilers
│   ├── reporting/       # CsvReportWriter
│   ├── cli/             # cli.py — the `reconcile` command
│   └── utils/           # logger, sha256 helper
├── sql/                 # schema.sql, indexes.sql, sample_queries.sql, reset_database.sql
├── data/sample/         # small curated CSVs, including one with intentional bad rows
├── tests/               # pytest suite (unit + integration)
├── config/              # config.example.ini (non-secret app config template)
├── .env.example         # database credential template
├── requirements.txt
└── README.md
```

## Installing

Requires Python 3.10+ and a PostgreSQL 16 server.

```bash
python3 -m venv venv && source venv/bin/activate
pip install -r requirements.txt
```

`requirements.txt` is intentionally short: `psycopg2-binary` for the
database driver, `pytest` and `pytest-cov` for testing. The reconciliation
engine itself (models, ingestion, matching algorithms) has **zero external
dependencies** — everything it does is achievable with the standard library,
which keeps the parts of the code an interviewer is most likely to ask about
free of "trust me, the library does it" answers.

## Database setup

```bash
createdb reconciliation
export RECON_DB_HOST=localhost RECON_DB_PORT=5432 RECON_DB_NAME=reconciliation \
       RECON_DB_USER=<you> RECON_DB_PASSWORD=<yours>
python -m recon_engine.cli.cli init-db
```

(Or copy `.env.example` to `.env` and fill in real values — the app reads
`.env` first, with real environment variables always taking precedence.)

## Demo (< 5 minutes)

```bash
python -m recon_engine.cli.cli init-db
python -m recon_engine.cli.cli ingest internal data/sample/internal_sample.csv
python -m recon_engine.cli.cli ingest external data/sample/external_sample.csv
python -m recon_engine.cli.cli run --algorithm hash
python -m recon_engine.cli.cli report --run 1
```

The sample data is seeded with clean matches, an amount mismatch, a tax
mismatch, a record missing from each side, and a duplicated transaction ID —
enough to show every status the engine produces in one short run.

```bash
python -m recon_engine.cli.cli --help
```

Also try:
```bash
python -m recon_engine.cli.cli run --algorithm two-pointer
python -m recon_engine.cli.cli run --algorithm brute-force
```
Each produces its own run row with identical matched/mismatched/duplicate
counts — verified directly in `tests/test_reconciliation.py`, not just
claimed here.

## Algorithms

| Algorithm | Complexity | Notes |
|---|---|---|
| Brute force | O(N·M) | No sorting/hashing anywhere — the deliberate unoptimized baseline |
| Sort + two-pointer | O(N log N + M log M) | Sorts both sides, then a single linear merge-walk |
| Hash-based | O(N + M) expected | Python `dict`; worst case degrades toward O(N·M) under hash collisions |

Duplicate handling: if a matching key has more than one record on **either**
side, every record under that key on **both** sides is reported as
`DUPLICATE` — including a lone record on the non-duplicated side, which
can no longer be matched unambiguously.

## Database design

Six tables: `ingestion_batches`, `internal_transactions`,
`external_transactions`, `ingestion_errors`, `reconciliation_runs`,
`reconciliation_results`. Full rationale is in the comments at the top of
`sql/schema.sql`, including:

- **Money as `BIGINT` cents, never `FLOAT`/`REAL`** — exact arithmetic,
  matching the Python `Money` class's integer-cents representation on both
  sides of the Python/SQL boundary. (Not `NUMERIC` — see "Design
  decisions" below for why.)
- **Surrogate keys, not `transaction_id`, as primary keys** — a business ID
  can legitimately appear more than once (that's the `DUPLICATE` status);
  making it the primary key would make ingesting a duplicate a constraint
  violation instead of data to analyze.
- **Idempotent ingestion** — each file is SHA-256 hashed; re-ingesting an
  identical file is detected and skipped rather than double-inserting.
- **`sql/sample_queries.sql`** includes a SQL-side (`FULL OUTER JOIN`)
  reconciliation approach as an alternative to the Python engine, along with
  a documented limitation around duplicate detection compared to the
  Python path.

## Design decisions

- **Batched `INSERT` vs. `COPY`** — `TransactionRepository` implements
  both (`insert_batch` and `bulk_insert_via_copy`). The default CLI path
  uses batched `INSERT` (via `psycopg2.extras.execute_values`) for
  simplicity; `COPY` exists specifically so throughput at higher volumes
  can be *benchmarked* rather than assumed. No performance number is
  claimed anywhere in this README that hasn't been measured on this
  machine — see "Benchmarking" below.
- **Streaming ingestion, not "read the whole file into memory"** —
  `StreamingCsvIngestor` processes the file in bounded-size batches, so
  peak memory usage is O(batch_size), not O(file size).
- **Hand-rolled CSV line splitting**, not the stdlib `csv` module — kept
  deliberately simple and inspectable (quoted-field and escaped-quote
  handling only) so every parsing rule is visible in ~30 lines, rather than
  delegated to a library whose edge-case behavior would need separate
  research to explain in an interview.

## Security

- Credentials live in `.env` (gitignored), never in `config/config.ini` or
  source — see `.env.example`.
- All queries use parameterized statements (`%s` placeholders via
  psycopg2) — the only string-concatenated SQL is table names, which come
  from a fixed two-way mapping, never from user input.

## Testing

```bash
pytest --cov=recon_engine
```

Unit tests cover `Money`, `Date`, `RowValidator`, the CSV parser and
streaming ingestor, and all three reconciliation algorithms — including a
randomized cross-algorithm consistency test. Integration tests (marked
separately, requiring a real PostgreSQL connection) cover the repository
layer and a full ingest → reconcile → report run.

## Known limitations / future improvements

- Single-threaded (multiprocessing would parallelize CSV parsing across
  batches and the hash-map build — Python's GIL makes threading, as
  opposed to multiprocessing, ineffective for this CPU-bound work).
- Default bulk-insert path uses batched `INSERT` rather than PostgreSQL
  `COPY`; `COPY` is implemented and benchmarkable but not yet the default —
  see "Design decisions."
- No REST API — CLI only. A thin Flask/FastAPI layer over the same
  repository classes would be a natural extension.
