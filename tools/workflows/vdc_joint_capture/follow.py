"""Tracked local frequency-follow layer over the remaining observer ancestry."""

from __future__ import annotations

import _imp
import hashlib
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace

from tools.domains.vdc.priority_trace import probe_support as tracked_probe_support


FOLLOW = "SYSTem:VDC:PRIORity:FOLLow"
STATUS = FOLLOW + ":STATus?"
U32 = (
    "schema active mode state last_reason request session generation calls tickets baselines "
    "waits prepared applied no_adjust rejected cancelled repeated baseline_sequence "
    "event_sequence carrier_sequence observer_epoch rx_epoch model_token expected_dco_seq "
    "before_dco_seq after_dco_seq local_slot reference_slot reserved"
).split()
I32 = "before_ppb after_ppb selected_delta_ppb reserved_signed".split()
U64 = (
    "arm_epoch baseline_raw raw_lo raw_hi interval_lo interval_hi "
    "local_interval_lo local_interval_hi"
).split()
I64 = "error_lo_ppb error_hi_ppb".split()
MODEL = (
    "schema token session role_generation applied_command_seq clock_epoch_id clock_run_id "
    "local_slot valid_from_raw valid dco_update_seq source_model_seq epoch_id run_id "
    "base_local_tick64 base_vdc_time64_ns nominal_period_ns period_adjust_ppb phase_offset_ns "
    "slew_limit_ppb lock_state tdma_schedule_crc32 servo_profile_crc32"
).split()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def _load_observer(path: Path):
    spec = importlib.util.spec_from_file_location("vdc_joint_observer_base", path)
    if spec is None or spec.loader is None:
        raise ValueError("Historical observer entrypoint cannot be loaded")
    module = importlib.util.module_from_spec(spec)
    missing = object()
    _imp.acquire_lock()
    previous = sys.modules.get("probe_support", missing)
    try:
        sys.modules["probe_support"] = tracked_probe_support
        try:
            spec.loader.exec_module(module)
            return module
        finally:
            if previous is missing:
                sys.modules.pop("probe_support", None)
            else:
                sys.modules["probe_support"] = previous
    finally:
        _imp.release_lock()


def load_follow(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked follower layer over an admitted ObserverProbe module."""
    observer = (runtime_module if runtime_module is not None else
                _load_observer(runtime_entrypoint))
    if not hasattr(observer, "ObserverProbe") or not hasattr(observer, "probe"):
        raise ValueError("Historical observer entrypoint lacks ObserverProbe/probe")
    match = observer.probe
    parent = match.parent
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()

    def parse_follow(raw):
        count = len(U32) + len(I32) + 2 * (len(U64) + len(I64))
        words = match.direct.numbers(raw, count)
        output = dict(zip(U32, words))
        position = len(U32)
        for name in I32:
            output[name] = signed(words[position], 32)
            position += 1
        for name in U64 + I64:
            number = words[position] | (words[position + 1] << 32)
            output[name] = signed(number, 64) if name in I64 else number
            position += 2
        return output

    def check_decision(status):
        require(status["prepared"] > 0 and
                status["applied"] + status["no_adjust"] > 0,
                "Follower lacks a prepared/terminal decision")
        require(0 < status["interval_lo"] <= status["interval_hi"],
                "Follower expected interval is invalid")
        require(0 < status["local_interval_lo"] <= status["local_interval_hi"],
                "Follower local interval is invalid")
        low = (status["local_interval_lo"] * 10**9 //
               status["interval_hi"] - 10**9)
        high = (-(-status["local_interval_hi"] * 10**9 //
                  status["interval_lo"]) - 10**9)
        require([low, high] ==
                [status["error_lo_ppb"], status["error_hi_ppb"]],
                "Follower error interval differs from independent ratio")
        step = (-(low // 4) if low > 10 else
                ((-high) // 4 if high < -10 else 0))
        step = max(-1000, min(1000, step))
        require(step == status["selected_delta_ppb"],
                "Follower step differs from decision criterion")
        require(status["event_sequence"] > status["baseline_sequence"],
                "Follower decision event did not advance")
        if step:
            require(status["applied"] > 0 and
                    status["after_dco_seq"] == status["before_dco_seq"] + 1,
                    "Follower applied decision sequence mismatch")
            require(status["after_ppb"] == status["before_ppb"] + step,
                    "Follower applied rate mismatch")
        else:
            require(status["no_adjust"] > 0 and
                    status["before_dco_seq"] == status["after_dco_seq"],
                    "Follower no-adjust sequence mismatch")
            require(status["before_ppb"] == status["after_ppb"],
                    "Follower no-adjust rate changed")
        return {"independent_error_ppb": [low, high],
                "independent_step_ppb": step,
                "actual_applied": bool(step),
                "interpretation": "bounded frequency decision, not phase lock"}

    class FollowProbe(observer.ObserverProbe):
        def __init__(self, options):
            super().__init__(options)
            self.report["schema"] = "TYPED_LOCAL_DCO_SHORT_PROBE_V1"
            self.report["tool_sources"].append({
                "path": str(source), "sha256": source_sha256})
            self.report["plan"].update(
                dco_control=True,
                typed_local_follow=True,
                require_valid_decision_on_each_follower=True,
            )
            parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[FOLLOW.upper()] = 1
            parent.scpi_serial.SCALAR_U32_QUERY_HEADERS.add(FOLLOW.upper() + "?")

        def control(self, board, text, expected="OK"):
            if text.upper() == parent.SYNC.upper() + " 0":
                super().control(board, FOLLOW + " 0", 0)
                require(int(self.command(board, FOLLOW + "?").strip('"')) == 0,
                        "Follower disable readback mismatch")
            result = super().control(board, text, expected)
            if text.upper().startswith(parent.SYNC.upper() + " ") and \
                    int(text.split()[-1]) > 0 and \
                    board.address == self.boards[0].address:
                for follower in self.boards[1:]:
                    row = self.report["boards"][follower.address]
                    row["follow_before_raw"] = self.command(follower, STATUS)
                    super().control(follower, FOLLOW + " 1", 1)
                    require(int(self.command(follower, FOLLOW + "?").strip('"')) == 1,
                            "Follower enable readback mismatch")
                    row["follow_enable_requested"] = True
                    self.save()
            return result

        def collect(self):
            require(self.barrier and not self.block_queries,
                    "Follower collection requires all-board STOP")
            for board in self.boards:
                row = self.report["boards"][board.address]
                row["follow_raw"] = self.command(board, STATUS)
                self.save()
            super().collect()
            for slot, board in enumerate(self.boards[1:], 1):
                row = self.report["boards"][board.address]
                try:
                    status = row["follow"] = parse_follow(row["follow_raw"])
                    model_values = list(map(
                        int, parent.fields(row["local_model_raw"])))
                    require(len(model_values) == len(MODEL),
                            "Follower model field count mismatch")
                    model = row["follow_model"] = dict(zip(MODEL, model_values))
                    require(status["schema"] == 1 and status["mode"] == 4,
                            "Follower status schema/mode mismatch")
                    require(
                        status["session"] == status["generation"] ==
                        self.report["binding_generation"],
                        "Follower status identity mismatch")
                    require(status["local_slot"] == slot and
                            status["reference_slot"] == 0,
                            "Follower status slot mismatch")
                    require(status["active"] == 0 and status["last_reason"] == 12,
                            "Follower status did not STOP normally")
                    row["decision_check"] = check_decision(status)
                    require(model["dco_update_seq"] == status["after_dco_seq"],
                            "Follower model DCO sequence mismatch")
                    require(model["period_adjust_ppb"] == status["after_ppb"],
                            "Follower model rate mismatch")
                    require(model["session"] == status["session"] and
                            model["local_slot"] == slot,
                            "Follower model identity/slot mismatch")
                    row["actual_dco_consistent"] = True
                except Exception as exc:
                    self.error("follow-assessment:" + board.address, exc)
                self.save()
            self.report["all_followers_applied"] = all(
                self.report["boards"][board.address].get("follow", {}).get(
                    "applied", 0) > 0 for board in self.boards[1:])
            self.report["passed"] = not self.report["errors"]

        def run(self):
            super().run()
            self.report["conclusion_scope"] = (
                "Typed local frequency decision and actual DCO application only. "
                "A zero adjustment is valid only when the full interval intersects "
                "the deadband. No ACK, phase capture, physical output lock, 100 ns, "
                "WCET or one-lap deadline claim.")
            self.save()

    return SimpleNamespace(
        __file__=str(source),
        FollowProbe=FollowProbe,
        observer=observer,
        match=match,
        parent=parent,
        FOLLOW=FOLLOW,
        STATUS=STATUS,
        U32=U32,
        I32=I32,
        U64=U64,
        I64=I64,
        MODEL=MODEL,
        signed=signed,
        parse_follow=parse_follow,
        check_decision=check_decision,
        source_path=source,
        source_sha256=source_sha256,
    )
