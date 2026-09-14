"""Export the frozen first-observer cut after every participating board STOPs.

The cut is a diagnostic interval of DMA completed writes. It is never an
edge-latched position, physical packet identity, or timestamp qualification.
"""
from datetime import datetime, timezone
import csv
import json
from pathlib import Path

from tools.analyzer_trace_export.analyzer_trace_export import parse_runtime_status, runtime_stopped

COMMAND = "SYSTem:TDMA:FLIGHT:RX:CUT?"
FIELDS = (
    "schema", "flags", "retire_reasons", "observer_epoch", "pio_hz",
    "physical_frame_words", "alignment_byte_shift", "alignment_bit_shift",
    "arm_epoch", "observation_epoch_before", "observation_epoch_after",
    "sample_before_ticks", "sample_after_ticks", "produced_before", "produced_after",
    "dma_count_before", "dma_count_after", "dma_ctrl_before", "dma_ctrl_after",
    "capture_fdebug_before", "capture_fdebug_after", "pads_before", "pads_after",
    "dma_channel", "capture_fifo_before", "capture_fifo_after",
    "capture_pc_before", "capture_pc_after",
)
WIDE_FIELDS = frozenset(FIELDS[8:15])
BYTE_FIELDS = frozenset(FIELDS[-4:])


def decode_start_cut(raw):
    fields = next(csv.reader([raw.strip()]))
    if fields == ["UNAVAILABLE"]:
        return None
    if len(fields) != len(FIELDS) + 1 or fields[0] != "RXSTARTCUT":
        raise ValueError("invalid startup cut schema or field count")
    result = dict(zip(FIELDS, (int(value, 10) for value in fields[1:])))
    if result["schema"] != 1:
        raise ValueError("unknown startup cut schema")
    for name, value in result.items():
        bits = 64 if name in WIDE_FIELDS else 8 if name in BYTE_FIELDS else 32
        if value < 0 or value >= 1 << bits:
            raise ValueError(f"startup cut {name} out of range")
    # Preserve even rejected/inconsistent brackets for analysis. Parsing
    # never upgrades flags or turns a failed diagnostic into an exact cut.
    return result


def export_frozen_cuts(boards, query, path, *, expected_build):
    """Read once per board, after all current STOP acknowledgements.

    boards maps UID to the caller's serial target. query(target, command)
    owns transport. On any barrier failure no board's cut is requested.
    Original responses and all export errors are retained without retries.
    """
    path = Path(path)
    report = dict(schema=1, export_passed=False, identity_qualified=False,
                  timestamp_qualified=False, dpll_qualified=False,
                  query_policy="FROZEN_AFTER_ALL_STOP_ACK", barrier=[], boards=[], errors={})
    # Reserve the result before any I/O; never overwrite an earlier attempt.
    with path.open("x", encoding="utf-8") as stream:
        def command(target, row, text):
            call = dict(command=text, started_utc=datetime.now(timezone.utc).isoformat())
            row.setdefault("commands", []).append(call)
            try:
                call["raw"] = query(target, text)
                return call["raw"]
            except Exception as exc:
                call["error"] = f"{type(exc).__name__}: {exc}"
                raise
            finally:
                call["ended_utc"] = datetime.now(timezone.utc).isoformat()

        try:
            if not boards:
                raise ValueError("no boards selected")
            for uid, target in boards.items():
                row = dict(uid=uid)
                report["barrier"].append(row)
                try:
                    idn = command(target, row, "*IDN?")
                    if uid not in idn:
                        raise ValueError("board UID mismatch")
                    build = command(target, row, "SYSTem:FW:BUILD?").strip().strip('"')
                    if build != str(expected_build):
                        raise ValueError("board build mismatch")
                    row["runtime"] = parse_runtime_status(command(target, row, "SYSTem:TDMA:RING:STATus?"))
                    if not runtime_stopped(row["runtime"]):
                        raise ValueError("STOP not acknowledged")
                    row["passed"] = True
                except Exception as exc:
                    report["errors"][f"barrier:{uid}"] = f"{type(exc).__name__}: {exc}"
            if not report["errors"]:
                for uid, target in boards.items():
                    row = dict(uid=uid)
                    report["boards"].append(row)
                    try:
                        row["cut"] = decode_start_cut(command(target, row, COMMAND))
                        row["available"] = row["cut"] is not None
                        row["passed"] = True
                    except Exception as exc:
                        report["errors"][f"export:{uid}"] = f"{type(exc).__name__}: {exc}"
            report["export_passed"] = not report["errors"] and len(report["boards"]) == len(boards)
        except Exception as exc:
            report["errors"]["export"] = f"{type(exc).__name__}: {exc}"
        finally:
            json.dump(report, stream, ensure_ascii=False, indent=2)
    return report
