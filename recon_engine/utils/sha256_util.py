"""
Thin wrapper around hashlib.sha256 so callers depend on this module's name
(matching the original C++ Sha256 utility) rather than reaching for
`hashlib` ad hoc in multiple places. Used for ingestion-file idempotency:
hashing the whole file's bytes lets `create_batch`/`find_existing_batch`
detect "this exact file was already ingested" via a UNIQUE(source_system,
file_hash) constraint, instead of re-reading and re-diffing file contents.
"""
import hashlib


def hash_file_contents(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()
