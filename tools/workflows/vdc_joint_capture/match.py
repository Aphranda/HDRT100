"""Tracked exact-match layer over the remaining direct-RX ancestry."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from types import SimpleNamespace

from tools.workflows.vdc_joint_capture.direct import load_direct


MATCH = "SYSTem:VDC:PRIORity:MATCH"
STATUS = MATCH + ":STATus?"
SCALARS = (
    "schema generation active retired have_match calls matched repeated superseded busy "
    "pending history_miss rejected last_reason history_result source_slot event_sequence "
    "carrier_sequence rx_epoch session role_generation clock_epoch clock_run model_token "
    "applied_command_seq ring_config_seq ring_applied_seq local_slot reference_slot "
    "node_count schedule_crc32 profile_crc32 observer_epoch tick_hz path_crc32 delay_ns "
    "path_direction path_qualification"
).split()
WIDE = (
    "arm_epoch valid_from_raw raw_lo raw_hi local_lo local_hi remote_lo remote_hi "
    "expected_lo expected_hi residual_lo residual_hi"
).split()
# Appended page trailer. Frozen lineage-v1 captures predate it, so a shorter
# reply is the legacy layout, not a malformed page.
TRAILER = ("first_reject_reason",)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_match(runtime_entrypoint: Path, *, runtime_module=None):
    """Build the tracked exact-match layer over an admitted DirectProbe module."""
    direct = (runtime_module if runtime_module is not None else
              load_direct(runtime_entrypoint))
    if not hasattr(direct, "DirectProbe") or not hasattr(direct, "parent"):
        raise ValueError("Historical direct-RX entrypoint lacks DirectProbe/parent")
    parent = direct.parent
    source = Path(__file__).resolve()
    source_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()

    def parse_match(raw):
        size = len(SCALARS) + 2 * len(WIDE) + len(TRAILER)
        try:
            numbers = direct.numbers(raw, size)
        except Exception as exc:
            if "Field count" not in str(exc):
                raise
            numbers = direct.numbers(raw, size - len(TRAILER))
            numbers = numbers + [0] * len(TRAILER)
        result = dict(zip(SCALARS, numbers))
        for index, name in enumerate(WIDE):
            position = len(SCALARS) + 2 * index
            value = numbers[position] | numbers[position + 1] << 32
            if name.startswith("residual") and value & (1 << 63):
                value -= 1 << 64
            result[name] = value
        for index, name in enumerate(TRAILER):
            result[name] = numbers[len(SCALARS) + 2 * len(WIDE) + index]
        return result

    def forward_delay(links, source_node, target_node):
        edges = {link["marker_source_node"]: link for link in links}
        require(len(edges) == len(links), "Duplicate marker source node")
        delay, node, visited = 0, source_node, set()
        while node != target_node:
            require(node not in visited and node in edges,
                    "Directed path is cyclic or incomplete")
            visited.add(node)
            edge = edges[node]
            require(edge["valid"] == 1 and edge["link_base_delay_ns"] >= 0,
                    "Directed path link is invalid")
            delay += edge["link_base_delay_ns"] * 2
            node = edge["marker_destination_node"]
        return delay

    class MatchProbe(direct.DirectProbe):
        def __init__(self, options):
            super().__init__(options)
            self.report["schema"] = "PRIORITY_EXACT_MATCH_SHORT_PROBE_V1"
            self.report["tool_sources"].append({
                "path": str(source), "sha256": source_sha256})
            self.report["plan"].update(
                exact_source_match=True,
                dco_control=False,
                delay_qualification=(
                    "provisional forward-CS transpose of installed reverse-DATA matrix"),
            )
            parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[MATCH.upper()] = 1
            parent.scpi_serial.SCALAR_U32_QUERY_HEADERS.add(MATCH.upper() + "?")
            self.tap_baseline = None
            self.tap_supported = None
            if options.tap_baseline:
                self.tap_baseline = json.loads(
                    options.tap_baseline.read_text(encoding="utf-8"))
                require(self.tap_baseline["passed"] is True,
                        "TAP baseline is not passing")
                self.report["tap_baseline"] = {
                    "path": str(options.tap_baseline),
                    "sha256": parent.base.sha(options.tap_baseline),
                }

        def control(self, board, text, expected="OK"):
            result = super().control(board, text, expected)
            if text.upper().startswith(parent.SYNC.upper() + " "):
                generation = int(text.split()[-1])
                if generation == 0:
                    super().control(board, MATCH + " 0", 0)
                    row = self.report["boards"][board.address]
                    if row.get("tap_installed"):
                        original = row["original_tap"][:3]
                        super().control(
                            board, "SYSTem:TDMA:EVENt:TAP " +
                            ",".join(map(str, original)))
                        row["restored_tap"] = direct.numbers(
                            self.command(board, "SYSTem:TDMA:EVENt:TAP?"), 12)
                        require(row["restored_tap"][:3] == original,
                                "TAP restore readback mismatch")
                        row["tap_installed"] = False
                        self.save()
                elif board.address == self.boards[0].address:
                    for follower in self.boards[1:]:
                        if self.tap_baseline and self.tap_supported is not False:
                            row = self.report["boards"][follower.address]
                            try:
                                original = direct.numbers(
                                    self.command(follower, "SYSTem:TDMA:EVENt:TAP?"), 12)
                            except RuntimeError as exc:
                                # The paired event observer is diagnostic-only
                                # and may be compiled out of the production
                                # follower persona. Its TAP query then returns
                                # a scalar/unsupported response. Preserve that
                                # fact and continue the core MATCH/DPLL path;
                                # a TAP baseline is optional instrumentation.
                                if "Field count" not in str(exc):
                                    raise
                                self.tap_supported = False
                                row["tap_supported"] = False
                                row["tap_skip_reason"] = str(exc)
                                self.report["tap_baseline_skipped"] = (
                                    "event observer/TAP unsupported by this firmware")
                                self.save()
                            else:
                                self.tap_supported = True
                                row["original_tap"] = original
                                request = self.tap_baseline["boards"][
                                    follower.address]["tap_request"]
                                require(len(request) == 3 and request[0] == 1,
                                        "TAP request is invalid")
                                row["tap_installed"] = True
                                row["tap_request"] = request
                                self.save()
                                super().control(
                                    follower, "SYSTem:TDMA:EVENt:TAP " +
                                    ",".join(map(str, request)))
                        self.composite(follower, parent.SESSION, generation)
                        super().control(
                            follower, f"{MATCH} {generation}", generation)
                        require(int(self.command(
                            follower, MATCH + "?").strip('"')) == generation,
                            "MATCH enable readback mismatch")
            return result

        def collect(self):
            require(self.barrier and not self.block_queries,
                    "Match collection requires all-board STOP")
            for key, command in (
                    ("origin_handoff_raw", "READ:CALibration:ORIGin:HANDoff?"),
                    ("origin_release_raw", "READ:CALibration:ORIGin:RELease?"),
                    ("origin_runtime_raw", "READ:CALibration:ORIGin:RUNTIme?"),
                    ("origin_live_raw", "READ:CALibration:ORIGin:LIVE?"),
                    ("origin_admission_raw", "READ:CALibration:ORIGin:DIAGnostic?")):
                self.report[key] = self.command(self.boards[0], command)
                self.save()
            for board in self.boards:
                row = self.report["boards"][board.address]
                for key, command in (
                        ("match_raw", STATUS),
                        ("local_model_raw", parent.MODEL),
                        ("event_live_raw", "SYSTem:TDMA:EVENt:LIVE?"),
                        ("tap_raw", "SYSTem:TDMA:EVENt:TAP?"),
                        ("recovery_raw", "SYSTem:TDMA:EVENt:RECovery?")):
                    try:
                        row[key] = self.command(board, command)
                    except Exception as exc:
                        self.error("collect:" + board.address + ":" + key, exc)
                    self.save()
            try:
                super().collect()
            except Exception as exc:
                self.error("transport-assessment", exc)
            for slot, board in enumerate(self.boards[1:], 1):
                row = self.report["boards"][board.address]
                try:
                    match = row["match"] = parse_match(row["match_raw"])
                    if row.get("tap_installed"):
                        tap = direct.numbers(row["tap_raw"], 12)
                        require(tap[:3] == tap[4:7] == row["tap_request"],
                                "TAP active/request fields mismatch")
                        require(tap[3] == tap[7] and tap[8:10] == [1, 1],
                                "TAP generation/enabled fields mismatch")
                        require(tap[10:] == row["tap_request"][1:],
                                "TAP parameter readback mismatch")
                    require(match["schema"] == 1 and
                            match["generation"] == self.report["binding_generation"],
                            "MATCH schema/generation mismatch")
                    require(match["have_match"] == 1 and match["matched"] > 0,
                            "MATCH has no accepted observation")
                    require(match["session"] == self.report["feedback_session"],
                            "MATCH feedback session mismatch")
                    require(match["source_slot"] == match["reference_slot"] == 0 and
                            match["local_slot"] == slot,
                            "MATCH slot identity mismatch")
                    require(match["node_count"] == 4 and match["arm_epoch"] > 0 and
                            match["observer_epoch"] > 0,
                            "MATCH ring/epoch identity mismatch")
                    require(match["rx_epoch"] == row["direct"]["epoch"],
                            "MATCH RX epoch mismatch")
                    require(match["schedule_crc32"] ==
                            self.report["tx_status"]["schedule_crc32"],
                            "MATCH schedule CRC mismatch")
                    require(match["profile_crc32"] ==
                            self.report["tx_status"]["profile_crc32"],
                            "MATCH profile CRC mismatch")
                    require(match["path_qualification"] == 1 and
                            match["delay_ns"] > 0 and match["path_direction"] == 0,
                            "MATCH path qualification/direction mismatch")
                    row["independent_forward_delay_ns"] = forward_delay(
                        row["stage_reuse"]["links"], 0, slot)
                    require(match["delay_ns"] == row["independent_forward_delay_ns"],
                            "MATCH delay differs from directed path")
                    require(match["valid_from_raw"] <= match["raw_lo"] <=
                            match["raw_hi"], "MATCH raw validity interval mismatch")
                    require(match["local_lo"] <= match["local_hi"] and
                            match["remote_lo"] < match["remote_hi"],
                            "MATCH local/remote interval is invalid")
                    require(match["expected_lo"] ==
                            match["remote_lo"] + match["delay_ns"] and
                            match["expected_hi"] ==
                            match["remote_hi"] + match["delay_ns"],
                            "MATCH expected interval projection mismatch")
                    require(match["residual_lo"] ==
                            match["local_lo"] - match["expected_hi"] and
                            match["residual_hi"] ==
                            match["local_hi"] - match["expected_lo"],
                            "MATCH residual interval mismatch")
                    row["match_vs_last_rx"] = {
                        "same_event": (match["event_sequence"] ==
                                       row["direct"]["event_sequence"]),
                        "note": (
                            "Last successfully matched source event can precede latest "
                            "retained RX."),
                    }
                    if row["match_vs_last_rx"]["same_event"]:
                        require(match["remote_lo"] == row["direct"]["lower_ns"] and
                                match["remote_hi"] - match["remote_lo"] ==
                                row["direct"]["width_ns"],
                                "MATCH remote interval differs from last RX")
                    row["matched_observation_passed"] = True
                except Exception as exc:
                    self.error("match-assessment:" + board.address, exc)
                self.save()
            self.report["passed"] = not self.report["errors"]

        def run(self):
            super().run()
            self.report["conclusion_scope"] = (
                "Core1 exact source-event lookup and projected residual with provisional "
                "directed delay. No DCO/ACK, physical lock, 100 ns accuracy, WCET or "
                "same-lap deadline claim.")
            self.save()

    return SimpleNamespace(
        __file__=str(source),
        MatchProbe=MatchProbe,
        direct=direct,
        parent=parent,
        MATCH=MATCH,
        STATUS=STATUS,
        SCALARS=SCALARS,
        WIDE=WIDE,
        TRAILER=TRAILER,
        parse_match=parse_match,
        forward_delay=forward_delay,
        source_path=source,
        source_sha256=source_sha256,
    )
