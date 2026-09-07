"""
Date
----
WHY a thin wrapper around datetime.date instead of just using datetime.date
everywhere directly?
    We do use datetime.date internally (it already gives us leap-year-aware,
    exact calendar validation, unlike the C++ original, which had to
    hand-roll isLeapYear/daysInMonth). The wrapper exists to enforce STRICT
    parsing: Python's datetime.strptime is lenient about some formats and,
    critically, datetime.date itself does not validate the *string shape*
    (e.g. "2026-7-1" vs "2026-07-01") — only the calendar values. Financial
    ingestion needs both: the shape must be exactly YYYY-MM-DD (matching the
    CSV format contract), and the calendar values must be real. A silently
    "fixed" date would corrupt the audit trail.
"""
from __future__ import annotations

import datetime as _dt


class DateParseError(ValueError):
    pass


class Date:
    __slots__ = ("_d",)

    def __init__(self, year: int, month: int, day: int):
        self._d = _dt.date(year, month, day)

    @classmethod
    def _from_date(cls, d: _dt.date) -> "Date":
        obj = cls.__new__(cls)
        obj._d = d
        return obj

    @classmethod
    def parse(cls, s: str) -> "Date":
        """Parses strict ISO-8601 "YYYY-MM-DD". Raises DateParseError on any
        malformed or out-of-range date (e.g. "2024-02-30" or "2026-7-1")."""
        if len(s) != 10 or s[4] != "-" or s[7] != "-":
            raise DateParseError(f"Date.parse: expected YYYY-MM-DD, got '{s}'")
        year_s, month_s, day_s = s[0:4], s[5:7], s[8:10]
        for part in (year_s, month_s, day_s):
            if not part.isdigit():
                raise DateParseError(f"Date.parse: non-numeric field in '{s}'")
        year, month, day = int(year_s), int(month_s), int(day_s)
        if not (1 <= month <= 12):
            raise DateParseError(f"Date.parse: invalid month in '{s}'")
        try:
            d = _dt.date(year, month, day)
        except ValueError:
            raise DateParseError(f"Date.parse: invalid day in '{s}'")
        return cls._from_date(d)

    def to_string(self) -> str:
        return self._d.isoformat()

    @property
    def year(self) -> int:
        return self._d.year

    @property
    def month(self) -> int:
        return self._d.month

    @property
    def day(self) -> int:
        return self._d.day

    def __eq__(self, other) -> bool:
        return isinstance(other, Date) and self._d == other._d

    def __lt__(self, other: "Date") -> bool:
        return self._d < other._d

    def __hash__(self) -> int:
        return hash(self._d)

    def __repr__(self) -> str:
        return f"Date({self.to_string()})"

    def __str__(self) -> str:
        return self.to_string()
