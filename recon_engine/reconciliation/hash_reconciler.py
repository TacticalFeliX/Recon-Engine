"""
HashReconciler
---------------
Builds a dict[key -> list[Transaction]] for each side (the O(N) / O(M)
indexing pass — Python dicts are hash tables, same as C++ unordered_map).
Using a list per bucket (rather than a single value) is what lets a key
legitimately hold more than one record — necessary to detect duplicates,
not just first-match.

Then walks the union of keys from both dicts once each — the O(N + M)
expected lookup pass (dict lookup/insert is O(1) average; worst case
degrades toward O(N*M) under adversarial hash collisions, same caveat as
the original C++ unordered_map-based version).
"""
from __future__ import annotations

from collections import defaultdict
from typing import Dict, List

from ..models.transaction import Transaction
from . import match_classifier as mc
from .base import IReconciler
from .config_and_result import MatchingConfig, ReconciliationResult


class HashReconciler(IReconciler):
    @property
    def name(self) -> str:
        return "hash"

    def reconcile(
        self,
        internal_transactions: List[Transaction],
        external_transactions: List[Transaction],
        config: MatchingConfig,
    ) -> List[ReconciliationResult]:
        internal_by_key: Dict[str, List[Transaction]] = defaultdict(list)
        for t in internal_transactions:
            internal_by_key[mc.build_key(t, config.strategy)].append(t)

        external_by_key: Dict[str, List[Transaction]] = defaultdict(list)
        for t in external_transactions:
            external_by_key[mc.build_key(t, config.strategy)].append(t)

        # Union of keys from both maps, visited once each.
        all_keys = set(internal_by_key.keys()) | set(external_by_key.keys())

        results: List[ReconciliationResult] = []

        for key in all_keys:
            internal_group = internal_by_key.get(key, [])
            external_group = external_by_key.get(key, [])
            internal_count = len(internal_group)
            external_count = len(external_group)

            if internal_count > 1 or external_count > 1:
                # Per the duplicate policy: once a key is ambiguous on
                # EITHER side, every record under that key on BOTH sides is
                # reported as duplicate — including a side that only has
                # one record under this key, since it can no longer be
                # matched unambiguously against an ambiguous opposite side.
                # Reporting only the >1 side would silently drop the other
                # side's record entirely, which is a data-loss bug, not a
                # simplification.
                for t in internal_group:
                    results.append(mc.duplicate(t))
                for t in external_group:
                    results.append(mc.duplicate(t))
            elif internal_count == 1 and external_count == 1:
                results.append(mc.classify_match(internal_group[0], external_group[0], config))
            elif internal_count == 1:
                results.append(mc.missing_external(internal_group[0]))
            else:  # external_count == 1
                results.append(mc.missing_internal(external_group[0]))

        return results
