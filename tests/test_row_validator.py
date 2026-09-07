import pytest

from recon_engine.ingestion.row_validator import RowValidator
from recon_engine.models.transaction import SourceSystem

GOOD_ROW = ["TX1001", "INV100", "V-42", "2026-07-01", "125.50", "22.50", "INR"]


@pytest.fixture
def validator():
    return RowValidator()


def test_valid_row_parses(validator):
    result = validator.validate_row(GOOD_ROW, row_number=2, source=SourceSystem.INTERNAL)
    assert result.is_valid
    t = result.transaction
    assert t.transaction_id == "TX1001"
    assert t.amount.cents() == 12550
    assert t.tax_amount.cents() == 2250
    assert t.currency == "INR"
    assert t.source == SourceSystem.INTERNAL
    assert t.source_row_number == 2


def test_wrong_column_count_rejected(validator):
    row = GOOD_ROW[:-1]  # drop currency
    result = validator.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result.is_valid
    assert "columns" in result.error_message


def test_missing_transaction_id_rejected(validator):
    row = list(GOOD_ROW)
    row[0] = ""
    result = validator.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result.is_valid
    assert "transaction_id" in result.error_message


def test_missing_invoice_number_rejected(validator):
    row = list(GOOD_ROW)
    row[1] = ""
    result = validator.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result.is_valid
    assert "invoice_number" in result.error_message


def test_missing_vendor_id_rejected(validator):
    row = list(GOOD_ROW)
    row[2] = ""
    result = validator.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result.is_valid
    assert "vendor_id" in result.error_message


def test_invalid_date_rejected(validator):
    row = list(GOOD_ROW)
    row[3] = "2026-13-40"
    result = validator.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result.is_valid
    assert "transaction_date" in result.error_message


def test_malformed_amount_rejected(validator):
    row = list(GOOD_ROW)
    row[4] = "not-a-number"
    result = validator.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result.is_valid
    assert "amount" in result.error_message


def test_negative_amount_rejected(validator):
    row = list(GOOD_ROW)
    row[4] = "-10.00"
    result = validator.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result.is_valid
    assert "negative amount" in result.error_message


def test_negative_tax_rejected(validator):
    row = list(GOOD_ROW)
    row[5] = "-1.00"
    result = validator.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result.is_valid
    assert "negative tax_amount" in result.error_message


def test_unsupported_currency_rejected(validator):
    row = list(GOOD_ROW)
    row[6] = "JPY"
    result = validator.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result.is_valid
    assert "currency" in result.error_message


def test_custom_currency_set():
    v = RowValidator(supported_currencies=frozenset({"JPY"}))
    row = list(GOOD_ROW)
    row[6] = "JPY"
    result = v.validate_row(row, 2, SourceSystem.INTERNAL)
    assert result.is_valid

    row[6] = "INR"
    result2 = v.validate_row(row, 2, SourceSystem.INTERNAL)
    assert not result2.is_valid


def test_duplicate_transaction_id_is_not_a_validation_error(validator):
    # By design, RowValidator does not check for duplicates -- that's a
    # reconciliation-time concern (DUPLICATE status), not an ingestion
    # rejection reason.
    r1 = validator.validate_row(GOOD_ROW, 2, SourceSystem.INTERNAL)
    r2 = validator.validate_row(GOOD_ROW, 3, SourceSystem.INTERNAL)
    assert r1.is_valid and r2.is_valid
