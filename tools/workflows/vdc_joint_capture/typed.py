"""Tracked typed-TX and child lifecycle over the remaining base probe."""

from __future__ import annotations

import _imp
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import time
from types import SimpleNamespace

from tools.domains.vdc.priority_trace import probe_support as tracked_probe_support
from tools.hardware_acceptance import p3_hardware_acceptance as gate


ROOT = Path(__file__).resolve().parents[3]
SESSION = "SYSTem:VDC:FEEDback:SESSion"
SYNC = "SYSTem:VDC:PRIORity:SYNC"
MODEL = "SYSTem:VDC:FEEDback:MODel?"
TX = "SYSTem:VDC:PRIORity:TX?"
CONS = "SYSTem:TDMA:FLIGHT:PRIORity:CONSumed?"
TX_FIELDS = (
    "schema generation active retired have_offer calls encoded repeated rejected last_reject "
    "source_epoch event_sequence source_identity published_version model_token session "
    "role_generation clock_epoch clock_run local_slot node_count schedule_crc32 profile_crc32 "
    "uncertainty_width"
).split()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def _load_base(path: Path):
    spec = importlib.util.spec_from_file_location("vdc_joint_probe_base", path)
    if spec is None or spec.loader is None:
        raise ValueError("Historical base entrypoint cannot be loaded")
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


def fields(raw):
    return next(csv.reader([raw]))


def crc16(raw):
    crc = 0xFFFF
    for byte in raw:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xFFFF
    return crc


def typed_mailbox(raw, generation, slot):
    wire = bytes.fromhex(raw)
    require(len(wire) == 32 and wire[3] == 0x14 and wire[4] == 0,
            "Typed mailbox size/class mismatch")
    require(wire[5] == 0x0E and (slot == 0 or wire[5] & (1 << slot)),
            "Typed mailbox target mask mismatch")
    require(crc16(wire[:30]) == struct.unpack_from("<H", wire, 30)[0],
            "Typed mailbox CRC mismatch")
    found_generation, sequence, lower_ns, width_ns, flags = \
        struct.unpack_from("<IIQIH", wire, 8)
    require(found_generation == generation and width_ns > 0 and flags == 0,
            "Typed mailbox generation/interval/flags mismatch")
    require(struct.unpack_from("<H", wire, 6)[0] == sequence & 0xFFFF,
            "Typed mailbox sequence16 mismatch")
    return {
        "binding_generation": found_generation,
        "event_sequence": sequence,
        "lower_ns": lower_ns,
        "width_ns": width_ns,
        "flags": flags,
        "mailbox_hex": wire.hex(),
    }


def load_typed(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked typed-TX layer over an admitted base Probe module."""
    base = runtime_module if runtime_module is not None else _load_base(runtime_entrypoint)
    if not hasattr(base, "Probe") or not hasattr(base, "BACKEND") or \
            not hasattr(base, "trn"):
        raise ValueError("Historical base entrypoint lacks Probe/BACKEND/trn")
    transport = getattr(base, "scpi_serial", None)
    if transport is None and hasattr(base.BACKEND, "SerialSession"):
        transport = sys.modules.get(base.BACKEND.SerialSession.__module__)
    if transport is None or not hasattr(transport, "SCALAR_U32_QUERY_HEADERS") or \
            not hasattr(transport, "SCALAR_ONE_QUERY_HEADERS"):
        raise ValueError("Historical base transport registries are unavailable")
    stage_owner = getattr(base, "stage_owner", sys.modules.get("trn03_stage"))
    if stage_owner is None or not hasattr(stage_owner, "verify_board"):
        raise ValueError("Historical base did not load trn03_stage owner")
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()

    def close_connections():
        failures = []
        connections = base.BACKEND._PERSISTENT_CONNECTIONS
        for uid, connection in list(connections.items()):
            try:
                connection.close()
            except Exception as exc:
                failures.append(f"{uid}: {type(exc).__name__}: {exc}")
            finally:
                connections.pop(uid, None)
        require(not failures, f"Connection close failures: {failures}")

    class TypedProbe(base.Probe):
        def __init__(self, options):
            super().__init__(options)
            self.report.update(
                schema="TYPED_PRIORITY_TX_SHORT_PROBE_V1",
                run_seconds=options.seconds)
            self.report["tool_sources"].append({
                "path": str(source), "sha256": source_sha256})
            self.report["plan"] = {
                "run_seconds": options.seconds,
                "trace_capture": False,
                "calibration_remeasured": False,
                "no_queries_during_run": True,
            }
            base.BACKEND.COMPOSITE_ACK_HEADERS.add(SESSION.upper())
            base.BACKEND.COMPOSITE_ACK_HEADERS.add("SYSTEM:TDMA:FLIGHT:TX")
            base.BACKEND.COMPOSITE_ACK_HEADERS.add("SYSTEM:TDMA:LOAD:MASK")
            base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[SYNC.upper()] = 1
            transport.SCALAR_U32_QUERY_HEADERS.add(SYNC.upper() + "?")

        def composite(self, board, header, value):
            response = base.fields(self.command(board, f"{header} {value}"))
            require(response == ["OK", str(value)],
                    f"Composite ACK mismatch: {header}: {response}")
            actual = int(self.command(board, header + "?").strip('"'))
            require(actual == value,
                    f"Composite readback mismatch: {header}: {actual}")

        def configure(self):
            matrix = json.loads((ROOT / self.receipt["trn03_matrix"]["path"]).read_text(
                encoding="utf-8"))
            generation = self.receipt["calibration_generation"]
            master = self.boards[0]
            old_generation = int(self.command(master, SYNC + "?").strip('"'))
            old_tx = fields(self.command(master, TX))
            old_generation = max(old_generation, int(old_tx[1]))
            old_session = int(self.command(master, SESSION + "?").strip('"'))
            fresh = max(int(time.time()), old_generation + 1, old_session + 1)
            require(1 < fresh <= 0xFFFFFFFF, "Fresh binding generation is invalid")
            self.report.update(binding_generation=fresh, feedback_session=fresh)
            for slot, board in enumerate(self.boards):
                row = self.report["boards"][board.address]
                self.control(board, "SYSTem:TDMA:RING:GEOMetry 0", 0)
                self.control(board, SYNC + " 0", 0)
                self.composite(board, SESSION, 0)
                for header in (
                        "SYSTem:VDC:FEEDback:REFerence",
                        "SYSTem:VDC:FEEDback:LOCALfollow"):
                    base.BACKEND.COMPOSITE_ACK_HEADERS.add(header.upper())
                    transport.SCALAR_ONE_QUERY_HEADERS.add(header.upper() + "?")
                    self.composite(board, header, 0)
                row["stage_reuse"] = stage_owner.verify_board(
                    board, matrix, self.args)
                require(row["stage_reuse"]["passed"],
                        "Previously measured stage differs; no retraining")
                old_mask = int(self.command(
                    board, "SYSTem:TDMA:LOAD:MASK?").strip('"'))
                row["original_load_mask"] = old_mask
                self.set_load_mask(
                    board, old_mask | base.trn.APP_REALTIME_LOAD_VDC_MASK |
                    base.trn.APP_REALTIME_LOAD_DPLL_MASK)
                row["original_diagnostic"] = int(self.command(
                    board, "SYSTem:TDMA:RING:DIAGnostic?").strip('"'))
                self.control(board, "SYSTem:TDMA:FLIGHT:FIFO:RESet")
                require(not base.trn.validate_fifo_reset(
                    base.trn.flight_snapshot(board, self.args)),
                    "Stopped FIFO reset not acknowledged")
                self.control(board, "SYSTem:TDMA:RING:DIAGnostic 1", 1)
                if slot == 0:
                    row["original_burst"] = int(self.command(
                        board, "SYSTem:TDMA:RING:BURSt?").strip('"'))
                    self.control(board, "SYSTem:TDMA:RING:BURSt 0", 0)
                seed = 0x40 + slot * 0x10
                response = base.fields(self.command(
                    board,
                    f"SYSTem:TDMA:FLIGHT:TX 32,{seed},{generation},{slot + 1},1"))
                expected = [
                    "OK", "32", str(sum((seed + index) & 255 for index in range(32))),
                    str(seed), str(generation), str(slot + 1), "1"]
                require(response == expected, "TX prime composite ACK mismatch")
            self.composite(master, SESSION, fresh)
            self.control(master, f"{SYNC} {fresh}", fresh)
            require(int(self.command(master, SYNC + "?").strip('"')) == fresh,
                    "SYNC generation readback mismatch")
            for board in reversed(self.boards):
                slot = base.IDS.index(board.address)
                row = self.report["boards"][board.address]
                row["arm"] = base.trn.arm_with_evidence(
                    board, self.args, slot)
                row["ready_config"] = base.trn.wait_runtime_config_applied(
                    board, self.args, slot)
            self.report["clock_hz"] = int(base.fields(self.command(
                master, "SYSTem:TDMA:SCHEDule?"))[1])
            self.report["model_before_start"] = self.command(master, MODEL)

        def acquire(self):
            self.block_queries = True
            self.barrier = False
            for board in reversed(self.boards):
                self.control(board, "SYSTem:TDMA:RING:START")
            time.sleep(0.2)
            epoch = int(time.time()) & 0xFFFFFFFF
            grant = int(self.command(
                self.boards[0],
                f"CALibration:ORIGin:TRIAL {epoch},8192,256,{self.report['clock_hz'] * 15}"
            ).strip('"'))
            require(grant > 0 and not grant & 1, "Origin trial grant rejected")
            self.report["trial_epoch"] = grant
            time.sleep(self.opt.seconds)

        def collect(self):
            require(self.barrier and not self.block_queries,
                    "Typed collection requires all-board STOP")
            master = self.boards[0]
            self.report["tx_raw"] = self.command(master, TX)
            self.report["model_after_stop"] = self.command(master, MODEL)
            tx = fields(self.report["tx_raw"])
            require(len(tx) == 27, "TX status field count mismatch")
            tx_status = dict(zip(TX_FIELDS, map(int, tx[:24])))
            tx_status["lower_ns"] = int(tx[24]) | int(tx[25]) << 32
            self.report["tx_status"] = tx_status
            self.save()
            for board in self.boards:
                row = self.report["boards"][board.address]
                for name, command in (
                        ("consumer_raw", CONS),
                        ("priority_raw", "SYSTem:TDMA:FLIGHT:PRIORity?"),
                        ("schedule_raw", "SYSTem:TDMA:SCHEDule?"),
                        ("error_raw", "SYSTem:ERR?")):
                    row[name] = self.command(board, command)
                    self.save()
            fresh = self.report["binding_generation"]
            require(tx_status["schema"] == 1 and tx_status["generation"] == fresh,
                    "TX schema/generation mismatch")
            require(tx_status["have_offer"] == 1 and tx_status["encoded"] > 0,
                    "TX has no encoded offer")
            require(tx_status["session"] == self.report["feedback_session"],
                    "TX feedback session mismatch")
            tx_event = typed_mailbox(tx[-1], fresh, 0)
            require(tx_event["event_sequence"] == tx_status["event_sequence"],
                    "TX event sequence mismatch")
            require(tx_event["lower_ns"] == tx_status["lower_ns"] and
                    tx_event["width_ns"] == tx_status["uncertainty_width"],
                    "TX event interval mismatch")
            self.report["tx_event"] = tx_event
            for slot, board in enumerate(self.boards[1:], 1):
                row = self.report["boards"][board.address]
                values = fields(row["consumer_raw"])
                require(len(values) == 28, "Consumer field count mismatch")
                numbers = list(map(int, values[:26]))
                require(numbers[0] == 1 and numbers[3] == 1 and
                        numbers[5] > 0 and numbers[12] == 0x14 and
                        numbers[20] == numbers[2],
                        "Consumer retained status mismatch")
                header = bytes.fromhex(values[26])
                require(len(header) == 32 and
                        struct.unpack_from("<I", header, 8)[0] == numbers[21],
                        "Consumer header size/sequence mismatch")
                require(__import__("zlib").crc32(header[:14] + header[15:24]) ==
                        struct.unpack_from("<I", header, 24)[0],
                        "Consumer header CRC mismatch")
                require(__import__("zlib").crc32(header[:28] + bytes(4)) ==
                        struct.unpack_from("<I", header, 28)[0],
                        "Consumer packet CRC mismatch")
                require(struct.unpack_from("<I", header, 16)[0] ==
                        tx_status["schedule_crc32"],
                        "Consumer schedule CRC mismatch")
                require(struct.unpack_from("<I", header, 20)[0] ==
                        tx_status["profile_crc32"],
                        "Consumer profile CRC mismatch")
                event = typed_mailbox(values[27], fresh, slot)
                row["typed_event"] = event
                row["matches_retained_master_event"] = (
                    event["event_sequence"] == tx_event["event_sequence"])
                if row["matches_retained_master_event"]:
                    require(event == tx_event,
                            "Same-identity follower/master event differs")
            self.report["flow_completed"] = True
            self.report["passed"] = not self.report["errors"]

        def run(self):
            original_stage = stage_owner.board_command
            try:
                self.receipt = json.loads(
                    self.opt.receipt.read_text(encoding="utf-8"))
                gate._validate_quick_diagnostic_receipt(ROOT, self.receipt)
                require(gate.working_source_fingerprint(ROOT) == (
                    self.receipt["source_tree_sha256"],
                    self.receipt["source_file_count"]),
                    "Current source differs from receipt")
                require(self.receipt["tdma_board_ids"] == base.IDS,
                        "Receipt board order mismatch")
                self.args.expected_build = int(self.receipt["build_id"])
                if self.opt.check_only:
                    self.report.update(check_only=True, passed=True)
                    return
                ports = {}
                for port in base.comports():
                    if port.serial_number in base.IDS:
                        require(port.serial_number not in ports,
                                "Duplicate USB UID")
                        ports[port.serial_number] = port.device
                require(set(ports) == set(base.IDS),
                        "Four-board USB UID scan incomplete")
                self.boards = [
                    base.trn.Board(ports[uid], uid, "", self.receipt["build_id"])
                    for uid in base.IDS]
                self.report["boards"] = {uid: {} for uid in base.IDS}
                base.trn.board_command = self.command
                stage_owner.board_command = self.command
                for board in self.boards:
                    connection = base.BACKEND.BoardConnection(board, self.args)
                    base.BACKEND._PERSISTENT_CONNECTIONS[board.address] = connection
                    connection.session = base.BACKEND.SerialSession(
                        board.port, self.args.baud, self.args.timeout,
                        self.args.settle, read_timeout_s=self.args.read_timeout)
                    connection.session.open()
                self.report["hardware_started"] = True
                self.stop_all("initial_stop")
                for slot, board in enumerate(self.boards):
                    require(board.address in self.command(board, "*IDN?"),
                            "Online UID mismatch")
                    require(self.command(
                        board, "SYSTem:FW:BUILD?").strip('"') == board.build,
                        "Online build mismatch")
                    require(int(self.command(board, "SYSTem:BOARD:NO?")) == slot + 1,
                            "Online board number mismatch")
                self.configure()
                self.acquire()
            except Exception as exc:
                self.error("run", exc)
            finally:
                if self.report["hardware_started"]:
                    try:
                        self.stop_all("final_stop", passes=2)
                    except Exception as exc:
                        self.error("final_stop", exc)
                    if self.barrier:
                        try:
                            self.collect()
                        except Exception as exc:
                            self.error("final_collect", exc)
                        for board in self.boards:
                            row = self.report["boards"][board.address]
                            cleanup = [
                                ("sync", lambda board=board:
                                 self.control(board, SYNC + " 0", 0)),
                                ("session", lambda board=board:
                                 self.composite(board, SESSION, 0)),
                            ]
                            if "original_load_mask" in row:
                                cleanup.append((
                                    "load-mask", lambda board=board, row=row:
                                    self.set_load_mask(board, row["original_load_mask"])
                                ))
                            if "original_diagnostic" in row:
                                cleanup.append((
                                    "diagnostic", lambda board=board, row=row:
                                    self.control(
                                        board,
                                        f"SYSTem:TDMA:RING:DIAGnostic {row['original_diagnostic']}",
                                        row["original_diagnostic"])
                                ))
                            if "original_burst" in row:
                                cleanup.append((
                                    "burst", lambda board=board, row=row:
                                    self.control(
                                        board,
                                        f"SYSTem:TDMA:RING:BURSt {row['original_burst']}",
                                        row["original_burst"])
                                ))
                            for name, action in cleanup:
                                try:
                                    action()
                                except Exception as exc:
                                    self.error(
                                        f"disable_restore:{board.address}:{name}", exc)
                        try:
                            self.control(self.boards[0], "CALibration:ORIGin:REVOKe")
                        except Exception as exc:
                            self.error("revoke", exc)
                try:
                    close_connections()
                except Exception as exc:
                    self.error("close", exc)
                finally:
                    base.trn.board_command = self.original_command
                    stage_owner.board_command = original_stage
                self.report["passed"] = bool(
                    self.report["passed"] and not self.report["errors"])
                self.report["conclusion_scope"] = (
                    "Typed class0x14 ns interval transported into follower Core1; offers "
                    "are not DMA commits. Final master and follower may retain different "
                    "events. No precision, one-lap deadline, local event match, ACK/control "
                    "or lock claim.")
                self.save()

    return SimpleNamespace(
        __file__=str(source),
        TypedProbe=TypedProbe,
        base=base,
        stage_owner=stage_owner,
        scpi_serial=transport,
        SESSION=SESSION,
        SYNC=SYNC,
        MODEL=MODEL,
        TX=TX,
        CONS=CONS,
        TX_FIELDS=TX_FIELDS,
        fields=fields,
        crc16=crc16,
        typed_mailbox=typed_mailbox,
        close_connections=close_connections,
        source_path=source,
        source_sha256=source_sha256,
    )
