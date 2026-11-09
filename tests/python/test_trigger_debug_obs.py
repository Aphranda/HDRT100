"""M5: the demo readback page reads switches through ControlPlane.read."""

import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "domains" / "trigger" / "trigger_debug_demo"))

from obs_read import ControlPlaneTransport, read_switch_obs  # noqa: E402


class FakeTransport:
    def __init__(self, response: str) -> None:
        self.response = response

    def execute(self, command: str) -> str:
        return self.response


def test_read_switch_obs_formats_per_slot_rows():
    response = "1,0,2,0,1,1,1,0,0,1,1,1,1,1,183,526,0,4,4"
    text = read_switch_obs(FakeTransport(response), 1, 0, 2)
    assert "key=1" in text
    assert "slot=1" in text
    assert "stale=0" in text
    assert "value=4,4" in text


def test_read_switch_obs_rejects_bad_key():
    with pytest.raises(Exception):
        read_switch_obs(FakeTransport('-200,"OBS_KEY"'), 1)


def test_transport_routes_refmem_queries_through_control_plane():
    raw = FakeTransport("1,0,1,0,1,1,1,0,0,1,1")
    wrapped = ControlPlaneTransport(raw)
    assert wrapped.execute("SYSTem:REFMEM:OBS? 1,0,1").startswith("1,0,1")


def test_transport_passes_non_refmem_commands():
    raw = FakeTransport("OK")
    assert ControlPlaneTransport(raw).execute("*IDN?") == "OK"


def test_transport_routes_ctrl_execute_through_write():
    raw = FakeTransport("QUEUED,7")
    wrapped = ControlPlaneTransport(raw)
    assert wrapped.execute("SYSTem:CTRL:EXECute 45,15,15,1,5") == "QUEUED,7"
