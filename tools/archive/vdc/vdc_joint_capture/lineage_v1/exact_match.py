"""Four-board Core1 exact match probe. Native Python is used for Windows USB serial.
Inherits verified receipt/UID/build/matrix checks, silent finite run and STOP cleanup.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("match_direct_parent",
    HERE.parent / "dpll-priority-rx-direct-r1/capture_direct_rx.py")
direct = importlib.util.module_from_spec(spec)
spec.loader.exec_module(direct)
parent = direct.parent
MATCH = "SYSTem:VDC:PRIORity:MATCH"
STATUS = MATCH + ":STATus?"
SCALARS = "schema generation active retired have_match calls matched repeated superseded busy pending history_miss rejected last_reason history_result source_slot event_sequence carrier_sequence rx_epoch session role_generation clock_epoch clock_run model_token applied_command_seq ring_config_seq ring_applied_seq local_slot reference_slot node_count schedule_crc32 profile_crc32 observer_epoch tick_hz path_crc32 delay_ns path_direction path_qualification".split()
WIDE = "arm_epoch valid_from_raw raw_lo raw_hi local_lo local_hi remote_lo remote_hi expected_lo expected_hi residual_lo residual_hi".split()

def parse_match(raw):
    n = direct.numbers(raw, len(SCALARS) + 2 * len(WIDE))
    d = dict(zip(SCALARS, n))
    for i, name in enumerate(WIDE):
        index = len(SCALARS) + 2*i
        value = n[index] | n[index+1] << 32
        if name.startswith("residual") and value & (1 << 63):
            value -= 1 << 64
        d[name] = value
    return d

def forward_delay(links, source, target):
    edges = {link["marker_source_node"]: link for link in links}
    assert len(edges) == len(links)
    delay, node, visited = 0, source, set()
    while node != target:
        assert node not in visited and node in edges
        visited.add(node)
        edge = edges[node]
        assert edge["valid"] == 1 and edge["link_base_delay_ns"] >= 0
        delay += edge["link_base_delay_ns"] * 2
        node = edge["marker_destination_node"]
    return delay

class MatchProbe(direct.DirectProbe):
    def __init__(self, opt):
        super().__init__(opt)
        self.report["schema"] = "PRIORITY_EXACT_MATCH_SHORT_PROBE_V1"
        self.report["tool_sources"].append(dict(path=str(Path(__file__)), sha256=parent.base.sha(Path(__file__))))
        self.report["plan"].update(exact_source_match=True, dco_control=False,
            delay_qualification="provisional forward-CS transpose of installed reverse-DATA matrix")
        parent.base.BACKEND.TDMA_CONTROL_RESULT_FIELDS[MATCH.upper()] = 1
        parent.scpi_serial.SCALAR_U32_QUERY_HEADERS.add(MATCH.upper()+"?")
        self.tap_baseline = None
        if opt.tap_baseline:
            self.tap_baseline = json.loads(opt.tap_baseline.read_text(encoding="utf-8"))
            assert self.tap_baseline["passed"]
            self.report["tap_baseline"] = dict(path=str(opt.tap_baseline), sha256=parent.base.sha(opt.tap_baseline))

    def control(self, board, text, expected="OK"):
        result = super().control(board, text, expected)
        if text.upper().startswith(parent.SYNC.upper()+" "):
            generation = int(text.split()[-1])
            if generation == 0:
                super().control(board, MATCH+" 0", 0)
                row = self.report["boards"][board.address]
                if row.get("tap_installed"):
                    original = row["original_tap"][:3]
                    super().control(board, "SYSTem:TDMA:EVENt:TAP " + ",".join(map(str, original)))
                    row["restored_tap"] = direct.numbers(self.command(board, "SYSTem:TDMA:EVENt:TAP?"), 12)
                    assert row["restored_tap"][:3] == original
                    row["tap_installed"] = False
                    self.save()
            elif board.address == self.boards[0].address:
                # Parent has installed each board and master session; ARM has not begun.
                for follower in self.boards[1:]:
                    if self.tap_baseline:
                        row = self.report["boards"][follower.address]
                        row["original_tap"] = direct.numbers(self.command(follower, "SYSTem:TDMA:EVENt:TAP?"), 12)
                        request = self.tap_baseline["boards"][follower.address]["tap_request"]
                        assert len(request) == 3 and request[0] == 1
                        row["tap_installed"] = True
                        row["tap_request"] = request
                        self.save()
                        super().control(follower, "SYSTem:TDMA:EVENt:TAP " + ",".join(map(str, request)))
                    self.composite(follower, parent.SESSION, generation)
                    super().control(follower, f"{MATCH} {generation}", generation)
                    assert int(self.command(follower, MATCH+"?").strip('"')) == generation
        return result

    def collect(self):
        assert self.barrier and not self.block_queries
        for key, command in (("origin_handoff_raw", "READ:CALibration:ORIGin:HANDoff?"),
                             ("origin_release_raw", "READ:CALibration:ORIGin:RELease?"),
                             ("origin_runtime_raw", "READ:CALibration:ORIGin:RUNTIme?"),
                             ("origin_live_raw", "READ:CALibration:ORIGin:LIVE?"),
                             ("origin_admission_raw", "READ:CALibration:ORIGin:DIAGnostic?")):
            self.report[key] = self.command(self.boards[0], command)
            self.save()
        # Preserve all match evidence before the inherited transport assessment.
        for board in self.boards:
            row = self.report["boards"][board.address]
            for key, command in (("match_raw", STATUS), ("local_model_raw", parent.MODEL),
                                 ("event_live_raw", "SYSTem:TDMA:EVENt:LIVE?"),
                                 ("tap_raw", "SYSTem:TDMA:EVENt:TAP?"),
                                 ("recovery_raw", "SYSTem:TDMA:EVENt:RECovery?")):
                try:
                    row[key] = self.command(board, command)
                except Exception as exc:
                    self.error("collect:"+board.address+":"+key, exc)
                self.save()
        try:
            super().collect()
        except Exception as exc:
            self.error("transport-assessment", exc)
        for slot, board in enumerate(self.boards[1:], 1):
            row = self.report["boards"][board.address]
            try:
                m = row["match"] = parse_match(row["match_raw"])
                if self.tap_baseline:
                    tap = direct.numbers(row["tap_raw"], 12)
                    assert tap[:3] == tap[4:7] == row["tap_request"]
                    assert tap[3] == tap[7] and tap[8:10] == [1, 1]
                    assert tap[10:] == row["tap_request"][1:]
                assert m["schema"] == 1 and m["generation"] == self.report["binding_generation"]
                assert m["have_match"] == 1 and m["matched"] > 0, m
                assert m["session"] == self.report["feedback_session"]
                assert m["source_slot"] == m["reference_slot"] == 0 and m["local_slot"] == slot
                assert m["node_count"] == 4 and m["arm_epoch"] > 0 and m["observer_epoch"] > 0
                assert m["rx_epoch"] == row["direct"]["epoch"]
                assert m["schedule_crc32"] == self.report["tx_status"]["schedule_crc32"]
                assert m["profile_crc32"] == self.report["tx_status"]["profile_crc32"]
                assert m["path_qualification"] == 1 and m["delay_ns"] > 0
                assert m["path_direction"] == 0
                row["independent_forward_delay_ns"] = forward_delay(row["stage_reuse"]["links"], 0, slot)
                assert m["delay_ns"] == row["independent_forward_delay_ns"]
                assert m["valid_from_raw"] <= m["raw_lo"] <= m["raw_hi"]
                assert m["local_lo"] <= m["local_hi"] and m["remote_lo"] < m["remote_hi"]
                assert m["expected_lo"] == m["remote_lo"] + m["delay_ns"]
                assert m["expected_hi"] == m["remote_hi"] + m["delay_ns"]
                assert m["residual_lo"] == m["local_lo"] - m["expected_hi"]
                assert m["residual_hi"] == m["local_hi"] - m["expected_lo"]
                row["match_vs_last_rx"] = dict(same_event=m["event_sequence"] == row["direct"]["event_sequence"],
                    note="Last successfully matched source event can precede latest retained RX.")
                if row["match_vs_last_rx"]["same_event"]:
                    assert m["remote_lo"] == row["direct"]["lower_ns"]
                    assert m["remote_hi"]-m["remote_lo"] == row["direct"]["width_ns"]
                row["matched_observation_passed"] = True
            except Exception as exc:
                self.error("match-assessment:"+board.address, exc)
            self.save()
        self.report["passed"] = not self.report["errors"]

    def run(self):
        super().run()
        self.report["conclusion_scope"] = (
            "Core1 exact source-event lookup and projected residual with provisional directed delay. "
            "No DCO/ACK, physical lock, 100 ns accuracy, WCET or same-lap deadline claim.")
        self.save()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--baseline", type=Path, default=None)
    parser.add_argument("--tap-baseline", type=Path, default=None)
    parser.add_argument("--check-only", action="store_true")
    opt = parser.parse_args()
    if not 2 <= opt.seconds <= 8:
        parser.error("seconds must be 2..8")
    probe = MatchProbe(opt)
    probe.run()
    print(json.dumps(dict(passed=probe.report["passed"], errors=probe.report["errors"], out=str(opt.out))))
    raise SystemExit(0 if probe.report["passed"] else 2)
