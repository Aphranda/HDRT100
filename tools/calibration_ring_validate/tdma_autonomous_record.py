"""Finite autonomous diagnostic acquisition with unconditional stopped export.

The caller supplies the existing TRN03 backend. Queries during RUN are forbidden;
control rejection remains a failure even when the stopped record is recovered.
"""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import csv
import json
import math
import time

from tools.calibration_ring_validate.tdma_board_record import export_frozen, record_status
from tools.tdma_ring_monitor.tdma_service_timing import parse_service_timing

GRANT_SECONDS = 30
GRANT_REARM_TICKS = 8192
GRANT_ABORT_POLLS = 256


def acquire_autonomous_records(ordered, start_order, args, actions, progress, out_dir, *, backend):
    if args.leave_running:
        raise ValueError("finite autonomous acquisition requires STOP and frozen export")
    if (out_dir / "board-records.json").exists() or (out_dir / "stopped-profiles.json").exists():
        raise ValueError("acquisition evidence directory has already been used")
    epoch = int(time.time()) & 0xFFFFFFFF
    interval = round(args.sample_interval_s * 1e6)
    count = math.ceil((args.startup_timeout_s + args.window_s) * 1e6 / interval) + 2
    evidence = dict(epoch=epoch, commands=[], resets={}, nodes={},
                    query_policy="NO_LIVE_SAMPLING", grant_seconds=GRANT_SECONDS,
                    grant_rearm_ticks=GRANT_REARM_TICKS,
                    scope="Cumulative peak after one control RESET; includes STOP and idle service")
    records, errors = {}, {}

    def command(board, text):
        row = dict(board=board.address, command=text, started_at=datetime.now(timezone.utc).isoformat())
        evidence["commands"].append(row)
        try:
            row["raw"] = backend.board_command(board, text, args)
            return row["raw"]
        except Exception as exc:
            row["error"] = f"{type(exc).__name__}: {exc}"
            raise
        finally:
            row["ended_at"] = datetime.now(timezone.utc).isoformat()

    def numeric_control(board, text, *, grant=False):
        raw = command(board, text)
        try:
            number = int(raw.strip().strip('"'), 10)
        except ValueError as exc:
            raise RuntimeError(f"control returned no numeric result: {text}: {raw!r}") from exc
        if number < 0 or (grant and (number == 0 or number & 1)):
            raise RuntimeError(f"control returned invalid generation: {text}: {raw!r}")
        return number

    def save():
        evidence["errors"] = errors
        (out_dir / "stopped-profiles.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")

    try:
        for board in ordered:
            response = command(board, f"SYSTem:TDMA:RECord:ARM {epoch},{interval},{count}")
            actions.append(dict(node=board.address, action="RECORD_ARM", epoch=epoch, response=response))
            deadline = time.monotonic() + args.arm_wait
            while time.monotonic() < deadline:
                status = record_status(command(board, "SYSTem:TDMA:RECord:STATus?"))
                if status and status["state"] == 3 and status["epoch"] == epoch:
                    break
                if status and status["state"] == 9:
                    raise RuntimeError(f"recorder ARM rejected: {status}")
                time.sleep(0.05)
            else:
                raise RuntimeError("recorder ARM acknowledgement deadline")
        hz = int(next(csv.reader([command(ordered[0], "SYSTem:TDMA:SCHEDule?")]))[1])
        if hz <= 0:
            raise RuntimeError("invalid clock rate before acquisition")
        for board in start_order:
            response = command(board, "SYSTem:TDMA:RING:START")
            actions.append(dict(node=board.address, action="START", response=response))
            if response.strip().strip('"') != "OK":
                raise RuntimeError(f"START rejected: {board.address}: {response!r}")
        evidence["acquisition_started_at"] = datetime.now(timezone.utc).isoformat()
        began = time.monotonic()
        deadline = began + args.startup_timeout_s + (count - 1) * interval / 1e6 + 0.5
        while time.monotonic() < began + 1.0:
            time.sleep(0.05)
        for board in ordered:
            evidence["resets"][board.address] = numeric_control(board, "SYSTem:TDMA:PROFile:RESet")
        evidence["grant_epoch"] = numeric_control(ordered[0],
            f"CALibration:ORIGin:TRIAL {epoch},{GRANT_REARM_TICKS},{GRANT_ABORT_POLLS},{hz * GRANT_SECONDS}", grant=True)
        while time.monotonic() < deadline:
            time.sleep(min(0.1, max(0.0, deadline - time.monotonic())))
        evidence["acquisition_wait_ended_at"] = datetime.now(timezone.utc).isoformat()
    except Exception as exc:
        errors["acquisition"] = f"{type(exc).__name__}: {exc}"
    finally:
        for board in ordered:
            try:
                raw = command(board, "SYSTem:TDMA:RING:STOP")
                if raw.strip().strip('"') != "OK":
                    errors[f"stop:{board.address}"] = f"STOP returned {raw!r}"
            except Exception as exc:
                errors[f"stop:{board.address}"] = f"{type(exc).__name__}: {exc}"
        try:
            raw = command(ordered[0], "CALibration:ORIGin:REVOKe")
            if raw.strip().strip('"') != "OK":
                errors["revoke"] = f"REVOKE returned {raw!r}"
        except Exception as exc:
            errors["revoke"] = f"{type(exc).__name__}: {exc}"
        save()

    stopped = {}
    for index, board in enumerate(ordered):
        try:
            stopped[board.address] = backend.wait_runtime_stopped(board, args, index)
        except Exception as exc:
            errors[f"ack:{board.address}"] = f"{type(exc).__name__}: {exc}"
    evidence["stop_barrier"] = stopped

    def export(index, board):
        result = export_frozen(board, args, lambda b, text, a: command(b, text),
                               epoch=epoch, path=out_dir / f"node{index}_board_record.bin")
        # Save the record before optional diagnostics, even if a query fails.
        metadata = dict(stopped_runtime=stopped[board.address])
        diagnostic_errors = {}
        for key, text in (("peak_raw", "SYSTem:TDMA:PROFile:PEAK?"),
                          ("admission_raw", "READ:CALibration:ORIGin:DIAGnostic?"),
                          ("handoff_raw", "READ:CALibration:ORIGin:HANDoff?"),
                          ("error_raw", "SYSTem:ERR?")):
            try:
                metadata[key] = command(board, text)
            except Exception as exc:
                diagnostic_errors[key] = f"{type(exc).__name__}: {exc}"
        try:
            metadata["peak"] = parse_service_timing(metadata.get("peak_raw", ""))
            metadata["reset_matches"] = metadata["peak"] is not None and (
                metadata["peak"]["reset_generation"] == evidence["resets"].get(board.address))
        except (KeyError, ValueError) as exc:
            diagnostic_errors["peak_decode"] = str(exc)
        metadata["diagnostic_errors"] = diagnostic_errors
        return result, metadata

    if len(stopped) == len(ordered):
        with ThreadPoolExecutor(max_workers=len(ordered)) as pool:
            futures = {b.address: pool.submit(export, index, b) for index, b in enumerate(ordered)}
            for uid, future in futures.items():
                try:
                    records[uid], evidence["nodes"][uid] = future.result()
                except Exception as exc:
                    errors[f"export:{uid}"] = f"{type(exc).__name__}: {exc}"
    save()
    (out_dir / "board-records.json").write_text(json.dumps(
        dict(records=records, errors=errors, epoch=epoch), indent=2), encoding="utf-8")
    if errors:
        raise RuntimeError(f"autonomous acquisition failed; stopped evidence preserved: {errors}")
    return records
