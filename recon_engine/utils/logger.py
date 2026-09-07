"""Minimal leveled logger to stderr/stdout — deliberately not using Python's
`logging` module's full configuration machinery, since this CLI has exactly
one output destination and three levels. Mirrors the original C++ Logger's
scope."""
import sys


class Logger:
    @staticmethod
    def info(message: str) -> None:
        print(f"[INFO] {message}")

    @staticmethod
    def warn(message: str) -> None:
        print(f"[WARN] {message}", file=sys.stderr)

    @staticmethod
    def error(message: str) -> None:
        print(f"[ERROR] {message}", file=sys.stderr)
