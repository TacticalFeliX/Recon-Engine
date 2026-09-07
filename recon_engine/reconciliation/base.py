"""
IReconciler
------------
Common interface for all three algorithms. This is what lets the CLI do
`reconcile run --algorithm hash` vs `--algorithm two-pointer` by swapping
which concrete class it instantiates, and what makes it possible to run the
exact same input through all three and compare wall-clock time directly.
"""
from __future__ import annotations

from abc import ABC, abstractmethod
from typing import List

from ..models.transaction import Transaction
from .config_and_result import MatchingConfig, ReconciliationResult


class IReconciler(ABC):
    @abstractmethod
    def reconcile(
        self,
        internal_transactions: List[Transaction],
        external_transactions: List[Transaction],
        config: MatchingConfig,
    ) -> List[ReconciliationResult]:
        ...

    @property
    @abstractmethod
    def name(self) -> str:
        """Short machine-readable name, used for logging and as the CLI's
        --algorithm value (matches the reconciliation_algorithm SQL enum)."""
        ...
