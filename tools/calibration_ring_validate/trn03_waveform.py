#!/usr/bin/env python3
"""Save, download, and compare TRN-03B NORMAL-persona ring captures."""

from __future__ import annotations

import argparse
import csv
import html
import json
import sys
import time
import zlib
from pathlib import Path
from typing import Any, Sequence

ROOT = Path(__file__).resolve().parents[2]
for tool_path in (ROOT / "tools", ROOT / "tools" / "tdma_ring_monitor",
                  ROOT / "tools" / "calibration_ring_validate"):
    if str(tool_path) not in sys.path:
        sys.path.insert(0, str(tool_path))

from calibration_data_train import parse_storage_read  # noqa: E402
from tdma_start_ring import Board, board_command  # noqa: E402


CAPTURE_SCHEMAS = {
    "HAOFV_TRN03_RING_CAPTURE_V1",
    "HAOFV_TRN03_RING_CAPTURE_V2",
}
DEFAULT_WINDOW_NS = 1000

TRANSPORT_RESULT_OK = "OK"
TRANSPORT_HEADER_SIZE = 32


def _transport_identity_crc32(transport: bytes | bytearray) -> int:
    identity_input = (
        transport[0:14] + transport[15:16] + transport[16:24])
    return zlib.crc32(identity_input) & 0xFFFFFFFF


def _transport_packet_crc32(transport: bytes | bytearray) -> int:
    crc_input = bytearray(transport)
    crc_input[28:32] = b"\0\0\0\0"
    return zlib.crc32(crc_input) & 0xFFFFFFFF


def _single_bit_repairs(transport: bytes, observed_crc32: int, *,
                        identity: bool) -> list[dict[str, int]]:
    offsets = (list(range(14)) + list(range(15, 24)) if identity else
               list(range(28)) + list(range(32, len(transport))))
    crc_function = (_transport_identity_crc32 if identity else
                    _transport_packet_crc32)
    repairs: list[dict[str, int]] = []
    candidate = bytearray(transport)
    for offset in offsets:
        for bit in range(8):
            candidate[offset] ^= 1 << bit
            if crc_function(candidate) == observed_crc32:
                repairs.append({"transport_byte_offset": offset, "bit": bit})
            candidate[offset] ^= 1 << bit
    return repairs


def validate_capture(value: object) -> dict[str, Any]:
    if not isinstance(value, dict) or value.get("schema") not in CAPTURE_SCHEMAS:
        raise ValueError(
            "capture schema must be one of " +
            ", ".join(sorted(CAPTURE_SCHEMAS)))
    node = int(value.get("node", -1))
    node_count = int(value.get("node_count", 0))
    bit_period_ns = int(value.get("bit_period_ns", 0))
    rx_bytes = value.get("rx_bytes")
    tx_bytes = value.get("tx_bytes")
    if (not 2 <= node_count <= 8 or not 0 <= node < node_count or
            bit_period_ns <= 0 or not isinstance(rx_bytes, list) or
            not isinstance(tx_bytes, list) or
            int(value.get("rx_byte_count", -1)) != len(rx_bytes) or
            int(value.get("tx_byte_count", -1)) != len(tx_bytes) or
            any(not isinstance(item, int) or not 0 <= item <= 0xFF
                for item in rx_bytes + tx_bytes) or
            int(value.get("rx_produced_bytes", -1)) < len(rx_bytes) or
            int(value.get("tx_produced_bytes", -1)) < len(tx_bytes)):
        raise ValueError("invalid TRN-03B capture metadata")
    if int(value.get("capture_version", 0)) >= 2:
        tx_frames = int(value.get("tx_complete_frame_count", -1))
        if ((tx_bytes and
             (len(tx_bytes) < 4 or tx_bytes[:2] != [0x54, 0x44] or
              len(tx_bytes) != 4 + tx_bytes[2] + (tx_bytes[3] << 8) or
              tx_frames <= 0)) or
                (not tx_bytes and tx_frames != 0)):
            raise ValueError("invalid complete TX frame evidence")
    return value


def save_ring_capture(board: Board, args: argparse.Namespace, *,
                      calibration_generation: int,
                      capture_epoch: int) -> dict[str, object]:
    last = ""
    ready_status: list[int] = []
    latch_attempts = 0
    retry_count = int(getattr(args, "capture_latch_retries", 1))
    for latch_attempt in range(retry_count + 1):
        latch_attempts = latch_attempt + 1
        latch = board_command(
            board,
            f"CALibration:RING:CAPTure:LATCh "
            f"{calibration_generation},{capture_epoch}", args)
        latch_values = [value.strip().strip('"')
                        for value in next(csv.reader([latch]), [])]
        if (len(latch_values) != 2 or
                int(latch_values[0], 0) != calibration_generation or
                int(latch_values[1], 0) != capture_epoch):
            last = f"latch rejected: {latch!r}"
            if latch_attempt == retry_count:
                raise RuntimeError(f"{board.address}: {last}")
            continue
        deadline = time.monotonic() + args.capture_timeout
        while time.monotonic() < deadline:
            last = board_command(
                board, "READ:CALibration:RING:CAPTure?", args)
            status = [int(value.strip().strip('"'), 0)
                      for value in next(csv.reader([last]), [])]
            if (len(status) >= 10 and
                    status[2] == calibration_generation and
                    status[3] == capture_epoch and status[0] == 2):
                ready_status = status
                break
            if (len(status) >= 10 and
                    status[2] == calibration_generation and
                    status[3] == capture_epoch and status[0] == 3):
                break
            time.sleep(0.01)
        if ready_status:
            break
    if not ready_status:
        raise RuntimeError(
            f"{board.address}: ring capture latch timeout: {last!r}")

    response = board_command(
        board,
        f"CALibration:RING:CAPTure:SAVE "
        f"{calibration_generation},{capture_epoch}", args)
    values = [value.strip().strip('"')
              for value in next(csv.reader([response]), [])]
    if len(values) != 3 or values[0] != "OK":
        raise RuntimeError(
            f"{board.address}: ring capture SD save rejected: {response!r}")
    job_id = int(values[1], 0)
    path = values[2]
    deadline = time.monotonic() + args.capture_timeout
    last = ""
    while time.monotonic() < deadline:
        last = board_command(board, "SYSTem:STORage:JOB?", args)
        job = [value.strip().strip('"')
               for value in next(csv.reader([last]), [])]
        if len(job) >= 8 and int(job[1], 0) == job_id:
            if job[0] == "DONE":
                return {"node_id": board.address, "sd_path": path,
                        "job_id": job_id, "size": int(job[4], 0),
                        "latch_attempts": latch_attempts,
                        "latch_status": ready_status,
                        "capture_debug": ({
                            "core1_service_count": ready_status[10],
                            "intent_read_fail_count": ready_status[11],
                            "last_seen_sequence": ready_status[12],
                            "copy_attempt_count": ready_status[13],
                            "copy_fail_count": ready_status[14],
                            "consumed_sequence": ready_status[15],
                        } if len(ready_status) >= 16 else {})}
            if job[0] == "FAILED":
                raise RuntimeError(
                    f"{board.address}: ring capture SD job failed: {last!r}")
        time.sleep(0.05)
    raise RuntimeError(
        f"{board.address}: ring capture SD job timeout: {last!r}")


def download_ring_capture(board: Board, capture_file: dict[str, object],
                          args: argparse.Namespace,
                          local_path: Path) -> dict[str, object]:
    path = str(capture_file["sd_path"])
    data = bytearray()
    file_size: int | None = None
    path_hash: int | None = None
    while file_size is None or len(data) < file_size:
        requested = (128 if file_size is None else
                     min(128, file_size - len(data)))
        response = board_command(
            board,
            f'SYSTem:STORage:FILE:READ? "{path}",{len(data)},{requested}',
            args)
        page = parse_storage_read(response, len(data))
        if file_size is None:
            file_size = int(page["file_size"])
            path_hash = int(page["path_hash"])
        elif int(page["file_size"]) != file_size:
            raise RuntimeError("ring capture file size changed during download")
        payload = page["payload"]
        assert isinstance(payload, bytes)
        if not payload and not bool(page["eof"]):
            raise RuntimeError("ring capture read made no progress before EOF")
        data.extend(payload)
        if bool(page["eof"]):
            break
    if file_size is None or len(data) != file_size:
        raise RuntimeError(
            f"ring capture download incomplete: {len(data)}/{file_size}")
    capture = validate_capture(json.loads(data.decode("utf-8")))
    local_path.parent.mkdir(parents=True, exist_ok=True)
    local_path.write_bytes(data)
    return {
        **capture_file,
        "local_path": str(local_path),
        "download_size": len(data),
        "path_hash": path_hash,
        "capture": capture,
    }


def byte_bits(values: Sequence[int]) -> list[int]:
    return [((int(value) >> shift) & 1)
            for value in values for shift in range(7, -1, -1)]


def latest_complete_packet(values: Sequence[int]) -> list[int] | None:
    """Return the newest complete 0x54,0x44,length packet in a byte stream."""
    fallback: list[int] | None = None
    for start in range(len(values) - 4, -1, -1):
        if values[start] != 0x54 or values[start + 1] != 0x44:
            continue
        frame_bytes = 4 + int(values[start + 2]) + (int(values[start + 3]) << 8)
        if frame_bytes >= 4 and start + frame_bytes <= len(values):
            packet = [int(value)
                      for value in values[start:start + frame_bytes]]
            # PHY and transport intentionally share the 0x54,0x44 magic.  A
            # backward-only search otherwise mistakes the nested transport
            # header for a newer PHY packet (version/class look like length).
            # Prefer the candidate whose payload is a self-consistent TDMA
            # transport frame, while retaining V1/generic capture support.
            if (frame_bytes >= 4 + TRANSPORT_HEADER_SIZE and
                    packet[4:6] == [0x54, 0x44] and
                    packet[6] == 1 and
                    packet[10] == TRANSPORT_HEADER_SIZE and
                    packet[8] + (packet[9] << 8) == frame_bytes - 4):
                return packet
            if fallback is None:
                fallback = packet
    return fallback


def decode_transport_evidence(
        physical_packet: Sequence[int] | None) -> dict[str, Any] | None:
    """Decode and CRC-check the TDMA transport frame inside a PHY packet."""
    if physical_packet is None:
        return None
    packet = bytes(int(value) for value in physical_packet)
    if len(packet) < 4:
        return {"valid": False, "result": "PHY_HEADER_TOO_SHORT"}
    frame_size = packet[2] | (packet[3] << 8)
    if packet[:2] != b"TD" or len(packet) != 4 + frame_size:
        return {"valid": False, "result": "PHY_HEADER_MISMATCH"}
    transport = packet[4:]
    if len(transport) < TRANSPORT_HEADER_SIZE:
        return {"valid": False, "result": "TRANSPORT_HEADER_TOO_SHORT"}
    if transport[:2] != b"TD":
        return {"valid": False, "result": "BAD_MAGIC"}
    if transport[2] != 1:
        return {"valid": False, "result": "BAD_VERSION"}
    if transport[6] != TRANSPORT_HEADER_SIZE:
        return {"valid": False, "result": "BAD_HEADER"}
    packet_size = transport[4] | (transport[5] << 8)
    if packet_size != len(transport):
        return {"valid": False, "result": "BAD_PACKET_SIZE",
                "packet_size": packet_size}

    read_u32 = lambda offset: int.from_bytes(
        transport[offset:offset + 4], "little")
    identity_crc32 = _transport_identity_crc32(transport)
    observed_identity_crc32 = read_u32(24)
    transport_crc32 = _transport_packet_crc32(transport)
    observed_transport_crc32 = read_u32(28)
    identity_repairs = _single_bit_repairs(
        transport, observed_identity_crc32, identity=True)
    transport_repairs = _single_bit_repairs(
        transport, observed_transport_crc32, identity=False)
    transport_repairs_after_identity: list[dict[str, object]] = []
    for identity_repair in identity_repairs:
        repaired = bytearray(transport)
        repaired[identity_repair["transport_byte_offset"]] ^= (
            1 << identity_repair["bit"])
        for remaining in _single_bit_repairs(
                bytes(repaired), observed_transport_crc32, identity=False):
            transport_repairs_after_identity.append({
                "identity_repair": identity_repair,
                "remaining_transport_repair": remaining,
            })
    result = (TRANSPORT_RESULT_OK
              if transport_crc32 == observed_transport_crc32 and
              identity_crc32 == observed_identity_crc32 else
              "TRANSPORT_CRC_MISMATCH"
              if transport_crc32 != observed_transport_crc32 else
              "IDENTITY_CRC_MISMATCH")
    return {
        "valid": result == TRANSPORT_RESULT_OK,
        "result": result,
        "packet_size": packet_size,
        "origin_node": transport[7],
        "sequence": read_u32(8),
        "payload_class": transport[12],
        "flags": transport[13],
        "hop_count": transport[14],
        "hop_limit": transport[15],
        "schedule_crc32": read_u32(16),
        "profile_crc32": read_u32(20),
        "identity_crc32": observed_identity_crc32,
        "expected_identity_crc32": identity_crc32,
        "identity_crc_xor": observed_identity_crc32 ^ identity_crc32,
        "identity_single_bit_repairs": identity_repairs,
        "transport_crc32": observed_transport_crc32,
        "expected_transport_crc32": transport_crc32,
        "transport_crc_xor": observed_transport_crc32 ^ transport_crc32,
        "transport_single_bit_repairs": transport_repairs,
        "transport_repairs_after_identity": transport_repairs_after_identity,
    }


def _display_bits(values: Sequence[int], window_bits: int) -> list[int]:
    packet = latest_complete_packet(values)
    bits = byte_bits(packet if packet is not None else values)
    if packet is not None:
        shown = bits[:window_bits]
        return shown + [0] * (window_bits - len(shown))
    shown = bits[-window_bits:]
    return [0] * (window_bits - len(shown)) + shown


def best_alignment(reference: Sequence[int], candidate: Sequence[int],
                   max_lag_bits: int = 64) -> dict[str, int] | None:
    if not reference or not candidate:
        return None
    best: tuple[int, int, int] | None = None
    for lag in range(-max_lag_bits, max_lag_bits + 1):
        ref_start = max(0, -lag)
        candidate_start = max(0, lag)
        overlap = min(len(reference) - ref_start,
                      len(candidate) - candidate_start)
        if overlap <= 0:
            continue
        distance = sum(
            reference[ref_start + index] !=
            candidate[candidate_start + index]
            for index in range(overlap))
        # Compare mismatch ratio first, then prefer larger overlap/smaller lag.
        score = (distance * 1000000) // overlap
        row = (score, -overlap, abs(lag), lag, distance, overlap)
        if best is None or row[:3] < best[:3]:
            best = row  # type: ignore[assignment]
    if best is None:
        return None
    return {"lag_bits": best[3], "distance": best[4],
            "overlap_bits": best[5]}


def _step_path(samples: Sequence[int], *, x0: float, y0: float,
               bit_width: float, height: float) -> str:
    if not samples:
        return ""
    points = [f"M{x0:.2f},{y0 + (1 - samples[0]) * height:.2f}"]
    for index, value in enumerate(samples):
        left = x0 + index * bit_width
        right = left + bit_width
        y = y0 + (1 - value) * height
        if index:
            previous_y = y0 + (1 - samples[index - 1]) * height
            points.append(f"L{left:.2f},{previous_y:.2f} L{left:.2f},{y:.2f}")
        points.append(f"L{right:.2f},{y:.2f}")
    return " ".join(points)


def _inbound_sources(config: dict[str, Any], node: int
                     ) -> tuple[int, int, int]:
    links = config.get("links")
    if not isinstance(links, list):
        raise ValueError("TRN-03 matrix links missing")
    marker_matches = [link for link in links
                      if int(link.get("marker_destination_node", -1)) == node]
    data_matches = [link for link in links
                    if int(link.get("data_destination_node", -1)) == node]
    if len(marker_matches) != 1 or len(data_matches) != 1:
        raise ValueError(f"node{node} direction mapping is incomplete")
    return (int(marker_matches[0]["marker_source_node"]),
            int(data_matches[0]["data_source_node"]),
            int(marker_matches[0]["link_index"]))


def render_node_svg(config: dict[str, Any],
                    captures: dict[int, dict[str, Any]], *, node: int,
                    svg_path: Path,
                    window_duration_ns: int = DEFAULT_WINDOW_NS
                    ) -> dict[str, Any]:
    if node not in captures:
        raise ValueError(f"node{node} capture missing")
    marker_source, data_source, marker_link = _inbound_sources(config, node)
    local = validate_capture(captures[node])
    bit_period_ns = int(local["bit_period_ns"])
    window_bits = max(1, window_duration_ns // bit_period_ns)
    observed_raw = local["rx_bytes"]
    logical_raw = captures[marker_source]["tx_bytes"]
    physical_raw = captures[data_source]["tx_bytes"]
    observed_packet = latest_complete_packet(observed_raw)
    logical_packet = latest_complete_packet(logical_raw)
    physical_packet = latest_complete_packet(physical_raw)
    observed_transport = decode_transport_evidence(observed_packet)
    logical_transport = decode_transport_evidence(logical_packet)
    physical_transport = decode_transport_evidence(physical_packet)
    observed = byte_bits(observed_packet) if observed_packet is not None else []
    logical = byte_bits(logical_packet) if logical_packet is not None else []
    physical = byte_bits(physical_packet) if physical_packet is not None else []
    logical_alignment = best_alignment(logical, observed)
    physical_alignment = best_alignment(physical, observed)

    width, height = 1400, 500
    x0, plot_width = 250.0, 1100.0
    bit_width = plot_width / window_bits
    tracks = [
        (f"node{marker_source} TX logical reference", logical_raw, "#3465a4"),
        (f"node{data_source} TX physical DATA source", physical_raw, "#75507b"),
        (f"node{node} RX sampled DATA", observed_raw, "#4e9a06"),
    ]
    paths: list[str] = []
    for index, (label, bits, color) in enumerate(tracks):
        y = 150.0 + index * 105.0
        shown = _display_bits(bits, window_bits)
        paths.append(
            f'<text x="20" y="{y + 18:.0f}" class="label">'
            f'{html.escape(label)}</text>')
        if bits:
            paths.append(
                f'<path d="{_step_path(shown, x0=x0, y0=y, bit_width=bit_width, height=45)}" '
                f'stroke="{color}" fill="none" stroke-width="3"/>')
        else:
            paths.append(
                f'<text x="{x0:.0f}" y="{y + 25:.0f}" class="missing">'
                f'NO SAMPLES</text>')
    ticks = []
    for bit in range(window_bits + 1):
        x = x0 + bit * bit_width
        ticks.append(
            f'<line x1="{x:.2f}" y1="120" x2="{x:.2f}" y2="390" '
            f'class="grid"/><text x="{x:.2f}" y="420" class="tick">'
            f'{bit * bit_period_ns}</text>')
    logical_delay = (None if logical_alignment is None else
                     logical_alignment["lag_bits"] * bit_period_ns)
    physical_delay = (None if physical_alignment is None else
                      physical_alignment["lag_bits"] * bit_period_ns)
    status = ("RX has no SCK-clocked samples" if not observed_raw else
              "physical DATA source has no complete TX frame; idle-low is expected"
              if not physical else
              "RX samples captured but no complete 0x54,0x44 frame was found"
              if observed_packet is None else
              "complete RX and source frames captured")
    if observed_transport is not None and not observed_transport["valid"]:
        status += f'; RX transport {observed_transport["result"]}'
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="500" '
        'viewBox="0 0 1400 500">\n'
        '<style>.title{font:700 20px sans-serif}.note,.label{font:15px sans-serif}'
        '.tick{font:12px monospace;text-anchor:middle}.grid{stroke:#ddd;stroke-width:1}'
        '.missing{font:700 18px monospace;fill:#c00}</style>\n'
        f'<text x="20" y="30" class="title">TRN-03B node{node}: '
        f'link{marker_link} marker input vs physical DATA input</text>\n'
        f'<text x="20" y="58" class="note">window {window_duration_ns / 1000:g} us; '
        f'bit period {bit_period_ns} ns; marker source node{marker_source}; '
        f'DATA source node{data_source}</text>\n'
        f'<text x="20" y="83" class="note">logical best bit-sequence shift '
        f'{logical_delay if logical_delay is not None else "N/A"} ns; '
        f'physical best bit-sequence shift '
        f'{physical_delay if physical_delay is not None else "N/A"} ns; '
        f'{html.escape(status)}</text>\n'
        + "".join(ticks) + "".join(paths) +
        '<text x="800" y="455" class="tick">time (ns)</text>\n</svg>\n')
    svg_path.parent.mkdir(parents=True, exist_ok=True)
    svg_path.write_text(svg, encoding="utf-8")
    return {
        "node": node,
        "marker_link": marker_link,
        "marker_source_node": marker_source,
        "data_source_node": data_source,
        "rx_sample_count": len(byte_bits(observed_raw)),
        "rx_complete_frame_found": observed_packet is not None,
        "logical_reference_sample_count": len(logical),
        "physical_source_sample_count": len(physical),
        "logical_alignment": logical_alignment,
        "physical_alignment": physical_alignment,
        "logical_best_delay_ns": logical_delay,
        "physical_best_delay_ns": physical_delay,
        "rx_transport": observed_transport,
        "logical_reference_transport": logical_transport,
        "physical_source_transport": physical_transport,
        "status": status,
        "svg": str(svg_path),
    }


def analyze_capture_set(config: dict[str, Any], capture_paths: Sequence[Path],
                        out_dir: Path, window_duration_ns: int
                        ) -> dict[str, Any]:
    captures: dict[int, dict[str, Any]] = {}
    for path in capture_paths:
        capture = validate_capture(json.loads(path.read_text(encoding="utf-8")))
        node = int(capture["node"])
        if node in captures:
            raise ValueError(f"duplicate node{node} capture")
        captures[node] = capture
    node_count = int(config.get("node_count", 0))
    if set(captures) != set(range(node_count)):
        raise ValueError("capture set does not cover every configured node")
    analyses = [
        render_node_svg(
            config, captures, node=node,
            svg_path=out_dir / f"node{node}_ring_capture_1us.svg",
            window_duration_ns=window_duration_ns)
        for node in range(node_count)
    ]
    result = {
        "schema": "HAOFV_TRN03_RING_WAVEFORM_ANALYSIS_V1",
        "calibration_generation": int(config["calibration_generation"]),
        "window_duration_ns": window_duration_ns,
        "nodes": analyses,
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "ring_capture_analysis.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--capture", action="append", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--window-duration-ns", type=int,
                        default=DEFAULT_WINDOW_NS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.window_duration_ns <= 0:
            raise ValueError("window-duration-ns must be positive")
        result = analyze_capture_set(
            json.loads(args.config.read_text(encoding="utf-8")),
            args.capture, args.out_dir, args.window_duration_ns)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAILED: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
