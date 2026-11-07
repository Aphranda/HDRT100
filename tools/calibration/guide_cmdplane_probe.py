#!/usr/bin/env python3
"""Bench driver for the CTRQS guide ladder (10 kHz guide ring + REFMEM plane).

This is the only reproducible evidence path for the GUIDE ladder: the P3
acceptance credential is scoped to FOUR_NODE_TDMA_QUICK_DIAGNOSTIC and does not
exercise `SYST:TDMA:GUIDE:` actions.  Registered as TOOL-CALIBRATION-GUIDE
(STANDALONE) in tools/tool_workflow_triage.json; evidence root
out/calibration/guide-10k/evidence/.

Subcommands:
  idn              list COM ports and their *IDN? identity
  status           dump RING:STATus? / CMD:STATus? / PLAN? compact fields
  stage [level]    per board: CAL:TRAIN:STAGE:CLEAR + OPMode:STAGe <level> + APPLy
  up               TOPO -> BRINGUP 1 -> ARM/START (followers first)
  down             RING:STOP on all boards
  ga <action> ...  run one guide action (ladder guard + strict verdict + archive)
  verify <a> <epoch> [gen] [repeats]
                   strict verdict: freshness, terminal state, mask, no abort
  plan / verdict / ladder / phases
"""

from __future__ import annotations

import os
import sys
import time
from datetime import datetime

ROOT = r"D:\Work\HDRT100"
sys.path.insert(0, ROOT)
sys.path.insert(0, ROOT + r"\tools")

from tools.common.scpi.scpi_serial import SerialSession  # noqa: E402

PORTS = ["COM5", "COM4", "COM3", "COM6"]  # NO.1 .. NO.4
LEVEL_10K = 23
NODES = 4
# C-segment tail: candidate_count, ref_slot, cycles[0], cycles[1], low, high.
# The cycle ladder is a plan choice (the canonical host tool sweeps cycles), so
# it can follow the profile: 16 cycles cost ~3.4 ring periods each at 10 kHz.
CAND = os.environ.get("GUIDE_CAND", "3,0,4,16,1,64")

# PLAN? field order (firmware scpi_cmd_system_tdma_guide_plan_q)
PLAN_FIELDS = [
    "valid", "action", "crc", "gen", "level", "window", "trials",
    "pending", "epoch", "ctrqs", "prog_valid", "prog_status", "trials_done",
    "deepest", "prog_reason", "reconcile", "resume_trial", "commit",
    "commit_appl", "commit_ok", "ladder_idx", "ladder_step",
    "passed_mask", "failed_mask", "abort_site",
]
# CMD:STATus? (SYSTem:REFMEM:CMD:STATus?) selected indices
CMD_FIELDS = {
    "submitted": 1, "rejected": 3, "posted": 7, "acked": 8,
    "tx_ok": 9, "tx_fail": 10, "maint_chunks": 12, "maint_frames": 13, "frag_ok": 40, "frag_reject": 41,
    "frag_complete": 42, "frag_submit_fail": 43, "frag_pending": 44,
    "queue_depth": 47, "queue_enq": 48, "queue_deq": 49, "queue_full": 50,
    "tx_awaiting_ack": 67, "tx_ack_ok": 68, "tx_retries": 69,
    "tx_ack_fail": 70, "tx_dropped": 71,
}


def now() -> str:
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


class Board:
    def __init__(self, port: str) -> None:
        self.port = port
        self.sess = SerialSession(port, 115200, 3.0, 0.05,
                                  read_timeout_s=0.05)
        self.no = 0
        self.build = "?"

    def cmd(self, text: str) -> str:
        try:
            return self.sess.execute(text, require_match=False)
        except Exception as exc:  # noqa: BLE001
            return f"<EXC {exc}>"

    def probe(self) -> None:
        idn = self.cmd("*IDN?")
        # e.g. "...,DHRT100,NO.3,<build>" - keep the raw text, parse NO.n
        self.idn = idn
        for token in idn.replace('"', " ").replace(",", " ").split():
            if token.upper().startswith("NO."):
                try:
                    self.no = int(token[3:])
                except ValueError:
                    pass
        self.build = self.cmd("SYSTem:FW:BUILD?")

    def u32(self, query: str) -> list[int]:
        text = self.cmd(query)
        out: list[int] = []
        for token in text.replace('"', "").replace(",", " ").split():
            try:
                out.append(int(token, 0))
            except ValueError:
                pass
        return out

    def tokens(self, query: str) -> list[str]:
        text = self.cmd(query)
        return text.replace('"', " ").replace(",", " ").split()

    def plan(self) -> list[int]:
        return self.u32("SYSTem:TDMA:GUIDE:PLAN?")

    def cmdstat(self) -> dict[str, int]:
        vals = self.u32("SYSTem:REFMEM:CMD:STATus?")
        res: dict[str, int] = {}
        for name, idx in CMD_FIELDS.items():
            res[name] = vals[idx] if idx < len(vals) else -1
        return res

    def ring(self) -> list[int]:
        return self.u32("SYSTem:TDMA:RING:STATus?")

    def close(self) -> None:
        self.sess.close()


def open_boards() -> list[Board]:
    boards = [Board(p) for p in PORTS]
    for b in boards:
        b.probe()
    boards.sort(key=lambda b: b.no if b.no else 99)
    print(f"{now()} identity: " + ", ".join(
        f"{b.port}=NO.{b.no} build={b.build.strip()}" for b in boards))
    return boards


def plan_line(b: Board) -> str:
    vals = b.plan()
    if not vals:
        return f"NO.{b.no} PLAN?=<empty>"
    gen = vals[3] if len(vals) > 3 else -1
    valid = vals[0] if vals else -1
    action = vals[1] if len(vals) > 1 else -1
    ctrqs = vals[9] if len(vals) > 9 else -1
    prog = vals[11] if len(vals) > 11 else -1
    trials = vals[12] if len(vals) > 12 else -1
    return (f"NO.{b.no} valid={valid} action={action} gen={gen} "
            f"ctrqs={ctrqs} prog={prog} trials_done={trials}")


def cmd_summary(s: dict[str, int]) -> str:
    return (f"tx_ok={s['tx_ok']} tx_fail={s['tx_fail']} "
            f"frag_ok={s['frag_ok']} frag_rej={s['frag_reject']} "
            f"frag_done={s['frag_complete']} submit_fail={s['frag_submit_fail']} "
            f"q_depth={s['queue_depth']} q_full={s['queue_full']} "
            f"await_ack={s['tx_awaiting_ack']} ack_ok={s['tx_ack_ok']} "
            f"retries={s['tx_retries']} ack_fail={s['tx_ack_fail']}")


def ring_line(b: Board) -> str:
    vals = b.ring()
    if len(vals) < 40:
        return f"NO.{b.no} RING?=<short {len(vals)}>"
    return (f"NO.{b.no} enabled={vals[0]} nodes={vals[1]} local={vals[2]} "
            f"up={vals[4]} down={vals[5]} adapter={vals[8]} "
            f"err={vals[23]} trip_ns={vals[16]} cfgseq={vals[38]} "
            f"applied={vals[39]}")


def configure_level(level: int) -> None:
    """Reset the calibration stage and apply one profile level on every board."""
    boards = open_boards()
    for b in boards:
        print(f"{now()} NO.{b.no} stage_clear -> {b.cmd('CAL:TRAIN:STAGE:CLEAR')}")
        print(f"{now()} NO.{b.no} stage {level} -> "
              f"{b.cmd(f'SYST:TDMA:OPMode:STAGe {level}')}")
        print(f"{now()} NO.{b.no} apply -> {b.cmd('SYST:TDMA:OPMode:APPLy')}")
    for b in boards:
        print(f"{now()} {ring_line(b)}")
    for b in boards:
        b.close()


def ring_healthy(b: Board) -> bool:
    vals = b.ring()
    return len(vals) > 8 and vals[0] == 1 and vals[4] == 1 and vals[5] == 1


def guide_config(b: Board, text: str) -> str:
    """Post CONFig, retrying while the transport-ready cache is still cold.

    Right after ARM/START the refmem service has not refreshed its ring-ready
    cache yet, so the submit is refused synchronously with -200.  A retry a
    couple of seconds later is the documented behaviour, not a fault.
    """
    response = ""
    for attempt in range(6):
        response = b.cmd(text)
        if "-200" not in response and "ERR" not in response.upper():
            return response
        print(f"{now()} CONFig refused (attempt {attempt}): {response}")
        time.sleep(2.0)
    return response


def bring_up() -> bool:
    """TOPO -> BRINGUP -> ARM/START with retries; the reference START is flaky."""
    boards = open_boards()
    followers = [b for b in boards if b.no != 1]
    ref = [b for b in boards if b.no == 1]
    for b in boards:
        print(f"{now()} NO.{b.no} topo -> "
              f"{b.cmd(f'SYST:TDMA:RING:TOPology {NODES},{b.no - 1},0')}")
    for b in boards:
        print(f"{now()} NO.{b.no} bringup -> {b.cmd('SYST:TDMA:RING:BRINGUP 1')}")
    healthy = False
    for attempt in range(4):
        pending = [b for b in boards if not ring_healthy(b)]
        if not pending:
            healthy = True
            break
        for b in followers + ref:
            if b in pending:
                print(f"{now()} attempt {attempt} NO.{b.no} arm -> "
                      f"{b.cmd('SYST:TDMA:RING:ARM')}")
        for b in followers + ref:
            if b in pending:
                print(f"{now()} attempt {attempt} NO.{b.no} start -> "
                      f"{b.cmd('SYST:TDMA:RING:START')}")
        time.sleep(1.0)
    for b in boards:
        print(f"{now()} {ring_line(b)}")
    if not healthy:
        print(f"{now()} WARNING: ring not healthy on all boards")
    for b in boards:
        b.close()
    return healthy


def tear_down() -> None:
    boards = open_boards()
    for b in boards:
        print(f"{now()} NO.{b.no} stop -> {b.cmd('SYST:TDMA:RING:STOP')}")
    for b in boards:
        b.close()


def poll(boards: list[Board], seconds: float, label: str,
         expect_gen: int | None = None) -> None:
    """Sample PLAN? on every board until all match expect_gen or timeout."""
    t0 = time.monotonic()
    last: dict[int, str] = {}
    first_seen: dict[int, float] = {}
    while time.monotonic() - t0 < seconds:
        for b in boards:
            vals = b.plan()
            gen = vals[3] if len(vals) > 3 else -1
            if expect_gen is not None and gen == expect_gen and b.no not in first_seen:
                first_seen[b.no] = time.monotonic() - t0
                print(f"{now()} {label}: NO.{b.no} gen reached {expect_gen} "
                      f"after {first_seen[b.no]:.2f}s")
            line = plan_line(b)
            if last.get(b.no) != line:
                print(f"{now()} {label}: {line}")
                last[b.no] = line
        if expect_gen is not None and len(first_seen) == len(boards):
            break
        time.sleep(0.5)
    print(f"{now()} {label}: elapsed={time.monotonic() - t0:.1f}s "
          f"reached={sorted(first_seen)}")


def cmd_probe(gen: int) -> None:
    boards = open_boards()
    ref = boards[0]
    print(f"{now()} pre CMD:STATus? issuer NO.{ref.no}: "
          f"{cmd_summary(ref.cmdstat())}")
    for b in boards[1:]:
        print(f"{now()} pre CMD:STATus? NO.{b.no}: {cmd_summary(b.cmdstat())}")
    # P1 plan: action=1, crc=0x12340000+gen, gen, level=23, window=4000,
    # trials=4, candidate_count=3 (0,4,16), thresholds 1,64
    text = (f"SYST:TDMA:GUIDE:CONFig 1,{0x12340000 + gen},{gen},"
            f"{LEVEL_10K},4000,4,{CAND}")
    t_send = time.monotonic()
    print(f"{now()} CONFig -> {guide_config(ref, text)}")
    poll(boards, 60.0, "CONFig", expect_gen=gen)
    for b in boards:
        print(f"{now()} post CMD:STATus? NO.{b.no}: {cmd_summary(b.cmdstat())}")
    print(f"{now()} CONFig wall={time.monotonic() - t_send:.1f}s")
    for b in boards:
        b.close()


def trigger_probe(epoch: int) -> None:
    boards = open_boards()
    ref = boards[0]
    print(f"{now()} TRIGger -> {ref.cmd(f'SYST:TDMA:GUIDE:TRIGger 1,{epoch}')}")
    t0 = time.monotonic()
    seen: dict[int, float] = {}
    while time.monotonic() - t0 < 60.0:
        for b in boards:
            vals = b.plan()
            if len(vals) > 8 and vals[8] == epoch and b.no not in seen:
                seen[b.no] = time.monotonic() - t0
                print(f"{now()} TRIGger: NO.{b.no} epoch={epoch} "
                      f"after {seen[b.no]:.2f}s")
        if len(seen) == len(boards):
            break
        time.sleep(0.5)
    print(f"{now()} TRIGger: elapsed={time.monotonic() - t0:.1f}s "
          f"reached={sorted(seen)}")
    for b in boards:
        print(f"{now()} post CMD:STATus? NO.{b.no}: {cmd_summary(b.cmdstat())}")
    for b in boards:
        b.close()


def watch(gen: int, seconds: float) -> None:
    """Send one CONFig and sample the delivery mechanism until it completes."""
    boards = open_boards()
    ref = boards[0]
    followers = boards[1:]
    text = (f"SYST:TDMA:GUIDE:CONFig 1,{0x12340000 + gen},{gen},"
            f"{LEVEL_10K},4000,4,{CAND}")
    t0 = time.monotonic()
    print(f"{now()} CONFig -> {guide_config(ref, text)}")
    last: dict[str, object] = {}
    while time.monotonic() - t0 < seconds:
        t = time.monotonic() - t0
        s = ref.cmdstat()
        issuer = (s["queue_depth"], s["tx_awaiting_ack"], s["tx_ok"],
                  s["tx_ack_ok"], s["tx_retries"], s["tx_ack_fail"],
                  s["frag_submit_fail"], s["maint_chunks"], s["maint_frames"])
        f = followers[0]
        fs = f.cmdstat()
        target = (fs["frag_ok"], fs["frag_complete"], fs["frag_reject"],
                  fs["rx_restarts"] if "rx_restarts" in fs else -1)
        gens = tuple((b.plan() or [0, 0, 0, -1])[3] for b in boards)
        key = (issuer, target, gens)
        if last.get("k") != key:
            print(f"{now()} t={t:6.1f}s issuer(q,await,txok,ackok,retry,"
                  f"ackfail,subfail,chunks,frames)={issuer} NO.2(frag,done,rej)="
                  f"{target[:3]} gens={gens}")
            last["k"] = key
        if all(g == gen for g in gens):
            print(f"{now()} complete at t={t:.1f}s")
            break
        time.sleep(0.25)
    for b in boards:
        print(f"{now()} final CMD:STATus? NO.{b.no}: {cmd_summary(b.cmdstat())}")
    for b in boards:
        b.close()


def ladder(action: int, gen: int, epoch: int) -> None:
    """One C->T->R->Q->S cycle at the guide profile with explicit confirmation.

    At 10 kHz a CONFig needs ~50 s and a TRIGger ~20 s on the wire, so the next
    command must not be issued until every board readback agrees; otherwise a
    new fragment sequence resets the target reassembler mid-transfer.
    """
    boards = open_boards()
    ref = boards[0]
    for b in boards:
        vals = b.ring()
        if len(vals) < 9 or vals[0] != 1 or vals[8] != 1:
            print(f"{now()} ABORT: NO.{b.no} ring not running "
                  f"(enabled/adapter={vals[:1]}/{vals[8:9] if len(vals) > 8 else '?'})")
            for x in boards:
                x.close()
            return
    text = (f"SYST:TDMA:GUIDE:CONFig {action},{0x12340000 + gen},{gen},"
            f"{LEVEL_10K},4000,4,{CAND}")
    t0 = time.monotonic()
    print(f"{now()} CONFig -> {guide_config(ref, text)}")
    deadline = t0 + 150.0
    gens = {b.no: -1 for b in boards}
    while time.monotonic() < deadline:
        for b in boards:
            vals = b.plan()
            gens[b.no] = vals[3] if len(vals) > 3 else -1
        if all(g == gen for g in gens.values()):
            break
        time.sleep(1.0)
    t_cfg = time.monotonic() - t0
    print(f"{now()} CONFig confirm: {t_cfg:.1f}s gens={gens}")
    if not all(g == gen for g in gens.values()):
        print(f"{now()} ABORT: CONFig not confirmed on all boards after {t_cfg:.1f}s")
        for x in boards:
            x.close()
        return
    t1 = time.monotonic()
    print(f"{now()} TRIGger -> {ref.cmd(f'SYST:TDMA:GUIDE:TRIGger {action},{epoch}')}")
    deadline = t1 + 120.0
    epochs = {b.no: -1 for b in boards}
    while time.monotonic() < deadline:
        for b in boards:
            vals = b.plan()
            epochs[b.no] = vals[8] if len(vals) > 8 else -1
        if all(e == epoch for e in epochs.values()):
            break
        time.sleep(1.0)
    t_trig = time.monotonic() - t1
    print(f"{now()} TRIGger confirm: {t_trig:.1f}s epochs={epochs}")
    if not all(e == epoch for e in epochs.values()):
        print(f"{now()} ABORT: TRIGger not confirmed after {t_trig:.1f}s")
    # Watch the execution (window stops the ring, trials run, ring recovers).
    t2 = time.monotonic()
    last: dict[int, tuple[int, ...]] = {}
    while time.monotonic() - t2 < 300.0:
        for b in boards:
            vals = b.plan()
            key = tuple(vals[9:16]) if len(vals) >= 16 else ()
            if last.get(b.no) != key:
                print(f"{now()} t={time.monotonic() - t2:6.1f}s NO.{b.no} "
                      f"ctrqs={vals[9] if len(vals) > 9 else -1} "
                      f"prog_valid={vals[10] if len(vals) > 10 else -1} "
                      f"prog={vals[11] if len(vals) > 11 else -1} "
                      f"trials={vals[12] if len(vals) > 12 else -1} "
                      f"deepest={vals[13] if len(vals) > 13 else -1} "
                      f"reason={vals[14] if len(vals) > 14 else -1} "
                      f"reconcile={vals[15] if len(vals) > 15 else -1}")
                last[b.no] = key
        states = [b.plan() for b in boards]
        done = sum(1 for v in states if len(v) > 11 and v[11] == 1)
        if done == len(boards):
            print(f"{now()} all boards progress_valid=1")
            break
        time.sleep(1.0)
    print(f"{now()} final PLAN?:")
    for b in boards:
        print(f"  NO.{b.no} {b.plan()}")
        print(f"        ring: {ring_line(b)}")
        print(f"        CMD:  {cmd_summary(b.cmdstat())}")
    for b in boards:
        b.close()


def trace(action: int, gen: int, epoch: int, reset: bool = False) -> None:
    """Discriminating trace for the reference-board P1 abort.

    Samples from *before* the plan is posted, so the phase in which the
    reference starts trials (plan accept vs trigger) and the branch that aborts
    (FORWARD vs TRIAL 2361 vs TRIAL 2367) are both visible in one run.
    """
    if reset:
        # A previous generation's trigger can still be in the maintenance
        # stream for tens of seconds; a fresh ring drops every queued frame and
        # makes the samples unambiguous.
        tear_down()
        configure_level(LEVEL_10K)
        bring_up()
        time.sleep(3.0)
    boards = open_boards()
    ref = boards[0]
    unhealthy = [b.no for b in boards if not ring_healthy(b)]
    if unhealthy:
        print(f"{now()} ABORT: ring not healthy on boards {unhealthy}")
        for x in boards:
            x.close()
        return
    # A running window would make every later sample ambiguous.
    wait0 = time.monotonic()
    while time.monotonic() - wait0 < 120.0:
        states = []
        for b in boards:
            vals = b.plan()
            states.append(vals[9] if len(vals) > 9 else -1)
        if all(s == 0 for s in states):
            break
        time.sleep(1.0)
    print(f"{now()} idle ctrqs={states}")
    if not all(s == 0 for s in states):
        print(f"{now()} ABORT: a window is still active")
        for x in boards:
            x.close()
        return
    base = {b.no: (b.plan() or [])[9:15] for b in boards}
    print(f"{now()} baseline {base}")
    t_send = time.monotonic()
    text = (f"SYST:TDMA:GUIDE:CONFig {action},{0x12340000 + gen},{gen},"
            f"{LEVEL_10K},4000,4,{CAND}")
    print(f"{now()} CONFig -> {guide_config(ref, text)}")
    trig_sent = None
    gen_confirmed = None
    rows: list[dict[str, object]] = []
    last_key: dict[str, tuple] = {}
    deadline = t_send + 300.0
    while time.monotonic() < deadline:
        t = time.monotonic() - t_send
        gens = []
        for b in boards:
            vals = b.plan()
            gens.append(vals[3] if len(vals) > 3 else -1)
        if gen_confirmed is None and all(g == gen for g in gens):
            gen_confirmed = t
            print(f"{now()} gen confirmed at t={t:.1f}s")
            trig_sent = time.monotonic()
            print(f"{now()} TRIGger -> "
                  f"{ref.cmd(f'SYST:TDMA:GUIDE:TRIGger {action},{epoch}')}")
        if gen_confirmed is None and t > 150.0:
            print(f"{now()} ABORT: gen not confirmed after {t:.0f}s gens={gens}")
            break
        tok = ref.tokens("SYST:TDMA:RING:TRAIN:STATus?")
        train: tuple = ()
        if len(tok) >= 28 and tok[0] == "CLKTRAIN":
            def num(i: int) -> int:
                try:
                    return int(tok[i], 0)
                except (ValueError, IndexError):
                    return -1
            # state, result, return_seen, return_before_tx_done, tx_done_lo,
            # return_lo, phys request_seq
            train = (num(2), num(3), num(9), num(10), num(17), num(19), num(5))
            rows.append({"t": round(t, 2), "board": 1, "train_sample": train})
        follower_train = {}
        for b in boards[1:]:
            snap = train_parse(b)
            follower_train[b.no] = (snap.get("state"), snap.get("result"),
                                    snap.get("request_seq"))
        for b in boards:
            vals = b.plan()
            key = tuple(vals[9:15]) if len(vals) >= 15 else ()
            masks = (vals[22], vals[23]) if len(vals) > 23 else ()
            label = f"no{b.no}"
            if last_key.get(label) != (key, masks):
                print(f"{now()} t={t:6.2f}s NO.{b.no} ctrqs={key[0] if key else -1} "
                      f"prog_valid={key[1] if len(key) > 1 else -1} "
                      f"prog_status={key[2] if len(key) > 2 else -1} "
                      f"trials_done={key[3] if len(key) > 3 else -1} "
                      f"deepest={key[4] if len(key) > 4 else -1} "
                      f"reason={key[5] if len(key) > 5 else -1} "
                      f"masks(passed,failed)={masks}"
                      + (f" | train(s,r,ret,rbtd,td,rt,req)={train} "
                         f"followers(s,r,req)={follower_train}"
                         if b.no == 1 else ""))
                last_key[label] = (key, masks)
                rows.append({"t": round(t, 2), "board": b.no,
                             "state": key[0] if key else -1,
                             "prog_valid": key[1] if len(key) > 1 else -1,
                             "prog_status": key[2] if len(key) > 2 else -1,
                             "trials_done": key[3] if len(key) > 3 else -1,
                             "deepest": key[4] if len(key) > 4 else -1,
                             "reason": key[5] if len(key) > 5 else -1,
                             "passed_mask": masks[0] if masks else -1,
                             "failed_mask": masks[1] if len(masks) > 1 else -1,
                             "train": train,
                             "follower_train": follower_train})
        if trig_sent is not None and time.monotonic() - trig_sent > 120.0:
            if all((b.plan() or [])[9] == 0 for b in boards):
                print(f"{now()} window closed on all boards at t={t:.1f}s")
                break
        time.sleep(0.5)
    stamp = datetime.now().strftime("%H%M%S")
    path = rf"{ROOT}\out\calibration\guide-10k\trace-{stamp}.json"
    import json as _json
    with open(path, "w", encoding="utf-8") as handle:
        _json.dump({"gen": gen, "epoch": epoch, "baseline": base,
                    "rows": rows}, handle)
    print(f"{now()} trace saved {path} rows={len(rows)}")
    for b in boards:
        print(f"  NO.{b.no} {b.plan()}")
        print(f"        {ring_line(b)}")
    for b in boards:
        b.close()


def train_snapshot() -> None:
    """Dump the reference board's last clock-train snapshot with full 64-bit times."""
    boards = open_boards()
    ref = boards[0]
    tok = ref.tokens("SYST:TDMA:RING:TRAIN:STATus?")
    names = ["tag", "version", "state", "result", "role", "request_seq",
             "service_count", "baud_hz", "requested_cycles", "return_seen",
             "return_before_tx_done", "tx_sck_pin", "rx_sck_pin",
             "ts_resolution_ns", "ts_flags", "tx_start_lo", "tx_start_hi",
             "tx_done_lo", "tx_done_hi", "return_lo", "return_hi",
             "burst_lo", "burst_hi", "ring_train_req", "ring_train_acc",
             "train_start_count", "train_reject_count", "training_dirty"]
    values: dict[str, int] = {}
    for name, raw in zip(names, tok):
        try:
            values[name] = int(raw, 0)
        except ValueError:
            values[name] = -1
    print(f"{now()} TRAIN snapshot NO.{ref.no}:")
    for name in names[1:]:
        print(f"  {name:24s} {values.get(name, -1)}")
    tx_start = (values.get("tx_start_lo", 0) |
                (values.get("tx_start_hi", 0) << 32))
    tx_done = (values.get("tx_done_lo", 0) |
               (values.get("tx_done_hi", 0) << 32))
    ret = values.get("return_lo", 0) | (values.get("return_hi", 0) << 32)
    burst = values.get("burst_lo", 0) | (values.get("burst_hi", 0) << 32)
    print(f"  derived: tx_start={tx_start} tx_done={tx_done} return={ret} "
          f"burst_ns={burst}")
    if tx_start and ret:
        print(f"  return - tx_start = {ret - tx_start} ns")
    if tx_start and tx_done:
        print(f"  tx_done - tx_start = {tx_done - tx_start} ns")
    for b in boards:
        b.close()


def train_parse(b: Board) -> dict[str, int]:
    tok = b.tokens("SYST:TDMA:RING:TRAIN:STATus?")
    names = ["tag", "version", "state", "result", "role", "request_seq",
             "service_count", "baud_hz", "requested_cycles", "return_seen",
             "return_before_tx_done", "tx_sck_pin", "rx_sck_pin",
             "ts_resolution_ns", "ts_flags", "tx_start_lo", "tx_start_hi",
             "tx_done_lo", "tx_done_hi", "return_lo", "return_hi",
             "burst_lo", "burst_hi", "ring_train_req", "ring_train_acc",
             "train_start_count", "train_reject_count", "training_dirty"]
    values: dict[str, int] = {}
    for name, raw in zip(names, tok):
        try:
            values[name] = int(raw, 0)
        except ValueError:
            values[name] = -1
    return values


def trainprobe(cycles: int, attempts: int, arm_followers: bool = False) -> None:
    """ARM-only ring (no START), then one clock train with 100 ms sampling.

    P1's reference trial is the only role that must reach MASTER_COMPLETE, so
    this isolates the train's return path from the executor and the window.
    """
    tear_down()
    configure_level(LEVEL_10K)
    boards = open_boards()
    ref = boards[0]
    for b in boards:
        print(f"{now()} NO.{b.no} topo -> "
              f"{b.cmd(f'SYST:TDMA:RING:TOPology {NODES},{b.no - 1},0')}")
    for b in boards:
        print(f"{now()} NO.{b.no} bringup -> {b.cmd('SYST:TDMA:RING:BRINGUP 1')}")
    for b in [x for x in boards if x.no != 1] + [ref]:
        print(f"{now()} NO.{b.no} arm -> {b.cmd('SYST:TDMA:RING:ARM')}")
    time.sleep(1.0)
    for b in boards:
        print(f"{now()} {ring_line(b)}")
    if arm_followers:
        # Put every follower in FORWARDING the same way the executor's FORWARD
        # segment does (a 1-cycle train), so the master's bursts have a live
        # forward path.  This isolates "does the 2nd burst return?" from the
        # executor state machine.
        for b in boards[1:]:
            print(f"{now()} NO.{b.no} follow-train -> "
                  f"{b.cmd('SYST:TDMA:RING:TRAIN 1')}")
        time.sleep(0.5)
        for b in boards[1:]:
            snap = train_parse(b)
            print(f"{now()} NO.{b.no} state={snap.get('state')} "
                  f"result={snap.get('result')} (1=FORWARDING)")
    for attempt in range(attempts):
        before = train_parse(ref)
        print(f"{now()} train #{attempt} before: state={before.get('state')} "
              f"req={before.get('request_seq')} acc={before.get('ring_train_acc')}")
        response = ref.cmd(f"SYST:TDMA:RING:TRAIN {cycles}")
        print(f"{now()} train #{attempt} -> {response}")
        if "-200" in response:
            break
        deadline = time.monotonic() + 6.0
        last: tuple | None = None
        while time.monotonic() < deadline:
            snap = train_parse(ref)
            tx_start = (snap.get("tx_start_lo", 0) |
                        (snap.get("tx_start_hi", 0) << 32))
            tx_done = (snap.get("tx_done_lo", 0) |
                       (snap.get("tx_done_hi", 0) << 32))
            ret = (snap.get("return_lo", 0) |
                   (snap.get("return_hi", 0) << 32))
            key = (snap.get("state"), snap.get("result"),
                   snap.get("return_seen"), snap.get("request_seq"))
            if key != last:
                extra = ""
                if tx_start and ret:
                    extra = f" ret-txstart={(ret - tx_start) / 1e6:.1f}ms"
                if tx_start and tx_done:
                    extra += f" txdone-txstart={(tx_done - tx_start) / 1e6:.1f}ms"
                fwd = [b.no for b in boards[1:]
                       if train_parse(b).get("state") == 2]  # FORWARDING
                print(f"{now()}   state={snap.get('state')} "
                      f"result={snap.get('result')} "
                      f"return_seen={snap.get('return_seen')} "
                      f"req={snap.get('request_seq')} "
                      f"burst_ns={snap.get('burst_lo', 0)} {extra} "
                      f"forwarding_boards={fwd}")
                last = key
            time.sleep(0.1)
        for b in boards[1:]:
            snap = train_parse(b)
            print(f"{now()}   NO.{b.no} follower state={snap.get('state')} "
                  f"result={snap.get('result')} forwarding="
                  f"{snap.get('state') == 2}")
        time.sleep(1.0)
    for b in boards:
        b.close()


def phases() -> None:
    """Dump ring-phase negotiation state on every board."""
    boards = open_boards()
    for b in boards:
        vals = b.u32("SYST:TDMA:RING:PHASE?")
        if len(vals) < 5:
            print(f"NO.{b.no} PHASE?=<short {vals}>")
            continue
        local, rnd, pub, recv, rej = vals[0], vals[1], vals[2], vals[3], vals[4]
        peers = []
        for slot in range(0, 8):
            base = 5 + slot * 3
            if base + 2 < len(vals):
                peers.append(f"{slot}:{vals[base]}/{vals[base + 1]}/{vals[base + 2]}")
        print(f"NO.{b.no} local={local} round={rnd} pub={pub} recv={recv} "
              f"rej={rej} peers(valid/phase/round)={' '.join(peers)}")
    for b in boards:
        b.close()


def show_plan() -> None:
    """Print the full PLAN? field list per board with names."""
    boards = open_boards()
    for b in boards:
        vals = b.plan()
        named = ", ".join(
            f"{PLAN_FIELDS[i]}={vals[i]}" for i in range(min(len(vals), len(PLAN_FIELDS)))
        )
        print(f"NO.{b.no} ({len(vals)} fields): {named}")
    for b in boards:
        b.close()


def stateprobe(gen: int, epoch: int, seconds: float) -> None:
    """Sample the reference's executor state and ring error at 0.2 s.

    The ARM/abort failure lasts about a second, which the normal 0.5 s trace
    misses; this prints every sample for NO.1 so the exact state and the
    adapter error that preceded the abort are both visible.
    """
    boards = open_boards()
    ref = boards[0]
    text = (f"SYST:TDMA:GUIDE:CONFig 1,{0x12340000 + gen},{gen},"
            f"{LEVEL_10K},4000,4,{CAND}")
    print(f"{now()} CONFig -> {guide_config(ref, text)}")
    t_cfg = time.monotonic()
    while time.monotonic() - t_cfg < 150.0:
        gens = [(b.plan() or [0, 0, 0, -1])[3] for b in boards]
        if all(g == gen for g in gens):
            break
        time.sleep(1.0)
    print(f"{now()} gen confirm {time.monotonic() - t_cfg:.1f}s gens={gens}")
    ref.cmd(f"SYST:TDMA:GUIDE:TRIGger 1,{epoch}")
    t0 = time.monotonic()
    last: tuple | None = None
    while time.monotonic() - t0 < seconds:
        vals = ref.plan()
        ring = ref.ring()
        state = vals[9] if len(vals) > 9 else -1
        prog = vals[11] if len(vals) > 11 else -1
        reason = vals[14] if len(vals) > 14 else -1
        abort_site = vals[24] if len(vals) > 24 else -1
        key = (state, prog, reason, abort_site)
        if key != last:
            extra = ""
            if len(ring) > 23:
                extra = (f" ring(en={ring[0]},up={ring[4]},dn={ring[5]},"
                         f"ad={ring[8]},err={ring[23]},cfg={ring[38]}/"
                         f"{ring[39]})")
            print(f"{now()} t={time.monotonic() - t0:6.2f}s ctrqs={state} "
                  f"prog={prog} reason={reason} abort_site={abort_site}{extra}")
            last = key
        if len(vals) > 8 and vals[8] == epoch and prog in (1, 2) and \
                reason != 0:
            print(f"{now()} final: {vals}")
            break
        time.sleep(0.2)
    for b in boards:
        b.close()


def verdict() -> None:
    """One line per board: the M6.1/M6.2 acceptance fields."""
    boards = open_boards()
    ok = 0
    lines = []
    for b in boards:
        v = b.plan()
        if len(v) < 25:
            lines.append(f"NO.{b.no} <short plan>")
            continue
        good = (v[11] == 1 and v[12] == v[6] and v[22] == 0xF and v[23] == 0)
        ok += 1 if good else 0
        lines.append(
            f"NO.{b.no} status={v[11]} trials={v[12]}/{v[6]} "
            f"passed=0x{v[22]:X} failed=0x{v[23]:X} reason={v[14]} "
            f"ladder={v[20]}/{v[21]} commit={v[17]}/{v[18]}/{v[19]} "
            f"{'GREEN' if good else 'NOT-GREEN'}")
    for b in boards:
        b.close()
    print("\n".join(lines))
    print(f"green_boards={ok}/{len(boards)}")


def p2(gen: int, epoch: int, window: int = 8000) -> None:
    """One P2 round with the canonical coded-measurement parameters.

    candidates = ref_slot, codebook, min_lag, max_lag, min_margin;
    threshold_low = probe phase cycles, threshold_high = max best-distance.
    The plan level is the *measurement* level (7); the ring itself is formed at
    the guide carrier the window was entered with.
    """
    boards = open_boards()
    ref = boards[0]
    text = (f"SYST:TDMA:GUIDE:CONFig 2,{0x22000000 + gen},{gen},7,"
            f"{window},1,5,0,1,80,120,0,10,512")
    print(f"{now()} P2 CONFig -> {guide_config(ref, text)}")
    t0 = time.monotonic()
    while time.monotonic() - t0 < 150.0:
        gens = [(b.plan() or [0, 0])[3] if len(b.plan() or []) > 3 else 0
                for b in boards]
        if all(g == gen for g in gens):
            break
        time.sleep(1.0)
    print(f"{now()} gen confirm {time.monotonic() - t0:.1f}s gens={gens}")
    print(f"{now()} P2 TRIGger -> "
          f"{ref.cmd(f'SYST:TDMA:GUIDE:TRIGger 2,{epoch}')}")
    deadline = time.monotonic() + 180.0
    last: tuple | None = None
    while time.monotonic() < deadline:
        states = []
        for b in boards:
            v = b.plan()
            states.append((v[9], v[11], v[12], v[22], v[23]) if len(v) > 23
                          else (-1,))
        key = tuple(states)
        if key != last:
            print(f"{now()} P2 states={states}")
            last = key
        if all(s[1] in (1, 2, 3) for s in states if len(s) > 1):
            break
        time.sleep(0.5)
    for b in boards:
        b.close()
    verdict()


def p3(gen: int, epoch: int, window: int = 20000) -> None:
    """One P3 round with the canonical per-link ranging parameters.

    candidates = start_link, signal_group(0=CLK_DATA,1=CS_DATA), pulse_count,
    capture_words, repeats_per_link; threshold_low = baud_hz,
    threshold_high = frequency tolerance per-mille.
    """
    boards = open_boards()
    ref = boards[0]
    text = (f"SYST:TDMA:GUIDE:CONFig 3,{0x33000000 + gen},{gen},7,"
            f"{window},1,5,0,0,32,256,3,10000000,50000")
    print(f"{now()} P3 CONFig -> {guide_config(ref, text)}")
    t0 = time.monotonic()
    gens = []
    while time.monotonic() - t0 < 150.0:
        gens = [(b.plan() or [0, 0, 0])[3] for b in boards]
        if all(g == gen for g in gens):
            break
        time.sleep(1.0)
    print(f"{now()} gen confirm {time.monotonic() - t0:.1f}s gens={gens}")
    print(f"{now()} P3 TRIGger -> "
          f"{ref.cmd(f'SYST:TDMA:GUIDE:TRIGger 3,{epoch}')}")
    deadline = time.monotonic() + 240.0
    last: tuple | None = None
    while time.monotonic() < deadline:
        states = []
        for b in boards:
            v = b.plan()
            states.append((v[9], v[11], v[12], v[13], v[22], v[23])
                          if len(v) > 23 else (-1,))
        key = tuple(states)
        if key != last:
            print(f"{now()} P3 states={states}")
            last = key
        # Terminal once the planner generation moved and progress is reported,
        # or all boards are back to the idle state with a stored result.
        if all(len(s) > 5 and s[1] in (1, 2) and s[0] == 0 for s in states):
            break
        time.sleep(0.5)
    for b in boards:
        b.close()
    verdict()


def guide_action(action: int, gen: int, epoch: int, level: int, window: int,
                 trials: int, cands: list[int], lo: int, hi: int) -> None:
    """Run one arbitrary guide action and report the four-board PLAN? result.

    The C segment layout differs per action; this keeps the probe generic so a
    new action does not need a new function (the layout is documented in
    application/src/app.c app_ctrqs_start()).
    """
    boards = open_boards()
    ref = boards[0]
    tail = ",".join(str(c) for c in cands)
    text = (f"SYST:TDMA:GUIDE:CONFig {action},{0x40000000 + gen},{gen},"
            f"{level},{window},{trials},{len(cands)},{tail},{lo},{hi}")
    if not ladder_ok(boards, action):
        # A diverged ladder makes every later action refuse; stop here instead
        # of burning a window on a round that cannot be accepted.
        print(f"{now()} ABORT: ladder diverged, reset all boards before retrying")
        for b in boards:
            b.close()
        return
    print(f"{now()} action {action} CONFig -> {guide_config(ref, text)}")
    t0 = time.monotonic()
    gens = []
    while time.monotonic() - t0 < 150.0:
        gens = [(b.plan() or [0, 0, 0])[3] for b in boards]
        if all(g == gen for g in gens):
            break
        time.sleep(1.0)
    print(f"{now()} gen confirm {time.monotonic() - t0:.1f}s gens={gens}")
    if not all(g == gen for g in gens):
        # Triggering on a half-applied plan mixes two generations across the
        # boards and produces a meaningless verdict, so stop here instead.
        print(f"{now()} ABORT: plan not applied on all boards (gens={gens})")
        for b in boards:
            b.close()
        return
    # The executor refuses a new trigger while it is still restoring the
    # previous action (RESTORE_WAIT/REBUILD), and the refusal is silent: the
    # trigger stays pending and the plan's progress keeps the old action's
    # numbers.  Wait for every board to be idle before triggering.
    t_idle = time.monotonic()
    while time.monotonic() - t_idle < 180.0:
        states = [(b.plan() or [0, 0])[9] for b in boards]
        if all(s == 0 for s in states):
            break
        time.sleep(1.0)
    print(f"{now()} idle wait {time.monotonic() - t_idle:.1f}s states={states}")
    print(f"{now()} action {action} TRIGger -> "
          f"{ref.cmd(f'SYST:TDMA:GUIDE:TRIGger {action},{epoch}')}")
    deadline = time.monotonic() + 240.0
    last: tuple | None = None
    ring_samples: list = []
    t_poll = time.monotonic()
    while time.monotonic() < deadline:
        states = []
        for b in boards:
            v = b.plan()
            states.append((v[9], v[11], v[12], v[22], v[23])
                          if len(v) > 23 else (-1,))
        key = tuple(states)
        if key != last:
            print(f"{now()} action {action} states={states}")
            last = key
        # In-window ring sampling: the loop-period fields are structurally zero
        # without a flight image, so keep the whole RING:STATUS? string several
        # times per round and derive the carrier from a monotonic counter.  T3
        # finishes in ~1.5 s, so the interval has to be well under a second or
        # the window yields a single sample.
        if int((time.monotonic() - t_poll) * 10) >= 1:
            t_poll = time.monotonic()
            stamp = round(time.monotonic(), 3)
            per_board = []
            for b in boards:
                raw = b.ring()
                if isinstance(raw, list) and raw:
                    per_board.append([b.no, raw])
            if per_board:
                ring_samples.append((stamp, per_board))
        if all(len(s) > 4 and s[1] in (1, 2) and s[0] in (0, 26, 27)
               for s in states) and any(s[2] for s in states):
            break
        # T3 lives ~1.5 s; a 0.5 s poll leaves two samples, so poll the
        # ring faster than the PLAN? sweep while the window is open.
        time.sleep(0.2)
    window_rate(f"action{action}", ring_samples)
    archive_evidence(f"action{action}-gen{gen}", boards, action, gen, epoch,
                     ring_samples, text)
    for b in boards:
        b.close()
    verdict()


def archive_evidence(tag: str, boards, action: int, gen: int, epoch: int,
                     ring_samples: list | None = None,
                     cseg: str | None = None) -> None:
    """Persist the four-board readback so a verdict can be re-run and audited.

    The acceptance credential does not cover the GUIDE ladder, so the only
    reproducible evidence for these milestones is the raw PLAN?/RING:STATUS?/
    RING:PHASE? text; keep it next to the probe instead of only in a console
    transcript.
    """
    import json as _json
    stamp = time.strftime("%Y%m%d-%H%M%S")
    # Evidence is an artifact, not source: keep it under the repo's out/ tree so
    # it never lands in a tools/ commit.
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    root = os.path.join(repo_root, "out", "calibration", "guide-10k",
                        "evidence", f"{stamp}-{tag}")
    os.makedirs(root, exist_ok=True)
    # The build label alone does not bind an artifact to a tree; record the
    # firmware identity the boards report plus the host-side source fingerprint
    # so a later reader can tell whether this evidence belongs to this tree.
    payload = {"tag": tag, "action": action, "gen": gen, "epoch": epoch,
               "stamp": stamp, "host": {}, "boards": [],
               "ring_samples": [[round(t, 3), s] for t, s in (ring_samples or [])],
               "cseg": cseg}
    try:
        from tools.hardware_acceptance.source_scope import (
            working_source_fingerprint)
        digest, count = working_source_fingerprint()
        payload["host"]["fingerprint"] = digest
        payload["host"]["fingerprint_files"] = count
    except Exception as exc:  # pragma: no cover - diagnostic only
        payload["host"]["fingerprint"] = f"unavailable: {exc}"
    payload["host"]["python"] = sys.version.split()[0]
    for b in boards:
        payload["boards"].append({
            "no": b.no,
            "idn": b.cmd("*IDN?"),
            "build": b.cmd("SYSTem:FW:BUILD?"),
            "plan": b.cmd("SYST:TDMA:GUIDE:PLAN?"),
            "ring": b.cmd("SYST:TDMA:RING:STATus?"),
            "ring_cfg": b.cmd("SYST:TDMA:RING:CONFig?"),
            "phase": b.cmd("SYST:TDMA:RING:PHASE?"),
            "arm": b.cmd("SYST:TDMA:RING:ARM:STATus?"),
        })
    with open(os.path.join(root, "readback.json"), "w", encoding="utf-8") as fh:
        _json.dump(payload, fh, ensure_ascii=False, indent=2)
    print(f"{now()} evidence -> {root}")


def verify(action: int, epoch: int, gen: int = 0, repeats: int = 3,
           archive: bool = False) -> bool:
    """Strict verdict for one action: freshness, terminal state, no abort.

    The PLAN? progress fields keep the *previous* Q reading until this round
    reaches Q and carry no epoch of their own, and the Q record is never cleared
    on a new trigger, so a plain "status==1 and failed_mask==0" check (or a
    reused epoch) can report an old success as a new one.  This requires this
    round's own epoch (and generation when given), a finished executor, the
    action's own trial count and pass mask, a cleared abort site and the
    action's own profile for T3.  Returns True only when every board passes.
    """
    boards = open_boards()
    ok = True
    lines: list[str] = []
    # Trial count and pass mask are set by the executor, not by the plan for
    # every action: P1 sets one bit per trial (4), P3 runs nodes x repeats,
    # T0 runs t0_repeats (candidates[4]), T1/T2 use the plan's trial_count and
    # T3 is forced to a single trial.
    if action == 1:
        want_trials = 4
    elif action == 3:
        want_trials = 4 * max(1, repeats)
    elif action == 4:
        want_trials = max(1, repeats)
    elif action == 7:
        want_trials = 1
    else:
        want_trials = 0  # taken from the plan below
    for b in boards:
        v = b.plan()
        if len(v) < 25:
            print(f"NO.{b.no} <short plan>")
            ok = False
            continue
        trials = want_trials if want_trials > 0 else (v[6] if v[6] > 0 else 1)
        want_mask = (1 << trials) - 1 if trials < 32 else 0xFFFFFFFF
        fresh = v[8] == epoch and (gen == 0 or v[3] == gen)
        same_action = v[1] == action
        # 28 = Q, 29 = DONE: the round published its result.  0 = IDLE is also
        # a valid end state (the round retires to IDLE), but on its own it does
        # not prove the round ran, so require the trigger to be consumed and at
        # least one trial recorded for this action.
        idle = v[9] in (0, 28, 29) and v[7] == 0 and v[12] > 0
        prog_valid = v[10] == 1
        complete = v[11] == 1
        counted = v[12] == want_trials
        # P2/T1/T2 take the plan's own trial_count; T3 forces a single trial and
        # must not be overridden by the plan, which plan_valid never checks.
        if action in (2, 5, 6) and v[6] > 0:
            want_trials = v[6]
            want_mask = (1 << want_trials) - 1 if want_trials < 32 else 0xFFFFFFFF
            counted = v[12] == want_trials
        passed_ok = v[22] == want_mask
        no_fail = v[23] == 0
        no_abort = v[24] == 0
        reconciled = v[15] == 1  # GUIDE_ACTION_RECONCILE_COMPLETE
        # T3 must have run at the measurement profile.  RING:STATUS? cannot
        # prove it: its tail field is sample_count (mean_period_ns is the
        # second-to-last), and closing the window replays the boot profile and
        # clears loop_time, so a 4 ms loop is not observable afterwards.  The
        # plan's own level is the verifiable statement (7 = 10 MHz / 1 ms).
        rate_ok = True
        if action == 7:
            rate_ok = v[4] == 7
        good = (fresh and same_action and idle and prog_valid and complete and
                counted and passed_ok and no_fail and no_abort and reconciled
                and rate_ok)
        ok = ok and good
        line = (f"NO.{b.no} {'PASS' if good else 'FAIL'} "
              f"fresh={fresh}(epoch={v[8]}/{epoch} gen={v[3]}/{gen}) "
              f"action={v[1]}/{action} idle={idle} pvalid={prog_valid} "
              f"status={v[11]} rec={v[15]} trials={v[12]}/{want_trials} "
              f"passed=0x{v[22]:X}/0x{want_mask:X} failed=0x{v[23]:X} "
              f"abort_site={v[24]} rate_ok={rate_ok}")
        lines.append(line)
    for b in boards:
        b.close()
    print(f"verify={'PASS' if ok else 'FAIL'} action={action}")
    if archive:
        import json as _json
        repo_root = os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__))))
        root = os.path.join(repo_root, 'out', 'calibration', 'guide-10k',
                            'evidence',
                            time.strftime('%Y%m%d-%H%M%S') + f'-verify-{action}')
        os.makedirs(root, exist_ok=True)
        with open(os.path.join(root, 'verdict.json'), 'w',
                  encoding='utf-8') as fh:
            _json.dump({'action': action, 'epoch': epoch, 'gen': gen,
                        'repeats': repeats, 'pass': ok, 'lines': lines},
                       fh, ensure_ascii=False, indent=2)
        print(f"{now()} verdict -> {root}")
    return ok


def ladder_state(boards):
    """Read every board's ladder index/step.

    Ladder divergence is the single most expensive failure mode on the bench:
    one board advances on a no-op pass while another fails, then every later
    action is refused by plan_valid with expected_action = index + 1.  Check it
    before every action instead of discovering it three actions later.
    """
    out = []
    for b in boards:
        v = b.plan()
        out.append((b.no, v[20] if len(v) > 20 else -1,
                    v[21] if len(v) > 21 else -1))
    return out


def ladder_ok(boards, action: int) -> bool:
    """Require every board to agree on the expected index.

    Only the index is compared: the step is a per-board decision (DONE vs
    STOPPED) and a board that failed the previous action legitimately differs
    there while still accepting a retry of the same action (plan_valid treats
    action == index as repeat_last_action).
    """
    state = ladder_state(boards)
    idx = {s[1] for s in state}
    ok = len(idx) == 1 and state[0][1] == action - 1
    print(f"{now()} ladder {state} expected_index={action - 1} "
          f"{'OK' if ok else 'DIVERGED'}")
    return ok


def window_rate(tag: str, samples: list) -> None:
    """Report which RING:STATUS? field grows fastest during the window.

    The loop-period fields are structurally zero without a flight image, so the
    only in-window evidence for the carrier is a monotonic counter: at 10 MHz a
    four-node ring advances ~250 frames/s, at 10 kHz ~6.7/s (a 150x gap that is
    unambiguous even with coarse sampling).  Printing the fastest-growing field
    keeps the index discovery empirical instead of hard-coded.
    """
    if len(samples) < 2:
        print(f"{now()} rate[{tag}] no samples")
        return
    first = samples[0]
    last = samples[-1]
    dt = last[0] - first[0]
    if first[1] and isinstance(first[1][0], list):
        # Per-board samples: report the carrier and the fastest counter
        # growth for each board, which is what the milestone needs.
        carriers = {}
        for entry in last[1]:
            no, values = entry[0], entry[1]
            carriers[no] = (values[-2] if len(values) >= 2 else -1,
                            values[-1] if len(values) >= 1 else -1,
                            values[0] if values else -1,
                            values[4] if len(values) > 4 else -1,
                            values[5] if len(values) > 5 else -1)
        print(f"{now()} carrier[baud,period,enabled,up,down] {carriers}")
        for entry in last[1]:
            no, values = entry[0], entry[1]
            before = next((e[1] for e in first[1] if e[0] == no), None)
            if before is None:
                continue
            ranked = sorted(
                ((values[i] - before[i], i)
                 for i in range(min(len(values), len(before)))),
                reverse=True)[:2]
            print(f"{now()} growth[NO.{no}] dt={dt:.1f}s top={ranked}")
        return
    if dt <= 0:
        return
    best = (0, -1)
    for idx in range(min(len(first[1]), len(last[1]))):
        delta = last[1][idx] - first[1][idx]
        if delta > best[0]:
            best = (delta, idx)
    # Only frame/service counters: the carrier fields (47/48) jump from 0
    # to the armed values once and would otherwise dominate the ranking.
    frame_fields = (9, 10, 11, 24, 25)
    ranked = sorted(
        ((last[1][i] - first[1][i], i) for i in frame_fields
         if i < len(first[1]) and i < len(last[1])),
        reverse=True)[:3]
    print(f"{now()} rate[{tag}] dt={dt:.1f}s top={ranked} "
          f"(10MHz~250/s, 10kHz~6.7/s) idx={best[1]} "
          f"rate={best[0] / dt:.1f}/s")


def status() -> None:
    boards = open_boards()
    for b in boards:
        print(ring_line(b))
        print(f"  {plan_line(b)}")
        print(f"  CMD: {cmd_summary(b.cmdstat())}")
    for b in boards:
        b.close()


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    what = argv[1]
    if what == "idn":
        for b in open_boards():
            b.close()
        return 0
    if what == "stage":
        level = int(argv[2]) if len(argv) > 2 else LEVEL_10K
        configure_level(level)
        return 0
    if what == "up":
        bring_up()
        return 0
    if what == "down":
        tear_down()
        return 0
    if what == "status":
        status()
        return 0
    if what == "train":
        train_snapshot()
        return 0
    if what == "phases":
        phases()
        return 0
    if what == "plan":
        show_plan()
        return 0
    if what == "verdict":
        verdict()
        return 0
    if what == "verify":
        if len(argv) < 4:
            print("usage: verify <action> <epoch> [gen] [repeats]")
            return 2
        action = int(argv[2])
        epoch = int(argv[3])
        gen = int(argv[4]) if len(argv) > 4 else 0
        repeats = int(argv[5]) if len(argv) > 5 else 3
        if epoch == 0:
            print("verify: epoch 0 is rejected (CONFig clears the epoch; a stale "
                  "Q would then look like this round)")
            return 2
        archive = len(argv) > 6 and argv[6] not in ('0', 'no', 'false')
        return 0 if verify(action, epoch, gen, repeats, archive) else 1
    if what == "p2":
        gen = int(argv[2]) if len(argv) > 2 else 700
        epoch = int(argv[3]) if len(argv) > 3 else 800
        p2(gen, epoch, int(argv[4]) if len(argv) > 4 else 8000)
        return 0
    if what == "ga":
        action = int(argv[2]); gen = int(argv[3]); epoch = int(argv[4])
        level = int(argv[5]); window = int(argv[6]); trials = int(argv[7])
        cands = [int(x) for x in argv[8:-2]]
        lo = int(argv[-2]); hi = int(argv[-1])
        guide_action(action, gen, epoch, level, window, trials, cands, lo, hi)
        return 0
    if what == "stateprobe":
        gen = int(argv[2]) if len(argv) > 2 else 500
        epoch = int(argv[3]) if len(argv) > 3 else 600
        stateprobe(gen, epoch, float(argv[4]) if len(argv) > 4 else 90.0)
        return 0
    if what == "trainprobe":
        trainprobe(int(argv[2]) if len(argv) > 2 else 4,
                   int(argv[3]) if len(argv) > 3 else 3,
                   len(argv) > 4 and argv[4] == 'fwd')
        return 0
    if what == "cfg":
        cmd_probe(int(argv[2]) if len(argv) > 2 else 200)
        return 0
    if what == "trig":
        trigger_probe(int(argv[2]) if len(argv) > 2 else 300)
        return 0
    if what == "watch":
        watch(int(argv[2]) if len(argv) > 2 else 201,
              float(argv[3]) if len(argv) > 3 else 70.0)
        return 0
    if what == "ladder":
        action = int(argv[2]) if len(argv) > 2 else 1
        gen = int(argv[3]) if len(argv) > 3 else 210
        epoch = int(argv[4]) if len(argv) > 4 else 310
        ladder(action, gen, epoch)
        return 0
    if what == "trace":
        action = int(argv[2]) if len(argv) > 2 else 1
        gen = int(argv[3]) if len(argv) > 3 else 220
        epoch = int(argv[4]) if len(argv) > 4 else 320
        trace(action, gen, epoch, len(argv) > 5 and argv[5] == "reset")
        return 0
    print(f"unknown subcommand {what}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
