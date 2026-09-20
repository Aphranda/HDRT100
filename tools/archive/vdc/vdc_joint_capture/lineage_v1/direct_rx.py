"""Short four-board direct typed RX probe; root reviews and executes hardware.

Reuses the typed-TX probe's receipt/build/UID checks, measured-matrix verification,
bounded trial, default silent 3-second run, all-board STOP barrier and restoration.
No serial is opened by --check-only. Output directory must be fresh.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys
import zlib

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
PARENT = HERE.parent / "dpll-priority-tx-r1/capture_typed_tx.py"
spec = importlib.util.spec_from_file_location("direct_rx_typed_parent", PARENT)
parent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parent)

RX = "SYSTem:VDC:PRIORity:RX?"
LANE = "SYSTem:TDMA:FLIGHT:PRIORity?"
TIMING = "SYSTem:TDMA:FLIGHT:PRIORity:TIMing?"
BUDGET = "SYSTem:TDMA:FLIGHT:PRIORity:BUDGet?"
RECORD = "SYSTem:TDMA:FLIGHT:PRIORity:RECord?"
RX_FIELDS = ("schema active epoch have_record carrier_count typed_accept_count typed_reject_count "
             "duplicate_count unique_count conflict_count last_status source_slot target_mask carrier_sequence "
             "irq_low irq_high binding_generation event_sequence lower_low lower_high width_ns flags").split()
LANE_FIELDS = ("schema active epoch irq_count publish_count overwrite_count duplicate_count "
               "sequence_gap_count reject_count last_reject latest_sequence latest_mailbox_seq16 "
               "irq_last_cycles irq_max_cycles total_low total_high entry_low entry_high "
               "retained_mask sequence0 sequence1 sequence2 sequence3").split()


def numbers(raw, size):
    value = parent.fields(raw)
    assert len(value) == size, (len(value), size, raw)
    out = [int(v) for v in value]
    assert all(0 <= v <= 0xffffffff for v in out), raw
    return out


def parse_direct(raw):
    d = dict(zip(RX_FIELDS, numbers(raw, 22)))
    d["irq_entry_ticks"] = d["irq_low"] | d["irq_high"] << 32
    d["lower_ns"] = d["lower_low"] | d["lower_high"] << 32
    return d


def parse_lane(raw):
    d = dict(zip(LANE_FIELDS, numbers(raw, 23)))
    d["irq_total_cycles"] = d["total_low"] | d["total_high"] << 32
    d["irq_average_cycles"] = d["irq_total_cycles"] / d["irq_count"] if d["irq_count"] else None
    return d


def parse_timing(raw):
    n = numbers(raw, 6)
    assert n[0] == 1
    return dict(schema=n[0], samples=n[1], segment_max_cycles=n[2:])


def parse_budget(raw):
    n = [int(v) for v in parent.fields(raw)]
    assert len(n) >= 9 and (len(n) - 6) % 3 == 0 and n[0] == 1
    assert all(0 <= v <= 0xffffffff for v in n)
    return dict(schema=n[0], candidate_irq_cycles=n[1], close_lead_cycles=n[2],
                physical_min_cycles=n[3], sample_failures=n[4], close_misses=n[5],
                phases=[dict(irq_max_cycles=n[i], background_max_cycles=n[i+1],
                             budget_misses=n[i+2]) for i in range(6, len(n), 3)])


def typed_subset(event):
    return {k: event[k] for k in ("binding_generation", "event_sequence", "lower_ns", "width_ns", "flags")}


def compare_events(a, b):
    same = (a["binding_generation"], a["event_sequence"]) == (b["binding_generation"], b["event_sequence"])
    return dict(same_identity=same, payload_equal=typed_subset(a) == typed_subset(b),
                disposition="same_identity_check_payload" if same else "different_retained_events_not_corruption")


def validate_header(raw, sequence, tx):
    h = bytes.fromhex(raw)
    assert len(h) == 32 and struct.unpack_from("<I", h, 8)[0] == sequence
    assert zlib.crc32(h[:14] + h[15:24]) == struct.unpack_from("<I", h, 24)[0]
    assert zlib.crc32(h[:28] + bytes(4)) == struct.unpack_from("<I", h, 28)[0]
    assert struct.unpack_from("<I", h, 16)[0] == tx["schedule_crc32"]
    assert struct.unpack_from("<I", h, 20)[0] == tx["profile_crc32"]


class DirectProbe(parent.TypedProbe):
    def __init__(self, opt):
        super().__init__(opt)
        self.report["schema"] = "DIRECT_PRIORITY_RX_SHORT_PROBE_V1"
        self.report["tool_sources"].append(dict(path=str(Path(__file__)), sha256=parent.base.sha(Path(__file__))))
        self.baseline = json.loads(opt.baseline.read_text(encoding="utf-8")) if opt.baseline else []
        if opt.baseline:
            self.report["baseline"] = dict(path=str(opt.baseline), sha256=parent.base.sha(opt.baseline))
        self.report["plan"].update(required=["three_follower_direct_delivery", "same_epoch_carrier_count",
            "typed_accept_positive", "zero_typed_reject_and_conflict", "raw_record_and_event_consistency"],
            timing_quality_is_observation=True, dco_control=False, polled_consumer_is_comparison=True)

    def timing_comparison(self, uid, row):
        previous = {r["command"].upper(): r["raw"] for r in self.baseline if r["uid"] == uid}
        result = dict(note="Per-measurement maxima may come from different IRQs; never summed. "
            "IRQ body excludes entry/exit and is not a WCET or one-lap completion proof. "
            "Cycle comparisons alone do not establish equal clock frequencies or workloads.")
        if LANE.upper() in previous:
            old = parse_lane(previous[LANE.upper()])
            result["irq_body"] = dict(before=old, after=row["lane"],
                max_delta_cycles=row["lane"]["irq_max_cycles"]-old["irq_max_cycles"])
        if TIMING.upper() in previous:
            old = parse_timing(previous[TIMING.upper()])
            result["segments"] = dict(before=old, after=row["timing"],
                individual_max_delta_cycles=[a-b for a,b in zip(row["timing"]["segment_max_cycles"], old["segment_max_cycles"])])
        result["baseline_build_raw"] = previous.get("SYSTEM:FW:BUILD?")
        row["timing_comparison"] = result

    def collect(self):
        assert self.barrier and not self.block_queries
        # Persist all STOP snapshots before any assertion can abort assessment.
        self.report["tx_raw"] = self.command(self.boards[0], parent.TX)
        self.report["model_after_stop"] = self.command(self.boards[0], parent.MODEL)
        for board in self.boards:
            row = self.report["boards"][board.address]
            for key, command in (("direct_raw", RX), ("consumer_raw", parent.CONS),
                    ("priority_raw", LANE), ("timing_raw", TIMING), ("budget_raw", BUDGET),
                    ("schedule_raw", "SYSTem:TDMA:SCHEDule?"), ("error_raw", "SYSTem:ERR?")):
                try:
                    row[key] = self.command(board, command)
                except Exception as exc:
                    self.error("collect:"+board.address+":"+key, exc)
                self.save()
        tx = parent.fields(self.report["tx_raw"])
        assert len(tx) == 27
        status = dict(zip(parent.TX_FIELDS, map(int, tx[:24])))
        status["lower_ns"] = int(tx[24]) | int(tx[25]) << 32
        fresh = self.report["binding_generation"]
        assert status["schema"] == 1 and status["generation"] == fresh
        assert status["have_offer"] == 1 and status["encoded"] > 0
        assert status["session"] == self.report["feedback_session"]
        event = parent.typed_mailbox(tx[-1], fresh, 0)
        assert event["event_sequence"] == status["event_sequence"]
        assert event["lower_ns"] == status["lower_ns"] and event["width_ns"] == status["uncertainty_width"]
        self.report.update(tx_status=status, tx_event=event)
        for slot, board in enumerate(self.boards):
            row = self.report["boards"][board.address]
            try:
                row["direct"] = parse_direct(row["direct_raw"])
                row["lane"] = parse_lane(row["priority_raw"])
                row["timing"] = parse_timing(row["timing_raw"])
                row["budget"] = parse_budget(row["budget_raw"])
                self.timing_comparison(board.address, row)
                assert row["direct"]["active"] == 0 and row["lane"]["active"] == 0
                if slot:
                    self.assess_follower(board, slot, row, status, event)
            except Exception as exc:
                self.error("assessment:"+board.address, exc)
            self.save()
        self.report["flow_completed"] = True
        self.report["passed"] = not self.report["errors"]

    def assess_follower(self, board, slot, row, tx, master_event):
        d, lane = row["direct"], row["lane"]
        assert d["schema"] == lane["schema"] == 1
        assert d["epoch"] == lane["epoch"] and d["epoch"] > 0
        assert d["carrier_count"] == lane["publish_count"] > 0
        assert d["have_record"] == 1 and d["typed_accept_count"] > 0
        assert d["typed_reject_count"] == d["conflict_count"] == 0
        assert d["typed_accept_count"] == d["unique_count"] + d["duplicate_count"]
        assert d["carrier_count"] >= d["typed_accept_count"] and d["unique_count"] > 0
        assert d["source_slot"] == 0 and d["target_mask"] == 0x0e
        assert d["binding_generation"] == self.report["binding_generation"]
        assert d["width_ns"] > 0 and d["flags"] == 0
        row["delivery_counts"] = dict(accepted_carriers=d["carrier_count"],
            ordinary_or_non_typed_carriers=d["carrier_count"]-d["typed_accept_count"],
            typed_carriers=d["typed_accept_count"], adjacent_unique_source_events=d["unique_count"],
            repeated_source_events=d["duplicate_count"],
            note="Ordinary startup traffic is not a typed reject. Delivery is not DCO application.")
        # Exact retained record is independent of the slower polled consumer.
        row["direct_record_raw"] = self.command(board, f"{RECORD} {d['epoch']},{d['carrier_sequence']}")
        self.save()
        r = parent.fields(row["direct_record_raw"])
        assert len(r) == 8
        assert int(r[0]) == d["epoch"] and int(r[1]) == d["carrier_sequence"]
        assert int(r[2]) | int(r[3]) << 32 == d["irq_entry_ticks"]
        validate_header(r[6], d["carrier_sequence"], tx)
        raw_event = parent.typed_mailbox(r[7], self.report["binding_generation"], slot)
        row["direct_record_event"] = raw_event
        assert typed_subset(d) == typed_subset(raw_event)
        row["master_comparison"] = compare_events(d, master_event)
        if row["master_comparison"]["same_identity"]:
            assert row["master_comparison"]["payload_equal"]
        values = parent.fields(row["consumer_raw"])
        assert len(values) == 28
        n = list(map(int, values[:26]))
        row["polled_summary"] = dict(epoch=n[2], have_record=n[3], copied_count=n[5],
            skipped_carriers=n[6], last_class=n[12], carrier_sequence=n[21])
        if n[3] and n[12] == 0x14:
            validate_header(values[26], n[21], tx)
            polled = parent.typed_mailbox(values[27], self.report["binding_generation"], slot)
            row["polled_event"] = polled
            row["polled_comparison"] = compare_events(d, polled)
            row["polled_comparison"]["same_lane_epoch"] = n[2] == d["epoch"]
            if n[2] == d["epoch"] and row["polled_comparison"]["same_identity"]:
                assert row["polled_comparison"]["payload_equal"]
        else:
            row["polled_comparison"] = dict(disposition="no_typed_polled_record_direct_evidence_retained")

    def run(self):
        super().run()
        self.report["conclusion_scope"] = (
            "Three-follower Core1 IRQ typed delivery and retained-byte consistency only. "
            "Carrier, adjacent unique source event, repeated event and polled-copy counts are distinct. "
            "Different final offers/consumers may retain different events. No DCO update, ACK, lock, "
            "100ns precision, physical edge, WCET or one-lap completion qualification.")
        self.save()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--baseline", type=Path, default=HERE / "stopped-baseline.json")
    parser.add_argument("--check-only", action="store_true")
    opt = parser.parse_args()
    if not 2 <= opt.seconds <= 8:
        parser.error("seconds must be 2..8")
    probe = DirectProbe(opt)
    probe.run()
    print(json.dumps(dict(passed=probe.report["passed"], errors=probe.report["errors"], out=str(opt.out))))
    raise SystemExit(0 if probe.report["passed"] else 2)
