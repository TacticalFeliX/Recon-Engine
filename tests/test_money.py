import pytest

from recon_engine.models.money import Money


def test_from_decimal_string_basic():
    assert Money.from_decimal_string("125.50").cents() == 12550


def test_from_decimal_string_no_fraction():
    assert Money.from_decimal_string("12.5").cents() == 1250  # pads to 2 decimals


def test_from_decimal_string_whole_number():
    assert Money.from_decimal_string("100").cents() == 10000


def test_from_decimal_string_negative():
    assert Money.from_decimal_string("-50.25").cents() == -5025


def test_from_decimal_string_explicit_plus():
    assert Money.from_decimal_string("+50.25").cents() == 5025


def test_from_decimal_string_truncates_beyond_two_decimals():
    # sub-cent digits are dropped, not rounded (documented behavior)
    assert Money.from_decimal_string("1.239").cents() == 123


def test_from_decimal_string_empty_raises():
    with pytest.raises(ValueError):
        Money.from_decimal_string("")


def test_from_decimal_string_multiple_dots_raises():
    with pytest.raises(ValueError):
        Money.from_decimal_string("1.2.3")


def test_from_decimal_string_invalid_char_raises():
    with pytest.raises(ValueError):
        Money.from_decimal_string("12a.50")


def test_from_decimal_string_no_digits_raises():
    with pytest.raises(ValueError):
        Money.from_decimal_string("-")


def test_from_decimal_string_only_dot_raises():
    with pytest.raises(ValueError):
        Money.from_decimal_string(".")


def test_to_string_roundtrip():
    m = Money.from_decimal_string("4200.50")
    assert m.to_string() == "4200.50"


def test_to_string_negative():
    m = Money(-99)
    assert m.to_string() == "-0.99"


def test_arithmetic_exact_no_float_drift():
    # The whole point of Money: 0.1 + 0.2 == 0.3 exactly, unlike floats.
    a = Money.from_decimal_string("0.10")
    b = Money.from_decimal_string("0.20")
    assert (a + b) == Money.from_decimal_string("0.30")


def test_abs_difference_cents():
    a = Money.from_decimal_string("10.00")
    b = Money.from_decimal_string("10.05")
    assert a.abs_difference_cents(b) == 5
    assert b.abs_difference_cents(a) == 5


def test_equality_and_hash():
    a = Money.from_decimal_string("10.00")
    b = Money(1000)
    assert a == b
    assert hash(a) == hash(b)


def test_ordering():
    assert Money(100) < Money(200)
    assert not (Money(200) < Money(100))


def test_to_float():
    assert Money.from_decimal_string("12.34").to_float() == pytest.approx(12.34)
