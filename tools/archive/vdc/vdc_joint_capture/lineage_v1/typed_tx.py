"""Proposed short typed-TX probe; root review required before hardware execution.

Reuse the just-completed P3 matrix on the boards. No calibration, OTA, RefMem
feedback mode, trace capture or RUN queries. Uses existing bounded STOP/ARM
helpers. --check-only never opens serial; all artifacts go to a fresh --out.
"""
from __future__ import annotations
import argparse
import csv
import json
from pathlib import Path
import struct
import sys
import time
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "out/HardwareAcceptance/20260915/dpll-input-probe-r1"))
import input_probe as base
from tools.hardware_acceptance import p3_hardware_acceptance as gate
from scpi_common import scpi_serial
stage_owner = sys.modules["trn03_stage"]

SESSION = "SYSTem:VDC:FEEDback:SESSion"
SYNC = "SYSTem:VDC:PRIORity:SYNC"
MODEL = "SYSTem:VDC:FEEDback:MODel?"
TX = "SYSTem:VDC:PRIORity:TX?"
CONS = "SYSTem:TDMA:FLIGHT:PRIORity:CONSumed?"
TX_FIELDS = ("schema generation active retired have_offer calls encoded repeated rejected last_reject "
             "source_epoch event_sequence source_identity published_version model_token session "
             "role_generation clock_epoch clock_run local_slot node_count schedule_crc32 profile_crc32 "
             "uncertainty_width").split()


def fields(raw):
    return next(csv.reader([raw]))


def crc16(raw):
    crc = 0xffff
    for byte in raw:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc


def typed_mailbox(raw, generation, slot):
    wire = bytes.fromhex(raw)
    assert len(wire) == 32 and wire[3] == 0x14 and wire[4] == 0
    assert wire[5] == 0x0e and (slot == 0 or wire[5] & (1 << slot))
    assert crc16(wire[:30]) == struct.unpack_from("<H", wire, 30)[0]
    gen, seq, lower_ns, width_ns, flags = struct.unpack_from("<IIQIH", wire, 8)
    assert gen == generation and width_ns > 0 and flags == 0
    assert struct.unpack_from("<H", wire, 6)[0] == (seq & 0xffff)
    return dict(binding_generation=gen, event_sequence=seq, lower_ns=lower_ns,
                width_ns=width_ns, flags=flags, mailbox_hex=wire.hex())


class TypedProbe(base.Probe):
    def __init__(self, opt):
        super().__init__(opt)
        self.report.update(schema="TYPED_PRIORITY_TX_SHORT_PROBE_V1", run_seconds=opt.seconds)
        self.report["tool_sources"].append(dict(path=str(Path(__file__)), sha256=base.sha(Path(__file__))))
        self.report["plan"] = dict(run_seconds=opt.seconds, trace_capture=False,
                                   calibration_remeasured=False, no_queries_during_run=True)
        base.BACKEND.COMPOSITE_ACK_HEADERS.add(SESSION.upper())
        base.BACKEND.COMPOSITE_ACK_HEADERS.add("SYSTEM:TDMA:FLIGHT:TX")
        base.BACKEND.COMPOSITE_ACK_HEADERS.add("SYSTEM:TDMA:LOAD:MASK")
        base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[SYNC.upper()] = 1
        scpi_serial.SCALAR_U32_QUERY_HEADERS.add(SYNC.upper() + "?")

    def composite(self, board, header, value):
        assert base.fields(self.command(board, f"{header} {value}")) == ["OK", str(value)]
        assert int(self.command(board, header + "?").strip('"')) == value

    def configure(self):
        matrix = json.loads((ROOT / self.receipt["trn03_matrix"]["path"]).read_text(encoding="utf8"))
        generation = self.receipt["calibration_generation"]
        master = self.boards[0]
        old_generation = int(self.command(master, SYNC + "?").strip('"'))
        old_tx = fields(self.command(master, TX))
        old_generation = max(old_generation, int(old_tx[1]))
        old_session = int(self.command(master, SESSION + "?").strip('"'))
        fresh = max(int(time.time()), old_generation + 1, old_session + 1)
        assert 1 < fresh <= 0xffffffff
        self.report.update(binding_generation=fresh, feedback_session=fresh)
        for slot, board in enumerate(self.boards):
            row = self.report["boards"][board.address]
            self.control(board, "SYSTem:TDMA:RING:GEOMetry 0", 0)
            self.control(board, SYNC + " 0", 0)
            self.composite(board, SESSION, 0)
            # Do not enable old RefMem reference/ACK/local-follow routes.
            for header in ("SYSTem:VDC:FEEDback:REFerence", "SYSTem:VDC:FEEDback:LOCALfollow"):
                base.BACKEND.COMPOSITE_ACK_HEADERS.add(header.upper())
                scpi_serial.SCALAR_ONE_QUERY_HEADERS.add(header.upper() + "?")
                self.composite(board, header, 0)
            row["stage_reuse"] = stage_owner.verify_board(board, matrix, self.args)
            assert row["stage_reuse"]["passed"], "Previously measured stage differs; no retraining"
            old_mask = int(self.command(board, "SYSTem:TDMA:LOAD:MASK?").strip('"'))
            row["original_load_mask"] = old_mask
            self.set_load_mask(board, old_mask | base.trn.APP_REALTIME_LOAD_VDC_MASK | base.trn.APP_REALTIME_LOAD_DPLL_MASK)
            row["original_diagnostic"] = int(self.command(board, "SYSTem:TDMA:RING:DIAGnostic?").strip('"'))
            self.control(board, "SYSTem:TDMA:FLIGHT:FIFO:RESet")
            assert not base.trn.validate_fifo_reset(base.trn.flight_snapshot(board, self.args))
            self.control(board, "SYSTem:TDMA:RING:DIAGnostic 1", 1)
            if slot == 0:
                row["original_burst"] = int(self.command(board, "SYSTem:TDMA:RING:BURSt?").strip('"'))
                self.control(board, "SYSTem:TDMA:RING:BURSt 0", 0)
            seed = 0x40 + slot * 0x10
            reply = base.fields(self.command(board, f"SYSTem:TDMA:FLIGHT:TX 32,{seed},{generation},{slot+1},1"))
            assert reply == ["OK", "32", str(sum((seed+i)&255 for i in range(32))),
                             str(seed), str(generation), str(slot+1), "1"]
        self.composite(master, SESSION, fresh)
        self.control(master, f"{SYNC} {fresh}", fresh)
        assert int(self.command(master, SYNC + "?").strip('"')) == fresh
        for board in reversed(self.boards):
            slot = base.IDS.index(board.address)
            row = self.report["boards"][board.address]
            row["arm"] = base.trn.arm_with_evidence(board, self.args, slot)
            row["ready_config"] = base.trn.wait_runtime_config_applied(board, self.args, slot)
        self.report["clock_hz"] = int(base.fields(self.command(master, "SYSTem:TDMA:SCHEDule?"))[1])
        self.report["model_before_start"] = self.command(master, MODEL)

    def acquire(self):
        self.block_queries = True
        self.barrier = False
        for board in reversed(self.boards):
            self.control(board, "SYSTem:TDMA:RING:START")
        time.sleep(0.2)
        epoch = int(time.time()) & 0xffffffff
        grant = int(self.command(self.boards[0], f"CALibration:ORIGin:TRIAL {epoch},8192,256,{self.report['clock_hz']*15}").strip('"'))
        assert grant > 0 and not grant & 1
        self.report["trial_epoch"] = grant
        time.sleep(self.opt.seconds)

    def collect(self):
        assert self.barrier and not self.block_queries
        master = self.boards[0]
        self.report["tx_raw"] = self.command(master, TX)
        self.report["model_after_stop"] = self.command(master, MODEL)
        tx = fields(self.report["tx_raw"])
        assert len(tx) == 27
        tx_status = dict(zip(TX_FIELDS, map(int, tx[:24])))
        tx_status["lower_ns"] = int(tx[24]) | int(tx[25]) << 32
        self.report["tx_status"] = tx_status
        self.save()
        # Preserve every follower record before assessing whether TX succeeded.
        for slot, board in enumerate(self.boards):
            row = self.report["boards"][board.address]
            for name, command in (("consumer_raw", CONS), ("priority_raw", "SYSTem:TDMA:FLIGHT:PRIORity?"),
                                  ("schedule_raw", "SYSTem:TDMA:SCHEDule?"), ("error_raw", "SYSTem:ERR?")):
                row[name] = self.command(board, command)
                self.save()
        fresh = self.report["binding_generation"]
        assert tx_status["schema"] == 1 and tx_status["generation"] == fresh
        assert tx_status["have_offer"] == 1 and tx_status["encoded"] > 0
        assert tx_status["session"] == self.report["feedback_session"]
        tx_event = typed_mailbox(tx[-1], fresh, 0)
        assert tx_event["event_sequence"] == tx_status["event_sequence"]
        assert tx_event["lower_ns"] == tx_status["lower_ns"] and tx_event["width_ns"] == tx_status["uncertainty_width"]
        self.report["tx_event"] = tx_event
        for slot, board in enumerate(self.boards[1:], 1):
            row = self.report["boards"][board.address]
            value = fields(row["consumer_raw"])
            assert len(value) == 28
            n = list(map(int, value[:26]))
            assert n[0] == 1 and n[3] == 1 and n[5] > 0 and n[12] == 0x14 and n[20] == n[2]
            header = bytes.fromhex(value[26])
            assert len(header) == 32 and struct.unpack_from("<I", header, 8)[0] == n[21]
            assert zlib.crc32(header[:14] + header[15:24]) == struct.unpack_from("<I", header, 24)[0]
            assert zlib.crc32(header[:28] + bytes(4)) == struct.unpack_from("<I", header, 28)[0]
            assert struct.unpack_from("<I", header, 16)[0] == tx_status["schedule_crc32"]
            assert struct.unpack_from("<I", header, 20)[0] == tx_status["profile_crc32"]
            event = typed_mailbox(value[27], fresh, slot)
            row["typed_event"] = event
            row["matches_retained_master_event"] = event["event_sequence"] == tx_event["event_sequence"]
            if row["matches_retained_master_event"]:
                assert event == tx_event
        self.report["flow_completed"] = True
        self.report["passed"] = not self.report["errors"]

    def run(self):
        original_stage = stage_owner.board_command
        try:
            self.receipt = json.loads(self.opt.receipt.read_text(encoding="utf8"))
            gate._validate_quick_diagnostic_receipt(ROOT, self.receipt)
            assert gate.working_source_fingerprint(ROOT) == (self.receipt["source_tree_sha256"], self.receipt["source_file_count"])
            assert self.receipt["tdma_board_ids"] == base.IDS
            self.args.expected_build = int(self.receipt["build_id"])
            if self.opt.check_only:
                self.report.update(check_only=True, passed=True)
                return
            ports = {}
            for port in base.comports():
                if port.serial_number in base.IDS:
                    assert port.serial_number not in ports
                    ports[port.serial_number] = port.device
            assert set(ports) == set(base.IDS)
            self.boards = [base.trn.Board(ports[uid], uid, "", self.receipt["build_id"]) for uid in base.IDS]
            self.report["boards"] = {uid: {} for uid in base.IDS}
            base.trn.board_command = self.command
            stage_owner.board_command = self.command
            for board in self.boards:
                connection = base.BACKEND.BoardConnection(board, self.args)
                base.BACKEND._PERSISTENT_CONNECTIONS[board.address] = connection
                connection.session = base.BACKEND.SerialSession(board.port, self.args.baud, self.args.timeout,
                    self.args.settle, read_timeout_s=self.args.read_timeout)
                connection.session.open()
            self.report["hardware_started"] = True
            self.stop_all("initial_stop")
            for slot, board in enumerate(self.boards):
                assert board.address in self.command(board, "*IDN?")
                assert self.command(board, "SYSTem:FW:BUILD?").strip('"') == board.build
                assert int(self.command(board, "SYSTem:BOARD:NO?")) == slot + 1
            self.configure()
            self.acquire()
        except Exception as exc:
            self.error("run", exc)
        finally:
            if self.report["hardware_started"]:
                try:
                    self.stop_all("final_stop", passes=2)
                    self.collect()
                except Exception as exc:
                    self.error("final_stop_or_collect", exc)
                if self.barrier:
                    for board in self.boards:
                        try:
                            self.control(board, SYNC + " 0", 0)
                            self.composite(board, SESSION, 0)
                            row = self.report["boards"][board.address]
                            if "original_load_mask" in row: self.set_load_mask(board, row["original_load_mask"])
                            if "original_diagnostic" in row: self.control(board, f"SYSTem:TDMA:RING:DIAGnostic {row['original_diagnostic']}", row["original_diagnostic"])
                            if "original_burst" in row: self.control(board, f"SYSTem:TDMA:RING:BURSt {row['original_burst']}", row["original_burst"])
                        except Exception as exc:
                            self.error("disable_restore:" + board.address, exc)
                    try: self.control(self.boards[0], "CALibration:ORIGin:REVOKe")
                    except Exception as exc: self.error("revoke", exc)
            base.trn.close_persistent_connections()
            base.trn.board_command = self.original_command
            stage_owner.board_command = original_stage
            self.report["passed"] = bool(self.report["passed"] and not self.report["errors"])
            self.report["conclusion_scope"] = "Typed class0x14 ns interval transported into follower Core1; offers are not DMA commits. Final master and follower may retain different events. No precision, one-lap deadline, local event match, ACK/control or lock claim."
            self.save()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--check-only", action="store_true")
    opt = parser.parse_args()
    if not 2 <= opt.seconds <= 8: parser.error("seconds must be 2..8")
    probe = TypedProbe(opt)
    probe.run()
    print(json.dumps(dict(passed=probe.report["passed"], errors=probe.report["errors"], out=str(opt.out))))
    raise SystemExit(0 if probe.report["passed"] else 2)
