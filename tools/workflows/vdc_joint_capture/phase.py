"""Tracked phase layer over the remaining long-window ancestry."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from types import SimpleNamespace

from tools.workflows.vdc_joint_capture.longwindow import load_longwindow


U32 = (
    "schema enabled active state reason request session generation calls prepared attempted "
    "committed applied held rejected cancelled event_sequence carrier_sequence source_slot "
    "target_slot before_model after_model before_dco after_dco rate_epoch reserved"
).split()
I64 = "delta_ns cumulative_ns residual_lo residual_hi".split()
U64 = (
    "before_base_vdc after_base_vdc raw_lo raw_hi local_lo local_hi expected_lo expected_hi "
    "valid_from_raw"
).split()


def load_phase(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked phase layer over an admitted LongWindowProbe module."""
    previous = (runtime_module if runtime_module is not None else
                load_longwindow(runtime_entrypoint))
    if not hasattr(previous, "LongWindowProbe") or not hasattr(previous, "parent"):
        raise ValueError("Historical long-window entrypoint lacks LongWindowProbe/parent")
    parent = previous.parent
    follow = parent.follow
    trace = parent.trace
    prefix = parent.PREFIX
    phase_prefix = follow.FOLLOW + ":PHASe"
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()
    previous_source = Path(previous.__file__).resolve()
    previous_sha256 = hashlib.sha256(previous_source.read_bytes()).hexdigest()

    def require(condition, message):
        if not condition:
            raise RuntimeError(message)

    def parse_phase(raw):
        words = follow.match.direct.numbers(raw, 52)
        result = dict(zip(U32, words))
        position = len(U32)
        for name in I64 + U64:
            value = words[position] | (words[position + 1] << 32)
            result[name] = follow.signed(value, 64) if name in I64 else value
            position += 2
        return result

    class PhaseProbe(parent.TraceProbe):
        def __init__(self, options):
            super().__init__(options)
            self.report["schema"] = "DPLL_LOCAL_PHASE_NATIVE_PREFIX_V1"
            self.report["plan"].update(
                runtime_queries=False,
                phase_enabled=True,
                run_seconds=11,
                native_schema=6,
                origin_native_schema=5,
                timebase="TIMER1_NS",
                capture_scope="complete finite prefix",
                trace_matches_min=2,
                trace_match_span_min_s=None,
                require_phase_on_each_follower=True,
                physical_lock_qualified=False,
                frequency_normalization_lineage_proven=False,
            )
            self.report["tool_sources"].extend((
                {"path": str(source), "sha256": source_sha256},
                {"path": str(previous_source), "sha256": previous_sha256},
            ))
            for header in (phase_prefix, prefix + "PHASe", prefix + "ORIGin"):
                follow.parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[header.upper()] = 1
            follow.parent.scpi_serial.SCALAR_ONE_QUERY_HEADERS.add(
                phase_prefix.upper() + "?")

        def control(self, board, text, expected="OK"):
            if text.upper() == follow.parent.SYNC.upper() + " 0":
                follow.FollowProbe.control(self, board, phase_prefix + " 0", 0)
            result = follow.FollowProbe.control(self, board, text, expected)
            if (text.upper().startswith(follow.parent.SYNC.upper() + " ") and
                    int(text.split()[-1]) > 0 and board == self.boards[0]):
                for index, participant in enumerate(self.boards):
                    row = self.report["boards"][participant.address]
                    if index:
                        follow.FollowProbe.control(
                            self, participant, phase_prefix + " 1", 1)
                        require(
                            int(self.command(
                                participant, phase_prefix + "?").strip('"')) == 1,
                            "Follower phase enable readback mismatch")
                    before = row["trace_before"] = self.trace_status(participant)
                    capture_id = max(
                        self.report["binding_generation"], before["capture_id"] + 1)
                    row["trace_capture_id"] = capture_id
                    row["trace_arm_requested"] = True
                    self.save()
                    follow.FollowProbe.control(
                        self, participant,
                        f'{prefix}{"PHASe" if index else "ORIGin"} {capture_id}',
                        capture_id)
                    row["trace_arm_ack"] = self.trace_ack(
                        participant, 1, 1, capture_id)
                    status = row["trace_arm_ack"]["status"]
                    require(status["schema"] == (6 if index else 5),
                            "Native trace schema mismatch")
                    require(status["record_count"] == 0,
                            "Native trace ARM already contains records")
                    require(
                        status["session"] == status["generation"] ==
                        self.report["binding_generation"],
                        "Native trace ARM identity mismatch")
                    self.save()
            return result

        acquire = previous.LongWindowProbe.acquire

        def collect(self):
            require(self.barrier and not self.block_queries,
                    "Phase collection requires all-board STOP")
            for index, board in enumerate(self.boards):
                row = self.report["boards"][board.address]
                try:
                    if index:
                        row["follow_raw"] = self.command(board, follow.STATUS)
                        row["phase_raw"] = self.command(
                            board, phase_prefix + ":STATus?")
                        row["phase"] = parse_phase(row["phase_raw"])
                    self.control(board, prefix + "STOP", 1)
                    capture_id = row["trace_capture_id"]
                    row["trace_stop_ack"] = self.trace_ack(
                        board, 2, 3, capture_id)
                    data, pages = trace.download_capture(
                        lambda query: self.command(board, query), capture_id)
                    path = self.opt.out / f"{board.address}-native.ram.bin"
                    path.write_bytes(data)
                    decoded = trace.decode(data, capture_id)
                    decoded_path = path.with_suffix(".json")
                    decoded_path.write_text(
                        json.dumps(decoded, indent=2) + "\n", encoding="utf-8")
                    row["trace_export"] = {
                        "path": str(path), "sha256": follow.parent.base.sha(path),
                        "bytes": len(data), "decoded": str(decoded_path),
                        "file_crc32": decoded["file_crc32"], "pages": pages,
                    }
                    row["trace_read_complete"] = True
                    status = decoded["status"]
                    require(status["schema"] == (6 if index else 5),
                            "Native trace export schema mismatch")
                    require(
                        status["session"] == status["generation"] ==
                        self.report["binding_generation"],
                        "Native trace export identity mismatch")
                    require(status["local_slot"] == index and status["reason"] in (1, 4),
                            "Native trace export slot/reason mismatch")
                    require(
                        status["dropped_count"] ==
                        (1 if index and status["reason"] == 4 else 0),
                        "Native trace dropped-count mismatch")
                    if index:
                        phases = [record for record in decoded["records"]
                                  if record["kind"] == 4]
                        decisions = [record for record in decoded["records"]
                                     if record["kind"] == 2]
                        matches = [record for record in decoded["records"]
                                   if record["kind"] == 1]
                        require(phases and decisions and len(matches) >= 2,
                                "Native follower trace lacks phase/decision/match records")
                        require(any(decision["index"] > phases[0]["index"]
                                    for decision in decisions),
                                "Native follower trace lacks a post-phase decision")
                        row["native_phase_count"] = len(phases)
                        row["native_decision_count"] = len(decisions)
                        row["native_phase_deltas_ns"] = [
                            record["delta_ns"] for record in phases]
                        row["native_residual_bounds"] = [
                            [record["residual_lo"], record["residual_hi"]]
                            for record in matches]
                        row["phase_prefix_confirmed"] = True
                    else:
                        require(decoded["replay_matches_encoded"] and
                                len(decoded["records"]) >= 8,
                                "Native origin trace replay is incomplete")
                except Exception as exc:
                    self.error("phase-native:" + board.address, exc)
                self.save()
            try:
                follow.observer.ObserverProbe.collect(self)
                for index, board in enumerate(self.boards[1:], 1):
                    row = self.report["boards"][board.address]
                    try:
                        phase = row["phase"]
                        follower = row["follow"] = follow.parse_follow(
                            row["follow_raw"])
                        values = list(map(
                            int, follow.parent.fields(row["local_model_raw"])))
                        require(len(values) == len(follow.MODEL),
                                "Follower model field count mismatch")
                        model = row["follow_model"] = dict(zip(follow.MODEL, values))
                        require(follower["schema"] == 1 and follower["mode"] == 4,
                                "Follower endpoint schema/mode mismatch")
                        require(
                            follower["session"] == follower["generation"] ==
                            self.report["binding_generation"],
                            "Follower endpoint identity mismatch")
                        require(follower["local_slot"] == index and
                                follower["reference_slot"] == 0,
                                "Follower endpoint slot mismatch")
                        require(follower["active"] == 0 and
                                follower["last_reason"] == 12,
                                "Follower endpoint did not STOP normally")
                        require(
                            phase["session"] == phase["generation"] ==
                            self.report["binding_generation"],
                            "Phase endpoint identity mismatch")
                        require(phase["source_slot"] == 0 and
                                phase["target_slot"] == index,
                                "Phase endpoint slot mismatch")
                        require(phase["applied"] > 0 and
                                phase["committed"] == phase["applied"] and
                                phase["active"] == 0,
                                "Phase endpoint application mismatch")
                        require(follower["applied"] + follower["no_adjust"] > 0,
                                "Follower endpoint lacks a decision")
                        require(model["session"] == phase["session"] and
                                model["local_slot"] == index,
                                "Follower model identity mismatch")
                        phase_sequence = phase["after_dco"] or phase["before_dco"]
                        final_sequence = max(
                            phase_sequence, follower["after_dco_seq"])
                        require(model["dco_update_seq"] == final_sequence,
                                "Follower model DCO sequence mismatch")
                        require(model["period_adjust_ppb"] == follower["after_ppb"],
                                "Follower model rate mismatch")
                        if final_sequence == phase_sequence:
                            require(model["base_vdc_time64_ns"] ==
                                    phase["after_base_vdc"],
                                    "Follower model phase base mismatch")
                            require(model["token"] ==
                                    (phase["after_model"] or phase["before_model"]),
                                    "Follower model phase token mismatch")
                        row["phase_frequency_endpoint_consistent"] = True
                    except Exception as exc:
                        self.error("phase-endpoint:" + board.address, exc)
                    self.save()
            finally:
                for board in self.boards:
                    row = self.report["boards"][board.address]
                    if not row.get("trace_read_complete"):
                        row["trace_retained_for_recovery"] = bool(
                            row.get("trace_arm_requested"))
                        continue
                    try:
                        self.control(board, prefix + "RELease", 1)
                        row["trace_release_ack"] = self.trace_ack(board, 3, 0)
                    except Exception as exc:
                        self.error("phase-release:" + board.address, exc)
                self.report["passed"] = not self.report["errors"]
                self.save()

        def run(self):
            super().run()
            self.report["conclusion_scope"] = (
                "Actual confirmed local phase updates plus continued frequency "
                "decisions and STOP endpoint. Native capture is a finite complete prefix. "
                "No GPIO, 100 ns, complete frequency normalization lineage or lock "
                "qualification.")
            self.save()

    return SimpleNamespace(
        PhaseProbe=PhaseProbe,
        previous=previous,
        parent=parent,
        follow=follow,
        trace=trace,
        PREFIX=prefix,
        PHASE=phase_prefix,
        U32=U32,
        I64=I64,
        U64=U64,
        parse_phase=parse_phase,
        source_path=source,
        source_sha256=source_sha256,
    )
