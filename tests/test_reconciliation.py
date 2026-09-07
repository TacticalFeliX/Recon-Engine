import random

import pytest

from recon_engine.models.date_value import Date
from recon_engine.models.money import Money
from recon_engine.models.transaction import SourceSystem, Transaction
from recon_engine.reconciliation import (
    BruteForceReconciler,
    HashReconciler,
    MatchingConfig,
    ReconciliationStatus,
    TwoPointerReconciler,
)

ALGORITHMS = [BruteForceReconciler(), TwoPointerReconciler(), HashReconciler()]


def make_tx(tid, vendor="V1", date="2026-07-01", amount="100.00", tax="10.00",
            currency="INR", source=SourceSystem.INTERNAL, invoice=None):
    return Transaction(
        transaction_id=tid,
        invoice_number=invoice or f"INV-{tid}",
        vendor_id=vendor,
        transaction_date=Date.parse(date),
        amount=Money.from_decimal_string(amount),
        tax_amount=Money.from_decimal_string(tax),
        currency=currency,
        source=source,
    )


def statuses(results):
    return sorted(r.status.value for r in results)


@pytest.mark.parametrize("reconciler", ALGORITHMS, ids=lambda r: r.name)
def test_clean_match(reconciler):
    internal = [make_tx("TX1", source=SourceSystem.INTERNAL)]
    external = [make_tx("TX1", source=SourceSystem.EXTERNAL)]
    results = reconciler.reconcile(internal, external, MatchingConfig())
    assert len(results) == 1
    assert results[0].status == ReconciliationStatus.MATCHED


@pytest.mark.parametrize("reconciler", ALGORITHMS, ids=lambda r: r.name)
def test_amount_mismatch(reconciler):
    internal = [make_tx("TX1", amount="100.00", source=SourceSystem.INTERNAL)]
    external = [make_tx("TX1", amount="105.00", source=SourceSystem.EXTERNAL)]
    results = reconciler.reconcile(internal, external, MatchingConfig())
    assert len(results) == 1
    assert results[0].status == ReconciliationStatus.AMOUNT_MISMATCH
    assert results[0].amount_difference_cents == 500


@pytest.mark.parametrize("reconciler", ALGORITHMS, ids=lambda r: r.name)
def test_amount_within_tolerance_is_matched(reconciler):
    internal = [make_tx("TX1", amount="100.00", source=SourceSystem.INTERNAL)]
    external = [make_tx("TX1", amount="100.02", source=SourceSystem.EXTERNAL)]
    config = MatchingConfig(amount_tolerance_cents=2)
    results = reconciler.reconcile(internal, external, config)
    assert results[0].status == ReconciliationStatus.MATCHED


@pytest.mark.parametrize("reconciler", ALGORITHMS, ids=lambda r: r.name)
def test_multiple_mismatches(reconciler):
    internal = [make_tx("TX1", amount="100.00", vendor="V1", source=SourceSystem.INTERNAL)]
    external = [make_tx("TX1", amount="200.00", vendor="V2", source=SourceSystem.EXTERNAL)]
    results = reconciler.reconcile(internal, external, MatchingConfig())
    assert results[0].status == ReconciliationStatus.MULTIPLE_MISMATCHES


@pytest.mark.parametrize("reconciler", ALGORITHMS, ids=lambda r: r.name)
def test_missing_external(reconciler):
    internal = [make_tx("TX1", source=SourceSystem.INTERNAL)]
    results = reconciler.reconcile(internal, [], MatchingConfig())
    assert len(results) == 1
    assert results[0].status == ReconciliationStatus.MISSING_EXTERNAL


@pytest.mark.parametrize("reconciler", ALGORITHMS, ids=lambda r: r.name)
def test_missing_internal(reconciler):
    external = [make_tx("TX1", source=SourceSystem.EXTERNAL)]
    results = reconciler.reconcile([], external, MatchingConfig())
    assert len(results) == 1
    assert results[0].status == ReconciliationStatus.MISSING_INTERNAL


@pytest.mark.parametrize("reconciler", ALGORITHMS, ids=lambda r: r.name)
def test_duplicate_on_internal_side_only(reconciler):
    # Two internal records share a key; one external record shares it too.
    # Per the documented duplicate policy, ALL records under an ambiguous
    # key -- on both sides -- become DUPLICATE, including the lone
    # external record.
    internal = [make_tx("TX1", source=SourceSystem.INTERNAL), make_tx("TX1", source=SourceSystem.INTERNAL)]
    external = [make_tx("TX1", source=SourceSystem.EXTERNAL)]
    results = reconciler.reconcile(internal, external, MatchingConfig())
    assert len(results) == 3
    assert all(r.status == ReconciliationStatus.DUPLICATE for r in results)


@pytest.mark.parametrize("reconciler", ALGORITHMS, ids=lambda r: r.name)
def test_empty_inputs(reconciler):
    assert reconciler.reconcile([], [], MatchingConfig()) == []


def test_all_three_algorithms_agree_on_random_data():
    """The core consistency guarantee this project is built around: three
    different-complexity algorithms must produce identical classification
    outcomes on the same data. This is what actually justifies having three
    implementations instead of just claiming Big-O differences on paper."""
    rng = random.Random(42)
    internal = []
    external = []

    # Clean matches
    for i in range(20):
        tid = f"TX{i}"
        internal.append(make_tx(tid, amount=f"{100 + i}.00", source=SourceSystem.INTERNAL))
        external.append(make_tx(tid, amount=f"{100 + i}.00", source=SourceSystem.EXTERNAL))

    # Amount mismatches
    for i in range(20, 25):
        tid = f"TX{i}"
        internal.append(make_tx(tid, amount="100.00", source=SourceSystem.INTERNAL))
        external.append(make_tx(tid, amount="150.00", source=SourceSystem.EXTERNAL))

    # Missing external
    for i in range(25, 28):
        internal.append(make_tx(f"TX{i}", source=SourceSystem.INTERNAL))

    # Missing internal
    for i in range(28, 30):
        external.append(make_tx(f"TX{i}", source=SourceSystem.EXTERNAL))

    # Duplicates
    internal.append(make_tx("DUP1", source=SourceSystem.INTERNAL))
    internal.append(make_tx("DUP1", source=SourceSystem.INTERNAL))
    external.append(make_tx("DUP1", source=SourceSystem.EXTERNAL))

    rng.shuffle(internal)
    rng.shuffle(external)

    result_sets = [statuses(r.reconcile(internal, external, MatchingConfig())) for r in ALGORITHMS]
    assert result_sets[0] == result_sets[1] == result_sets[2]

    counts = {s: result_sets[0].count(s) for s in set(result_sets[0])}
    assert counts["MATCHED"] == 20
    assert counts["AMOUNT_MISMATCH"] == 5
    assert counts["MISSING_EXTERNAL"] == 3
    assert counts["MISSING_INTERNAL"] == 2
    assert counts["DUPLICATE"] == 3  # 2 internal + 1 external, all under DUP1
