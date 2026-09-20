"""Tracked direct-RX layer over the remaining typed-TX ancestry."""

from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path
import struct
from types import SimpleNamespace
import zlib

from tools.workflows.vdc_joint_capture.typed import load_typed


RX = "SYSTem:VDC:PRIORity:RX?"
LANE = "SYSTem:TDMA:FLIGHT:PRIORity?"
TIMING = "SYSTem:TDMA:FLIGHT:PRIORity:TIMing?"
BUDGET = "SYSTem:TDMA:FLIGHT:PRIORity:BUDGet?"
RECORD = "SYSTem:TDMA:FLIGHT:PRIORity:RECord?"
RX_FIELDS = (
    "schema active epoch have_record carrier_count typed_accept_count typed_reject_count "
    "duplicate_count unique_count conflict_count last_status source_slot target_mask "
    "carrier_sequence irq_low irq_high binding_generation event_sequence lower_low "
    "lower_high width_ns flags"
).split()
LANE_FIELDS = (
    "schema active epoch irq_count publish_count overwrite_count duplicate_count "
    "sequence_gap_count reject_count last_reject latest_sequence latest_mailbox_seq16 "
    "irq_last_cycles irq_max_cycles total_low total_high entry_low entry_high "
    "retained_mask sequence0 sequence1 sequence2 sequence3"
).split()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_direct(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked direct-RX layer over an admitted TypedProbe module."""
    typed = (runtime_module if runtime_module is not None else
             load_typed(runtime_entrypoint))
    if not hasattr(typed, "TypedProbe") or not hasattr(typed, "base"):
        raise ValueError("Historical typed-TX entrypoint lacks TypedProbe/base")
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()

    def fields(raw):
        parser = getattr(typed, "fields", None)
        return parser(raw) if parser is not None else next(csv.reader([raw]))

    def numbers(raw, size):
        values = fields(raw)
        require(len(values) == size,
                f"Field count {len(values)} differs from expected {size}")
        output = [int(value) for value in values]
        require(all(0 <= value <= 0xFFFFFFFF for value in output),
                "Field is outside uint32 range")
        return output

    def parse_direct(raw):
        output = dict(zip(RX_FIELDS, numbers(raw, len(RX_FIELDS))))
        output["irq_entry_ticks"] = output["irq_low"] | output["irq_high"] << 32
        output["lower_ns"] = output["lower_low"] | output["lower_high"] << 32
        return output

    def parse_lane(raw):
        output = dict(zip(LANE_FIELDS, numbers(raw, len(LANE_FIELDS))))
        output["irq_total_cycles"] = output["total_low"] | output["total_high"] << 32
        output["irq_average_cycles"] = (
            output["irq_total_cycles"] / output["irq_count"]
            if output["irq_count"] else None)
        return output

    def parse_timing(raw):
        values = numbers(raw, 6)
        require(values[0] == 1, "Timing schema mismatch")
        return {"schema": values[0], "samples": values[1],
                "segment_max_cycles": values[2:]}

    def parse_budget(raw):
        values = [int(value) for value in fields(raw)]
        require(len(values) >= 9 and (len(values) - 6) % 3 == 0 and
                values[0] == 1, "Budget shape/schema mismatch")
        require(all(0 <= value <= 0xFFFFFFFF for value in values),
                "Budget field is outside uint32 range")
        return {
            "schema": values[0], "candidate_irq_cycles": values[1],
            "close_lead_cycles": values[2], "physical_min_cycles": values[3],
            "sample_failures": values[4], "close_misses": values[5],
            "phases": [
                {"irq_max_cycles": values[index],
                 "background_max_cycles": values[index + 1],
                 "budget_misses": values[index + 2]}
                for index in range(6, len(values), 3)],
        }

    def typed_subset(event):
        return {key: event[key] for key in (
            "binding_generation", "event_sequence", "lower_ns", "width_ns", "flags")}

    def compare_events(first, second):
        same = (first["binding_generation"], first["event_sequence"]) == (
            second["binding_generation"], second["event_sequence"])
        return {
            "same_identity": same,
            "payload_equal": typed_subset(first) == typed_subset(second),
            "disposition": ("same_identity_check_payload" if same else
                            "different_retained_events_not_corruption"),
        }

    def validate_header(raw, sequence, tx):
        header = bytes.fromhex(raw)
        require(len(header) == 32 and
                struct.unpack_from("<I", header, 8)[0] == sequence,
                "Retained header size/sequence mismatch")
        require(zlib.crc32(header[:14] + header[15:24]) ==
                struct.unpack_from("<I", header, 24)[0],
                "Retained transport header CRC mismatch")
        require(zlib.crc32(header[:28] + bytes(4)) ==
                struct.unpack_from("<I", header, 28)[0],
                "Retained packet CRC mismatch")
        require(struct.unpack_from("<I", header, 16)[0] == tx["schedule_crc32"],
                "Retained schedule CRC mismatch")
        require(struct.unpack_from("<I", header, 20)[0] == tx["profile_crc32"],
                "Retained profile CRC mismatch")

    class DirectProbe(typed.TypedProbe):
        def __init__(self, options):
            super().__init__(options)
            self.report["schema"] = "DIRECT_PRIORITY_RX_SHORT_PROBE_V1"
            self.report["tool_sources"].append({
                "path": str(source), "sha256": source_sha256})
            self.baseline = (json.loads(options.baseline.read_text(encoding="utf-8"))
                             if options.baseline else [])
            if options.baseline:
                self.report["baseline"] = {
                    "path": str(options.baseline),
                    "sha256": typed.base.sha(options.baseline),
                }
            self.report["plan"].update(
                required=[
                    "three_follower_direct_delivery", "same_epoch_carrier_count",
                    "typed_accept_positive", "zero_typed_reject_and_conflict",
                    "raw_record_and_event_consistency",
                ],
                timing_quality_is_observation=True,
                dco_control=False,
                polled_consumer_is_comparison=True,
            )

        def timing_comparison(self, uid, row):
            previous = {record["command"].upper(): record["raw"]
                        for record in self.baseline if record["uid"] == uid}
            result = {
                "note": (
                    "Per-measurement maxima may come from different IRQs; never summed. "
                    "IRQ body excludes entry/exit and is not a WCET or one-lap completion "
                    "proof. Cycle comparisons alone do not establish equal clock frequencies "
                    "or workloads.")}
            if LANE.upper() in previous:
                old = parse_lane(previous[LANE.upper()])
                result["irq_body"] = {
                    "before": old, "after": row["lane"],
                    "max_delta_cycles": row["lane"]["irq_max_cycles"] -
                    old["irq_max_cycles"],
                }
            if TIMING.upper() in previous:
                old = parse_timing(previous[TIMING.upper()])
                result["segments"] = {
                    "before": old, "after": row["timing"],
                    "individual_max_delta_cycles": [
                        after - before for after, before in zip(
                            row["timing"]["segment_max_cycles"],
                            old["segment_max_cycles"])],
                }
            result["baseline_build_raw"] = previous.get("SYSTEM:FW:BUILD?")
            row["timing_comparison"] = result

        def collect(self):
            require(self.barrier and not self.block_queries,
                    "Direct collection requires all-board STOP")
            self.report["tx_raw"] = self.command(self.boards[0], typed.TX)
            self.report["model_after_stop"] = self.command(self.boards[0], typed.MODEL)
            for board in self.boards:
                row = self.report["boards"][board.address]
                for key, command in (
                        ("direct_raw", RX), ("consumer_raw", typed.CONS),
                        ("priority_raw", LANE), ("timing_raw", TIMING),
                        ("budget_raw", BUDGET),
                        ("schedule_raw", "SYSTem:TDMA:SCHEDule?"),
                        ("error_raw", "SYSTem:ERR?")):
                    try:
                        row[key] = self.command(board, command)
                    except Exception as exc:
                        self.error("collect:" + board.address + ":" + key, exc)
                    self.save()
            tx_values = fields(self.report["tx_raw"])
            require(len(tx_values) == 27, "TX status field count mismatch")
            status = dict(zip(typed.TX_FIELDS, map(int, tx_values[:24])))
            status["lower_ns"] = int(tx_values[24]) | int(tx_values[25]) << 32
            fresh = self.report["binding_generation"]
            require(status["schema"] == 1 and status["generation"] == fresh,
                    "TX schema/generation mismatch")
            require(status["have_offer"] == 1 and status["encoded"] > 0,
                    "TX has no encoded offer")
            require(status["session"] == self.report["feedback_session"],
                    "TX feedback session mismatch")
            event = typed.typed_mailbox(tx_values[-1], fresh, 0)
            require(event["event_sequence"] == status["event_sequence"],
                    "TX event sequence mismatch")
            require(event["lower_ns"] == status["lower_ns"] and
                    event["width_ns"] == status["uncertainty_width"],
                    "TX event interval mismatch")
            self.report.update(tx_status=status, tx_event=event)
            for slot, board in enumerate(self.boards):
                row = self.report["boards"][board.address]
                try:
                    row["direct"] = parse_direct(row["direct_raw"])
                    row["lane"] = parse_lane(row["priority_raw"])
                    row["timing"] = parse_timing(row["timing_raw"])
                    row["budget"] = parse_budget(row["budget_raw"])
                    self.timing_comparison(board.address, row)
                    require(row["direct"]["active"] == 0 and
                            row["lane"]["active"] == 0,
                            "Direct/lane status is active after STOP")
                    if slot:
                        self.assess_follower(board, slot, row, status, event)
                except Exception as exc:
                    self.error("assessment:" + board.address, exc)
                self.save()
            self.report["flow_completed"] = True
            self.report["passed"] = not self.report["errors"]

        def assess_follower(self, board, slot, row, tx_status, master_event):
            direct, lane = row["direct"], row["lane"]
            require(direct["schema"] == lane["schema"] == 1,
                    "Direct/lane schema mismatch")
            require(direct["epoch"] == lane["epoch"] and direct["epoch"] > 0,
                    "Direct/lane epoch mismatch")
            require(direct["carrier_count"] == lane["publish_count"] > 0,
                    "Direct/lane carrier count mismatch")
            require(direct["have_record"] == 1 and
                    direct["typed_accept_count"] > 0,
                    "Direct path has no accepted typed record")
            require(direct["typed_reject_count"] == direct["conflict_count"] == 0,
                    "Direct path rejected/conflicted typed traffic")
            require(direct["typed_accept_count"] ==
                    direct["unique_count"] + direct["duplicate_count"],
                    "Direct typed count decomposition mismatch")
            require(direct["carrier_count"] >= direct["typed_accept_count"] and
                    direct["unique_count"] > 0,
                    "Direct carrier/unique count mismatch")
            require(direct["source_slot"] == 0 and direct["target_mask"] == 0x0E,
                    "Direct source/target identity mismatch")
            require(direct["binding_generation"] ==
                    self.report["binding_generation"],
                    "Direct binding generation mismatch")
            require(direct["width_ns"] > 0 and direct["flags"] == 0,
                    "Direct interval/flags mismatch")
            row["delivery_counts"] = {
                "accepted_carriers": direct["carrier_count"],
                "ordinary_or_non_typed_carriers": (
                    direct["carrier_count"] - direct["typed_accept_count"]),
                "typed_carriers": direct["typed_accept_count"],
                "adjacent_unique_source_events": direct["unique_count"],
                "repeated_source_events": direct["duplicate_count"],
                "note": "Ordinary startup traffic is not a typed reject. Delivery is not DCO application.",
            }
            row["direct_record_raw"] = self.command(
                board, f"{RECORD} {direct['epoch']},{direct['carrier_sequence']}")
            self.save()
            record = fields(row["direct_record_raw"])
            require(len(record) == 8, "Direct retained record field count mismatch")
            require(int(record[0]) == direct["epoch"] and
                    int(record[1]) == direct["carrier_sequence"],
                    "Direct retained record identity mismatch")
            require(int(record[2]) | int(record[3]) << 32 ==
                    direct["irq_entry_ticks"],
                    "Direct retained IRQ timestamp mismatch")
            validate_header(record[6], direct["carrier_sequence"], tx_status)
            raw_event = typed.typed_mailbox(
                record[7], self.report["binding_generation"], slot)
            row["direct_record_event"] = raw_event
            require(typed_subset(direct) == typed_subset(raw_event),
                    "Direct retained event payload mismatch")
            row["master_comparison"] = compare_events(direct, master_event)
            if row["master_comparison"]["same_identity"]:
                require(row["master_comparison"]["payload_equal"],
                        "Direct event differs from same-identity master event")
            consumer = fields(row["consumer_raw"])
            require(len(consumer) == 28, "Polled consumer field count mismatch")
            values = list(map(int, consumer[:26]))
            row["polled_summary"] = {
                "epoch": values[2], "have_record": values[3],
                "copied_count": values[5], "skipped_carriers": values[6],
                "last_class": values[12], "carrier_sequence": values[21],
            }
            if values[3] and values[12] == 0x14:
                validate_header(consumer[26], values[21], tx_status)
                polled = typed.typed_mailbox(
                    consumer[27], self.report["binding_generation"], slot)
                row["polled_event"] = polled
                row["polled_comparison"] = compare_events(direct, polled)
                row["polled_comparison"]["same_lane_epoch"] = (
                    values[2] == direct["epoch"])
                if values[2] == direct["epoch"] and \
                        row["polled_comparison"]["same_identity"]:
                    require(row["polled_comparison"]["payload_equal"],
                            "Polled event differs from same-identity direct event")
            else:
                row["polled_comparison"] = {
                    "disposition": "no_typed_polled_record_direct_evidence_retained"}

        def run(self):
            super().run()
            self.report["conclusion_scope"] = (
                "Three-follower Core1 IRQ typed delivery and retained-byte consistency "
                "only. Carrier, adjacent unique source event, repeated event and "
                "polled-copy counts are distinct. Different final offers/consumers may "
                "retain different events. No DCO update, ACK, lock, 100ns precision, "
                "physical edge, WCET or one-lap completion qualification.")
            self.save()

    return SimpleNamespace(
        __file__=str(source),
        DirectProbe=DirectProbe,
        parent=typed,
        typed=typed,
        numbers=numbers,
        RX=RX,
        LANE=LANE,
        TIMING=TIMING,
        BUDGET=BUDGET,
        RECORD=RECORD,
        RX_FIELDS=RX_FIELDS,
        LANE_FIELDS=LANE_FIELDS,
        parse_direct=parse_direct,
        parse_lane=parse_lane,
        parse_timing=parse_timing,
        parse_budget=parse_budget,
        typed_subset=typed_subset,
        compare_events=compare_events,
        validate_header=validate_header,
        source_path=source,
        source_sha256=source_sha256,
    )
