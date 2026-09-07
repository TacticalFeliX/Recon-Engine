"""
Money
-----
WHY NOT float/Decimal-with-arithmetic-everywhere?
    Python floats have the exact same IEEE-754 problem as C++ floats/doubles:
    0.1 cannot be represented exactly in binary, so summing many monetary
    values as floats accumulates rounding error. For a system whose entire
    job is detecting *cent-level* mismatches between two data sources, that
    error could mask a real mismatch or invent a fake one.

WHY int cents instead of decimal.Decimal?
    decimal.Decimal *would* also solve the precision problem exactly, and is
    the more "idiomatic Python" choice. This project deliberately mirrors the
    C++ original's design instead: money is stored as an integer number of
    cents (e.g. $125.50 -> 12550), matching the PostgreSQL schema's
    `BIGINT ... amount_cents` columns exactly. That means no conversion or
    rounding step is needed when values cross the Python/SQL boundary, and
    integer equality/addition is trivially exact in both places. Decimal
    would work too, but would require a decimal<->cents translation at
    every read/write for no benefit here.
"""
from __future__ import annotations


class Money:
    __slots__ = ("_cents",)

    def __init__(self, cents: int = 0):
        self._cents = int(cents)

    @classmethod
    def from_decimal_string(cls, s: str) -> "Money":
        """Parses a decimal string like "12500.50" into exact cents (1250050).

        Raises ValueError on malformed input rather than silently truncating
        or guessing — financial ingestion code must never "guess" at a
        monetary value.
        """
        if not s:
            raise ValueError("Money.from_decimal_string: empty string")

        negative = False
        i = 0
        if s[0] == "-":
            negative = True
            i = 1
        elif s[0] == "+":
            i = 1

        whole_part = 0
        frac_part = 0
        saw_digit = False
        saw_dot = False
        frac_digits = 0

        n = len(s)
        while i < n:
            c = s[i]
            if c == ".":
                if saw_dot:
                    raise ValueError(f"Money.from_decimal_string: multiple decimal points in '{s}'")
                saw_dot = True
                i += 1
                continue
            if not c.isdigit():
                raise ValueError(f"Money.from_decimal_string: invalid character in '{s}'")
            saw_digit = True
            digit = ord(c) - ord("0")
            if not saw_dot:
                whole_part = whole_part * 10 + digit
            else:
                if frac_digits < 2:
                    frac_part = frac_part * 10 + digit
                    frac_digits += 1
                # Digits beyond the 2nd decimal place are intentionally
                # dropped rather than rounded, since sub-cent amounts are
                # not valid currency values in this system. A stricter
                # policy (reject instead of truncate) is a reasonable
                # alternative.
            i += 1

        if not saw_digit:
            raise ValueError(f"Money.from_decimal_string: no digits in '{s}'")

        while frac_digits < 2:
            frac_part *= 10
            frac_digits += 1

        total = whole_part * 100 + frac_part
        return cls(-total if negative else total)

    def cents(self) -> int:
        return self._cents

    def to_float(self) -> float:
        return self._cents / 100.0

    def to_string(self) -> str:
        c = self._cents
        neg = c < 0
        if neg:
            c = -c
        whole, frac = divmod(c, 100)
        return f"{'-' if neg else ''}{whole}.{frac:02d}"

    def abs_difference_cents(self, other: "Money") -> int:
        return abs(self._cents - other._cents)

    def __add__(self, other: "Money") -> "Money":
        return Money(self._cents + other._cents)

    def __sub__(self, other: "Money") -> "Money":
        return Money(self._cents - other._cents)

    def __eq__(self, other) -> bool:
        return isinstance(other, Money) and self._cents == other._cents

    def __lt__(self, other: "Money") -> bool:
        return self._cents < other._cents

    def __hash__(self) -> int:
        return hash(self._cents)

    def __repr__(self) -> str:
        return f"Money({self.to_string()})"

    def __str__(self) -> str:
        return self.to_string()
