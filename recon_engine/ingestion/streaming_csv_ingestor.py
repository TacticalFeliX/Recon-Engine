"""
StreamingCsvIngestor
---------------------
This is "Approach B" from the original design comparison:

    stream file -> parse batches -> hand batch to callback -> release memory

as opposed to "Approach A" (read the entire CSV into one big list[Transaction]
up front). Approach A is simpler but its peak memory usage is O(file size) —
a 10 GB CSV needs ~10 GB (plus per-object overhead, which is worse in Python
than C++) of RAM before a single row reaches the database. Approach B's peak
memory usage is O(batch_size), independent of file size, because completed
batches are handed to the caller's callback (typically an INSERT) and then
discarded before the next batch is read.

The tradeoff: Approach B is a little more code (batching + callback
plumbing) and, if the caller's callback is slow (e.g. an unbatched
round-trip per row), overall ingestion is only as fast as the slowest
stage — which is exactly why bulk-insert strategy (batched INSERT vs
COPY, see database/transaction_repository.py) is a separate, later
consideration from "did we stream the file".

WHY A CALLBACK INSTEAD OF RETURNING A LIST OF BATCHES?
    Returning all batches would just reintroduce Approach A's memory
    problem one level up. The callback lets the caller (typically
    TransactionRepository.insert_batch) consume and discard each batch
    before this class reads the next one.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, List

from ..models.transaction import SourceSystem, Transaction
from .csv_parser import split_line
from .row_validator import RowValidator
from .validation_error import ValidationError

BatchCallback = Callable[[List[Transaction]], None]


@dataclass
class IngestionSummary:
    rows_read: int = 0        # data rows read, excluding header
    rows_valid: int = 0
    rows_failed: int = 0
    errors: List[ValidationError] = field(default_factory=list)


class StreamingCsvIngestor:
    def __init__(self, validator: RowValidator, batch_size: int = 10_000):
        if batch_size <= 0:
            raise ValueError("StreamingCsvIngestor: batch_size must be > 0")
        self._validator = validator
        self._batch_size = batch_size

    def ingest(self, file_path: str, source: SourceSystem, on_batch: BatchCallback) -> IngestionSummary:
        """Streams `file_path`, validating each row, and invokes `on_batch`
        once per full batch (and once more at the end for any remaining
        partial batch). Raises FileNotFoundError if the file cannot be
        opened — that's a setup failure (bad path/permissions), not a
        row-level data-quality issue, so it's treated differently from
        validation errors."""
        summary = IngestionSummary()
        current_batch: List[Transaction] = []

        try:
            f = open(file_path, "r", newline="")
        except OSError as e:
            raise FileNotFoundError(f"StreamingCsvIngestor: could not open file: {file_path}") from e

        with f:
            header = f.readline()  # header row, discarded (not counted in rows_read)
            if header == "":
                return summary  # empty file: no header, no rows

            row_number = 1  # header is row 1
            for raw_line in f:
                row_number += 1
                line = raw_line.rstrip("\r\n")
                if line == "":
                    continue  # trailing blank lines are common in hand-edited CSVs; not an error

                summary.rows_read += 1
                fields = split_line(line)
                validated = self._validator.validate_row(fields, row_number, source)

                if validated.is_valid:
                    summary.rows_valid += 1
                    current_batch.append(validated.transaction)
                    if len(current_batch) >= self._batch_size:
                        on_batch(current_batch)
                        current_batch = []
                else:
                    summary.rows_failed += 1
                    summary.errors.append(ValidationError(row_number, line, validated.error_message))

        if current_batch:
            on_batch(current_batch)

        return summary
