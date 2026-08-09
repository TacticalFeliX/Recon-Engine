-- =============================================================================
-- indexes.sql — kept separate from schema.sql on purpose.
--
-- WHY A SEPARATE FILE?
-- Indexes speed up reads but slow down writes (every INSERT has to update
-- every index on the table). This project explicitly wants to demonstrate
-- and benchmark that tradeoff (spec §10, §48 Q6: "why can too many indexes
-- hurt ingestion?"). Keeping index creation separate from table creation
-- lets you run ingestion both WITH and WITHOUT these indexes applied,
-- and measure the difference directly instead of just asserting it.
-- =============================================================================

-- ---------------------------------------------------------------------------
-- Exact-ID matching lookups (spec §12 "exact_id" matching strategy).
-- This is the single most important index in the schema: reconciliation's
-- SQL-side JOIN strategy (spec §15) and the C++ hash-matching strategy's
-- data-loading query both filter/join on transaction_id.
-- ---------------------------------------------------------------------------
CREATE INDEX IF NOT EXISTS idx_internal_transaction_id ON internal_transactions (transaction_id);
CREATE INDEX IF NOT EXISTS idx_external_transaction_id ON external_transactions (transaction_id);

-- ---------------------------------------------------------------------------
-- Composite-key matching strategy (spec §12: vendor_id + invoice_number + date).
-- A composite index supports this exact matching strategy, and also speeds
-- up the "find all transactions for vendor X" style analytical queries in
-- sql/sample_queries.sql.
-- ---------------------------------------------------------------------------
CREATE INDEX IF NOT EXISTS idx_internal_composite_key
    ON internal_transactions (vendor_id, invoice_number, transaction_date);
CREATE INDEX IF NOT EXISTS idx_external_composite_key
    ON external_transactions (vendor_id, invoice_number, transaction_date);

-- ---------------------------------------------------------------------------
-- Duplicate detection (spec §14). An index on transaction_id alone (already
-- created above) is sufficient for `GROUP BY transaction_id HAVING COUNT(*) > 1`
-- — no separate index needed here; noted explicitly so it's clear this was
-- considered, not missed.
-- ---------------------------------------------------------------------------

-- ---------------------------------------------------------------------------
-- Batch/audit lookups: "which rows came from this ingestion batch?"
-- ---------------------------------------------------------------------------
CREATE INDEX IF NOT EXISTS idx_internal_batch ON internal_transactions (ingestion_batch_id);
CREATE INDEX IF NOT EXISTS idx_external_batch ON external_transactions (ingestion_batch_id);

-- ---------------------------------------------------------------------------
-- Reconciliation results: results are almost always queried scoped to one
-- run, then optionally filtered by status (e.g. "show me all
-- AMOUNT_MISMATCH rows for run 12" — the CLI's `reconcile report` command).
-- ---------------------------------------------------------------------------
CREATE INDEX IF NOT EXISTS idx_results_run_id ON reconciliation_results (run_id);
CREATE INDEX IF NOT EXISTS idx_results_run_status ON reconciliation_results (run_id, status);
CREATE INDEX IF NOT EXISTS idx_results_transaction_id ON reconciliation_results (transaction_id);
