-- =============================================================================
-- schema.sql — High-Speed Financial Reconciliation Engine
-- =============================================================================
--
-- DESIGN DECISIONS (see docs/DATABASE_DESIGN.md for full rationale, added in
-- a later module — summarized here so the schema is self-documenting):
--
-- 1. SURROGATE KEYS, NOT transaction_id AS PRIMARY KEY.
--    transaction_id is a *business* identifier, not a database key. Real
--    source systems can and do emit duplicate transaction_ids (that's an
--    explicit scenario this project must detect — see reconciliation
--    status DUPLICATE). If transaction_id were the primary key, ingesting
--    a duplicate would either throw a constraint violation (destroying
--    the very data we need to analyze) or silently overwrite the first
--    row. Using a BIGSERIAL surrogate `id` lets every row exist
--    independently; duplicate detection becomes a query concern
--    (GROUP BY transaction_id HAVING COUNT(*) > 1), not a constraint
--    violation.
--
-- 2. MONEY STORED AS BIGINT CENTS, NOT NUMERIC OR FLOAT.
--    Mirrors the C++ Money class exactly (int64_t cents). Using the same
--    representation on both sides of the C++/SQL boundary means no
--    conversion/rounding step is needed when values cross that boundary,
--    and BIGINT arithmetic in SQL is exact, just like int64_t in C++.
--    (NUMERIC would also be exact, but forces a decimal<->cents
--    conversion at every read/write for no benefit here.)
--
-- 3. ingestion_batches EXISTS FOR IDEMPOTENCY AND AUDITABILITY (spec §46, §47).
--    Every batch of ingested rows is tied to a source file hash. Ingesting
--    the same file twice is detected via a unique constraint on
--    (source_system, file_hash) rather than silently duplicating data.
--    Every transaction row links back to the batch that created it, so we
--    can always answer "which file, and when, produced this row?".
--
-- 4. reconciliation_runs / reconciliation_results ARE SEPARATE FROM THE
--    RAW TRANSACTION TABLES.
--    Reconciliation is repeatable — the same ingested data can be
--    reconciled multiple times (different algorithm, different tolerance
--    config) without re-ingesting anything. Keeping results in their own
--    run-scoped table means historical runs stay comparable
--    (see spec §17, "reconciliation runs").
-- =============================================================================

-- ---------------------------------------------------------------------------
-- Enum: reconciliation status
-- A Postgres ENUM (rather than a free-text column or a lookup table) is
-- used because the status set is small, fixed at the application-design
-- level, and benefits from ENUM's storage efficiency (4 bytes) and
-- built-in validation (invalid values are rejected at insert time, not
-- caught later by application code).
-- ---------------------------------------------------------------------------
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'reconciliation_status') THEN
        CREATE TYPE reconciliation_status AS ENUM (
            'MATCHED',
            'AMOUNT_MISMATCH',
            'TAX_MISMATCH',
            'DATE_MISMATCH',
            'VENDOR_MISMATCH',
            'MISSING_INTERNAL',
            'MISSING_EXTERNAL',
            'DUPLICATE',
            'MULTIPLE_MISMATCHES'
        );
    END IF;
END $$;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'source_system_type') THEN
        CREATE TYPE source_system_type AS ENUM ('INTERNAL', 'EXTERNAL');
    END IF;
END $$;

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'reconciliation_algorithm') THEN
        CREATE TYPE reconciliation_algorithm AS ENUM (
            'BRUTE_FORCE', 'TWO_POINTER', 'HASH', 'SQL_JOIN'
        );
    END IF;
END $$;

-- ---------------------------------------------------------------------------
-- ingestion_batches
-- One row per CSV file ingested. file_hash (SHA-256 of file contents) plus
-- the unique constraint below is the idempotency mechanism from spec §46:
-- re-running `reconcile ingest internal data/internal.csv` on an unchanged
-- file is detected and rejected (or made a safe no-op) instead of silently
-- double-inserting every row.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ingestion_batches (
    id              BIGSERIAL PRIMARY KEY,
    source_system   source_system_type NOT NULL,
    file_name       TEXT NOT NULL,
    file_hash       CHAR(64) NOT NULL,           -- SHA-256 hex digest
    records_read    INTEGER NOT NULL DEFAULT 0,
    records_valid   INTEGER NOT NULL DEFAULT 0,
    records_failed  INTEGER NOT NULL DEFAULT 0,
    started_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    completed_at    TIMESTAMPTZ,

    CONSTRAINT uq_ingestion_batch_file UNIQUE (source_system, file_hash)
);

COMMENT ON TABLE ingestion_batches IS
    'One row per ingested CSV file. Enables idempotent re-ingestion detection and full audit trail of which file produced which rows.';

-- ---------------------------------------------------------------------------
-- internal_transactions / external_transactions
-- Deliberately two separate tables rather than one table with a
-- source_system discriminator column. Reasoning:
--   - Internal and external records are NEVER joined by primary key across
--     sources (they're joined by business key during reconciliation), so
--     there's no relational benefit to a shared table.
--   - Keeping them separate lets each table be indexed, partitioned, and
--     vacuumed independently, and keeps ingestion code for each source
--     simple (INSERT INTO internal_transactions ... vs a discriminator
--     WHERE clause on every query).
--   - It mirrors the two independent CSV sources conceptually, which
--     makes the schema easier to reason about in an interview.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS internal_transactions (
    id                  BIGSERIAL PRIMARY KEY,
    transaction_id      TEXT NOT NULL,
    invoice_number      TEXT NOT NULL,
    vendor_id           TEXT NOT NULL,
    transaction_date    DATE NOT NULL,
    amount_cents        BIGINT NOT NULL,
    tax_amount_cents    BIGINT NOT NULL,
    currency            CHAR(3) NOT NULL,
    ingestion_batch_id  BIGINT NOT NULL REFERENCES ingestion_batches(id) ON DELETE CASCADE,
    source_row_number   INTEGER NOT NULL,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT ck_internal_currency_format CHECK (currency ~ '^[A-Z]{3}$'),
    CONSTRAINT ck_internal_tax_nonnegative CHECK (tax_amount_cents >= 0)
);

CREATE TABLE IF NOT EXISTS external_transactions (
    id                  BIGSERIAL PRIMARY KEY,
    transaction_id      TEXT NOT NULL,
    invoice_number      TEXT NOT NULL,
    vendor_id           TEXT NOT NULL,
    transaction_date    DATE NOT NULL,
    amount_cents        BIGINT NOT NULL,
    tax_amount_cents    BIGINT NOT NULL,
    currency            CHAR(3) NOT NULL,
    ingestion_batch_id  BIGINT NOT NULL REFERENCES ingestion_batches(id) ON DELETE CASCADE,
    source_row_number   INTEGER NOT NULL,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT ck_external_currency_format CHECK (currency ~ '^[A-Z]{3}$'),
    CONSTRAINT ck_external_tax_nonnegative CHECK (tax_amount_cents >= 0)
);

COMMENT ON COLUMN internal_transactions.amount_cents IS
    'Exact integer cents — mirrors C++ Money::cents(). Never store as FLOAT/REAL.';

-- ---------------------------------------------------------------------------
-- ingestion_errors
-- Malformed rows are NOT dropped silently (spec §7, §21) — every rejected
-- row is recorded with enough context to fix the source file.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS ingestion_errors (
    id                  BIGSERIAL PRIMARY KEY,
    ingestion_batch_id  BIGINT NOT NULL REFERENCES ingestion_batches(id) ON DELETE CASCADE,
    row_number          INTEGER NOT NULL,
    raw_line            TEXT NOT NULL,
    error_message       TEXT NOT NULL,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------------------
-- reconciliation_runs
-- One row per invocation of `reconcile run`. Historical runs stay queryable
-- forever (spec §17) so you can show an interviewer "here's a run using
-- brute force on 100k rows, and here's the same data with hash matching."
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS reconciliation_runs (
    id                      BIGSERIAL PRIMARY KEY,
    algorithm               reconciliation_algorithm NOT NULL,
    matching_strategy       TEXT NOT NULL DEFAULT 'exact_id',
    amount_tolerance_cents  BIGINT NOT NULL DEFAULT 0,
    records_internal        INTEGER NOT NULL DEFAULT 0,
    records_external        INTEGER NOT NULL DEFAULT 0,
    matched_count           INTEGER NOT NULL DEFAULT 0,
    mismatched_count        INTEGER NOT NULL DEFAULT 0,
    missing_internal_count  INTEGER NOT NULL DEFAULT 0,
    missing_external_count  INTEGER NOT NULL DEFAULT 0,
    duplicate_count         INTEGER NOT NULL DEFAULT 0,
    started_at              TIMESTAMPTZ NOT NULL DEFAULT now(),
    completed_at            TIMESTAMPTZ,
    duration_ms             BIGINT,

    CONSTRAINT ck_tolerance_nonnegative CHECK (amount_tolerance_cents >= 0)
);

-- ---------------------------------------------------------------------------
-- reconciliation_results
-- One row per matched pair / unmatched record produced by a run. Both FKs
-- are nullable because MISSING_INTERNAL results have no internal row and
-- MISSING_EXTERNAL results have no external row — but transaction_id is
-- always populated (from whichever side exists) so results stay readable
-- without a join.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS reconciliation_results (
    id                          BIGSERIAL PRIMARY KEY,
    run_id                      BIGINT NOT NULL REFERENCES reconciliation_runs(id) ON DELETE CASCADE,
    transaction_id              TEXT NOT NULL,
    internal_transaction_id     BIGINT REFERENCES internal_transactions(id) ON DELETE SET NULL,
    external_transaction_id     BIGINT REFERENCES external_transactions(id) ON DELETE SET NULL,
    status                      reconciliation_status NOT NULL,
    amount_difference_cents     BIGINT,
    tax_difference_cents        BIGINT,
    mismatch_details            TEXT,
    created_at                  TIMESTAMPTZ NOT NULL DEFAULT now(),

    CONSTRAINT ck_result_has_a_side CHECK (
        internal_transaction_id IS NOT NULL OR external_transaction_id IS NOT NULL
    )
);

COMMENT ON TABLE reconciliation_results IS
    'One row per reconciled outcome for a given run. status MISSING_INTERNAL means external_transaction_id is set and internal_transaction_id is NULL, and vice versa.';
