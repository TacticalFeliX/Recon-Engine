import pytest

from recon_engine.ingestion.csv_parser import split_line
from recon_engine.ingestion.row_validator import RowValidator
from recon_engine.ingestion.streaming_csv_ingestor import StreamingCsvIngestor
from recon_engine.models.transaction import SourceSystem


# --- csv_parser ---------------------------------------------------------

def test_split_simple_line():
    assert split_line("a,b,c") == ["a", "b", "c"]


def test_split_handles_quoted_comma():
    assert split_line('a,"b,c",d') == ["a", "b,c", "d"]


def test_split_handles_escaped_quote():
    assert split_line('a,"say ""hi""",c') == ["a", 'say "hi"', "c"]


def test_split_empty_fields():
    assert split_line(",,") == ["", "", ""]


# --- StreamingCsvIngestor -------------------------------------------------

HEADER = "transaction_id,invoice_number,vendor_id,transaction_date,amount,tax_amount,currency\n"


def write_csv(tmp_path, rows):
    path = tmp_path / "data.csv"
    with open(path, "w") as f:
        f.write(HEADER)
        for row in rows:
            f.write(row + "\n")
    return str(path)


def test_ingest_all_valid_single_batch(tmp_path):
    rows = [
        "TX1,INV1,V1,2026-07-01,100.00,10.00,INR",
        "TX2,INV2,V2,2026-07-02,200.00,20.00,INR",
    ]
    path = write_csv(tmp_path, rows)
    ingestor = StreamingCsvIngestor(RowValidator(), batch_size=10)

    batches_seen = []
    summary = ingestor.ingest(path, SourceSystem.INTERNAL, lambda batch: batches_seen.append(list(batch)))

    assert summary.rows_read == 2
    assert summary.rows_valid == 2
    assert summary.rows_failed == 0
    assert len(batches_seen) == 1
    assert len(batches_seen[0]) == 2


def test_ingest_respects_batch_size(tmp_path):
    rows = [f"TX{i},INV{i},V1,2026-07-01,100.00,10.00,INR" for i in range(5)]
    path = write_csv(tmp_path, rows)
    ingestor = StreamingCsvIngestor(RowValidator(), batch_size=2)

    batch_sizes = []
    ingestor.ingest(path, SourceSystem.INTERNAL, lambda batch: batch_sizes.append(len(batch)))

    # 5 rows, batch size 2 -> batches of 2, 2, 1
    assert batch_sizes == [2, 2, 1]


def test_ingest_isolates_malformed_rows_without_halting(tmp_path):
    rows = [
        "TX1,INV1,V1,2026-07-01,100.00,10.00,INR",   # valid
        ",INV2,V2,2026-07-02,200.00,20.00,INR",      # missing transaction_id
        "TX3,INV3,,2026-07-03,300.00,30.00,INR",     # missing vendor_id
        "TX4,INV4,V4,2026-13-40,400.00,40.00,INR",   # invalid date
        "TX5,INV5,V5,2026-07-05,500.00,50.00,INR",   # valid
    ]
    path = write_csv(tmp_path, rows)
    ingestor = StreamingCsvIngestor(RowValidator(), batch_size=10)

    batches_seen = []
    summary = ingestor.ingest(path, SourceSystem.INTERNAL, lambda batch: batches_seen.append(list(batch)))

    assert summary.rows_read == 5
    assert summary.rows_valid == 2
    assert summary.rows_failed == 3
    assert len(summary.errors) == 3
    # A single bad row must not abort processing of the good rows around it.
    valid_ids = {t.transaction_id for t in batches_seen[0]}
    assert valid_ids == {"TX1", "TX5"}


def test_ingest_skips_blank_lines(tmp_path):
    path = write_csv(tmp_path, ["TX1,INV1,V1,2026-07-01,100.00,10.00,INR", ""])
    ingestor = StreamingCsvIngestor(RowValidator(), batch_size=10)
    summary = ingestor.ingest(path, SourceSystem.INTERNAL, lambda batch: None)
    assert summary.rows_read == 1  # blank line not counted


def test_ingest_missing_file_raises():
    ingestor = StreamingCsvIngestor(RowValidator())
    with pytest.raises(FileNotFoundError):
        ingestor.ingest("/nonexistent/path.csv", SourceSystem.INTERNAL, lambda batch: None)


def test_batch_size_zero_rejected():
    with pytest.raises(ValueError):
        StreamingCsvIngestor(RowValidator(), batch_size=0)
