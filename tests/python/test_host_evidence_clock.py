import time

import pytest

from tools.common import host_evidence_clock as clock


def test_host_evidence_clock_uses_high_resolution_monotonic_counter():
    identity = clock.identity()
    assert identity["schema"] == clock.SCHEMA
    assert identity["name"] == "perf_counter"
    assert identity["monotonic"] is True
    assert identity["adjustable"] is False
    assert 0 < identity["resolution_ns"] <= clock.MAX_RESOLUTION_NS
    assert clock.now_ns() <= clock.now_ns()


@pytest.mark.parametrize("mutation", [
    lambda value: value.update(name="monotonic"),
    lambda value: value.update(monotonic=False),
    lambda value: value.update(adjustable=True),
    lambda value: value.update(resolution_ns=clock.MAX_RESOLUTION_NS + 1),
    lambda value: value.update(resolution_ns=True),
])
def test_host_evidence_clock_rejects_unqualified_identity(mutation):
    value = clock.identity()
    mutation(value)
    with pytest.raises(ValueError, match="clock identity"):
        clock.validate(value)


def test_perf_counter_is_strictly_finer_than_monotonic_on_windows():
    if time.get_clock_info("monotonic").implementation != "GetTickCount64()":
        pytest.skip("Windows GetTickCount64 contrast only")
    assert (time.get_clock_info("perf_counter").resolution <
            time.get_clock_info("monotonic").resolution)
