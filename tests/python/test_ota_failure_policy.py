"""Static guards for the OTA first-failure and terminal-session policy.

These are intentionally source-level: the RP2350 shim is not part of the
portable-OTA host harness, so the semantic first-failure test lives in the
ingress host unit, while these checks prevent the policy from being removed.
"""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_first_failure_is_owned_by_ingress_and_cleared_on_open() -> None:
    header = (ROOT / "third_party/portable_ota/include/"
              "pota_stream_ingress.h").read_text(encoding="utf-8")
    source = (ROOT / "third_party/portable_ota/src/"
              "pota_stream_ingress.c").read_text(encoding="utf-8")
    shim = (ROOT / "middleware/portable_ota_port/src/"
            "portable_ota_core_port.c").read_text(encoding="utf-8")
    assert "first_failure_token" in header
    assert "pota_stream_ingress_record_failure" in header
    assert "ingress->first_failure_token = 0u;" in source
    assert "pota_stream_ingress_record_failure(&s_stream_ingress" in shim
    assert "pota_stream_ingress_clear_failure(&s_stream_ingress)" in shim


def test_terminal_data_is_dropped_and_admission_released() -> None:
    source = (ROOT / "components/ota_manager/src/ota_ao.c").read_text(
        encoding="utf-8")
    assert "stream_status.state != POTA_STREAM_STATE_RECEIVING" in source
    assert "resource_arbiter_release_ota_admission()" in source
    assert "event_bus_complete_ota_event(&event);" in source
    assert "open_status.state == POTA_STREAM_STATE_OPEN" in source
    assert "portable_ota_port_stream_service(budget_us)" in source
    assert "flash_transaction_ao_is_occupied()" in source
    assert "s_release_admission_pending" in source
    assert "FLASH_TRANSACTION_STATE_FAILED" in source
    assert "flash_transaction_ao_service()" in source


def test_scpi_first_block_gate_requires_durable_progress() -> None:
    source = (ROOT / "middleware/scpi_port/src/scpi_ota_commands.c").read_text(
        encoding="utf-8")
    assert "offset > 0u" in source
    assert "stream_status.durable_offset == 0u" in source
    assert "flash_transaction_submit_reject()" in source
    assert "flash_transaction_ao_is_occupied()" in source


def test_scpi_never_releases_ota_admission_directly() -> None:
    source = (ROOT / "middleware/scpi_port/src/scpi_ota_commands.c").read_text(
        encoding="utf-8")
    assert "resource_arbiter_release_ota_admission" not in source
    assert source.count("ota_ao_request_admission_release") >= 10


def test_ota_fb_and_tdma_binding_use_deferred_release() -> None:
    for relative in ("components/ota_manager/src/ota_fb.c",
                     "application/src/ota_tdma_binding.c"):
        source = (ROOT / relative).read_text(encoding="utf-8")
        assert "resource_arbiter_release_ota_admission" not in source
        assert "ota_ao_request_admission_release" in source
