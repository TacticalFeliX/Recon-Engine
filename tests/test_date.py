import pytest

from recon_engine.models.date_value import Date, DateParseError


def test_parse_valid_date():
    d = Date.parse("2026-07-01")
    assert (d.year, d.month, d.day) == (2026, 7, 1)


def test_to_string_roundtrip():
    assert Date.parse("2026-07-01").to_string() == "2026-07-01"


def test_parse_rejects_wrong_length():
    with pytest.raises(DateParseError):
        Date.parse("2026-7-1")


def test_parse_rejects_wrong_separators():
    with pytest.raises(DateParseError):
        Date.parse("2026/07/01")


def test_parse_rejects_invalid_month():
    with pytest.raises(DateParseError):
        Date.parse("2026-13-01")


def test_parse_rejects_invalid_day_for_month():
    with pytest.raises(DateParseError):
        Date.parse("2026-02-30")


def test_parse_rejects_feb_29_non_leap_year():
    with pytest.raises(DateParseError):
        Date.parse("2026-02-29")  # 2026 is not a leap year


def test_parse_accepts_feb_29_leap_year():
    d = Date.parse("2024-02-29")
    assert (d.year, d.month, d.day) == (2024, 2, 29)


def test_parse_rejects_non_numeric():
    with pytest.raises(DateParseError):
        Date.parse("YYYY-MM-DD")


def test_equality():
    assert Date.parse("2026-07-01") == Date.parse("2026-07-01")
    assert Date.parse("2026-07-01") != Date.parse("2026-07-02")


def test_ordering():
    assert Date.parse("2026-07-01") < Date.parse("2026-07-02")
    assert Date.parse("2025-12-31") < Date.parse("2026-01-01")
