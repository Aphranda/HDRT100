"""Tracked joint-capture runtime base over the remaining RunProbe ancestry."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import time
import traceback
from types import SimpleNamespace

from tools.common.host_evidence_clock import now_ns
from tools.workflows.vdc_joint_capture.run_output import load_run_output


def load_runtime(runtime_entrypoint: Path, board_uids, *, runtime_module=None):
    """Build the tracked control/START base after manifest admission."""
    capture = (runtime_module if runtime_module is not None else
               load_run_output(runtime_entrypoint))
    if not hasattr(capture, "RunProbe") or not hasattr(capture, "phase"):
        raise ValueError("Historical runtime entrypoint lacks RunProbe/phase")
    phase = capture.phase
    follow = phase.follow
    backend = follow.parent.base.BACKEND
    board_uids = tuple(board_uids)
    if not board_uids or len(set(board_uids)) != len(board_uids):
        raise ValueError("Runtime board UID order must be nonempty and unique")
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()

    def parallel_start(probe):
        boards = tuple(probe.boards)
        if (len(boards) != len(board_uids)
                or len({board.address for board in boards}) != len(board_uids)
                or len({board.port for board in boards}) != len(board_uids)):
            raise ValueError("Distinct declared identities and ports required")
        if set(board.address for board in boards) != set(board_uids):
            raise ValueError("Four-board allowlist mismatch")
        if time.monotonic() > probe.deadline or len(probe.report["actions"]) + len(boards) > 2000:
            raise TimeoutError("Bounded work budget exhausted before START")
        connections = []
        for board in boards:
            connection = backend._PERSISTENT_CONNECTIONS.get(board.address)
            if (connection is None or connection.session is None
                    or connection.session.ser is None):
                raise RuntimeError("Preverified persistent session required; no reopen")
            if connection.board != board:
                raise RuntimeError("Connection identity mismatch")
            connections.append(connection)
        if len({id(connection.session.ser) for connection in connections}) != len(boards):
            raise RuntimeError("Serial transports must be distinct")
        if not probe.block_queries or probe.barrier or not probe.scope.quiet:
            raise RuntimeError("Parallel START requires quiet acquisition barrier")
        command = "SYSTem:TDMA:RING:START"
        probe.report["parallel_start_intents"] = [
            {"uid": board.address, "port": board.port, "command": command}
            for board in boards]
        probe.save()

        def execute(board, connection):
            row = {
                "uid": board.address,
                "port": board.port,
                "command": command,
                "start_ns": now_ns(),
                "utc": datetime.now(timezone.utc).isoformat(),
                "dispatch": "one preverified connection per worker",
            }
            try:
                row["raw"] = connection.command(command)
            except Exception as exc:
                row["error"] = f"{type(exc).__name__}: {exc}"
            finally:
                row["end_ns"] = now_ns()
            return row

        with ThreadPoolExecutor(max_workers=len(boards)) as executor:
            pending = [
                executor.submit(execute, board, connection)
                for board, connection in zip(boards, connections)]
            rows = [future.result() for future in pending]
        rows.sort(key=lambda row: row["start_ns"])
        probe.report["actions"].extend(rows)
        with (probe.opt.out / "actions.jsonl").open("a", encoding="utf-8") as stream:
            for row in rows:
                stream.write(json.dumps(row, ensure_ascii=False) + "\n")
        probe.report["parallel_start_joined_ns"] = now_ns()
        probe.save()
        if any(row.get("error") or row.get("raw", "").strip().strip('"') != "OK"
               for row in rows):
            raise RuntimeError(
                "At least one START lacks explicit OK; all workers joined; STOP before TRIAL")
        return rows

    class Probe(capture.RunProbe):
        def __init__(self, opt):
            super().__init__(opt)
            self.deadline = time.monotonic() + opt.seconds + 240
            self.samples = []
            self.report["monitor_script_sha256"] = source_sha256
            self.report.setdefault("tool_sources", []).append({
                "path": str(source), "sha256": source_sha256})
            self.report["plan"].update(
                output_duration_ms=0,
                run_seconds=opt.seconds,
                internal_trace=False,
                run_status_schema=11,
                sample_interval_s=5,
                checkpoints_s=list(range(60, opt.seconds + 1, 60)),
                runtime_board_queries=False,
                stop_on_failed_checkpoint=True,
            )

        def error(self, scope, exc):
            super().error(scope, exc)
            self.report["errors"][-1]["traceback"] = traceback.format_exc()
            self.save()

        def control(self, board, text, expected="OK"):
            if text.upper() == follow.parent.SYNC.upper() + " 0":
                follow.FollowProbe.control(self, board, phase.PHASE + " 0", 0)
            result = follow.FollowProbe.control(self, board, text, expected)
            if (text.upper().startswith(follow.parent.SYNC.upper() + " ") and
                    int(text.split()[-1]) > 0 and board == self.boards[0]):
                for index, participant in enumerate(self.boards):
                    if index:
                        follow.FollowProbe.control(self, participant, phase.PHASE + " 1", 1)
                        applied = int(self.command(
                            participant, phase.PHASE + "?").strip('"'))
                        if applied != 1:
                            raise RuntimeError("Follower phase enable rejected")
                    row = self.report["boards"][participant.address]
                    row["run_prepare_requested"] = True
                    self.save()
                    # RUN prepare needs a non-zero feedback session on every
                    # board, but that session must stay the generation the host
                    # already armed with: the VDC owner re-arms any lane whose
                    # session it does not recognise (see
                    # components/vdc_dpll_manager/src/vdc_dpll_manager.c,
                    # priority_match_owner_arm_core1), which moves the matcher
                    # off the armed generation and makes every typed record
                    # reject with no follower success. So never mint a new
                    # session here; repair a zero one with the armed generation
                    # and refuse any other drift by name.
                    armed = int(self.report.get("binding_generation") or 0)
                    if armed <= 0:
                        raise RuntimeError(
                            "run prepare requires an armed binding generation")
                    current = int(self.command(
                        participant,
                        "SYSTem:VDC:FEEDback:SESSion?").strip('"') or 0)
                    if current == 0:
                        # The session setter answers `"OK",<id>` (not a bare
                        # int), so verify with a readback instead of control().
                        self.command(
                            participant,
                            f"SYSTem:VDC:FEEDback:SESSion {armed}")
                        current = int(self.command(
                            participant,
                            "SYSTem:VDC:FEEDback:SESSion?").strip('"') or 0)
                    if current != armed:
                        raise RuntimeError(
                            f"feedback session drift on {participant.address}: "
                            f"{current} != armed {armed}")
                    row["feedback_session"] = current
                    row["feedback_session_armed"] = armed
                    row["run_request"] = int(self.command(
                        participant, f"{capture.PREFIX} 1000000,2000,0").strip('"'))
                    if row["run_request"] <= 0:
                        raise RuntimeError("Output RUN prepare rejected")
            return result

    return SimpleNamespace(
        Probe=Probe,
        base=SimpleNamespace(),
        capture=capture,
        follow=follow,
        phase=phase,
        startup=SimpleNamespace(parallel_start=parallel_start),
        source_path=source,
        source_sha256=source_sha256,
    )
