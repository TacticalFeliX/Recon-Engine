"""
CsvParser
---------
A tiny, dependency-free line splitter that handles the two things Python's
str.split(",") gets wrong for real-world CSV: quoted fields containing
commas, and doubled-quote escaping inside a quoted field. This mirrors the
original C++ CsvParser::splitLine rather than reaching for the standard
library `csv` module, so the parsing behavior (and any edge-case bugs) are
visible and explainable line-by-line in an interview, exactly like the
original.

For a "real" production system, Python's built-in `csv` module (or
`pandas.read_csv`) would be the obvious choice — this hand-rolled version is
kept because it is what the original project actually implements, and this
port's job is to reproduce that project's behavior in Python, not to
silently swap in a different library with different edge-case semantics.
"""
from __future__ import annotations

from typing import List


def split_line(line: str) -> List[str]:
    fields: List[str] = []
    current: List[str] = []
    in_quotes = False
    i = 0
    n = len(line)

    while i < n:
        c = line[i]
        if in_quotes:
            if c == '"':
                if i + 1 < n and line[i + 1] == '"':
                    current.append('"')
                    i += 2
                    continue
                in_quotes = False
                i += 1
                continue
            current.append(c)
            i += 1
        else:
            if c == '"':
                in_quotes = True
                i += 1
            elif c == ",":
                fields.append("".join(current))
                current = []
                i += 1
            else:
                current.append(c)
                i += 1

    fields.append("".join(current))
    return fields
