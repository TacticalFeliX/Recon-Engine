# High-Speed Financial Reconciliation Engine

A C++17 backend that reconciles financial transactions between two independent
data sources (e.g. internal ledger vs. a bank/payment-processor export),
backed by PostgreSQL. Built as a portfolio project to demonstrate C++, SQL,
algorithms, database design, and backend system design.

## Problem it solves

Two systems record the same transactions but rarely agree perfectly: records
go missing on one side, amounts drift by a few cents, duplicates creep in.
This engine ingests both sources, detects and classifies every discrepancy,
and produces both a SQL audit trail and a CSV report.

## Why C++ (not just Python/SQL)

The reconciliation step is the actual computational core — for two datasets
of size N and M, a naive comparison is O(N·M). This project implements three
approaches (brute force, sort + two-pointer, hash-based) to make that
complexity difference concrete rather than theoretical. C++ also forces
explicit decisions about memory layout, ownership, and exact arithmetic that
matter for financial correctness (see "Design decisions" below).

## Architecture

```
CSV A (internal) ──┐
                    ├──> Streaming CSV Ingestion + Validation
CSV B (external) ──┘              │
                                   ▼
                          Batched inserts (libpqxx)
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

- **C++17**, CMake, libpqxx (PostgreSQL client)
- **PostgreSQL 16**, with a hand-written schema (no ORM)

## Repository structure

```
├── CMakeLists.txt
├── include/, src/    # models, ingestion, database, reconciliation, reporting, cli, utils
├── sql/              # schema.sql, indexes.sql, sample_queries.sql, reset_database.sql
├── data/sample/       # small curated CSVs, including one with intentional bad rows
├── config/            # config.example.ini (non-secret app config template)
├── .env.example       # database credential template
└── README.md
```

## Building

Requires a C++17 compiler, CMake, and libpqxx (`apt install libpqxx-dev` on
Ubuntu; `vcpkg install libpqxx` on Windows).

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

> **Why C++17, not C++20?** The Ubuntu-packaged libpqxx was compiled without
> `std::source_location` support, which C++20 headers expect — a real ABI
> mismatch discovered and verified while building this project (see the
> `CMakeLists.txt` comment for the full explanation). C++17 sidesteps it
> cleanly since nothing here needs a C++20-only feature.

## Database setup

```bash
createdb reconciliation
export RECON_DB_HOST=localhost RECON_DB_PORT=5432 RECON_DB_NAME=reconciliation \
       RECON_DB_USER=<you> RECON_DB_PASSWORD=<yours>
./build/reconcile init-db
```

(Or copy `.env.example` to `.env` and fill in real values — the app reads
`.env` first, with real environment variables always taking precedence.)

## Demo (< 5 minutes)

```bash
./build/reconcile init-db
./build/reconcile ingest internal data/sample/internal_sample.csv
./build/reconcile ingest external data/sample/external_sample.csv
./build/reconcile run --algorithm hash
./build/reconcile report --run 1
```

The sample data is seeded with 6 clean matches, an amount mismatch, a tax
mismatch, a record missing from each side, and a duplicated transaction ID —
enough to show every status the engine produces in one short run.

```bash
./build/reconcile --help
```

Also try:
```bash
./build/reconcile run --algorithm two-pointer
./build/reconcile run --algorithm brute-force
```
Each produces its own run row with identical matched/mismatched/duplicate
counts — all three algorithms share one classification function
(`MatchClassifier`), so they are provably consistent with each other.

## Algorithms

| Algorithm | Complexity | Notes |
|---|---|---|
| Brute force | O(N·M) | No sorting/hashing anywhere — the deliberate unoptimized baseline |
| Sort + two-pointer | O(N log N + M log M) | Sorts both sides, then a single linear merge-walk |
| Hash-based | O(N + M) expected | `unordered_map`; worst case degrades toward O(N·M) under hash collisions |

Duplicate handling: if a matching key has more than one record on **either**
side, every record under that key on **both** sides is reported as
`DUPLICATE` — including a lone record on the non-duplicated side, which
can no longer be matched unambiguously.

## Database design

Six tables: `ingestion_batches`, `internal_transactions`,
`external_transactions`, `ingestion_errors`, `reconciliation_runs`,
`reconciliation_results`. Full rationale is in the comments at the top of
`sql/schema.sql`, including:

- **Money as `BIGINT` cents, never `FLOAT`** — exact arithmetic, matching
  the C++ `Money` class's `int64_t` cents representation on both sides of
  the C++/SQL boundary.
- **Surrogate keys, not `transaction_id`, as primary keys** — a business ID
  can legitimately appear more than once (that's the `DUPLICATE` status);
  making it the primary key would make ingesting a duplicate a constraint
  violation instead of data to analyze.
- **Idempotent ingestion** — each file is SHA-256 hashed; re-ingesting an
  identical file is detected and skipped rather than double-inserting.
- **`sql/sample_queries.sql`** includes a SQL-side (`FULL OUTER JOIN`)
  reconciliation approach as an alternative to the C++ engine, along with a
  documented limitation around duplicate detection compared to the C++ path.

## Security

- Credentials live in `.env` (gitignored), never in `config/config.ini` or
  source — see `.env.example`.
- All queries use parameterized statements (`pqxx::exec_params`) — the only
  string-concatenated SQL is table names, which come from a fixed two-way
  enum switch, never from user input.

## Known limitations / future improvements

- Single-threaded (multithreading would parallelize CSV parsing across
  batches and the hash-map build).
- Bulk insert currently uses batched `INSERT` rather than PostgreSQL
  `COPY`; `COPY` would be the next step for ingestion throughput at
  multi-million-row scale.
- No REST API — CLI only. A thin HTTP layer over the same repository
  classes would be a natural extension.

