"""
TransactionRepository
------------------------
Owns all SQL touching internal_transactions / external_transactions.

Two insert paths are provided so the "batched INSERT vs COPY" performance
question (raised on the resume as "optimized SQL queries... sub-second
latency") can actually be *measured* instead of asserted:

  insert_batch()       -- psycopg2.extras.execute_values: one round-trip
                           per batch, multi-row VALUES list. Correct,
                           atomic, and reasonably fast; the default path.

  bulk_insert_via_copy() -- COPY FROM STDIN via psycopg2's copy_expert:
                           Postgres's fast-path bulk loader, which avoids
                           per-row SQL parsing/planning and batches WAL
                           writes. Exists specifically so scripts/benchmark.py
                           can report real numbers for "how much faster is
                           COPY than batched INSERT on this machine, at this
                           volume" rather than repeating the commonly-cited
                           but unverified claim.

ATOMICITY: insert_batch() wraps the whole batch in one transaction (psycopg2
connections are non-autocommit by default here — see connection.py). If any
row fails, the whole batch rolls back: a batch must never leave the database
in a partial state where "25,000 of 50,000 records" silently exist with no
record of the failure.
"""
from __future__ import annotations

import io
from typing import List

import psycopg2
import psycopg2.extras

from ..models.date_value import Date
from ..models.money import Money
from ..models.transaction import SourceSystem, Transaction
from .connection import DatabaseConnection, DatabaseException

_COLUMNS = (
    "transaction_id", "invoice_number", "vendor_id", "transaction_date",
    "amount_cents", "tax_amount_cents", "currency", "ingestion_batch_id", "source_row_number",
)


class TransactionRepository:
    def __init__(self, connection: DatabaseConnection):
        self._connection = connection

    @staticmethod
    def _table_for(source: SourceSystem) -> str:
        # Table name comes from this fixed two-way mapping, never from user
        # input, so string-formatting it into SQL below is safe. Contrast
        # this with %s bound parameters used for every actual data value —
        # those go through psycopg2's parameterized-query mechanism
        # specifically because they DO originate from file contents.
        return "internal_transactions" if source == SourceSystem.INTERNAL else "external_transactions"

    def insert_batch(self, source: SourceSystem, transactions: List[Transaction], batch_id: int) -> int:
        if not transactions:
            return 0

        table = self._table_for(source)
        sql = (
            f"INSERT INTO {table} "
            f"({', '.join(_COLUMNS)}) VALUES %s"
        )
        rows = [
            (
                t.transaction_id, t.invoice_number, t.vendor_id, t.transaction_date.to_string(),
                t.amount.cents(), t.tax_amount.cents(), t.currency, batch_id, t.source_row_number,
            )
            for t in transactions
        ]

        try:
            with self._connection.handle.cursor() as cur:
                psycopg2.extras.execute_values(cur, sql, rows)
            self._connection.handle.commit()
            return len(transactions)
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"insertBatch failed ({table}): {e}") from e

    def bulk_insert_via_copy(self, source: SourceSystem, transactions: List[Transaction], batch_id: int) -> int:
        """COPY-based bulk loader — see module docstring. Uses a text buffer
        with tab-separated values (COPY's default text format) rather than
        row-by-row exec_params."""
        if not transactions:
            return 0

        table = self._table_for(source)
        buf = io.StringIO()
        for t in transactions:
            fields = [
                t.transaction_id, t.invoice_number, t.vendor_id, t.transaction_date.to_string(),
                str(t.amount.cents()), str(t.tax_amount.cents()), t.currency,
                str(batch_id), str(t.source_row_number),
            ]
            # COPY's text format needs backslash-escaping for literal tabs/
            # newlines/backslashes in the data; none of these fields can
            # legitimately contain those characters per RowValidator, but
            # escaping defensively costs nothing.
            escaped = [f.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n") for f in fields]
            buf.write("\t".join(escaped) + "\n")
        buf.seek(0)

        try:
            with self._connection.handle.cursor() as cur:
                cur.copy_expert(
                    f"COPY {table} ({', '.join(_COLUMNS)}) FROM STDIN WITH (FORMAT text)",
                    buf,
                )
            self._connection.handle.commit()
            return len(transactions)
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"bulkInsertViaCopy failed ({table}): {e}") from e

    def find_all(self, source: SourceSystem) -> List[Transaction]:
        table = self._table_for(source)
        sql = (
            "SELECT id, transaction_id, invoice_number, vendor_id, transaction_date, "
            "       amount_cents, tax_amount_cents, currency, source_row_number "
            f"FROM {table} ORDER BY id"
        )
        try:
            with self._connection.handle.cursor() as cur:
                cur.execute(sql)
                rows = cur.fetchall()
            self._connection.handle.commit()
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"findAll failed ({table}): {e}") from e

        results = []
        for row in rows:
            db_id, tid, invoice, vendor, tx_date, amount_cents, tax_cents, currency, row_num = row
            results.append(Transaction(
                transaction_id=tid,
                invoice_number=invoice,
                vendor_id=vendor,
                transaction_date=Date.parse(tx_date.isoformat() if hasattr(tx_date, "isoformat") else tx_date),
                amount=Money(amount_cents),
                tax_amount=Money(tax_cents),
                currency=currency,
                source=source,
                source_row_number=row_num,
                db_id=db_id,
            ))
        return results

    def count_all(self, source: SourceSystem) -> int:
        table = self._table_for(source)
        try:
            with self._connection.handle.cursor() as cur:
                cur.execute(f"SELECT COUNT(*) FROM {table}")
                count = cur.fetchone()[0]
            self._connection.handle.commit()
            return count
        except psycopg2.Error as e:
            self._connection.handle.rollback()
            raise DatabaseException(f"countAll failed ({table}): {e}") from e
