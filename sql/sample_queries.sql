-- =============================================================================
-- sample_queries.sql — demonstration and teaching queries.
--
-- Run these against a database that already has data from `reconcile ingest`
-- and at least one `reconcile run` completed.
-- =============================================================================

-- -----------------------------------------------------------------------------
-- Query 1: SQL-side reconciliation via FULL OUTER JOIN (spec Section 15).
--
-- This is the "let the database do it" alternative to the C++ algorithms in
-- include/reconciliation/. It's genuinely fast for exact_id matching at
-- moderate scale (Postgres can hash-join or merge-join a FULL OUTER JOIN
-- itself, using the idx_internal_transaction_id / idx_external_transaction_id
-- indexes from sql/indexes.sql), and requires no data leaving the database
-- at all.
--
-- KNOWN, VERIFIED LIMITATION vs the C++ MatchClassifier (this was checked
-- by actually running this query against the demo dataset and diffing it
-- against the C++ HashReconciler's output, not assumed):
--   1. Duplicates: when a transaction_id appears more than once on either
--      side (e.g. the demo's TX1009, duplicated in external_transactions),
--      a FULL OUTER JOIN produces the cross product of matching rows —
--      here, 2 joined rows (one per external duplicate), each looking like
--      an ordinary potential match. The CASE expression below has no
--      concept of "this key is ambiguous", so it will emit a MATCHED or
--      MISMATCH verdict for each of those rows individually, rather than
--      the single, symmetric DUPLICATE classification the C++ path
--      produces via MatchClassifier::duplicate(). Getting the SQL query to
--      replicate that would require a window-function pass (COUNT(*) OVER
--      (PARTITION BY transaction_id)) added on each side before the join —
--      solvable, but not "free" the way the join itself is.
--   2. Multiple simultaneous mismatches: the CASE expression below returns
--      the FIRST failing condition, matching only one mismatch type, where
--      MatchClassifier reports every differing field and uses
--      MULTIPLE_MISMATCHES when more than one applies (spec Section 13
--      explicitly requires not losing that information). Reproducing that
--      in pure SQL is possible but reads far less clearly than the
--      equivalent ~15 lines of C++ in MatchClassifier::classifyMatch.
-- These aren't reasons to avoid the SQL approach — for a straightforward
-- exact-match reconciliation with no duplicates expected, this query is a
-- perfectly good, fast choice. They're reasons the C++ path exists as a
-- richer alternative, and reasons to be explicit in an interview about
-- what each approach actually guarantees.
-- -----------------------------------------------------------------------------
WITH joined AS (
    SELECT
        COALESCE(i.transaction_id, e.transaction_id) AS transaction_id,
        i.id AS internal_id,
        e.id AS external_id,
        i.amount_cents AS internal_amount_cents,
        e.amount_cents AS external_amount_cents,
        i.tax_amount_cents AS internal_tax_cents,
        e.tax_amount_cents AS external_tax_cents,
        i.transaction_date AS internal_date,
        e.transaction_date AS external_date,
        i.vendor_id AS internal_vendor,
        e.vendor_id AS external_vendor
    FROM internal_transactions i
    FULL OUTER JOIN external_transactions e ON i.transaction_id = e.transaction_id
)
SELECT
    transaction_id,
    CASE
        WHEN internal_id IS NULL THEN 'MISSING_INTERNAL'
        WHEN external_id IS NULL THEN 'MISSING_EXTERNAL'
        WHEN internal_amount_cents <> external_amount_cents THEN 'AMOUNT_MISMATCH'
        WHEN internal_tax_cents <> external_tax_cents THEN 'TAX_MISMATCH'
        WHEN internal_date <> external_date THEN 'DATE_MISMATCH'
        WHEN internal_vendor <> external_vendor THEN 'VENDOR_MISMATCH'
        ELSE 'MATCHED'
    END AS status
FROM joined
ORDER BY transaction_id;

-- -----------------------------------------------------------------------------
-- Query 2: status breakdown for a specific run (what `reconcile report
-- --run N` uses under the hood — Module 6).
-- -----------------------------------------------------------------------------
SELECT status, COUNT(*) AS count
FROM reconciliation_results
WHERE run_id = 1
GROUP BY status
ORDER BY count DESC;

-- -----------------------------------------------------------------------------
-- Query 3: duplicate detection directly on raw ingested data (no
-- reconciliation run required) — GROUP BY / HAVING, spec Section 14 & 26.
-- -----------------------------------------------------------------------------
SELECT transaction_id, COUNT(*) AS occurrences
FROM external_transactions
GROUP BY transaction_id
HAVING COUNT(*) > 1;

-- -----------------------------------------------------------------------------
-- Query 4: comparing algorithms across historical runs — this is the query
-- a duration comparison across algorithms would be built from. Demonstrates why
-- reconciliation_runs exists as its own table rather than being derived
-- from reconciliation_results: run-level stats (duration, algorithm) are
-- cheap to query without touching the (much larger) results table.
-- -----------------------------------------------------------------------------
SELECT algorithm, records_internal, records_external, duration_ms, matched_count, mismatched_count
FROM reconciliation_runs
ORDER BY started_at DESC;

-- -----------------------------------------------------------------------------
-- Query 5: full audit trail for one transaction — which file it came from,
-- when, and what every reconciliation run concluded about it (spec Section 47).
-- -----------------------------------------------------------------------------
SELECT
    it.transaction_id,
    ib.file_name AS source_file,
    ib.started_at AS ingested_at,
    rr.run_id,
    rr.status,
    rr.mismatch_details
FROM internal_transactions it
JOIN ingestion_batches ib ON ib.id = it.ingestion_batch_id
LEFT JOIN reconciliation_results rr ON rr.internal_transaction_id = it.id
WHERE it.transaction_id = 'TX1002'
ORDER BY rr.run_id;
