"""Tracked native trace layer over the remaining follower ancestry."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import time
from types import SimpleNamespace

from tools.vdc_priority_trace import vdc_priority_trace as trace_codec
from tools.workflows.vdc_joint_capture.follow import load_follow


PREFIX = "SYSTem:VDC:PRIORity:TRACe:"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_trace(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked native trace layer over an admitted FollowProbe module."""
    follow = (runtime_module if runtime_module is not None else
              load_follow(runtime_entrypoint))
    if not hasattr(follow, "FollowProbe") or not hasattr(follow, "parent"):
        raise ValueError("Historical follow entrypoint lacks FollowProbe/parent")
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()

    class TraceProbe(follow.FollowProbe):
        def __init__(self, options):
            super().__init__(options)
            self.report["schema"] = "TYPED_TRACE_EIGHT_SECOND_PROBE_V1"
            self.report["plan"].update(
                native_typed_trace=True,
                diagnostic_gpio_output=False,
                trace_match_span_min_s=6,
                trace_matches_min=20,
            )
            dependencies = (
                source, Path(trace_codec.__file__),
                Path(follow.observer.__file__),
            )
            known = {row["path"] for row in self.report["tool_sources"]}
            for path in dependencies:
                if str(path) not in known:
                    self.report["tool_sources"].append({
                        "path": str(path),
                        "sha256": follow.parent.base.sha(path),
                    })
                    known.add(str(path))
            follow.parent.scpi_serial.SCALAR_ONE_QUERY_HEADERS.add(
                follow.FOLLOW.upper() + "?")
            for suffix in ("ARM", "STOP", "RELease"):
                follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[
                    (PREFIX + suffix).upper()] = 1

        def trace_status(self, board):
            return trace_codec.parse_status(
                self.command(board, PREFIX + "STATus?"))

        def trace_ack(self, board, command, state, capture_id=None):
            end = time.monotonic() + 2
            samples = []
            while time.monotonic() < end:
                status = self.trace_status(board)
                samples.append(status)
                if (status["request_seq"] == status["ack_seq"] != 0 and
                        status["command"] == command and
                        status["state"] == state and
                        (capture_id is None or
                         status["capture_id"] == capture_id)):
                    return {"status": status, "samples": samples}
                time.sleep(0.01)
            raise RuntimeError(
                "Trace request not acknowledged: " + json.dumps(samples))

        def control(self, board, text, expected="OK"):
            result = super().control(board, text, expected)
            if (text.upper().startswith(follow.parent.SYNC.upper() + " ") and
                    int(text.split()[-1]) > 0 and
                    board.address == self.boards[0].address):
                for follower in self.boards[1:]:
                    row = self.report["boards"][follower.address]
                    previous = row["trace_before"] = self.trace_status(follower)
                    capture_id = max(
                        self.report["binding_generation"],
                        previous["capture_id"] + 1)
                    row["trace_capture_id"] = capture_id
                    row["trace_arm_requested"] = True
                    self.save()
                    super().control(
                        follower, f"{PREFIX}ARM {capture_id}", capture_id)
                    row["trace_arm_ack"] = self.trace_ack(
                        follower, 1, 1, capture_id)
                    self.save()
            return result

        def collect(self):
            require(self.barrier and not self.block_queries,
                    "Trace collection requires all-board STOP")
            for board in self.boards[1:]:
                row = self.report["boards"][board.address]
                if not row.get("trace_arm_requested"):
                    continue
                try:
                    self.control(board, PREFIX + "STOP", 1)
                    capture_id = row["trace_capture_id"]
                    row["trace_stop_ack"] = self.trace_ack(
                        board, 2, 3, capture_id)
                    self.save()
                    data, pages = trace_codec.download_capture(
                        lambda query: self.command(board, query), capture_id)
                    path = self.opt.out / f"{board.address}-typed.ram.bin"
                    path.write_bytes(data)
                    decoded = trace_codec.decode(data, capture_id)
                    decoded_path = path.with_suffix(".json")
                    decoded_path.write_text(
                        json.dumps(decoded, indent=2) + "\n", encoding="utf-8")
                    row["trace_export"] = {
                        "path": str(path),
                        "sha256": follow.parent.base.sha(path),
                        "bytes": len(data),
                        "decoded": str(decoded_path),
                        "file_crc32": decoded["file_crc32"],
                        "pages": pages,
                    }
                    row["trace_read_complete"] = True
                    status = decoded["status"]
                    require(
                        status["session"] == status["generation"] ==
                        self.report["binding_generation"],
                        "Trace export identity mismatch")
                    require(status["local_slot"] == self.boards.index(board) and
                            status["reference_slot"] == 0,
                            "Trace export slot mismatch")
                    require(status["reason"] == 1 and
                            status["dropped_count"] == 0,
                            "Trace export completion/drop mismatch")
                    matches = [record for record in decoded["records"]
                               if record["kind"] == 1]
                    decisions = [record for record in decoded["records"]
                                 if record["kind"] == 2]
                    require(len(matches) >= 20 and len(decisions) >= 1,
                            "Trace export lacks match/decision coverage")
                    span = ((matches[-1]["raw_lo"] - matches[0]["raw_hi"]) /
                            status["tick_hz"])
                    row["trace_match_span_s"] = span
                    require(span >= 6, "Trace match span is shorter than six seconds")
                    row["trace_decisions"] = decisions
                except Exception as exc:
                    self.error("typed-trace:" + board.address, exc)
                self.save()
            try:
                super().collect()
                for board in self.boards[1:]:
                    row = self.report["boards"][board.address]
                    try:
                        last = row["trace_decisions"][-1]
                        follower = row["follow"]
                        keys = {
                            "event_sequence": "event_sequence",
                            "baseline_sequence": "baseline_sequence",
                            "before_seq": "before_dco_seq",
                            "after_seq": "after_dco_seq",
                            "before_ppb": "before_ppb",
                            "after_ppb": "after_ppb",
                            "delta_ppb": "selected_delta_ppb",
                            "error_lo_ppb": "error_lo_ppb",
                            "error_hi_ppb": "error_hi_ppb",
                        }
                        require(all(last[key] == follower[value]
                                    for key, value in keys.items()),
                                "Trace final decision differs from follower endpoint")
                        row["trace_last_decision_consistent"] = True
                    except Exception as exc:
                        self.error(
                            "trace-last-decision:" + board.address, exc)
            finally:
                for board in self.boards[1:]:
                    row = self.report["boards"][board.address]
                    if not row.get("trace_read_complete"):
                        row["trace_retained_for_recovery"] = bool(
                            row.get("trace_arm_requested"))
                        continue
                    try:
                        self.control(board, PREFIX + "RELease", 1)
                        row["trace_release_ack"] = self.trace_ack(board, 3, 0)
                    except Exception as exc:
                        self.error("trace-release:" + board.address, exc)
                self.report["passed"] = not self.report["errors"]
                self.save()

    return SimpleNamespace(
        __file__=str(source),
        TraceProbe=TraceProbe,
        follow=follow,
        trace=trace_codec,
        PREFIX=PREFIX,
        source_path=source,
        source_sha256=source_sha256,
    )
