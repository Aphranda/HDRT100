"""Exercise finite collector control failures without serial or wall-clock waits."""
import json
from types import SimpleNamespace

import pytest

from tools.calibration_ring_validate import tdma_autonomous_record as collector


class Clock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now

    def sleep(self, duration):
        self.now += duration

    def time(self):
        return 1234567


class Backend:
    def __init__(self, grant="2", start_failure=None, stop_failure=None, peak_failure=False,
                 export_failure=None):
        self.grant = grant
        self.start_failure = start_failure
        self.stop_failure = stop_failure
        self.peak_failure = peak_failure
        self.export_failure = export_failure
        self.started = set()
        self.stops = set()
        self.acks = set()
        self.commands = []
        self.exports = []

    def board_command(self, board, command, args):
        self.commands.append((board.address, command))
        # Every query must precede START or follow all four STOP requests.
        if "?" in command and self.started:
            assert len(self.stops) == 4, command
        if command.startswith("SYSTem:TDMA:RECord:ARM"):
            return "OK"
        if command == "SYSTem:TDMA:RECord:STATus?":
            return "3,1234567,500000,18,0,0,0,1804,0"
        if command == "SYSTem:TDMA:SCHEDule?":
            return "2,250000000"
        if command == "SYSTem:TDMA:RING:START":
            if board.address == self.start_failure:
                return "<timeout>"
            self.started.add(board.address)
            return "OK"
        if command == "SYSTem:TDMA:PROFile:RESet":
            return "1"
        if command.startswith("CALibration:ORIGin:TRIAL"):
            assert command.endswith(",8192,256,7500000000")
            return self.grant
        if command == "SYSTem:TDMA:RING:STOP":
            self.stops.add(board.address)
            return "<timeout>" if board.address == self.stop_failure else "OK"
        if command == "CALibration:ORIGin:REVOKe":
            assert len(self.stops) == 4
            return "OK"
        if command == "SYSTem:TDMA:PROFile:PEAK?" and self.peak_failure:
            raise OSError("read disconnected after native export")
        return "diagnostic"

    def wait_runtime_stopped(self, board, args, index):
        assert len(self.stops) == 4
        if board.address == self.stop_failure:
            raise RuntimeError("STOP not acknowledged")
        self.acks.add(board.address)
        return dict(ring_enabled=0, ring_adapter_started=0, ring_config_seq=4, ring_applied_config_seq=4)


def run(monkeypatch, tmp_path, backend):
    clock = Clock()
    monkeypatch.setattr(collector, "time", clock)
    monkeypatch.setattr(collector, "parse_service_timing", lambda raw: dict(reset_generation=1))

    def export(board, args, command, *, epoch, path):
        assert epoch == 1234567 and len(backend.acks) == 4
        backend.exports.append(board.address)
        if board.address == backend.export_failure:
            raise OSError("native record read failed")
        path.write_bytes(board.address.encode())
        return dict(path=str(path), decoded=dict(collection_passed=backend.grant == "2"))

    monkeypatch.setattr(collector, "export_frozen", export)
    boards = [SimpleNamespace(address=str(i)) for i in range(4)]
    args = SimpleNamespace(leave_running=False, sample_interval_s=0.5,
                           startup_timeout_s=2, window_s=6, arm_wait=1)
    return collector.acquire_autonomous_records(boards, boards[1:] + boards[:1], args, [], None,
                                                tmp_path, backend=backend)


@pytest.mark.parametrize("grant", ["<timeout>", "0", "3", "-2", "garbled"])
def test_grant_failure_stops_and_exports_without_retry(monkeypatch, tmp_path, grant):
    backend = Backend(grant=grant)
    with pytest.raises(RuntimeError, match="stopped evidence preserved"):
        run(monkeypatch, tmp_path, backend)
    record = json.loads((tmp_path / "board-records.json").read_text(encoding="utf-8"))
    evidence = json.loads((tmp_path / "stopped-profiles.json").read_text(encoding="utf-8"))
    assert len(record["records"]) == 4 and "acquisition" in record["errors"]
    assert all(not r["decoded"]["collection_passed"] for r in record["records"].values())
    assert len(backend.exports) == 4 and len(backend.stops) == len(backend.acks) == 4
    assert sum("ORIGin:TRIAL" in cmd for _, cmd in backend.commands) == 1
    assert "grant_epoch" not in evidence
    assert grant in evidence["errors"]["acquisition"]
    assert all(r["ended_at"] for r in evidence["commands"])


def test_success_uses_bounded_grant_and_stopped_queries(monkeypatch, tmp_path):
    backend = Backend()
    assert len(run(monkeypatch, tmp_path, backend)) == 4
    evidence = json.loads((tmp_path / "stopped-profiles.json").read_text(encoding="utf-8"))
    assert evidence["grant_epoch"] == 2 and not evidence["errors"]
    assert evidence["grant_rearm_ticks"] == 8192 and evidence["grant_seconds"] == 30
    assert len(evidence["stop_barrier"]) == 4
    assert all(node["reset_matches"] for node in evidence["nodes"].values())


def test_partial_start_still_stops_all_and_exports(monkeypatch, tmp_path):
    backend = Backend(start_failure="2")
    with pytest.raises(RuntimeError, match="START rejected"):
        run(monkeypatch, tmp_path, backend)
    assert backend.started == {"1"} and len(backend.exports) == 4
    assert not any("ORIGin:TRIAL" in cmd for _, cmd in backend.commands)


def test_missing_stop_ack_prevents_all_exports(monkeypatch, tmp_path):
    backend = Backend(stop_failure="2")
    with pytest.raises(RuntimeError, match="STOP not acknowledged"):
        run(monkeypatch, tmp_path, backend)
    assert len(backend.stops) == 4 and not backend.exports
    record = json.loads((tmp_path / "board-records.json").read_text(encoding="utf-8"))
    assert not record["records"] and "ack:2" in record["errors"]


def test_later_diagnostic_exception_does_not_lose_native_export(monkeypatch, tmp_path):
    backend = Backend(grant="<timeout>", peak_failure=True)
    with pytest.raises(RuntimeError, match="control returned no numeric result"):
        run(monkeypatch, tmp_path, backend)
    record = json.loads((tmp_path / "board-records.json").read_text(encoding="utf-8"))
    evidence = json.loads((tmp_path / "stopped-profiles.json").read_text(encoding="utf-8"))
    assert len(record["records"]) == 4
    assert all("peak_raw" in node["diagnostic_errors"] for node in evidence["nodes"].values())


def test_one_export_failure_preserves_other_boards(monkeypatch, tmp_path):
    backend = Backend(export_failure="2")
    with pytest.raises(RuntimeError, match="native record read failed"):
        run(monkeypatch, tmp_path, backend)
    record = json.loads((tmp_path / "board-records.json").read_text(encoding="utf-8"))
    assert set(record["records"]) == {"0", "1", "3"}
    assert "export:2" in record["errors"]
    assert len(backend.exports) == 4 and len(backend.acks) == 4
