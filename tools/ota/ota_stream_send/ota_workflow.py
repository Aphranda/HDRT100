"""Step-driven TDMA stream OTA workflow (bounded, resumable, no hangs).

Each step opens its own serial link, performs one bounded action, writes
``steps/<NN>_<name>.json`` and updates ``state.json``.  A step that times out is
recorded as ``step_timeout`` and the run stops -- no unbounded loops.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import random
import time
from dataclasses import dataclass, field
from pathlib import Path

from ...ota.ota_stream_send import ota_stream_send as core


STEPS = [
    "identify",
    "open",
    "header",
    "data",
    "close",
    "wait_ready",
    "commit",
    "reboot",
    "verify",
]


@dataclass
class Link:
    port: str
    baud: int = 115200
    timeout: float = 3.0

    def command(self, payload: bytes | str, timeout: float | None = None) -> str:
        import serial

        with serial.Serial(self.port, self.baud, timeout=0.2) as ser:
            ser.reset_input_buffer()
            data = payload.encode() if isinstance(payload, str) else payload
            ser.write(data + b"\n")
            ser.flush()
            buf = b""
            deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
            while time.monotonic() < deadline:
                chunk = ser.read(1)
                if not chunk:
                    continue
                if chunk == b"\n":
                    text = buf.decode("ascii", errors="replace").strip()
                    buf = b""
                    if text:
                        return text
                    continue
                if chunk != b"\r":
                    buf += chunk
                    # OPEN replies with a bare quoted token and no CRLF.
                    if (len(buf) >= 2 and buf.startswith(b'"')
                            and buf.endswith(b'"')):
                        return buf.decode("ascii", errors="replace").strip()
            return "<timeout>"


@dataclass
class State:
    attempt_dir: Path
    image: str
    port: str
    block_size: int
    steps: dict = field(default_factory=dict)
    open_params: dict = field(default_factory=dict)
    durable: int = 0
    completed: list = field(default_factory=list)

    def save(self) -> None:
        self.attempt_dir.mkdir(parents=True, exist_ok=True)
        payload = {
            "image": self.image,
            "port": self.port,
            "block_size": self.block_size,
            "durable": self.durable,
            "completed": self.completed,
            "open_params": self.open_params,
            "steps": self.steps,
        }
        (self.attempt_dir / "state.json").write_text(
            json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    @classmethod
    def load(cls, attempt_dir: Path, image: str, port: str,
             block_size: int) -> "State":
        path = attempt_dir / "state.json"
        if path.is_file():
            data = json.loads(path.read_text(encoding="utf-8"))
            state = cls(attempt_dir, data.get("image", image),
                        data.get("port", port),
                        data.get("block_size", block_size))
            state.steps = data.get("steps", {})
            state.open_params = data.get("open_params", {})
            state.durable = data.get("durable", 0)
            state.completed = data.get("completed", [])
            return state
        return cls(attempt_dir, image, port, block_size)


def record(state: State, index: int, name: str, ok: bool, detail: dict) -> None:
    entry = {"step": name, "ok": ok, **detail}
    state.steps[name] = entry
    path = state.attempt_dir / "steps"
    path.mkdir(parents=True, exist_ok=True)
    (path / f"{index:02d}_{name}.json").write_text(
        json.dumps(entry, indent=2) + "\n", encoding="utf-8")
    if ok:
        state.completed.append(name)
    state.save()


def _status(link: Link) -> list[int]:
    raw = link.command("SYST:OTA:STREAM:STAT?")
    try:
        return [int(x) for x in raw.split(",")]
    except ValueError:
        return []



def _stream_image(state: State) -> bytes:
    """Package header + selected slot object, matching ota_stream_send."""
    source = Path(state.image).read_bytes()
    if state.open_params.get("package", True):
        return core.selected_package_object(
            source, int(state.open_params.get("target_slot", 2)))
    return source

def step_identify(state: State, link: Link) -> tuple[bool, dict]:
    idn = link.command("*IDN?")
    build = link.command("SYST:FW:BUILD?")
    caps = link.command("SYST:OTA:STREAM:CAP?")
    mode = link.command("SYST:OTA:MODE?")
    ok = "DHRT100" in idn and caps != "<timeout>"
    detail = {"idn": idn, "build": build, "caps": caps, "mode": mode,
              "status": link.command("SYST:OTA:STAT?")}
    if ok:
        parts = [int(x) for x in caps.strip('"').split(",")]
        state.open_params["map_version"] = parts[2] if len(parts) > 2 else 1
        state.open_params["max_block"] = parts[0] if parts else 4096
        targ = link.command("SYST:OTA:TARG?")
        try:
            state.open_params["target_slot"] = int(targ)
        except ValueError:
            state.open_params["target_slot"] = 0
    return ok, detail


def step_open(state: State, link: Link) -> tuple[bool, dict]:
    image = _stream_image(state)
    identity = hashlib.sha256(link.command("*IDN?").encode()).digest()[:16]
    map_version = state.open_params.get("map_version", 2)
    session_id = random.getrandbits(31)
    target_slot = int(state.open_params.get("target_slot", 2))
    wire = core.encode_open(
        identity, image, target_slot, session_id, session_id, map_version,
        core.app_partition_id(map_version, target_slot),
        core.SOURCE_USB_CDC, True)
    response = link.command(
        b"SYST:OTA:STREAM:OPEN " + str(core.SOURCE_USB_CDC).encode()
        + b"," + core.scpi_block(wire))
    state.open_params.update({"session_id": session_id,
                              "generation": session_id,
                              "map_version": map_version})
    ok = response == '"OK"'
    return ok, {"response": response, "status": _status(link)}


def _send_chunk(state: State, link: Link, offset: int, data: bytes) -> str:
    return link.command(
        b"SYST:OTA:STREAM:DATA " + str(core.SOURCE_USB_CDC).encode()
        + b"," + str(offset).encode()
        + b"," + str(core.crc32(data)).encode()
        + b"," + core.scpi_block(data))


def _wait_durable(link: Link, target: int, timeout_s: float) -> tuple[bool, list[int]]:
    deadline = time.monotonic() + timeout_s
    last: list[int] = []
    while time.monotonic() < deadline:
        last = _status(link)
        if len(last) >= 3 and last[2] >= target:
            return True, last
        if len(last) >= 2 and last[1] == 6:
            return False, last
        time.sleep(0.2)
    return False, last


def step_header(state: State, link: Link) -> tuple[bool, dict]:
    image = _stream_image(state)
    chunk = image[:core.PACKAGE_HEADER_SIZE]
    response = _send_chunk(state, link, 0, chunk)
    ok, status = _wait_durable(link, len(chunk), 300.0)
    state.durable = max(state.durable, len(chunk)) if ok else state.durable
    return ok and response == '"OK"', {
        "response": response, "durable": state.durable, "status": status}


def step_data(state: State, link: Link) -> tuple[bool, dict]:
    image = _stream_image(state)
    offset = state.durable or core.PACKAGE_HEADER_SIZE
    deadline = time.monotonic() + 3600.0
    chunks = 0
    while offset < len(image):
        if time.monotonic() > deadline:
            return False, {"reason": "data deadline", "offset": offset,
                           "durable": state.durable, "chunks": chunks}
        data = image[offset:offset + state.block_size]
        response = _send_chunk(state, link, offset, data)
        ok, status = _wait_durable(link, offset + len(data), 400.0)
        if not ok or response != '"OK"':
            return False, {"offset": offset, "length": len(data),
                           "response": response, "durable": state.durable,
                           "status": status, "chunks": chunks}
        offset += len(data)
        state.durable = offset
        chunks += 1
        if chunks == 10:
            mid_phase = link.command("SYST:WATCHdog:OTA:PHase?")
            print(f"mid-transfer phase={mid_phase}", flush=True)
        if chunks % 8 == 0:
            state.save()
    return True, {"note": "complete", "durable": state.durable,
                  "chunks": chunks,
                  "phase_after": link.command("SYST:WATCHdog:OTA:PHase?"),
                  "rtos": link.command("SYST:RTOS:STAT?")}


def step_close(state: State, link: Link) -> tuple[bool, dict]:
    response = link.command(
        b"SYST:OTA:STREAM:CLOSe " + str(core.SOURCE_USB_CDC).encode())
    return response == '"OK"', {"response": response, "status": _status(link)}


def step_wait_ready(state: State, link: Link) -> tuple[bool, dict]:
    deadline = time.monotonic() + 300.0
    last: list[int] = []
    while time.monotonic() < deadline:
        last = _status(link)
        if len(last) >= 2 and last[1] == 4:
            return True, {"status": last}
        if len(last) >= 2 and last[1] == 6:
            return False, {"status": last, "reason": "session failed"}
        time.sleep(0.5)
    return False, {"status": last, "reason": "ready timeout"}


def step_commit(state: State, link: Link) -> tuple[bool, dict]:
    target_slot = int(state.open_params.get("target_slot", 2))
    response = link.command("SYST:OTA:COMMit", timeout=15.0)
    detail = {"response": response, "target_slot": target_slot}
    # COMMIT only records the pending slot on this build; the bootloader
    # applies it on the next reset (step_reboot).
    deadline = time.monotonic() + 30.0
    last_slot = ""
    while time.monotonic() < deadline:
        last_slot = link.command("SYST:OTA:SLOT?")
        fields = [int(x) for x in last_slot.split(",") if x.strip().isdigit()]
        if len(fields) >= 2:
            active, pending = fields[0], fields[1]
            detail.update({"slot": last_slot, "active": active,
                           "pending": pending})
            if pending == target_slot:
                detail["pending_set"] = True
                return True, detail
            if active == target_slot and pending == 0:
                detail["already_applied"] = True
                return True, detail
        time.sleep(1.0)
    detail["slot"] = last_slot
    return False, detail


def step_reboot(state: State, link: Link) -> tuple[bool, dict]:
    target_slot = int(state.open_params.get("target_slot", 2))
    deadline = time.monotonic() + 90.0
    reset_sent = False
    last_slot = ""
    while time.monotonic() < deadline:
        try:
            last_slot = link.command("SYST:OTA:SLOT?")
        except Exception:  # noqa: BLE001 - port vanishes during reset
            time.sleep(1.0)
            continue
        fields = [int(x) for x in last_slot.split(",") if x.strip().isdigit()]
        if len(fields) >= 2 and fields[0] == target_slot and fields[1] == 0:
            return True, {"slot": last_slot, "active": fields[0],
                          "reset_sent": reset_sent}
        if not reset_sent:
            try:
                link.command("SYST:BOOT:RESET", timeout=5.0)
            except Exception:  # noqa: BLE001
                pass
            reset_sent = True
        time.sleep(1.5)
    return False, {"slot": last_slot, "reset_sent": reset_sent}


def step_verify(state: State, link: Link) -> tuple[bool, dict]:
    target_slot = int(state.open_params.get("target_slot", 2))
    deadline = time.monotonic() + 60.0
    detail = {}
    while time.monotonic() < deadline:
        try:
            idn = link.command("*IDN?")
            slot = link.command("SYST:OTA:SLOT?")
            build = link.command("SYST:FW:BUILD?")
            result = link.command("SYST:OTA:RESult?")
        except Exception:  # noqa: BLE001
            time.sleep(1.0)
            continue
        fields = [int(x) for x in slot.split(",") if x.strip().isdigit()]
        detail = {"idn": idn, "build": build, "slot": slot, "result": result}
        if ("DHRT100" in idn and len(fields) >= 2
                and fields[0] == target_slot and fields[1] == 0):
            return True, detail
        time.sleep(1.0)
    return False, detail


HANDLERS = {
    "identify": step_identify,
    "open": step_open,
    "header": step_header,
    "data": step_data,
    "close": step_close,
    "wait_ready": step_wait_ready,
    "commit": step_commit,
    "reboot": step_reboot,
    "verify": step_verify,
}


def run(args: argparse.Namespace) -> int:
    state = State.load(args.attempt_dir, str(args.image), args.port,
                       args.block_size)
    if not args.resume and args.attempt_dir.exists():
        for name in STEPS:
            state.steps.pop(name, None)
        state.completed = []
        state.durable = 0
    link = Link(args.port, args.baud, args.timeout)
    selected = args.steps.split(",") if args.steps else STEPS
    for index, name in enumerate(STEPS, start=1):
        if name not in selected:
            continue
        if name in state.completed and args.resume:
            continue
        started = time.monotonic()
        try:
            ok, detail = HANDLERS[name](state, link)
        except Exception as exc:  # noqa: BLE001 - step boundary records it
            ok, detail = False, {"error": repr(exc)}
        detail["elapsed_s"] = round(time.monotonic() - started, 2)
        record(state, index, name, ok, detail)
        print(f"step {index:02d} {name}: {'OK' if ok else 'FAIL'} "
              f"({detail['elapsed_s']}s) {json.dumps(detail)[:160]}")
        if not ok:
            print("stopped at step", name)
            return 1
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--port", required=True)
    parser.add_argument("--attempt-dir", type=Path, required=True)
    parser.add_argument("--block-size", type=int, default=4096)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=300.0,
                        help="per-command timeout; OPEN/DATA can block during erase")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--steps", default="")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main())
