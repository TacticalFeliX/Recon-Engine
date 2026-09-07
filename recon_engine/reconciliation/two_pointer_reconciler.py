"""
TwoPointerReconciler
----------------------
Sorts both sides by key (the O(N log N + M log M) step), then does a single
linear merge-walk (O(N + M)) comparing keys pairwise, exactly like the merge
step of merge sort. Sorting is what makes the merge-walk a single linear
pass instead of needing to search.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import List

from ..models.transaction import Transaction
from . import match_classifier as mc
from .base import IReconciler
from .config_and_result import MatchingConfig, ReconciliationResult


@dataclass
class _KeyedRef:
    key: str
    tx: Transaction


def _group_end(keyed: List[_KeyedRef], start: int) -> int:
    """Returns the index one past the last element sharing keyed[start].key,
    i.e. [start, end) is the run of equal-key elements. Requires `keyed` to
    already be sorted by key."""
    end = start + 1
    while end < len(keyed) and keyed[end].key == keyed[start].key:
        end += 1
    return end


class TwoPointerReconciler(IReconciler):
    @property
    def name(self) -> str:
        return "two_pointer"

    def reconcile(
        self,
        internal_transactions: List[Transaction],
        external_transactions: List[Transaction],
        config: MatchingConfig,
    ) -> List[ReconciliationResult]:
        internal_keyed = [_KeyedRef(mc.build_key(t, config.strategy), t) for t in internal_transactions]
        external_keyed = [_KeyedRef(mc.build_key(t, config.strategy), t) for t in external_transactions]

        # The O(N log N + M log M) step.
        internal_keyed.sort(key=lambda kr: kr.key)
        external_keyed.sort(key=lambda kr: kr.key)

        results: List[ReconciliationResult] = []
        i = j = 0
        n, m = len(internal_keyed), len(external_keyed)

        while i < n or j < m:
            have_internal = i < n
            have_external = j < m

            if have_internal and (not have_external or internal_keyed[i].key < external_keyed[j].key):
                # Internal-only key at this point in the merge.
                end = _group_end(internal_keyed, i)
                group_size = end - i
                for k in range(i, end):
                    results.append(
                        mc.duplicate(internal_keyed[k].tx) if group_size > 1
                        else mc.missing_external(internal_keyed[k].tx)
                    )
                i = end

            elif have_external and (not have_internal or external_keyed[j].key < internal_keyed[i].key):
                # External-only key.
                end = _group_end(external_keyed, j)
                group_size = end - j
                for k in range(j, end):
                    results.append(
                        mc.duplicate(external_keyed[k].tx) if group_size > 1
                        else mc.missing_internal(external_keyed[k].tx)
                    )
                j = end

            else:
                # Equal keys on both sides.
                i_end = _group_end(internal_keyed, i)
                j_end = _group_end(external_keyed, j)
                internal_group_size = i_end - i
                external_group_size = j_end - j

                if internal_group_size > 1 or external_group_size > 1:
                    # See hash_reconciler.py for the full rationale: both
                    # sides' records under this key are reported as
                    # duplicate once EITHER side is ambiguous, so a size-1
                    # side under an ambiguous key is never silently dropped.
                    for k in range(i, i_end):
                        results.append(mc.duplicate(internal_keyed[k].tx))
                    for k in range(j, j_end):
                        results.append(mc.duplicate(external_keyed[k].tx))
                else:
                    results.append(mc.classify_match(internal_keyed[i].tx, external_keyed[j].tx, config))

                i, j = i_end, j_end

        return results
