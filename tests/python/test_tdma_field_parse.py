from __future__ import annotations

import pytest

from tools.tdma_ring_monitor.tdma_field_parse import (
    FIELDS,
    TdmaStatusParseError,
    parse_status_fields,
    parse_status_named,
)


def test_status_schema_matches_current_scpi_field_count() -> None:
    response = ",".join(str(index) for index in range(len(FIELDS)))

    values = parse_status_fields(response)
    named = parse_status_named(response)

    assert len(values) == len(FIELDS)
    assert named["ring_enabled"] == FIELDS.index("ring_enabled")
    assert named["ring_clock_observation_valid"] == FIELDS.index(
        "ring_clock_observation_valid")
    assert named["ring_clock_local_rx_timestamp_ns_hi"] == len(FIELDS) - 1


def test_status_schema_accepts_scpi_quoted_values() -> None:
    response = ",".join(f'"{index}"' for index in range(len(FIELDS)))

    assert parse_status_fields(response)[42] == 42


@pytest.mark.parametrize(
    "response",
    [
        "",
        "0,1,2",
        ",".join(str(index) for index in range(len(FIELDS) - 1)),
        ",".join(str(index) for index in range(len(FIELDS)))[:-1] + ",bad",
    ],
)
def test_status_schema_rejects_truncated_or_non_integer_response(response: str) -> None:
    with pytest.raises(TdmaStatusParseError):
        parse_status_fields(response)
