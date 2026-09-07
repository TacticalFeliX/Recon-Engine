-- =============================================================================
-- reset_database.sql
-- Drops everything this project owns, in FK-safe order, so `reconcile
-- init-db` (or manually re-running schema.sql + indexes.sql) always starts
-- from a known-clean state. Used heavily by the demo scenario (spec §49)
-- and by tests that need a predictable starting point.
-- =============================================================================

DROP TABLE IF EXISTS reconciliation_results CASCADE;
DROP TABLE IF EXISTS reconciliation_runs CASCADE;
DROP TABLE IF EXISTS ingestion_errors CASCADE;
DROP TABLE IF EXISTS internal_transactions CASCADE;
DROP TABLE IF EXISTS external_transactions CASCADE;
DROP TABLE IF EXISTS ingestion_batches CASCADE;

DROP TYPE IF EXISTS reconciliation_status;
DROP TYPE IF EXISTS source_system_type;
DROP TYPE IF EXISTS reconciliation_algorithm;
