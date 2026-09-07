from dataclasses import dataclass


@dataclass
class ValidationError:
    row_number: int
    raw_line: str
    message: str
