"""
BruteForceReconciler
----------------------
The deliberate unoptimized baseline: no sorting, no hashing, anywhere in
this file. For every internal record, count (via naive linear scans) how
many internal records share its key and how many external records share its
key; symmetrically for external records. This nested-loop cross-counting is
what makes this class O(n^2 + m^2 + n*m).

Exists specifically so the complexity difference against TwoPointerReconciler
(O(N log N + M log M)) and HashReconciler (O(N+M) expected) is something you
can *measure* on the same dataset, not just assert.
"""
from __future__ import annotations

from typing import List

from ..models.transaction import Transaction
from . import match_classifier as mc
from .base import IReconciler
from .config_and_result import MatchingConfig, ReconciliationResult


class BruteForceReconciler(IReconciler):
    @property
    def name(self) -> str:
        return "brute_force"

    def reconcile(
        self,
        internal_transactions: List[Transaction],
        external_transactions: List[Transaction],
        config: MatchingConfig,
    ) -> List[ReconciliationResult]:
        n = len(internal_transactions)
        m = len(external_transactions)

        internal_keys = [mc.build_key(t, config.strategy) for t in internal_transactions]
        external_keys = [mc.build_key(t, config.strategy) for t in external_transactions]

        # For every internal record, count how many internal records (self
        # included) share its key, and how many external records share its
        # key -- both via naive linear scans. O(n^2 + n*m).
        internal_own_count = [0] * n
        internal_cross_count = [0] * n
        for i in range(n):
            for k in range(n):
                if internal_keys[k] == internal_keys[i]:
                    internal_own_count[i] += 1
            for j in range(m):
                if external_keys[j] == internal_keys[i]:
                    internal_cross_count[i] += 1

        external_own_count = [0] * m
        external_cross_count = [0] * m
        for j in range(m):
            for k in range(m):
                if external_keys[k] == external_keys[j]:
                    external_own_count[j] += 1
            for i in range(n):
                if internal_keys[i] == external_keys[j]:
                    external_cross_count[j] += 1

        results: List[ReconciliationResult] = []

        # Internal side: duplicate (ambiguous key on either side), matched
        # (exactly one record on each side), or missing-external (no
        # external record shares this key at all).
        for i in range(n):
            if internal_own_count[i] > 1 or internal_cross_count[i] > 1:
                results.append(mc.duplicate(internal_transactions[i]))
            elif internal_cross_count[i] == 1:
                # Exactly one external record shares this key -- find it via
                # linear scan (this scan, repeated per internal record, is
                # the O(n*m) matching pass).
                for j in range(m):
                    if external_keys[j] == internal_keys[i]:
                        results.append(mc.classify_match(internal_transactions[i], external_transactions[j], config))
                        break
            else:
                results.append(mc.missing_external(internal_transactions[i]))

        # External side: only records whose key never appeared on the
        # internal side (missing-internal), or whose key is ambiguous on
        # either side (duplicate) -- the 1-to-1 matched case was already
        # emitted by the internal-side loop above, so it's intentionally
        # not repeated here.
        for j in range(m):
            if external_own_count[j] > 1 or external_cross_count[j] > 1:
                results.append(mc.duplicate(external_transactions[j]))
            elif external_cross_count[j] == 0:
                results.append(mc.missing_internal(external_transactions[j]))
            # external_cross_count[j] == 1 and external_own_count[j] == 1:
            # this is the clean 1-to-1 match, already reported above.

        return results
