"""T2 bring-up workflow: addressable steps from calibration self-check to publication.

One code path, 22 steps (0..21), each runnable on its own, in ranges or continuously:

  --list                     step table
  --step 6                   exactly one step, whole chain
  --steps 1-6 / 3,7,12,16-19  an arbitrary selection, in order
  --steps all | --full       the continuous chain
  --interactive              wait for Enter between steps

Every step reports its own success/failure with a named reason, its wall-clock cost
and (for the expensive ones) the commands that dominated it; the whole session is
teed into out/tdma_t2/steps-<stamp>.log.

Structure by design (measured, not assumed):
  * steps 1-13 are stopped-state work (configuration and mounting): the firmware
    refuses all of it once the ring is enabled (tdma_service.c:1033-1035/1079-1081),
    and the T2 mount has to precede the stopped-state writes that invalidate a
    binding (T2:CONFigure diag 91/92/93);
  * step 14 configures the turntable and the counter source but does not start them;
  * steps 16/17 start the ring and the time base, step 18 requires a valid cursor;
  * step 19 starts the pulse flow only now, and refuses when the counter has already
    passed a position threshold (Core0 never previews a passed position,
    trigger_t2_runtime.c:919-924);
  * step 21 stops everything again.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import time
from types import SimpleNamespace

sys.path.insert(0, r"D:\\Work\\HDRT100")
from tools.tdma_ring_monitor.tdma_start_ring import (  # noqa: E402
    board_command, discover, close_persistent_connections)

ROLES = ("COUNTER", "SWITCH_A", "GATEWAY", "SWITCH_B")
ROLE_SLOT = (0, 1, 2, 3)
# Bench mapping from the T2 architecture: NO1 COUNTER (IN1), NO2 SWITCH_A (OUT1-3),
# NO3 GATEWAY (OUT4 + IN1 READY), NO4 SWITCH_B (OUT1). Masks are 4-line local.
def action_row(slot: int) -> tuple[int, int, int]:
    """Role, kind and argument of the action row a board installs.

    The firmware grammar (trigger_action_table.h) accepts only two kinds:
    TRIGGER_ACTION_SWITCH_CODE (1) for the SWITCH_A/SWITCH_B roles and
    TRIGGER_ACTION_GATEWAY_PULSE (2) for the GATEWAY role. The COUNTER role (NO1,
    the position source) has no action kind of its own, so it carries a valid
    GATEWAY row instead of an invalid row for itself - its table only has to
    validate, it never executes a local action.
    """
    if slot == 0:                     # COUNTER: no action kind of its own
        return 2, 2, 20
    if slot == 2:                     # GATEWAY: pulse width in the units below
        return 2, 2, 20
    return slot, 1, 0                 # SWITCH_A (1) / SWITCH_B (3): switch code


ROWS = (
    # COUNTER rows may not declare outputs (trigger_deployment_validate); the OUT4
    # turntable source loops into IN1 outside the deployment table.
    (0x0010071E65B5CB38, 0, 0x1, 0x0, 0),
    (0xFB276192BEF9CCE1, 1, 0x0, 0x7, 0),
    (0x2BD5090FE009FA2A, 2, 0x1, 0x8, 1),
    (0xA1E549202D18ED6A, 3, 0x0, 0x1, 0),
)


def crc_word(crc: int, word: int) -> int:
    for _ in range(4):
        crc ^= word & 0xFF
        word >>= 8
        for _ in range(8):
            crc = ((crc >> 1) ^ (0xEDB88320 if (crc & 1) else 0)) & 0xFFFFFFFF
    return crc


def deployment_crc(generation: int, rows) -> int:
    crc = crc_word(0xFFFFFFFF, generation & 0xFFFFFFFF)
    for uid, slot, imask, omask, fb in rows:
        for word in (uid & 0xFFFFFFFF, (uid >> 32) & 0xFFFFFFFF, slot, imask, omask, fb):
            crc = crc_word(crc, word & 0xFFFFFFFF)
    return crc ^ 0xFFFFFFFF


def action_table_crc(generation: int, dep_generation: int, dep_crc: int, grid_epoch: int,
                     grid_generation: int, anchor: int, period_ns: int, subdivisions: int,
                     rows) -> int:
    header = (generation, dep_generation, dep_crc, grid_epoch, grid_generation,
              anchor & 0xFFFFFFFF, (anchor >> 32) & 0xFFFFFFFF, period_ns, subdivisions, len(rows))
    crc = 0xFFFFFFFF
    for word in header:
        crc = crc_word(crc, word & 0xFFFFFFFF)
    for action_id, role, kind, argument, ordinal, span in rows:
        for word in (action_id, role, kind, argument, ordinal & 0xFFFFFFFF,
                     (ordinal >> 32) & 0xFFFFFFFF, span):
            crc = crc_word(crc, word & 0xFFFFFFFF)
    return crc ^ 0xFFFFFFFF


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", default="0010071E65B5CB38")
    parser.add_argument("--nodes", type=int, default=4,
                        help="bench node count: the freeze target mask is every node "
                             "except the position source")
    parser.add_argument("--epoch", type=int, default=1)
    parser.add_argument("--generation", type=int, default=1)
    parser.add_argument("--anchor", type=int, default=0)
    parser.add_argument("--period-ns", type=int, default=1000000)
    parser.add_argument("--subdivisions", type=int, default=2)
    parser.add_argument("--stage", action="store_true", help="send grid + deployment")
    parser.add_argument("--actions", action="store_true", help="install the action table")
    parser.add_argument("--t2", action="store_true", help="configure and start the T2 runtime")
    parser.add_argument("--observe", action="store_true", help="read the position transport page")
    parser.add_argument("--full", action="store_true",
                        help="per-board whole chain incl. fifo reset, T2 configure/start, then observe")
    parser.add_argument("--list", action="store_true",
                        help="print the step table and exit")
    parser.add_argument("--step", type=int, default=None,
                        help="run exactly one step (1..20) for the whole chain")
    parser.add_argument("--steps", default=None,
                        help="run a step selection, e.g. '1-6', '3,7', '4-9,16'; "
                             "'all' or --full runs the continuous chain")
    parser.add_argument("--interactive", action="store_true",
                        help="with --steps/--full: wait for Enter between steps")
    parser.add_argument("--dep-generation", type=int, default=None,
                        help="pin the run identity (grid epoch / deployment generation / "
                             "VDC session / T2 run_id) so single-step invocations stay "
                             "coherent; default is the current time")
    parser.add_argument("--refresh-html", action="store_true",
                        help="re-inject the newest run's data into the HTML view and exit")
    parser.add_argument("--pre-lock-mask", type=int, default=None,
                        help="EXPERIMENT: assert this LOAD:MASK on all boards at the end "
                             "of step 16 (before the lock wait), e.g. 91 to include the "
                             "sync-trigger bit 6 that the normal chain withholds until "
                             "the lock")
    args = parser.parse_args()

    if args.refresh_html:
        return refresh_html_view()

    if args.list:
        for number in sorted(STEP_TITLES):
            print(f"{number:>3}  {STEP_TITLES[number]}")
        return 0

    if args.step is not None:
        return run_steps([f"{row[0]:016X}" for row in ROWS], [args.step],
                         interactive=args.interactive,
                         generation=args.dep_generation,
                         pre_lock_mask=args.pre_lock_mask)
    if args.steps is not None:
        return run_steps([f"{row[0]:016X}" for row in ROWS], parse_selection(args.steps),
                         interactive=args.interactive,
                         generation=args.dep_generation,
                         pre_lock_mask=args.pre_lock_mask)
    if args.full:
        # One code path: the step engine, run continuously over all 21 steps (the
        # legacy run_full remains only as a reference implementation).
        return run_steps([f"{row[0]:016X}" for row in ROWS], parse_selection("all"),
                         interactive=args.interactive,
                         pre_lock_mask=args.pre_lock_mask)
    crc = deployment_crc(args.generation, ROWS)
    print("deployment crc =", crc)
    for uid, slot, imask, omask, fb in ROWS:
        print("  row", slot, ROLES[slot], hex(uid), "in", hex(imask), "out", hex(omask), "fb", fb)

    ctx = SimpleNamespace(board_ids=[args.board], baud=115200, timeout=8.0,
                          read_timeout=0.02, settle=0.02, keep_open=True, short_open=False)
    boards = discover(ctx)
    board = boards.get(args.board)
    if board is None:
        print("board not found")
        return 1

    def q(command: str) -> str:
        try:
            return board_command(board, command, ctx).strip()
        except Exception as exc:  # noqa: BLE001
            return f"<{type(exc).__name__}>"

    print("STOP ->", q("SYSTem:TDMA:RING:STOP")[:12])
    if args.stage:
        print("GRID ->", q(f"TRIGger:REServation:GRID:CONFigure {args.epoch},{args.generation},"
                           f"{args.anchor},{args.period_ns},{args.subdivisions}")[:24])
        payload = ",".join(f"{uid},{slot},{imask},{omask},{fb}"
                           for uid, slot, imask, omask, fb in ROWS)
        print("STAGe ->", q(f"TRIGger:REServation:DEPloyment:STAGe {args.generation},{crc},{payload}")[:24])
        print("ACTivate ->", q(f"TRIGger:REServation:DEPloyment:ACTivate {args.generation},{crc}")[:24])
    if args.actions:
        rows = [(index + 1, index, (0, 1, 2, 1)[index], 0, 0, 1) for index in range(4)]
        act_crc = action_table_crc(args.generation, args.generation, crc, args.epoch,
                                   args.generation, args.anchor, args.period_ns,
                                   args.subdivisions, rows)
        print("action crc =", act_crc)
        flat = ",".join(str(v) for row in rows for v in row)
        print("ACTion ->", q(f"TRIGger:REServation:ACTion:INSTall {args.generation},{act_crc},"
                             f"{args.generation},{crc},{args.epoch},{args.generation},{args.anchor},"
                             f"{args.period_ns},{args.subdivisions},{len(rows)},{flat}")[:24])
        print("ACTion? ->", q("TRIGger:REServation:ACTion?")[:80])
    if args.t2:
        # The freeze target mask must EXCLUDE the position source slot: the
         # acknowledgement path can only fill a slot from a received image, so a
         # mask that carries the source slot can never complete and the origin
         # resends every position until its window closes.
        source_slot = 0
        target_mask = ((1 << args.nodes) - 1) & ~(1 << source_slot)
        if not target_mask:
            print("T2: note: single-node bench has no freeze peer to wait for")
        print("T2:CONFigure ->", q(f"TRIGger:REServation:T2:CONFigure 1,{source_slot},"
                                   f"{target_mask},1,0,0,999,0,0,0,5000,1000")[:24])
        print("T2:STARt ->", q("TRIGger:REServation:T2:STARt")[:24])
        print("T2? ->", q("TRIGger:REServation:T2?")[:90])
    if args.observe:
        print("POSition ->", q("SYSTem:REFMEM:SYNC:FLIGHT:POSition?")[:140])
        print("FLIGHT syn ->", q("SYSTem:REFMEM:SYNC:FLIGHT?")[:120])
    print("GRID? ->", q("TRIGger:REServation:GRID?")[:90])
    print("DEPloyment? ->", q("TRIGger:REServation:DEPloyment?")[:120])
    close_persistent_connections()
    return 0




def run_full(board_ids) -> int:
    """Benchtop chain: stopped-state configuration -> T2 first -> turntable -> VDC -> ring.

    Order matters: every configuration command belongs to the stopped window, T2 must be
    running before the turntable source is started, and the VDC run supplies the time
    base the T2 cursor needs.
    """
    ctx = SimpleNamespace(board_ids=list(board_ids), baud=115200, timeout=8.0,
                          read_timeout=0.02, settle=0.02, keep_open=True, short_open=False,
                          action_timeout=0.25)
    boards = discover(ctx)
    print("discovered:", sorted(boards.keys()))
    src = boards.get("0010071E65B5CB38")
    # DEPloyment:CLEar keeps last_generation, so every run stages a newer one.
    dep_gen = int(time.time()) & 0xFFFFFFFF
    dep_crc = deployment_crc(dep_gen, ROWS)
    payload = ",".join(f"{uid},{slot},{imask},{omask},{fb}"
                       for uid, slot, imask, omask, fb in ROWS)
    kinds = {0: 0, 1: 1, 2: 2, 3: 1}
    pulses = {2: 20}

    def set_load_mask(board, mask, label):
        """Write the Core1 realtime load mask and verify it stuck.

        Bit 6 (APP_REALTIME_LOAD_SYNC_TRIGGER) is what schedules
        app_realtime_sync_trigger_phase(); when the write is dropped the T2 service
        never runs (segments 0/0/0) and nothing is published, which is exactly the
        difference between the one passing cycle and the failing repeats (measured:
        LOAD:MASK? read back 27 after the failure, 91 in the passing run).
        """
        for _attempt in range(12):
            send(board, f"SYSTem:TDMA:LOAD:MASK {mask}")
            _mask = send(board, "SYSTem:TDMA:LOAD:MASK?")
            _value = _mask.split(",")[0].strip() if _mask else ""
            if _value.isdigit() and (int(_value) & mask) == mask:
                return True
            time.sleep(0.25)
        print(f"  {label} WARNING: load mask stayed {_value} (wanted {mask})", flush=True)
        return False

    def set_priority(board, command, page, label, retries=12):
        """Write a VDC priority setting and verify it by readback.

        These setters refuse silently for state reasons (session/priority generation
        mismatch, in-flight boundary request), which is exactly how the FOLLow step
        went missing in the failing cycles.
        """
        for _attempt in range(retries):
            send(board, command)
            _value = send(board, page).split(",")[0].strip()
            if _value.isdigit() and int(_value) != 0:
                return True
            time.sleep(0.5)
        print(f"  {label} WARNING: readback stayed {_value}", flush=True)
        return False

    def send(board, command):
        try:
            return board_command(board, command, ctx).strip()
        except Exception as exc:  # noqa: BLE001
            return f"<{type(exc).__name__}>"

    # (1) stopped-state configuration per board; T2 configure + start at the end
    for slot, (uid, _slot, _i, _o, _f) in enumerate(ROWS):
        board = boards.get(f"{uid:016X}")
        if board is None:
            print("missing board", hex(uid))
            continue
        send(board, "SYSTem:TDMA:RING:STOP")
        # Wait for the physical stop before anything else: the load-mask write and
        # the ring configuration are both refused while the previous cycle's ring is
        # still enabled/started, which silently left segments 0/0/0 and published
        # nothing on a back-to-back run.
        for _poll in range(40):
            _ring = send(board, "SYSTem:TDMA:RING:STATus?").split(",")
            if len(_ring) > 8 and _ring[0] == "0" and _ring[8] == "0":
                break
            time.sleep(0.25)
        else:
            print(f"  slot {slot} WARNING: ring not stopped before configuration: "
                  f"{_ring[:9]}", flush=True)
        send(board, "SYSTem:VDC:OUTPut:RUN:STOP")
        # A T2 run left ACTIVE refuses every stopped-state configuration without
        # replying, which surfaces only as a timeout: stop it first and verify.
        send(board, "TRIGger:REServation:T2:STOP")
        send(board, "MODEl:TURNtable:STOP")
        send(board, "TRIGger:REServation:COUNter:STOP")
        for _attempt in range(12):
            _state = send(board, "TRIGger:REServation:T2?").split(",")
            _active = _state[3] if len(_state) > 3 else "?"
            if _active == "0":
                break
            time.sleep(0.25)
        else:
            print(f"  slot {slot} WARNING: T2 still active before configuring: {_state[:4]}",
                  flush=True)
        # Ring/depoyment linkage, in the order the firmware preconditions require:
        #  * vdc_dpll_manager_set_tdma_ring_topology() refuses while a VDC feedback
        #    session is live, so a session left by an earlier run (or by the P3
        #    chain) has to be closed first;
        #  * the ring topology is staged while stopped and ARM enables it;
        #  * a deployment binding is only current while that staged ring config is
        #    enabled (current_binding(): staged.enabled, ring.config_seq ==
        #    binding.ring_config, staged.local_slot_id/schedule_crc32 == binding),
        #    otherwise ACTion:INSTall answers TRIGGER_ACTION_SERVICE_CHANGED (35)
        #    and T2:CONFigure fails its template prerequisite (93).
        send(board, "SYSTem:VDC:FEEDback:SESSion 0")
        for _attempt in range(12):
            if send(board, "SYSTem:VDC:FEEDback:SESSion?") == "0":
                break
            time.sleep(0.25)
        else:
            print(f"  slot {slot} WARNING: VDC feedback session still live", flush=True)
        # Probe reset block (t2_bench_probe.py lines 200-207): the load mask and
        # flight mode gate position traffic, the priority class is cleared before
        # it is re-armed, and the feedback reference/local-follow roles are reset.
        # Deliberately 27 here, not 91: enabling the sync-trigger phase before the
        # ring is up makes the realtime loop run the T2/cursor path during the ring
        # bring-up, and the ARM then fails (measured: cursor err 7 IDENTITY, seq 2,
        # segments 0/0/0 for the whole cycle). Bit 6 is asserted after RING:START,
        # which is where the one passing cycle had it.
        # Order taken from the ONE run whose command log is known to work
        # (out/HardwareAcceptance/20261011/dpll60-110200/.../input-probe.json):
        #   OUTPut:TIMing <profile> -> priority clears -> SESSion 0 ->
        #   FEEDback:REFerence/LOCALfollow 0 -> DIAGnostic 1 -> FIFO:RESet ->
        #   RING:BURSt 0
        # The load mask here carries only the static phases (27); bit 6
        # (APP_REALTIME_LOAD_SYNC_TRIGGER) IS the dynamic T2 start and is asserted
        # after RING:START once the VDC phase lock has been observed stable - see
        # the lock-detection block below. Asserting it during bring-up makes the
        # sync-trigger phase participate while the ring is still starting.
        send(board, "SYSTem:VDC:OUTPut:TIMing 24000,32000,16000")
        for _suffix in ("FOLLow:PHASe", "FOLLow", "SYNC", "MATCH"):
            send(board, f"SYSTem:VDC:PRIORity:{_suffix} 0")
        send(board, "SYSTem:VDC:FEEDback:REFerence 0")
        send(board, "SYSTem:VDC:FEEDback:LOCALfollow 0")
        set_load_mask(board, 27, f"slot {slot}")
        send(board, "SYSTem:TDMA:FLIGHT:MODE 1")
        # The topology stages the ring config (RING:CONFig? then reports
        # enabled=1 with this board's local_slot_id and the deployment's
        # schedule_crc32), which is what current_binding() checks. ARM is NOT sent
        # here: it makes the later grid/deployment/action configuration fail.
        _status = send(board, "SYSTem:TDMA:RING:STATus?").split(",")
        _before_seq = _status[38] if len(_status) > 39 else None
        # Canonical stopped-state preparation (calibration_clk_coded.py:470-483):
        # without the topology probe phase delay the ring ARM is refused with
        # TDMA_PIO_SPI_PHYS_ERROR_OWNER_TOPOLOGY_PHASE (measured adapter error 18)
        # because tdma_runtime_owner.c:138 requires a phase in 1..31 whenever the
        # calibration stage is not enabled; the operating mode stage/apply sets the
        # calibration level the ring schedule was derived from.
        # NOTE: the training stage is deliberately NOT cleared here. The
        # calibration stage is established by the acceptance/calibration flow; the
        # T2 bring-up only has to satisfy the stopped-state gates, and clearing it
        # leaves the stage incomplete, which makes the origin trial reject with
        # STAGE_INCOMPLETE and the provisional DPLL training refuse (measured).
        send(board, "CALibration:TOPology:PROBe 1,1")
        send(board, "SYSTem:TDMA:OPMode:STAGe 7")
        send(board, "SYSTem:TDMA:OPMode:APPLy")
        send(board, "SYSTem:TDMA:RING:GEOMetry 0")
        send(board, f"SYSTem:TDMA:RING:TOPology {len(ROWS)},{slot},0")
        # The staged topology is committed asynchronously, and bind_local() copies
        # ring.config_seq into the deployment binding when the deployment is
        # activated. Activating before that commit leaves the binding permanently
        # stale: current_binding() then rejects with its code 44 and
        # ACTion:INSTall answers TRIGGER_ACTION_SERVICE_CHANGED (35), so the
        # origin never reaches T2:CONFigure. Wait for the commit first, exactly
        # like the calibration flow's _wait_ring_state(after_config_seq=...).
        for _attempt in range(24):
            _status = send(board, "SYSTem:TDMA:RING:STATus?").split(",")
            _applied = (len(_status) > 39 and _status[38] == _status[39] and
                        True)  # equality only: a repeated topology does not advance the seq
            if _applied:
                break
            time.sleep(0.25)
        else:
            print(f"  slot {slot} WARNING: ring config not committed: "
                  f"{_status[38:40] if len(_status) > 39 else _status[:4]}", flush=True)
        # Now that the staged ring config exists and is enabled, the diagnostic
        # continuation flag can be set: tdma_service_set_ring_diagnostic() refuses
        # while ring_staged_config.enabled == 0, and without this flag the origin
        # trial is rejected with CALIBRATION_ORIGIN_ATTEMPT_DIAGNOSTIC_REQUIRED
        # (reason 9) - which is what authorises position publication.
        send(board, "SYSTem:TDMA:RING:DIAGnostic 1")
        if send(board, "SYSTem:TDMA:RING:DIAGnostic?") != "1":
            print(f"  slot {slot} WARNING: diagnostic-continue flag not set "
                  f"(origin trial will be refused)", flush=True)
        # Provisional DPLL training: without it the T2 cursor stays invalid
        # (last_error 5 = CURSOR) and no position deadline can be planned.
        send(board, "SYSTem:SYNC:VDC:DPLL:PROVisional 1")
        _prov = send(board, "SYSTem:SYNC:VDC:DPLL:PROVisional?").split(",")[:1]
        print(f"  slot {slot} provisional -> {_prov}", flush=True)
        _cfg = send(board, "SYSTem:TDMA:RING:CONFig?")
        print(f"  slot {slot} RING:CONFig? -> {_cfg[:70]} seq="
              f"{_status[38] if len(_status) > 38 else '?'}", flush=True)
        send(board, "TRIGger:REServation:COUNter:CLEar")
        send(board, "TRIGger:REServation:COUNter:UNLoad")
        send(board, "TRIGger:REServation:DEPloyment:CLEar")
        send(board, "TRIGger:REServation:GRID:CLEar")
        send(board, f"TRIGger:REServation:GRID:CONFigure {dep_gen},{dep_gen + 1},0,1000000,2000")
        # Clear both stores BEFORE the activation: clearing a non-empty store is a
        # stopped-metadata change that advances ring.config_seq, and bind_local()
        # copies that sequence into the deployment binding at activation time. A
        # clear after the activation therefore makes the binding stale
        # (current_binding() code 44) and ACTion:INSTall then answers CHANGED (35).
        send(board, "TRIGger:REServation:TEMPLate:RELease")
        send(board, "TRIGger:REServation:ACTion:CLEar")
        send(board, "TRIGger:REServation:TEMPLate:CLEar")
        for _attempt in range(8):
            _action = send(board, "TRIGger:REServation:ACTion?").split(",")
            _installed = _action[2] if len(_action) > 2 else "?"
            if _installed == "0":
                break
            time.sleep(0.25)
        else:
            print(f"  slot {slot} WARNING: action table still installed: {_action[:4]}",
                  flush=True)
        # bind_local() snapshots ring.config_seq into the deployment binding, so the
        # activation has to happen after the ring config stops moving and must be
        # verified: any later commit makes the binding stale, current_binding()
        # then rejects with its code 44 and ACTion:INSTall answers CHANGED (35).
        # Re-wait and re-activate until the binding matches the live sequence.
        for _attempt in range(6):
            _status = send(board, "SYSTem:TDMA:RING:STATus?").split(",")
            for _poll in range(24):
                _status = send(board, "SYSTem:TDMA:RING:STATus?").split(",")
                if (len(_status) > 39 and _status[38] == _status[39]):
                    break
                time.sleep(0.25)
            _seq = _status[38] if len(_status) > 38 else "?"
            send(board, f"TRIGger:REServation:DEPloyment:STAGe {dep_gen},{dep_crc},{payload}")
            send(board, f"TRIGger:REServation:DEPloyment:ACTivate {dep_gen},{dep_crc}")
            _dep = send(board, "TRIGger:REServation:DEPloyment?").split(",")
            _bound = _dep[9] if len(_dep) > 9 else None
            if _bound == _seq:
                break
            time.sleep(0.3)
        else:
            print(f"  slot {slot} WARNING: deployment ring_config {_bound} != ring seq {_seq}",
                  flush=True)
        print(f"  deploy after activate: {_dep[2:7]} ring_config={_bound} seq={_seq}"
              " (active?, last_gen, staged?, staged_crc, active_crc)")
        role, kind, arg = action_row(slot)
        # EVERY board installs an action table, the origin included: T2:CONFigure
        # requires a current template store on all of them (diag 93 otherwise) and
        # the template install is validated against this table. The origin does not
        # execute its own positions, but it still has to pass the same gate.
        # install_stopped() refuses a table whose generation is <= last_generation
        # unless it is byte-identical with the same binding, and clear_stopped()
        # deliberately preserves last_generation. A hard-coded generation 1
        # therefore installs once and then fails forever with
        # TRIGGER_ACTION_SERVICE_GENERATION (36) -> T2:CONFigure 93. Use the
        # run-unique deployment generation.
        acrc = action_table_crc(dep_gen, dep_gen, dep_crc, 1, 1, 0, 1000000, 2,
                                [(1, role, kind, arg, 0, 1)])
        reply = send(board, f"TRIGger:REServation:ACTion:INSTall {dep_gen},{acrc},{dep_gen},{dep_crc},"
                                f"1,1,0,1000000,2,1,1,{role},{kind},{arg},0,1")
        print(f"  slot {slot} ACTion:INSTall -> {reply[:60]}")
        rows = [(1, dep_gen, role, kind, arg, 1, 0)]
        tcrc = template_crc(dep_gen, rows)
        reply = send(board, f"TRIGger:REServation:TEMPLate:INSTall {dep_gen},{tcrc},1,"
                                + ",".join(str(v) for row in rows for v in row))
        print(f"  slot {slot} TEMPlate:INSTall -> {reply[:60]}")
        reply = send(board, "TRIGger:REServation:TEMPLate:ACTivate")
        print(f"  slot {slot} TEMPlate:ACTivate -> {reply[:60]}")
        print(f"  slot {slot} TEMPlate? -> {send(board, 'TRIGger:REServation:TEMPLate?')[:80]}")
        _action_page = send(board, "TRIGger:REServation:ACTion?").split(",")
        _template_page = send(board, "TRIGger:REServation:TEMPLate?").split(",")
        _diag = send(board, "TRIGger:REServation:SEAT:MOUNT?").split(",")
        print(f"  slot {slot} ACTion? -> {','.join(_action_page[:5])}"
              f" TEMPlate? -> {','.join(_template_page[:5])}"
              f" diag {_diag[4] if len(_diag) > 4 else '?'}"
              " (diag 20+r = action install result)", flush=True)
        if slot == 0:
            # The counter binding is as freshness-sensitive as the action table
            # (COUNTER_CHANGED = 6), but its stopped-metadata commands advance the
            # ring config sequence and therefore invalidate a binding made earlier.
            # Re-activate first, then load/start the counter - never between the
            # activation and the installs.
            send(board, f"TRIGger:REServation:DEPloyment:STAGe {dep_gen},{dep_crc},{payload}")
            send(board, f"TRIGger:REServation:DEPloyment:ACTivate {dep_gen},{dep_crc}")
            send(board, "TRIGger:REServation:COUNter:UNLoad")
            send(board, f"TRIGger:REServation:COUNter:LOAD {dep_gen},{dep_crc}")
            send(board, "TRIGger:REServation:COUNter:CLEar")
            send(board, "TRIGger:REServation:COUNter:STARt")
            _page = send(board, "TRIGger:REServation:COUNter?").split(",")
            print(f"  slot 0 counter loaded {_page[2] if len(_page) > 2 else '?'}"
                  f" state {_page[13] if len(_page) > 13 else '?'}"
                  f" count {_page[16] if len(_page) > 16 else '?'}", flush=True)

        send(board, "SYSTem:TDMA:FLIGHT:FIFO:RESet")
        # NO1 (slot 0) is the only position source; every board names source slot 0.
        send(board, f"TRIGger:REServation:T2:CONFigure {dep_gen},0,14,{int(slot == 0)},"
                    f"5000,5000,950,5000,10000,15000,5000,1000")
        diag = send(board, "TRIGger:REServation:SEAT:MOUNT?").split(",")
        print(f"  configure diag {diag[4] if len(diag) > 4 else '?'}"
              " (91/92/93 prereq, 100/101 inner, 140-148 validate, 111-137 stopped)")
        send(board, f"TRIGger:REServation:T2:STARt {dep_gen},0")
        state = send(board, "TRIGger:REServation:T2?").split(",")
        print(f"{uid:016X} slot {slot} | T2 configured {state[2] if len(state) > 2 else '?'}"
              f" active {state[3] if len(state) > 3 else '?'}"
              f" last_error {state[1] if len(state) > 1 else '?'}")

    # (1b) VDC feedback session (monotonic) + output run with valid ranges,
    #      prepared while stopped so the ring start can enable the hardware output.
    # The VDC feedback session and the grid epoch must be the SAME number: the T2
    # grid cursor only binds when the committed DCO model's session matches the
    # grid's, otherwise pulse_index stays 0 and the origin never services a coarse
    # sample (measured: model committed, cursor page all zeros, no publication).
    session = dep_gen
    for uid, _s, _i, _o, _f in ROWS:
        board = boards.get(f"{uid:016X}")
        if board is None:
            continue
        send(board, "SYSTem:VDC:OUTPut:RUN:STOP")
        time.sleep(0.2)
        # Output timing profile and run parameters follow the working capture
        # (input-probe.json: "OUTPut:TIMing 24000,32000,16000" and
        # "OUTPut:RUN 1000000,2000,0"); the board default 12000,16000,6000 and the
        # previous 1000000,400000,10000 were never part of a passing cycle.
        send(board, "SYSTem:VDC:OUTPut:TIMing 24000,32000,16000")
        send(board, f"SYSTem:VDC:FEEDback:SESSion {session}")
        send(board, "SYSTem:VDC:OUTPut:RUN 1000000,2000,0")
        _opened = send(board, "SYSTem:VDC:FEEDback:SESSion?")
        _run = send(board, "SYSTem:VDC:OUTPut:RUN?").split(",")[:3]
        if _opened != str(session):
            print(f"  slot {slot} WARNING: feedback session {_opened} != {session}",
                  flush=True)
        elif len(_run) > 1 and _run[1] in ("0", ""):
            print(f"  slot {slot} WARNING: VDC output run not live: {_run}", flush=True)
    print("vdc prepared, session", session)
    if src is not None:
        print("vdc session?", send(src, "SYSTem:VDC:FEEDback:SESSion?")[:16])

    # (2) Position source, priority class and ring bring-up, in the order the
    #     proven probe (tools/sync_trigger/t2_bench_probe.py) uses and the firmware
    #     requires:
    #      * the counter source feeds the origin's turntable counter - without it
    #        T2 reports last_error 4 (COUNTER) and publishes nothing;
    #      * the priority class carries the first-class seat over the shared fast
    #        channel (SYNC on the source, MATCH/FOLLow on the peers);
    #      * the ring has to be TRAINed and ARMed before RING:START is accepted:
    #        tdma_ring_runtime_set_data_enabled() requires enabled &&
    #        adapter_started && train_command_seq == train_accepted_seq.
    src = boards.get("0010071E65B5CB38")
    if src is None:
        print("source: NO1 NOT DISCOVERED - positions cannot be announced")
    else:
        # The model is the physical count source for the origin's counter; without
        # it the counter stays at zero (T2 reports counter_count 0) and no position
        # threshold is ever crossed.
        for command in ("CONFigure:MODEl:TURNtable:LOAD 0,3",
                        "CONFigure:MODEl:TURNtable:MOTion 360,360",
                        "CONFigure:MODEl:TURNtable:TRIGger 1,0,360,1,100,1,1000",
                        "MODEl:TURNtable:STARt"):
            send(src, command)
        print("turntable:", send(src, "READ:MODEl:TURNtable:STATe?")[:60], flush=True)
        # The counter is loaded and started inside the per-board configuration
        # phase, where its deployment binding is still current; here only the
        # source is released and configured.
        send(src, "TRIGger:REServation:SOURce:RELease")
        send(src, "TRIGger:REServation:SOURce:CONFigure 1000")
        _counter = send(src, "TRIGger:REServation:COUNter?").split(",")
        print(f"counter loaded {_counter[2] if len(_counter) > 2 else '?'}"
              f" state {_counter[13] if len(_counter) > 13 else '?'}"
              f" count {_counter[16] if len(_counter) > 16 else '?'}", flush=True)
        print("source:", send(src, "TRIGger:REServation:SOURce?")[:40], flush=True)
        send(src, "SYSTem:TDMA:RING:BURSt 0")
        set_priority(board=src, command=f"SYSTem:VDC:PRIORity:SYNC {dep_gen}",
                     page="SYSTem:VDC:PRIORity:SYNC?", label="SYNC")
    for node, (uid, _s, _i, _o, _f) in enumerate(ROWS):
        board = boards.get(f"{uid:016X}")
        if board is None or node == 0:
            continue
        # FOLLow is the step that never took effect in the failing runs: without it
        # the priority lane stays inactive, the SYNC_TRIGGER phase gets no ingress
        # slack, is skipped by start_missed on every cycle and the T2 service never
        # runs. Its preconditions (vdc_priority_follow.inc:62-71,
        # vdc_boundary_control.inc:66-80) include a live session, a matching
        # priority_generation, s_priority_match_request_session == session and an
        # EVEN boundary request counter (an in-flight boundary request refuses the
        # next one), so write with readback and retry instead of assuming.
        # The working capture re-asserts the feedback session immediately before
        # every priority write, retries FOLLow/PHASe three times and reads their
        # STATUS pages (never FOLLow?, which refuses service while the lane is
        # active), and re-issues OUTPut:RUN between them.
        send(board, f"SYSTem:VDC:FEEDback:SESSion {dep_gen}")
        set_priority(board, f"SYSTem:VDC:PRIORity:MATCH {dep_gen}",
                     "SYSTem:VDC:PRIORity:MATCH?", f"{uid:016X} MATCH")
        for _attempt in range(3):
            send(board, f"SYSTem:VDC:FEEDback:SESSion {dep_gen}")
            send(board, "SYSTem:VDC:OUTPut:RUN 1000000,2000,0")
            send(board, "SYSTem:VDC:PRIORity:FOLLow 1")
            _follow = send(board, "SYSTem:VDC:PRIORity:FOLLow:STATus?").split(",")
            if len(_follow) > 1 and _follow[1] not in ("0", ""):
                break
            time.sleep(0.5)
        for _attempt in range(3):
            send(board, "SYSTem:VDC:OUTPut:RUN 1000000,2000,0")
            send(board, "SYSTem:VDC:PRIORity:FOLLow:PHASe 1")
            _phase = send(board, "SYSTem:VDC:PRIORity:FOLLow:PHASe?").split(",")
            if _phase and _phase[0].strip() not in ("0", ""):
                break
            time.sleep(0.5)
        print(f"  {uid:016X} follow status {send(board, 'SYSTem:VDC:PRIORity:FOLLow:STATus?')[:40]}"
              f" health {send(board, 'SYSTem:VDC:PRIORity:FOLLow:HEALth?')[:40]}", flush=True)
    # ARM is what enables the ring configuration and starts its adapter, so the
    # armed state has to be awaited; the reference (NO1) is armed LAST because it
    # releases the ring start handshake (calibration_clk_train.py: followers armed
    # together, reference appended, then every board must read enabled &&
    # adapter_started). TRAIN follows ARM, and RING:START (data enable) only after
    # train_command_seq == train_accepted_seq.
    def arm_and_wait(board, label):
        send(board, "SYSTem:TDMA:RING:ARM")
        for _poll in range(40):
            if send(board, "SYSTem:TDMA:RING:ARM:STATus?").strip() == "1":
                break
            time.sleep(0.25)
        else:
            print(f"  {label} WARNING: RING:ARM not accepted", flush=True)
            return False
        for _poll in range(40):
            status = send(board, "SYSTem:TDMA:RING:STATus?").split(",")
            if len(status) > 8 and status[0] != "0" and status[8] != "0":
                return True
            time.sleep(0.25)
        print(f"  {label} WARNING: ring not enabled/started after ARM: "
              f"{status[:9]}", flush=True)
        return False

    for node, (uid, _s, _i, _o, _f) in enumerate(ROWS):
        board = boards.get(f"{uid:016X}")
        if board is not None and node != 0:
            arm_and_wait(board, f"{uid:016X}")
    src_board = boards.get("0010071E65B5CB38")
    if src_board is not None:
        arm_and_wait(src_board, "0010071E65B5CB38")
    for uid, _s, _i, _o, _f in ROWS:
        board = boards.get(f"{uid:016X}")
        if board is None:
            continue
        send(board, "SYSTem:TDMA:RING:TRAIN 1")
        for _poll in range(20):
            train = send(board, "SYSTem:TDMA:RING:TRAIN:STATus?")
            if "CLKTRAIN" in train:
                break
            time.sleep(0.25)
        send(board, "SYSTem:TDMA:RING:START")
    # Load mask bit 6 is APP_REALTIME_LOAD_SYNC_TRIGGER: without it the Core1
    # realtime loop never runs app_realtime_sync_trigger_phase(), so
    # sync_trigger_service()/trigger_t2_runtime_service()/trigger_time_cursor_service()
    # are never called - T2 reports configured+active with segments 0/0/0, the cursor
    # stays NO_SYNC with session 0 and nothing is ever published. The probe enables
    # 27 during the bring-up and 91 (27 | 64) once the ring is running.
    # The load mask here carries only the static phases (27). Bit 6 is the DYNAMIC
    # T2 start: every stopped-state write (T2:STARt, SESSion, OUTPut:RUN,
    # PRIORity:*) is refused once the ring is enabled (tdma_service.c:1033/1079
    # reject 115/135), so the only firmware-legal way to "start T2 after the DPLL
    # has locked" is to arm T2 during the stopped configuration and assert bit 6
    # once the VDC output is observed running and stable.
    print("VDC lock detection (up to 15 s per board)", flush=True)
    _locked = {}
    for _pass in range(5):
        for uid, _s, _i, _o, _f in ROWS:
            board = boards.get(f"{uid:016X}")
            if board is None or _locked.get(uid):
                continue
            # Authoritative phase-lock instrument: SYSTem:SYNC:VDC:LOCK:READiness?
            # fields: 0 input_ready, 1 locked (= dpll.state == VDC_DOMAIN_LOCK_LOCKED
            # && input_ready), 2 reason, 3 dpll.state ladder
            # (0 OFF,1 CHECKING,2 INITIAL_SYNC,3 FREQ_LOCK,4 PHASE_LOCK,5 LOCKED,
            #  6 HOLDOVER,7 RELOCKING,8 FAULT), 4 health, 5 accepted, 6 rejected,
            # 7 last_reject.
            _ready = send(board, "SYSTem:SYNC:VDC:LOCK:READiness?").split(",")
            _run = send(board, "SYSTem:VDC:OUTPut:RUN?").split(",")
            _cursor = send(board, "TRIGger:REServation:T2:CURSor?").split(",")
            _in = _ready[0].strip() if len(_ready) > 0 else "?"
            _lock = _ready[1].strip() if len(_ready) > 1 else "?"
            _reason = _ready[2].strip() if len(_ready) > 2 else "?"
            _state = _ready[3].strip() if len(_ready) > 3 else "?"
            _acc = _ready[5].strip() if len(_ready) > 5 else "?"
            _rej = _ready[6].strip() if len(_ready) > 6 else "?"
            _blocks = _run[4].strip() if len(_run) > 4 else "?"
            _cerr = _cursor[1].strip() if len(_cursor) > 1 else "?"
            _cvalid = _cursor[3].strip() if len(_cursor) > 3 else "?"
            print(f"  lock t+{3 * (_pass + 1)}s {uid:016X}: input_ready {_in}"
                  f" LOCKED {_lock} (dpll.state {_state}, reason {_reason})"
                  f" accepted {_acc} rejected {_rej} run_blocks {_blocks}"
                  f" cursor err {_cerr} valid {_cvalid}", flush=True)
            if _lock == "1":
                _locked[uid] = True
        if len(_locked) == len(ROWS):
            break
        time.sleep(3.0)
    if len(_locked) != len(ROWS):
        # Per the operating rule the dynamic T2 start requires a stable phase lock;
        # without it the seat would publish against an unlocked time base, so stop
        # here (the teardown below still runs and leaves the bench clean).
        print("lock detection: NOT locked on every board - dynamic T2 start withheld",
              flush=True)
        return 3
    # Dynamic start: enable the sync-trigger phase now that the time base is locked.
    for uid, _s, _i, _o, _f in ROWS:
        board = boards.get(f"{uid:016X}")
        if board is not None:
            set_load_mask(board, 91, f"{uid:016X} dynamic T2 start")
    print("ring start requested", flush=True)
    # Preflight verdict before the sampling loop: every earlier cycle cost 7-16 min
    # only to falsify one bit, so name the blocking bit here in one line. Bit 6 of
    # the load mask must be set and the T2 service must actually execute (its
    # segment counters are written on every call, so all-zero means never called).
    time.sleep(2.0)
    _frozen = []
    _abort = False
    for uid, slot, _i, _o, _f in ROWS:
        board = boards.get(f"{uid:016X}")
        if board is None:
            continue
        _mask = send(board, "SYSTem:TDMA:LOAD:MASK?").split(",")[0].strip()
        _t2 = send(board, "TRIGger:REServation:T2?").split(",")
        _ring = send(board, "SYSTem:TDMA:RING:STATus?").split(",")
        _sched = send(board, "SYSTem:TDMA:SCHEDule?").split(",")
        # Phase 6 is SYNC_TRIGGER; its block starts at header(8) + 9 * 6 and its run
        # counter at offset 6 inside that block. Two samples 2 s apart separate
        # "the phase never runs" (eligibility/window gate) from "the phase runs but
        # trigger_t2_runtime_service() returns early".
        _base = 8 + 9 * 6
        _runs1 = _sched[_base + 6] if len(_sched) > _base + 6 else "?"
        time.sleep(2.0)
        _sched2 = send(board, "SYSTem:TDMA:SCHEDule?").split(",")
        _runs2 = _sched2[_base + 6] if len(_sched2) > _base + 6 else "?"
        _sync = (_mask.isdigit() and (int(_mask) & 64) == 64)
        _seg = _t2[47] if len(_t2) > 47 else "?"
        _up = (len(_ring) > 8 and _ring[0] != "0" and _ring[8] != "0")
        _q = _sched[5] if len(_sched) > 5 else "?"
        print(f"  preflight slot {slot}: mask {_mask} (bit6 {'set' if _sync else 'CLEAR'})"
              f" quarantine {_q} ring {'ok' if _up else 'NOT UP'}"
              f" | phase6 runs {_runs1}->{_runs2}"
              f" ({'running' if _runs1 != _runs2 else 'NOT RUNNING'})"
              f" | T2 cfg {_t2[2] if len(_t2) > 2 else '?'}"
              f" act {_t2[3] if len(_t2) > 3 else '?'} segments {_seg}"
              f" => {'service running' if _seg not in ('0', '?') else 'SERVICE NEVER RAN'}",
              flush=True)
        _frozen.append((slot, _runs1 == _runs2 and _seg in ("0", "?")))
    if _frozen and all(frozen for _slot, frozen in _frozen):
        # Abort before the sampling loop, but still run the teardown below: exiting
        # here would leave the bench dirty and skip the stopped-state priority
        # readout, which is the measurement this abort exists to make cheap.
        print("preflight: SYNC_TRIGGER phase frozen on every board (segments 0/0/0)"
              " - skipping the sampling loop", flush=True)
        _abort = True
    if src is not None:
        send(src, "TRIGger:REServation:SOURce:STARt")
        grant = send(src, f"CALibration:ORIGin:TRIAL {dep_gen},8192,256,0")
        print("origin trial:", grant[:40], flush=True)
        print("vdc state:", send(src, "SYSTem:VDC:OUTPut:RUN?")[:70], flush=True)

    # (4) sample the T2 runtime and the position transport
    for tick in range(0 if _abort else 5):
        time.sleep(2.0)
        for uid, slot, _i, _o, _f in ROWS:
            board = boards.get(f"{uid:016X}")
            if board is None:
                continue
            state = send(board, "TRIGger:REServation:T2?").split(",")
            position = send(board, "SYSTem:REFMEM:SYNC:FLIGHT:POSition?")
            counter = send(board, "TRIGger:REServation:COUNter?").split(",")
            if slot == 0:
                cursor = send(board, "TRIGger:REServation:T2:CURSor?").split(",")
                _sess = send(board, "SYSTem:VDC:FEEDback:SESSion?")
                _model = send(board, "SYSTem:VDC:FEEDback:MODel?").split(",")
                print(f"t+{2 * (tick + 1)}s vdc session {_sess}"
                      f" | model ok {_model[0] if _model else '?'}"
                      f" token {_model[1] if len(_model) > 1 else '?'}"
                      f" session {_model[2] if len(_model) > 2 else '?'}"
                      f" | ring enabled "
                      f"{send(board, 'SYSTem:TDMA:RING:STATus?').split(',')[0]} "
                      f"started {send(board, 'SYSTem:TDMA:RING:STATus?').split(',')[8]}",
                      flush=True)
                # CURSor? = ok,last_error,configured,valid,grid_epoch,grid_generation,
                # session,sample_seq,carrier,identity_crc,ring_config_seq,model_token,
                # dco_update_seq,event_local_ns(2),event_vdc_ns(2),action_cursor(2),
                # pulse_index(2),missed_sync_count
                print(f"t+{2 * (tick + 1)}s cursor err "
                      f"{cursor[1] if len(cursor) > 1 else '?'}"
                      f" cfg {cursor[2] if len(cursor) > 2 else '?'}"
                      f" valid {cursor[3] if len(cursor) > 3 else '?'}"
                      f" epoch {cursor[4] if len(cursor) > 4 else '?'}"
                      f" session {cursor[6] if len(cursor) > 6 else '?'}"
                      f" seq {cursor[7] if len(cursor) > 7 else '?'}"
                      f" pulse_lo {cursor[19] if len(cursor) > 19 else '?'}"
                      f" model_token {cursor[11] if len(cursor) > 11 else '?'}"
                      # segment max cycles are written by the T2 service itself: all
                      # zero proves the service never ran at all.
                      f" | segments {state[47] if len(state) > 47 else '?'}"
                      f"/{state[48] if len(state) > 48 else '?'}"
                      f"/{state[49] if len(state) > 49 else '?'}"
                      f" sync_seq {state[8] if len(state) > 8 else '?'}", flush=True)
                # T2 page indices (raw split): 9 published_mask, 28 last_counter_count
                # (u64), 30 cursor.pulse_index, 33 announced_mask, 44/45 core0
                # prepare/preview counters. POSition? index 5 is tx_publish_count.
                fields = position.split(",")
                # The rolling window's health facts are appended LAST on the T2 page
                # (order guarded by tests/python/test_t2_page_field_order.py), so they
                # are read from the tail and never by index: the migration acceptance
                # is announced == entered with overwrote_unannounced == 0.
                window_tail = state[-9:] if len(state) >= 9 else []
                print(f"t+{2 * (tick + 1)}s counter loaded "
                      f"{counter[2] if len(counter) > 2 else '?'} count "
                      f"{counter[16] if len(counter) > 16 else '?'}"
                      f" | T2 counter {state[28] if len(state) > 28 else '?'}"
                      f" published_mask {state[9] if len(state) > 9 else '?'}"
                      f" announced {state[33] if len(state) > 33 else '?'}"
                      f" pulse {state[30] if len(state) > 30 else '?'}"
                      f" prep {state[44] if len(state) > 44 else '?'}"
                      f"/{state[45] if len(state) > 45 else '?'}"
                      f" | POS pub {fields[5] if len(fields) > 5 else '?'}"
                      f" rx {fields[12] if len(fields) > 12 else '?'}"
                      f" | window announced {window_tail[1] if len(window_tail) > 1 else '?'}"
                      f"/entered {window_tail[0] if len(window_tail) > 0 else '?'}"
                      f" overwrote_unannounced {window_tail[2] if len(window_tail) > 2 else '?'}"
                      f" late {window_tail[3] if len(window_tail) > 3 else '?'}"
                      f" step_mdeg {window_tail[6] if len(window_tail) > 6 else '?'}"
                      f" geometry {window_tail[7] if len(window_tail) > 7 else '?'}pos"
                      f"/{window_tail[8] if len(window_tail) > 8 else '?'}pul",
                      flush=True)
            err = state[1] if len(state) > 1 else "?"
            cfg = state[2] if len(state) > 2 else "?"
            act = state[3] if len(state) > 3 else "?"
            ev = state[13] if len(state) > 13 else "?"
            print(f"t+{2 * (tick + 1)}s {uid:016X} slot {slot} err {err} cfg {cfg}"
                  f" act {act} ev {ev} | POS {position[:40]}")

    # (5) teardown: complete stop, including the ring and the load mask, so a
    #     back-to-back run starts from the same clean state this one required.
    for uid, _s, _i, _o, _f in ROWS:
        _board = boards.get(f"{uid:016X}")
        if _board is None:
            continue
        send(_board, "SYSTem:TDMA:RING:STOP")
        send(_board, "TRIGger:REServation:T2:STOP")
        # The priority-lane diagnostics are stopped-state only (they refuse while
        # ring.enabled/adapter_started or while the lane is active), so this is the
        # one moment they can be read: it shows whether the bring-up actually armed
        # the priority class that gives the SYNC_TRIGGER phase its ingress slack.
        for _page in ("SYSTem:VDC:PRIORity:RX?", "SYSTem:VDC:PRIORity:TX?"):
            print(f"  {uid:016X} {_page} -> {send(_board, _page)[:80]}", flush=True)
        send(_board, "SYSTem:VDC:OUTPut:RUN:STOP")
        send(_board, "SYSTem:VDC:FEEDback:SESSion 0")
        send(_board, "TRIGger:REServation:T2:STOP")
        send(_board, "TRIGger:REServation:COUNter:UNLoad")
        set_load_mask(_board, 27, f"{uid:016X} teardown")
    # (5b) teardown: turntable, VDC run, T2
    if src is not None:
        send(src, "MODEl:TURNtable:STOP")
    for uid, _s, _i, _o, _f in ROWS:
        board = boards.get(f"{uid:016X}")
        if board is None:
            continue
        send(board, "SYSTem:VDC:OUTPut:RUN:STOP")
        send(board, "TRIGger:REServation:T2:STOP")
        send(board, "SYSTem:TDMA:RING:STOP")
    close_persistent_connections()
    return 3 if _abort else 0


# ---------------------------------------------------------------------------
# Step engine: the same verified command sequence, addressable per step.
#
# The continuous chain (run_full) keeps its own code path so the acceptance flow
# is untouched. This engine re-expresses steps 1-12 (the stopped-state
# configuration) as one function per step, executed for all four boards before
# the next step starts. Each board's *local* command order is unchanged, so the
# binding-freshness rules still hold; only the interleaving changes.
# ---------------------------------------------------------------------------

STEP_TITLES = {
    0: "标定阶段自检与回放（EMPTY/不完整时从健康板恢复）",
    1: "停机序列（RING:STOP 等 + 等待物理停止）",
    2: "停机态配置块（TIMing / 优先级清 0 / SESSion 0 / mask 27 / FLIGHT:MODE）",
    3: "标定参数重放（TOPology:PROBe + OPMode:STAGe 7 + APPLy）",
    4: "拓扑暂存（GEOMetry + TOPology）并等待提交",
    5: "诊断继续标志（RING:DIAGnostic 1）",
    6: "路径延时交接（SYNC:VDC:DPLL:ProVisional 1）+ 前置判定",
    7: "清表（COUNter/DEPloyment/TEMPLate/ACTion/TEMPlate CLEar）",
    8: "网格（GRID:CLEar + GRID:CONFigure）",
    9: "部署激活（验证式，最多 6 轮）",
    10: "装动作表与模板（ACTion/TEMPlate INSTall + ACTivate）",
    11: "计数器装载（仅 slot 0：再激活 + UNLoad/LOAD/CLEar）+ 残值守门",
    12: "T2 配置 + 挂载（FIFO:RESet + CONFigure + STARt）",
    13: "VDC 反馈会话 + OUTPut:RUN 请求",
    14: "转台模型与源配置（只配置，不启动）",
    15: "优先级类（SYNC / MATCH / FOLLow / FOLLow:PHASe）",
    16: "TDMA 环启动（BURSt 0 → ARM → TRAIN → START）+ 原点试运行",
    17: "锁相检测（LOCK:READiness?）+ 动态启动（mask 位6）",
    18: "时基就绪判定（cursor valid / epoch / model + 相位7 预检）",
    19: "启动 PWM 源与转台（守门：游标有效且计数未越首阈值）+ 计数递增判据",
    20: "运行采样（segments[42..44] / published_mask / POSition 发布）",
    21: "停机 teardown（含停机态优先级读数）+ 阶段结论",
}
# Steps implemented as individual functions in this engine.
STEP_FUNCTIONS = {}
# "boards" = run once per board (the function receives slot/board/uid);
# "once"   = run once for the whole chain (the function iterates boards itself).
STEP_SCOPE = {14: "once", 15: "once", 16: "once", 17: "once",
              18: "once", 19: "once", 20: "once", 21: "once"}

# Firmware field maps (verified against the handlers and a live readout, 2026-10-11).
# T2? = trigger_t2_runtime_commands.inc: [0] 1, [1] last_error, [2] configured,
# [3] active, [4] origin, [5] source_slot, [6] target_mask, [7] run_id,
# [8] last_sync_sequence, [9] published_mask, [10] received_mask,
# [11..12] descriptor/transport rejects, [13] event_count, [14] event_dropped,
# [15] frozen_mask, [16..18] freeze mismatch/fallback/missing_local,
# [19..27] 3 positions x (freeze_version, freeze_local_version, freeze_local_age),
# [28] last_counter_count, [29] cursor.pulse_index, [30] cursor.fine_slice_index,
# [31] predictor.announced_mask, [32..37] 3 x (target_count, threshold_count),
# [38] predictor.last_error, [39] core0_prepare_calls, [40] core0_preview_saved,
# [41] core0_prepare_last_reason, [42..44] segment_max_cycles, [45] freeze_complete,
# [46] freeze_abandoned, [47] freeze_resend_count, ...
T2_FIELDS = {"last_error": 1, "configured": 2, "active": 3, "origin": 4,
             "source_slot": 5, "target_mask": 6, "run_id": 7,
             "last_sync_sequence": 8, "published_mask": 9, "received_mask": 10,
             "event_count": 13, "frozen_mask": 15,
             "last_counter_count": 28, "pulse_index": 29, "fine_slice_index": 30,
             "announced_mask": 31, "prepared_last_reason": 41,
             "prepare_calls": 39, "preview_saved": 40}
T2_SEGMENTS = (42, 43, 44)
ALL_POSITIONS_MASK = 7             # TRIGGER_T2_RUNTIME_POSITION_COUNT == 3
T2_TARGETS = (32, 34, 36)          # target_count per position
T2_THRESHOLDS = (33, 35, 37)       # threshold_count per position

# SYSTem:TDMA:SCHEDule? = 8 header fields + 11 fields per phase, phases in the
# app_realtime cycle order (application/src/app.c:1256-1303):
# 0 TDMA, 1 VDC, 2 DPLL, 3 CALIBRATION, 4 SYNC_CAPTURE, 5 REFMEM, 6 MODEL,
# 7 SYNC_TRIGGER, 8 TRIGGER_MEASURE.
SCHEDULE_HEADER = 8
SCHEDULE_STRIDE = 11
SCHEDULE_PHASE_SYNC_TRIGGER = 7
SCHEDULE_RUN_COUNT = 6
SCHEDULE_START_MISS = 8


def schedule_field(fields, phase, offset):
    index = SCHEDULE_HEADER + SCHEDULE_STRIDE * phase + offset
    return fields[index] if len(fields) > index else "?"


# CALibration:TRAINing:STAGe:LINK write order mapped onto the READ page fields
# (calibration_manager_stage_training_link()): read indices 1..6 then 12..23 are the
# eighteen leading values, 24..33 the offsets/delays that follow them.
STAGE_LINK_WRITE_ORDER = (1, 2, 3, 4, 5, 6, 12, 13, 14, 15, 16, 17,
                          18, 19, 20, 21, 22, 23)
STAGE_LINK_TAIL_ORDER = (24, 25, 26, 27, 28, 29, 30, 31, 32, 33)


def stage_complete(page):
    """True when READ:CALibration:TRAINing:STAGe? reports a committed stage."""
    fields = page.replace('"', "").split(",")
    return (len(fields) >= 10 and fields[0].strip().upper() == "TRN03STG"
            and fields[4].strip() == "1")


def read_stage_links(ctx, board):
    """Per-link parameters of a healthy stage, keyed by link index."""
    links = {}
    for index in range(len(ROWS)):
        page = ctx.send(board,
                        f"READ:CALibration:TRAINing:STAGe:LINK? {index}").replace('"', "")
        values = page.split(",")
        if values and values[0].strip().upper() == "TRN03LNK" and len(values) > 34:
            links[index] = values[1:]
    return links


def t2_field(fields, name):
    index = T2_FIELDS[name]
    return fields[index] if len(fields) > index else "?"


# TRIGger:REServation:T2:CURSor? (scpi_trigger_t2_runtime_commands.inc,
# scpi_cmd_trigger_t2_cursor_q): ok, last_error, configured, valid, grid_epoch,
# grid_generation, session, sample_seq, carrier_sequence, identity_crc32 (legacy,
# always 0), ring_config_seq, model_token, dco_update_seq, ... The ring/model fields
# are the identity the cursor bound to on its last successful projection.
CURSOR_FIELDS = {"err": 1, "valid": 3, "epoch": 4, "session": 6, "seq": 7,
                 "ring_cfg": 10, "model_token": 11}
# Appended after missed_sync_count [17] (scpi_cmd_trigger_t2_cursor_q): the first sticky
# binding fault, computed by Core1 when it latched (trigger_time_cursor.h). Site 1 RING
# (flags/config), 2 MODEL (identity on a new token), 3 RING_MODEL (model slot/schedule
# vs ring), 4 BACKWARD, 5 OVERFLOW. ring_bits: 1 enabled, 2 adapter started, 4 data
# enabled (only meaningful when the check read the ring).
CURSOR_FAULT_BASE = 18
CURSOR_FAULT_NAMES = ("site", "error", "ring_bits", "diff", "sample_seq",
                      "ring_config", "model_token")
CURSOR_FAULT_SITES = {"0": "none", "1": "RING", "2": "MODEL", "3": "RING_MODEL",
                      "4": "BACKWARD", "5": "OVERFLOW"}
# trigger_time_cursor_fault_diff_t
CURSOR_FAULT_DIFF_BITS = ((0, "ring config!=applied"), (1, "ring config!=bound"),
                          (2, "ring schedule_crc=0"), (3, "role"), (4, "clock_epoch"),
                          (5, "clock_run"), (6, "slot"), (7, "schedule_crc"),
                          (8, "model period!=grid"), (9, "model slot!=ring"),
                          (10, "model schedule!=ring"))


def cursor_fault(cursor):
    """Decode the appended fault record; None when the firmware predates it."""
    if len(cursor) < CURSOR_FAULT_BASE + len(CURSOR_FAULT_NAMES):
        return None
    return {name: cursor[CURSOR_FAULT_BASE + i].strip()
            for i, name in enumerate(CURSOR_FAULT_NAMES)}


def cursor_fault_mismatches(f):
    """Name what the latching check found: dropped ring flags plus the diff mask."""
    out = []
    if f["site"] in ("1", "3") and f["ring_bits"].isdigit():
        bits = int(f["ring_bits"])
        for bit, name in ((1, "ring enabled"), (2, "adapter started"), (4, "data enabled")):
            if not bits & bit:
                out.append(f"{name}=0")
    if f["diff"].isdigit():
        diff = int(f["diff"])
        out.extend(name for bit, name in CURSOR_FAULT_DIFF_BITS if diff >> bit & 1)
    return out
# SYSTem:VDC:FEEDback:MODel? (scpi_cmd_vdc_feedback_model_q): the live committed model
# the cursor qualifies against (trigger_time_cursor.c cursor_qualify_model).
MODEL_FIELDS = {"token": 1, "session": 2, "role": 3, "clock_epoch": 5, "clock_run": 6,
                "slot": 7, "schedule_crc": 21}
# SYSTem:TDMA:RING:STATus?: the ring identity half of the same check.
RING_FIELDS = {"enabled": 0, "slot": 2, "adapter_started": 8, "config_seq": 38,
               "applied_seq": 39}


def page_field(fields, table, name):
    index = table[name]
    return fields[index].strip() if len(fields) > index else "?"


def cursor_probe(ctx, board, label):
    """One line: the cursor's bound identity against the live model and ring.

    A sticky IDENTITY fault (err 7 with seq frozen) can only be set after the cursor
    bound once (trigger_time_cursor.c:241/257/265, sticky == s_binding.bound), so the
    first probe that shows it brackets the step that changed the identity.
    """
    if board is None:
        return
    cursor = ctx.send(board, "TRIGger:REServation:T2:CURSor?").split(",")
    model = ctx.send(board, "SYSTem:VDC:FEEDback:MODel?").split(",")
    ring = ctx.send(board, "SYSTem:TDMA:RING:STATus?").split(",")
    # The counter count shows whether anything but the PWM source advances it (the
    # source is held stopped until step 19).
    counter = ctx.send(board, "TRIGger:REServation:COUNter?").split(",")
    count = counter[16].strip() if len(counter) > 16 else "?"
    c = {name: page_field(cursor, CURSOR_FIELDS, name) for name in CURSOR_FIELDS}
    m = {name: page_field(model, MODEL_FIELDS, name) for name in MODEL_FIELDS}
    r = {name: page_field(ring, RING_FIELDS, name) for name in RING_FIELDS}
    ring_drift = c["ring_cfg"] not in ("0", "?") and c["ring_cfg"] != r["config_seq"]
    token_drift = c["model_token"] not in ("0", "?") and c["model_token"] != m["token"]
    # Timestamped: the probe sequence is read as a timeline across steps 16-18.
    ctx.log(f"  cursor probe [{label}]: err {c['err']} valid {c['valid']} seq {c['seq']}"
            f" session {c['session']}"
            f" | bound ring_cfg {c['ring_cfg']} vs ring {r['config_seq']}/{r['applied_seq']}"
            f"{' DRIFT' if ring_drift else ''}"
            f" | bound token {c['model_token']} vs model {m['token']}"
            f"{' DRIFT' if token_drift else ''}"
            f" | model role {m['role']} clk {m['clock_epoch']}/{m['clock_run']}"
            f" slot {m['slot']} sched {m['schedule_crc']}"
            f" | ring en {r['enabled']} adapter {r['adapter_started']} slot {r['slot']}"
            f" | count {count}")
    fault = cursor_fault(cursor)
    if fault is not None and fault["site"] not in ("0", ""):
        mismatches = cursor_fault_mismatches(fault)
        print(f"    sticky fault [{label}]: site {CURSOR_FAULT_SITES.get(fault['site'], fault['site'])}"
              f" err {fault['error']} at seq {fault['sample_seq']}"
              f" | differs: {', '.join(mismatches) if mismatches else 'nothing decoded'}",
              flush=True)
        print(f"    sticky fault record: "
              f"{' '.join(f'{k}={v}' for k, v in fault.items())}", flush=True)
        if not getattr(ctx, "fault_reported", False):
            ctx.fault_reported = True
            ctx.failures.append(f"cursor sticky fault site"
                                f" {CURSOR_FAULT_SITES.get(fault['site'], fault['site'])}"
                                f" at seq {fault['sample_seq']}:"
                                f" {', '.join(mismatches) if mismatches else 'see record'}")
    if r["enabled"] == "0":
        origin_stop_probe(ctx, board, label)


# READ:CALibration:ORIGin:STOP? (scpi_calibration_origin_stop_q): why the origin
# lifetime path last stopped the ring, latched by Core1 at the revoke
# (tdma_runtime_origin.inc tdma_runtime_owner_origin_note_stop).
ORIGIN_STOP_FIELDS = ("tag", "stop_count", "trial_epoch", "origin_epoch", "cause",
                      "auth_mask", "fault_site", "comm_state", "comm_last_error",
                      "trial_config_seq", "ring_config_seq", "release_state", "stop_ticks",
                      "begin_reject", "begin_observed", "begin_expected")
# TDMA_ORIGIN_REJECT_BEGIN details: tdma_pio_spi_phys_origin_begin() checks in
# source order (1..47), then the owner terms TDMA_ORIGIN_BEGIN_OWNER_* (0x81..).
ORIGIN_REJECT_BEGIN = 31
ORIGIN_BEGIN_CHECKS = (
    "null argument", "packet size", "not armed", "process image disabled", "role not master",
    "workspace owned", "build job busy", "overlay prepare busy", "prepare stage not idle",
    "tx pending", "tx completion pending", "persona not FLIGHT_ORIGIN", "tx dma channel",
    "rx dma channel", "config disabled", "DIAGNOSTIC_CONTINUE missing", "burst limit set",
    "node count range", "node count != phys", "baud != phys", "local != reference slot",
    "local != phys slot", "local slot >= node count", "config tx dma", "config rx dma",
    "payload slots", "overlay alignment unstable", "capture copy busy",
    "waveform capture busy", "data phase delay", "capture phase delay", "rearm/abort budget",
    "workspace overlap", "cadence", "guard floor <= rearm", "alignment bit shift",
    "physical bytes", "prefix", "frame decode", "payload class", "payload size",
    "origin slot", "hop count", "hop limit", "schedule crc", "ring profile crc", "frame flags")
ORIGIN_BEGIN_OWNER = {0x81: "owner context", 0x82: "owner unauthorized", 0x83: "owner rearm",
                      0x84: "owner abort poll", 0x85: "owner config changed"}
ORIGIN_STOP_CAUSES = ((0, "UNAUTHORIZED"), (1, "COMM_FAULT"))
# TDMA_ORIGIN_AUTH_* bit order (tdma_runtime_owner.h).
ORIGIN_AUTH_TERMS = ("FAULT_LATCHED", "NO_EPOCH", "DISABLED", "BURST_LIMIT", "VERSION",
                     "EPOCH_SUPERSEDED", "MODEL_EPOCH", "ADMISSION_CLASS", "CAPABILITY",
                     "CONFIG_SEQ", "RING_DISABLED", "FOUNDATION", "CALIBRATION", "CLOCK",
                     "EXPIRED")
ORIGIN_FAULT_SITES = {"0": "none", "1": "ADMISSION", "2": "BEGIN", "3": "BUILD",
                      "4": "UNHEALTHY", "5": "BLACKOUT"}


def decode_origin_stop(raw):
    """The stop-cause page as named fields; None when the firmware predates it."""
    fields = [value.strip().strip('"') for value in raw.split(",")]
    # Firmware before the BEGIN refusal fields stops at stop_ticks.
    if len(fields) not in (len(ORIGIN_STOP_FIELDS), len(ORIGIN_STOP_FIELDS) - 3) or             fields[0] != "ORIGINSTOP":
        return None
    record = dict(zip(ORIGIN_STOP_FIELDS, fields))
    record["begin_check"] = decode_begin_reject(record.get("begin_reject", ""))
    cause = int(record["cause"]) if record["cause"].isdigit() else 0
    auth = int(record["auth_mask"]) if record["auth_mask"].isdigit() else 0
    record["cause_names"] = [name for bit, name in ORIGIN_STOP_CAUSES if cause >> bit & 1]
    record["auth_terms"] = [name for bit, name in enumerate(ORIGIN_AUTH_TERMS) if auth >> bit & 1]
    record["fault_site_name"] = ORIGIN_FAULT_SITES.get(record["fault_site"], record["fault_site"])
    return record


def decode_begin_reject(raw):
    """Name a TDMA_ORIGIN_REJECT_BEGIN code; '' when absent or zero."""
    if not raw.isdigit() or int(raw) == 0:
        return ""
    code = int(raw)
    reason, detail = code >> 16, code & 0xFFFF
    if reason != ORIGIN_REJECT_BEGIN:
        return f"reason {reason} detail {detail}"
    if 1 <= detail <= len(ORIGIN_BEGIN_CHECKS):
        return f"#{detail} {ORIGIN_BEGIN_CHECKS[detail - 1]}"
    return ORIGIN_BEGIN_OWNER.get(detail, f"detail {detail:#x}")


def origin_stop_probe(ctx, board, label):
    """When the reference ring is found stopped, name the revoke branch once."""
    raw = ctx.send(board, "READ:CALibration:ORIGin:STOP?")
    record = decode_origin_stop(raw)
    if record is None:
        print(f"    origin stop page unavailable [{label}]: {raw[:60]}", flush=True)
        return
    if record["stop_count"] == getattr(ctx, "origin_stop_seen", None):
        return
    ctx.origin_stop_seen = record["stop_count"]
    runtime = ctx.send(board, "READ:CALibration:ORIGin:RUNTIme?")
    release = ctx.send(board, "READ:CALibration:ORIGin:RELease?")
    ctx.log(f"  ORIGIN STOP [{label}]: cause {'+'.join(record['cause_names']) or 'none'}"
            f" | failed auth terms {', '.join(record['auth_terms']) or 'none'}"
            f" | first fault site {record['fault_site_name']}"
            f" | comm state {record['comm_state']} last_error {record['comm_last_error']}"
            f" | epoch trial {record['trial_epoch']} now {record['origin_epoch']}"
            f" | config trial {record['trial_config_seq']} ring {record['ring_config_seq']}"
            f" | release state {record['release_state']} | stops {record['stop_count']}"
            + (f" | begin refused {record['begin_check']} observed {record['begin_observed']}"
               f" expected {record['begin_expected']}" if record["begin_check"] else ""))
    print(f"    origin runtime: {runtime[:160]}", flush=True)
    print(f"    origin release: {release[:160]}", flush=True)
    samples = getattr(ctx, "origin_stop_samples", [])
    samples.append({"label": label, **record, "runtime": runtime, "release": release})
    ctx.origin_stop_samples = samples


def step(number):
    def register(function):
        STEP_FUNCTIONS[number] = function
        return function
    return register


class StepContext:
    """Shared state for one step run."""

    def __init__(self, board_ids, generation=None):
        self.ns = SimpleNamespace(board_ids=list(board_ids), baud=115200, timeout=8.0,
                                  read_timeout=0.02, settle=0.02, keep_open=True,
                                  short_open=False,
                                  # Ordinary writes only need their acknowledgement,
                                  # not the full query timeout: without this the shared
                                  # sender falls back to args.timeout and every write
                                  # costs 8 s (measured: step 2 took 195 s for 24
                                  # writes). The project convention is 0.10 s
                                  # (calibration_timeout_config.TIMEOUT_DEFAULTS_S
                                  # ["action_ack"]); 0.25 s keeps CDC margin.
                                  action_timeout=0.25)
        self.boards = discover(self.ns)
        self.src = self.boards.get("0010071E65B5CB38")
        # Initialise the bookkeeping before anything can call send(): the session
        # probe below goes through it.
        self.command_times = []
        self.stage_donor = None
        self.failures = []
        self.records = []
        # The run's identity (grid epoch, deployment generation, VDC session, T2
        # run_id) all come from this number, and the T2 cursor only binds when the
        # grid epoch and the model's session are the same. Running one range per
        # invocation keeps it consistent by construction.
        # The VDC feedback session must also increase STRICTLY across runs
        # (vdc_model_feedback.inc tracks the last accepted session), so a time-based
        # number is not automatically safe: an earlier run (or a pinned generation)
        # may already have used a larger one. Read what the boards last accepted and
        # stay above it - otherwise the session write is refused, the origin trial
        # follows it down, and the DPLL never sees input.
        last_accepted = 0
        # SESSion? reports the ACTIVE session (0 after a teardown), while the firmware
        # compares against its internal last-accepted value, which is not exposed. Keep
        # our own high-water mark so a new run always asks for a larger number.
        marker = os.path.join("out", "tdma_t2", "last_generation.json")
        try:
            with open(marker, encoding="utf-8") as handle:
                last_accepted = int(json.load(handle).get("last_generation", 0))
        except (OSError, ValueError, TypeError):
            last_accepted = 0
        for uid, _s, _i, _o, _f in ROWS:
            member = self.boards.get(f"{uid:016X}")
            if member is None:
                continue
            page = self.send(member, "SYSTem:VDC:FEEDback:SESSion?")
            if page.strip().isdigit():
                last_accepted = max(last_accepted, int(page.strip()))
        requested = generation & 0xFFFFFFFF if generation else int(time.time()) & 0xFFFFFFFF
        if requested <= last_accepted:
            print(f"  note: generation {requested} is not above the last accepted "
                  f"session {last_accepted}; using {last_accepted + 1}", flush=True)
            requested = last_accepted + 1
        self.dep_gen = requested
        try:
            with open(marker, "w", encoding="utf-8") as handle:
                json.dump({"last_generation": self.dep_gen}, handle)
        except OSError:
            pass
        self.dep_crc = deployment_crc(self.dep_gen, ROWS)
        self.payload = ",".join(f"{uid},{slot},{imask},{omask},{fb}"
                                for uid, slot, imask, omask, fb in ROWS)
        self.preflight_frozen = False
        self.lock_failed = False
        # Set by step 18; step 19 starts the PWM source only on a valid reference cursor.
        self.reference_ready = False
        self.log_dir = os.path.join("out", "tdma_t2")
        os.makedirs(self.log_dir, exist_ok=True)
        self.log_path = os.path.join(self.log_dir,
                                     f"steps-{time.strftime('%Y%m%d-%H%M%S')}.log")
        self.log(f"run start: deployment generation {self.dep_gen}"
                 f" boards {sorted(self.boards.keys())}")

    def log(self, message):
        """Timestamped line; durability is handled by the stdout tee in run_steps."""
        print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)

    def fire(self, board, command):
        """Send a write whose result code is not the evidence (a readback follows).

        The shared sender keeps args.timeout for "result writes" such as
        OPMode:APPLy and RING:TOPology, and a persistent connection caches the args
        of the call that created it - so a per-call override only takes effect on a
        fresh connection. short_open gives exactly that: one port open per call, so
        the bound really applies instead of costing the full query timeout.
        """
        namespace = SimpleNamespace(**vars(self.ns))
        namespace.timeout = 0.6
        namespace.keep_open = False
        namespace.short_open = True
        return self.send(board, command, namespace=namespace)

    def send(self, board, command, timeout_override=None, namespace=None):
        """Send one command and record how long the exchange took.

        A refused command answers with an error reply that the matcher reports as a
        timeout, so a refused write costs the serial read timeout. The timing record
        makes that cost visible instead of guessed.
        """
        started = time.time()
        if namespace is None:
            namespace = self.ns
            if timeout_override is not None:
                namespace = SimpleNamespace(**vars(self.ns))
                namespace.timeout = timeout_override
        try:
            reply = board_command(board, command, namespace).strip()
        except Exception as exc:                                          # noqa: BLE001
            reply = f"<{type(exc).__name__}>"
        elapsed = time.time() - started
        self.command_times.append((elapsed, command, reply[:40]))
        if elapsed >= 1.0:
            self.log(f"    slow {elapsed:5.1f}s  {command[:58]}  -> {reply[:26]}")
        return reply

    def set_priority(self, board, command, page, label, retries=12):
        """Write a VDC priority setting and verify it by readback.

        These setters refuse silently for state reasons (session/priority generation
        mismatch, in-flight boundary request), which is how the FOLLow step went
        missing in the failing cycles.
        """
        value = ""
        for _ in range(retries):
            self.send(board, command)
            value = self.send(board, page).split(",")[0].strip()
            if value.isdigit() and int(value) != 0:
                return True
            time.sleep(0.5)
        self.failures.append(f"Step 15 priority {label}: readback stayed {value}")
        print(f"  {label} WARNING: readback stayed {value}", flush=True)
        return False

    def arm_and_wait(self, board, label):
        """ARM the ring and wait for the adapter: enabled && adapter_started."""
        self.send(board, "SYSTem:TDMA:RING:ARM")
        for _ in range(40):
            if self.send(board, "SYSTem:TDMA:RING:ARM:STATus?").strip() == "1":
                break
            time.sleep(0.25)
        else:
            print(f"  {label} WARNING: RING:ARM not accepted", flush=True)
            return False
        for _ in range(40):
            status = self.send(board, "SYSTem:TDMA:RING:STATus?").split(",")
            if len(status) > 8 and status[0] != "0" and status[8] != "0":
                return True
            time.sleep(0.25)
        print(f"  {label} WARNING: ring not enabled/started after ARM: {status[:9]}",
              flush=True)
        return False

    def poll(self, board, command, index, expected, tries=24, pause=0.25,
             budget_s=8.0):
        """Poll a page until it matches, bounded by a wall-clock budget.

        The budget matters because a page that refuses to answer costs the serial
        read timeout on every attempt: without it a 12-attempt poll burns ~96 s.
        `expected=None` accepts any value at that index (used for "field exists").
        """
        started = time.time()
        value = ""
        for _ in range(tries):
            value = self.send(board, command)
            fields = value.split(",")
            if expected is None:
                if len(fields) > index:
                    return True
            elif len(fields) > index and fields[index] == expected:
                return True
            if time.time() - started >= budget_s:
                break
            time.sleep(pause)
        return False

    def set_load_mask(self, board, mask, label):
        value = ""
        for _ in range(12):
            self.send(board, f"SYSTem:TDMA:LOAD:MASK {mask}")
            value = self.send(board, "SYSTem:TDMA:LOAD:MASK?").split(",")[0].strip()
            if value.isdigit() and (int(value) & mask) == mask:
                return True
            time.sleep(0.25)
        print(f"  {label} WARNING: load mask stayed {value} (wanted {mask})", flush=True)
        return False

    def each(self, function):
        for slot, (uid, _slot, _i, _o, _f) in enumerate(ROWS):
            board = self.boards.get(f"{uid:016X}")
            if board is None:
                print(f"  slot {slot} MISSING board {uid:016X}", flush=True)
                continue
            function(self, slot, board, uid)


@step(1)
def step_1_stop(ctx, slot, board, uid):
    ctx.send(board, "SYSTem:TDMA:RING:STOP")
    if not ctx.poll(board, "SYSTem:TDMA:RING:STATus?", 8, "0", tries=40):
        print(f"  slot {slot} WARNING: ring not stopped before configuration", flush=True)
    ctx.send(board, "SYSTem:VDC:OUTPut:RUN:STOP")
    ctx.send(board, "TRIGger:REServation:T2:STOP")
    ctx.send(board, "MODEl:TURNtable:STOP")
    if board is ctx.src:
        # The PWM source advances the counter; a source left running by an earlier run
        # would push the count past the thresholds before step 19 starts it on purpose.
        ctx.send(board, "TRIGger:REServation:SOURce:STOP")
    ctx.send(board, "TRIGger:REServation:COUNter:STOP")
    if not ctx.poll(board, "TRIGger:REServation:T2?", 3, "0", tries=12):
        print(f"  slot {slot} WARNING: T2 still active before configuring", flush=True)
    print(f"  slot {slot} stopped: ring "
          f"{ctx.send(board, 'SYSTem:TDMA:RING:STATus?').split(',')[:1]}", flush=True)


@step(2)
def step_2_stopped_block(ctx, slot, board, uid):
    ctx.send(board, "SYSTem:VDC:FEEDback:SESSion 0")
    if not ctx.poll(board, "SYSTem:VDC:FEEDback:SESSion?", 0, "0", tries=12):
        print(f"  slot {slot} WARNING: VDC feedback session still live", flush=True)
    ctx.send(board, "SYSTem:VDC:OUTPut:TIMing 24000,32000,16000")
    for suffix in ("FOLLow:PHASe", "FOLLow", "SYNC", "MATCH"):
        ctx.send(board, f"SYSTem:VDC:PRIORity:{suffix} 0")
    ctx.send(board, "SYSTem:VDC:FEEDback:REFerence 0")
    ctx.send(board, "SYSTem:VDC:FEEDback:LOCALfollow 0")
    # The foundation mask is VDC|DPLL|SYNC_CAPTURE|REFMEM|SYNC_TRIGGER = 91
    # (application/inc/app.h:25-29, APP_REALTIME_LOAD_FOUNDATION_MASK). 27 was not
    # a valid baseline: without the SYNC_TRIGGER bit the VDC domain never accepts a
    # sample, so the reference could never lock (measured).
    ctx.set_load_mask(board, 91, f"slot {slot}")
    ctx.send(board, "SYSTem:TDMA:FLIGHT:MODE 1")
    print(f"  slot {slot} timing {ctx.send(board, 'SYSTem:VDC:OUTPut:TIMing?')[:24]}"
          f" mask {ctx.send(board, 'SYSTem:TDMA:LOAD:MASK?')[:8]}"
          f" session {ctx.send(board, 'SYSTem:VDC:FEEDback:SESSion?')[:8]}", flush=True)


@step(3)
def step_3_replay(ctx, slot, board, uid):
    ctx.fire(board, "CALibration:TOPology:PROBe 1,1")
    # The applied level is the context the NVS calibration record is bound to, so read
    # it back: a fire()d APPLy can be lost (measured: all four boards stayed at the
    # DPLL60 adapter's level 0 / 2 nodes and the record never loaded).
    # STAGe/APPLy are sent on the persistent session with the full query timeout: on
    # a short-open port (fire) the board's reply arrives after the 0.6 s budget and
    # the APPLy is dropped on the followers (measured: NO.2-NO.4 stayed at level 0
    # after three fire() rounds, while the same commands through one session applied).
    for attempt in range(3):
        ctx.send(board, "SYSTem:TDMA:OPMode:STAGe 7")
        ctx.send(board, "SYSTem:TDMA:OPMode:APPLy")
        opmode = ctx.send(board, "SYSTem:TDMA:OPMode?")
        if opmode.split(",")[0].strip() == "7":
            break
        time.sleep(0.5)
    else:
        ctx.failures.append(f"slot {slot} Step 3 OPMode level 7 not applied ({opmode[:44]})")
    print(f"  slot {slot} opmode {opmode[:44]}", flush=True)


@step(4)
def step_4_topology(ctx, slot, board, uid):
    # Same as step 3: the topology is part of the identity the NVS calibration record
    # binds to, so write it on the persistent session and verify the node count.
    for attempt in range(3):
        ctx.send(board, "SYSTem:TDMA:RING:GEOMetry 0")
        ctx.send(board, f"SYSTem:TDMA:RING:TOPology {len(ROWS)},{slot},0")
        config = ctx.send(board, "SYSTem:TDMA:RING:CONFig?").split(",")
        if len(config) > 2 and config[1].strip() == str(len(ROWS)) and config[2].strip() == str(slot):
            break
        time.sleep(0.5)
    else:
        ctx.failures.append(f"slot {slot} Step 4 topology {len(ROWS)},{slot} not applied"
                            f" ({','.join(config[:4])})")
    if not ctx.poll(board, "SYSTem:TDMA:RING:STATus?", 39, None, tries=24):
        # equality-only wait: a repeated topology does not advance the sequence
        for _ in range(24):
            fields = ctx.send(board, "SYSTem:TDMA:RING:STATus?").split(",")
            if len(fields) > 39 and fields[38] == fields[39]:
                break
            time.sleep(0.25)
        else:
            print(f"  slot {slot} WARNING: ring config not committed", flush=True)
    print(f"  slot {slot} config {ctx.send(board, 'SYSTem:TDMA:RING:CONFig?')[:70]}", flush=True)


@step(5)
def step_5_diagnostic(ctx, slot, board, uid):
    ctx.send(board, "SYSTem:TDMA:RING:DIAGnostic 1")
    value = ctx.send(board, "SYSTem:TDMA:RING:DIAGnostic?")
    print(f"  slot {slot} diagnostic {'1' if value == '1' else value}"
          f"{'' if value == '1' else '  (origin trial will be refused)'}", flush=True)


@step(6)
def step_6_provisional(ctx, slot, board, uid):
    """Provisional DPLL training, with the precondition comparison that explains a 0.

    vdc_dpll_manager.c:4341-4363 refuses unless the ring is stopped, the staged ring
    config is enabled, a *feedback session is not open*, and the calibration stage is
    complete with node_count / topology_crc32 / profile_crc32 / schedule_crc32 equal
    to the staged ring config.
    """
    stage_raw = ctx.send(board, "READ:CALibration:TRAINing:STAGe?").replace('"', "")
    if slot in getattr(ctx, "restore_pending_slots", set()):
        # Step 0 saw a valid NVS record waiting for its context; steps 3-5 restored the
        # topology/schedule/profile, so the CalibrationManager service loads it within
        # a few service periods. Wait for that instead of judging the stale page.
        for _poll in range(20):
            if stage_complete(stage_raw):
                print(f"  slot {slot} NVS calibration record loaded: {stage_raw[:48]}",
                      flush=True)
                break
            time.sleep(0.25)
            stage_raw = ctx.send(board, "READ:CALibration:TRAINing:STAGe?").replace('"', "")
    config_raw = ctx.send(board, "SYSTem:TDMA:RING:CONFig?")
    session = ctx.send(board, "SYSTem:VDC:FEEDback:SESSion?")
    reply = ctx.send(board, "SYSTem:SYNC:VDC:DPLL:PROVisional 1")
    readback = ctx.send(board, "SYSTem:SYNC:VDC:DPLL:PROVisional?")
    stage = stage_raw.split(",")
    config = config_raw.split(",")
    verdict = "readback " + readback[:8]
    if len(stage) >= 10 and stage[0].strip().upper() == "TRN03STG" and len(config) >= 9:
        body = stage[1:]
        checks = {
            "complete==1": body[3] == "1",
            "node_count": body[1] == config[1],
            "topology_crc": body[6] == config[5],
            "profile_crc": body[7] == config[6],
            "schedule_crc": body[8] == config[4],
            "session_closed": session.strip() == "0",
            "staged_enabled": config[0] == "1",
        }
        bad = [name for name, ok in checks.items() if not ok]
        verdict += f" | stage {'OK' if not bad else 'FAILS: ' + ', '.join(bad)}"
        if bad:
            ctx.failures.append(f"slot {slot} Step 6 preconditions: {', '.join(bad)}")
            print(f"  slot {slot} stage  {stage_raw[:80]}", flush=True)
            print(f"  slot {slot} config {config_raw[:80]}", flush=True)
    else:
        # An EMPTY (or unreadable) stage page is the strongest failure of this step:
        # without a committed calibration stage the DPLL has no schedule, no
        # timestamp dictionary and no path-delay table, so it can never lock, and
        # the ring bring-up is refused as well (measured: NO.1/NO.2 report EMPTY,
        # Provisional answers 0 and the adapter start fails with reason 9).
        ctx.failures.append(f"slot {slot} Step 6 calibration stage missing or "
                            f"unreadable (page '{stage_raw[:24]}')")
        verdict += " | stage MISSING"
    if readback.strip().split(",")[0] not in ("1",):
        ctx.failures.append(f"slot {slot} Step 6 provisional readback {readback[:12]}")
    print(f"  slot {slot} provisional reply {reply[:20]} -> {verdict}", flush=True)


@step(7)
def step_7_clear_stores(ctx, slot, board, uid):
    ctx.send(board, "TRIGger:REServation:COUNter:CLEar")
    ctx.send(board, "TRIGger:REServation:COUNter:UNLoad")
    ctx.send(board, "TRIGger:REServation:DEPloyment:CLEar")
    ctx.send(board, "TRIGger:REServation:TEMPLate:RELease")
    ctx.send(board, "TRIGger:REServation:ACTion:CLEar")
    ctx.send(board, "TRIGger:REServation:TEMPlate:CLEar")
    if not ctx.poll(board, "TRIGger:REServation:ACTion?", 2, "0", tries=8):
        print(f"  slot {slot} WARNING: action table still installed", flush=True)
    print(f"  slot {slot} ACTion? {ctx.send(board, 'TRIGger:REServation:ACTion?')[:16]}",
          flush=True)


@step(8)
def step_8_grid(ctx, slot, board, uid):
    ctx.send(board, "TRIGger:REServation:GRID:CLEar")
    ctx.send(board, f"TRIGger:REServation:GRID:CONFigure "
                    f"{ctx.dep_gen},{ctx.dep_gen + 1},0,1000000,2000")
    print(f"  slot {slot} grid epoch {ctx.dep_gen}", flush=True)


@step(9)
def step_9_deployment(ctx, slot, board, uid):
    bound = seq = None
    for _attempt in range(6):
        for _poll in range(24):
            fields = ctx.send(board, "SYSTem:TDMA:RING:STATus?").split(",")
            if len(fields) > 39 and fields[38] == fields[39]:
                break
            time.sleep(0.25)
        seq = fields[38] if len(fields) > 38 else "?"
        ctx.send(board, f"TRIGger:REServation:DEPloyment:STAGe "
                        f"{ctx.dep_gen},{ctx.dep_crc},{ctx.payload}")
        ctx.send(board, f"TRIGger:REServation:DEPloyment:ACTivate {ctx.dep_gen},{ctx.dep_crc}")
        deployment = ctx.send(board, "TRIGger:REServation:DEPloyment?").split(",")
        bound = deployment[9] if len(deployment) > 9 else None
        if bound == seq:
            break
        time.sleep(0.3)
    else:
        print(f"  slot {slot} WARNING: deployment ring_config {bound} != ring seq {seq}",
              flush=True)
        ctx.failures.append(f"slot {slot} Step 9 binding stale ({bound} != {seq})")
    print(f"  slot {slot} deployment active?={deployment[2] if len(deployment) > 2 else '?'}"
          f" staged_crc={deployment[5] if len(deployment) > 5 else '?'}"
          f" active_crc={deployment[6] if len(deployment) > 6 else '?'}"
          f" ring_config={bound} seq={seq}", flush=True)


@step(10)
def step_10_installs(ctx, slot, board, uid):
    role, kind, arg = action_row(slot)
    acrc = action_table_crc(ctx.dep_gen, ctx.dep_gen, ctx.dep_crc, 1, 1, 0, 1000000, 2,
                            [(1, role, kind, arg, 0, 1)])
    reply = ctx.send(board, f"TRIGger:REServation:ACTion:INSTall {ctx.dep_gen},{acrc},"
                            f"{ctx.dep_gen},{ctx.dep_crc},1,1,0,1000000,2,1,1,"
                            f"{role},{kind},{arg},0,1")
    rows = [(1, ctx.dep_gen, role, kind, arg, 1, 0)]
    tcrc = template_crc(ctx.dep_gen, rows)
    template_reply = ctx.send(board, f"TRIGger:REServation:TEMPLate:INSTall "
                                     f"{ctx.dep_gen},{tcrc},1,"
                                     + ",".join(str(v) for row in rows for v in row))
    activate_reply = ctx.send(board, "TRIGger:REServation:TEMPLate:ACTivate")
    action = ctx.send(board, "TRIGger:REServation:ACTion?").split(",")
    template = ctx.send(board, "TRIGger:REServation:TEMPLate?").split(",")
    diag = ctx.send(board, "TRIGger:REServation:SEAT:MOUNT?").split(",")
    installed = action[2] if len(action) > 2 else "?"
    binding = action[4] if len(action) > 4 else "?"
    t_installed = template[2] if len(template) > 2 else "?"
    t_binding = template[3] if len(template) > 3 else "?"
    t_resources = template[4] if len(template) > 4 else "?"
    ok = installed == "1" and binding == "1" and t_installed == "1" \
        and t_binding == "1" and t_resources == "1"
    if not ok:
        ctx.failures.append(f"slot {slot} Step 10 stores not current")
    print(f"  slot {slot} ACTion:INSTall {reply[:24]} TEMPlate:INSTall {template_reply[:24]}"
          f" ACTivate {activate_reply[:24]}", flush=True)
    print(f"  slot {slot} ACTion? {installed}/{binding} TEMPlate?"
          f" {t_installed}/{t_binding}/{t_resources} diag "
          f"{diag[4] if len(diag) > 4 else '?'} {'OK' if ok else 'NOT CURRENT'}", flush=True)


@step(11)
def step_11_counter(ctx, slot, board, uid):
    """Load the counter and clear it, but do NOT start the pulse flow yet.

    Starting the counter / turntable / source here is what made the failure
    deterministic: Core0 never generates a preview for a position whose
    last_counter_count has already passed threshold_count
    (trigger_t2_runtime.c:919-924), and without a preview stage_one_freeze()
    returns immediately (trigger_t2_runtime.c:210-211). The pulse flow therefore
    starts in step 19, after the ring is up, the time base is locked and the cursor
    is valid - and only if the residual count is still below the first threshold.
    """
    if slot != 0:
        print(f"  slot {slot} counter: not the count source, skipped", flush=True)
        return
    # The deployment activation is processed by the stopped-state maintenance
    # worker, so a LOAD issued in the same breath can still see the previous
    # binding and refuse with CHANGED(6). Verify after LOAD and retry the
    # activation+load pair instead of assuming the round trip was enough.
    stage_reply = activate_reply = unload_reply = load_reply = clear_reply = ""
    loaded = "0"
    for _attempt in range(4):
        stage_reply = ctx.send(board, f"TRIGger:REServation:DEPloyment:STAGe "
                                      f"{ctx.dep_gen},{ctx.dep_crc},{ctx.payload}")
        activate_reply = ctx.send(board, f"TRIGger:REServation:DEPloyment:ACTivate "
                                         f"{ctx.dep_gen},{ctx.dep_crc}")
        unload_reply = ctx.send(board, "TRIGger:REServation:COUNter:UNLoad")
        load_reply = ctx.send(board, f"TRIGger:REServation:COUNter:LOAD "
                                     f"{ctx.dep_gen},{ctx.dep_crc}")
        after_load = ctx.send(board, "TRIGger:REServation:COUNter?").split(",")
        loaded = after_load[2] if len(after_load) > 2 else "0"
        if loaded == "1":
            break
        print(f"  slot {slot} counter LOAD attempt {_attempt + 1} -> {load_reply[:20]}"
              f" (loaded {loaded}); re-activating and retrying", flush=True)
        time.sleep(0.5)
    clear_reply = ctx.send(board, "TRIGger:REServation:COUNter:CLEar")
    page = ctx.send(board, "TRIGger:REServation:COUNter?").split(",")
    loaded = page[2] if len(page) > 2 else "?"
    state = page[13] if len(page) > 13 else "?"
    count = page[16] if len(page) > 16 else "?"
    if loaded != "1":
        ctx.failures.append(f"slot {slot} Step 11 counter not loaded after retries "
                            f"(LOAD -> {load_reply[:20]})")
    ctx.counter_after_clear = count
    print(f"  slot {slot} STAGe {stage_reply[:14]} ACTivate {activate_reply[:14]}"
          f" UNLoad {unload_reply[:14]} LOAD {load_reply[:14]} CLEar {clear_reply[:14]}",
          flush=True)
    print(f"  slot {slot} counter? loaded {loaded} state {state} count {count}"
          f" | binding gen {page[3] if len(page) > 3 else '?'}"
          f" crc {page[4] if len(page) > 4 else '?'}"
          f" role {page[6] if len(page) > 6 else '?'}"
          f" slot {page[7] if len(page) > 7 else '?'}"
          f" ring_config {page[8] if len(page) > 8 else '?'}"
          f" schedule_crc {page[9] if len(page) > 9 else '?'}"
          f" | asked gen {ctx.dep_gen} crc {ctx.dep_crc}", flush=True)


@step(12)
def step_12_t2_mount(ctx, slot, board, uid):
    # The mount is retried because a single round can land while the stopped-state worker
    # is still applying the deployment/table (the configure then answers last_error 1 and
    # the readback can time out, which used to be reported as "not mounted" and cost the
    # whole acceptance run its configured==4 requirement - measured on slot 1).
    configured = active = "?"
    diag = []
    for attempt in range(4):
        ctx.send(board, "SYSTem:TDMA:FLIGHT:FIFO:RESet")
        ctx.send(board, f"TRIGger:REServation:T2:CONFigure {ctx.dep_gen},0,14,"
                        f"{int(slot == 0)},5000,5000,950,5000,10000,15000,5000,1000")
        diag = ctx.send(board, "TRIGger:REServation:SEAT:MOUNT?").split(",")
        ctx.send(board, f"TRIGger:REServation:T2:STARt {ctx.dep_gen},0")
        state = ctx.send(board, "TRIGger:REServation:T2?").split(",")
        configured = state[2] if len(state) > 2 else "?"
        active = state[3] if len(state) > 3 else "?"
        if configured == "1" and active == "1":
            break
        if attempt < 3:
            print(f"  slot {slot} mount attempt {attempt + 1}: configured {configured}"
                  f" active {active} (last_error"
                  f" {state[1] if len(state) > 1 else '?'}); retrying", flush=True)
            time.sleep(0.5)
    if configured != "1" or active != "1":
        ctx.failures.append(f"slot {slot} Step 12 T2 not mounted after retries "
                            f"(configured {configured} active {active})")
    # The wording "configured ... active ..." is the acceptance contract
    # (p3_hardware_acceptance.py counts "T2 configured 1 active 1" lines); the
    # semantics of this mount step are explained in the step table, not by renaming
    # the field here.
    print(f"  slot {slot} configure diag {diag[4] if len(diag) > 4 else '?'}"
          f" -> T2 configured {configured} active {active}"
          f" (mounted, not started) last_error {state[1] if len(state) > 1 else '?'}",
          flush=True)


@step(13)
def step_13_vdc_session(ctx, slot, board, uid):
    """Open the feedback session and submit the VDC output run request.

    Every value here is a stopped-state write, so this step is only legal before the
    ring is enabled. The run backend cannot produce blocks until the ring and the
    model are available, so {schema, generation 0, state 0} immediately after the
    request is the expected pre-ring state, not a fault.
    """
    # Arm and start the turntable model BEFORE the VDC output takes the sync-io
    # scheduler: the model's pulse schedule is a legacy sync-io operation and
    # sync_io_pulse_schedule_arm_on_pin_common() refuses unless the schedule can be
    # reserved from IDLE (sync_io_model_sched.c:1046-1049). Starting it in step 14 or 19
    # - i.e. after this session's OUTPut:RUN - failed with fault_code 3 in both places
    # (measured "running 0 phase 5 emitted 0/181 fault 3"). Configure and start it here,
    # once, on the counter/position source board.
    src = ctx.src
    if src is not None and slot == 0:
        for command in ("CONFigure:MODEl:TURNtable:LOAD 0,3",
                        "CONFigure:MODEl:TURNtable:MOTion 360,360",
                        "CONFigure:MODEl:TURNtable:TRIGger 1,0,360,2,100,1,1000"):
            ctx.send(src, command)
        reply = ctx.send(src, "MODEl:TURNtable:STARt")
        model = ctx.send(src, "READ:MODEl:TURNtable:STATe?").split(",")
        running = model[2].strip() if len(model) > 2 else "?"
        print(f"  turntable pre-session start: reply {reply[:14]} running {running}"
              f" phase {model[3].strip() if len(model) > 3 else '?'}"
              f" emitted {model[8].strip() if len(model) > 8 else '?'}"
              f"/{model[7].strip() if len(model) > 7 else '?'}"
              f" fault {model[16].strip() if len(model) > 16 else '?'}", flush=True)
    ctx.send(board, "SYSTem:VDC:OUTPut:RUN:STOP")
    time.sleep(0.2)
    ctx.send(board, "SYSTem:VDC:OUTPut:TIMing 24000,32000,16000")
    # The feedback session must increase strictly (vdc_model_feedback.inc tracks the
    # last accepted session), so re-issuing a generation that was already used after
    # a teardown closes the session is REFUSED and leaves SESSion? at 0 - which then
    # refuses the origin trial and starves the DPLL. Skip the write when the session
    # already carries this generation, and name the case when it cannot.
    before = ctx.send(board, "SYSTem:VDC:FEEDback:SESSion?").strip()
    if before != str(ctx.dep_gen):
        ctx.send(board, f"SYSTem:VDC:FEEDback:SESSion {ctx.dep_gen}")
    ctx.send(board, "SYSTem:VDC:OUTPut:RUN 1000000,2000,0")
    opened = ctx.send(board, "SYSTem:VDC:FEEDback:SESSion?")
    run = ctx.send(board, "SYSTem:VDC:OUTPut:RUN?").split(",")
    ok = opened == str(ctx.dep_gen)
    if not ok:
        ctx.failures.append(f"slot {slot} Step 13 session {opened} != {ctx.dep_gen}"
                            f" (the session number must increase strictly: pass a"
                            f" larger --dep-generation)")
    print(f"  slot {slot} session {opened}"
          f" run schema/state {run[0] if run else '?'}"
          f"/{run[2] if len(run) > 2 else '?'} (state 0 before the ring is expected)"
          f" timing {ctx.send(board, 'SYSTem:VDC:OUTPut:TIMing?')[:20]}"
          f" {'OK' if ok else 'SESSION MISMATCH'}", flush=True)


@step(14)
def step_14_source(ctx, slot, board, uid):
    """Configure the turntable model and the counter source - but do NOT start them.

    The pulse flow is started in step 19, once the ring is up and the time base is
    locked, so the counters cannot run past the position thresholds before T2 has a
    valid cursor to observe them.
    """
    src = ctx.src
    if src is None:
        ctx.failures.append("source NO1 not discovered")
        print("  source NO1 NOT DISCOVERED - positions cannot be announced", flush=True)
        return
    # CONFigure:MODEl:TURNtable:TRIGger <dimension>,<start>,<stop>,<step>,<pulse_width_us>,
    # <rising_edge>,<timeout_ms> (scpi_model_commands.c:45-63). A 1-degree step over
    # 0..360 needs 361 pulses, but the model's schedule array holds
    # MODEL_TURNTABLE_MAX_SCHEDULE_PULSES = 256 entries (model_turntable.c:19), so
    # build_schedule() set fault_code 2, model_turntable_start() returned false and the
    # turntable never moved: measured "running 0 phase 0 emitted 0/361 fault 2", with the
    # counter flat at 0 for the whole run (no pulses, no preview, no publication).
    # A 2-degree step gives 181 pulses - inside the limit - and still sweeps 0..360.
    for command in ("CONFigure:MODEl:TURNtable:LOAD 0,3",
                    "CONFigure:MODEl:TURNtable:MOTion 360,360",
                    "CONFigure:MODEl:TURNtable:TRIGger 1,0,360,2,100,1,1000"):
        ctx.send(src, command)
    ctx.send(src, "TRIGger:REServation:SOURce:RELease")
    ctx.send(src, "TRIGger:REServation:SOURce:CONFigure 1000")
    # COUNter:STARt belongs HERE, before the ring is armed: counter_binding() refuses the
    # start once ring.enabled or ring.adapter_started is set, with COUNTER_CHANGED
    # (trigger_counter_service.c:56-61) - which is why the counter stayed at state 1
    # (LOADED) and count 0 for every run that started it in step 19, after the ring.
    # Counts still cannot run away: the pulses come from the PWM source, which is started
    # in step 19 after the cursor is valid.
    start_reply = ctx.send(src, "TRIGger:REServation:COUNter:STARt")
    counter = ctx.send(src, "TRIGger:REServation:COUNter?").split(",")
    print(f"  counter started before the ring: reply {start_reply[:20]}"
          f" loaded {counter[2] if len(counter) > 2 else '?'}"
          f" state {counter[13] if len(counter) > 13 else '?'}"
          f" count {counter[16] if len(counter) > 16 else '?'}", flush=True)
    # Start the turntable model HERE, before the ring comes up: its pulse schedule is a
    # legacy sync-io operation, and sync_io_pulse_schedule_arm_on_pin_common() refuses
    # unless the schedule can be reserved from IDLE (sync_io_model_sched.c:1046-1049) -
    # once the ring/VDC realtime path owns the scheduler the arm fails with fault_code 3
    # (measured: "running 0 phase 5 emitted 0/181 fault 3" when started in step 19).
    # The counter stays unstarted, so early model pulses cannot advance the count and the
    # threshold guard in step 19 is unaffected.
    start_reply = ctx.send(src, "MODEl:TURNtable:STARt")
    model = ctx.send(src, "READ:MODEl:TURNtable:STATe?").split(",")

    def model_field(index):
        return model[index].strip() if len(model) > index else "?"

    print(f"  turntable started early: reply {start_reply[:16]}"
          f" | loaded {model_field(0)} cfg {model_field(1)} running {model_field(2)}"
          f" phase {model_field(3)} emitted {model_field(8)}/{model_field(7)}"
          f" fault {model_field(16)}", flush=True)
    counter = ctx.send(src, "TRIGger:REServation:COUNter?").split(",")
    print(f"  turntable configured (not started): "
          f"{ctx.send(src, 'READ:MODEl:TURNtable:STATe?')[:46]}", flush=True)
    print(f"  source {ctx.send(src, 'TRIGger:REServation:SOURce?')[:36]}"
          f" counter loaded {counter[2] if len(counter) > 2 else '?'}"
          f" count {counter[16] if len(counter) > 16 else '?'} (not started)", flush=True)


@step(15)
def step_15_priority(ctx, slot, board, uid):
    """Priority class: SYNC on the origin, MATCH/FOLLow/PHASe on the peers."""
    src = ctx.src
    # Every write here needs a LIVE feedback session (priority_tx_set_stopped and the
    # match/follow setters all check it): without one they are refused and the retry
    # loops below would spend ~55 s proving it. Check once and fail fast with the
    # reason and the fix.
    probe = None
    for uid_r, _s, _i, _o, _f in ROWS:
        candidate = ctx.boards.get(f"{uid_r:016X}")
        if candidate is not None:
            probe = candidate
            break
    if probe is not None:
        session_now = ctx.send(probe, "SYSTem:VDC:FEEDback:SESSion?").strip()
        if session_now != str(ctx.dep_gen):
            ctx.failures.append(f"Step 15 refused: feedback session is {session_now}, "
                                f"expected {ctx.dep_gen} - run step 13 first with a "
                                f"larger generation")
            print(f"  session {session_now} != {ctx.dep_gen}: skipping the priority "
                  f"writes (they would all be refused)", flush=True)
            return
    if src is not None:
        ctx.set_priority(src, f"SYSTem:VDC:PRIORity:SYNC {ctx.dep_gen}",
                         "SYSTem:VDC:PRIORity:SYNC?", "SYNC")
    for node, (uid_r, _s, _i, _o, _f) in enumerate(ROWS):
        peer = ctx.boards.get(f"{uid_r:016X}")
        if peer is None or node == 0:
            continue
        ctx.send(peer, f"SYSTem:VDC:FEEDback:SESSion {ctx.dep_gen}")
        ctx.set_priority(peer, f"SYSTem:VDC:PRIORity:MATCH {ctx.dep_gen}",
                         "SYSTem:VDC:PRIORity:MATCH?", f"{uid_r:016X} MATCH")
        for _attempt in range(3):
            ctx.send(peer, f"SYSTem:VDC:FEEDback:SESSion {ctx.dep_gen}")
            ctx.send(peer, "SYSTem:VDC:OUTPut:RUN 1000000,2000,0")
            ctx.send(peer, "SYSTem:VDC:PRIORity:FOLLow 1")
            follow = ctx.send(peer, "SYSTem:VDC:PRIORity:FOLLow:STATus?").split(",")
            if len(follow) > 1 and follow[1] not in ("0", ""):
                break
            time.sleep(0.5)
        for _attempt in range(3):
            ctx.send(peer, "SYSTem:VDC:OUTPut:RUN 1000000,2000,0")
            ctx.send(peer, "SYSTem:VDC:PRIORity:FOLLow:PHASe 1")
            # FOLLow:PHASe? refuses service while the lane is active (8 s per attempt,
            # measured); its STATUS companion answers and carries the same evidence.
            phase = ctx.send(peer, "SYSTem:VDC:PRIORity:FOLLow:PHASe:STATus?").split(",")
            if phase and phase[0].strip() not in ("0", ""):
                break
            time.sleep(0.5)
        print(f"  {uid_r:016X} follow status "
              f"{ctx.send(peer, 'SYSTem:VDC:PRIORity:FOLLow:STATus?')[:36]}"
              f" phase status "
              f"{ctx.send(peer, 'SYSTem:VDC:PRIORity:FOLLow:PHASe:STATus?')[:28]}"
              f" health {ctx.send(peer, 'SYSTem:VDC:PRIORity:FOLLow:HEALth?')[:32]}",
              flush=True)


@step(16)
def step_16_ring_start(ctx, slot, board, uid):
    """Ring data path: BURSt 0, ARM the peers then the reference, TRAIN, START."""
    src = ctx.src
    if src is not None:
        ctx.send(src, "SYSTem:TDMA:RING:BURSt 0")
    for node, (uid_r, _s, _i, _o, _f) in enumerate(ROWS):
        peer = ctx.boards.get(f"{uid_r:016X}")
        if peer is None or node == 0:
            continue
        ctx.arm_and_wait(peer, f"{uid_r:016X}")
    if src is not None:
        ctx.arm_and_wait(src, "0010071E65B5CB38")
    for uid_r, _s, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        ctx.send(member, "SYSTem:TDMA:RING:TRAIN 1")
        for _poll in range(20):
            if "CLKTRAIN" in ctx.send(member, "SYSTem:TDMA:RING:TRAIN:STATus?"):
                break
            time.sleep(0.25)
        ctx.send(member, "SYSTem:TDMA:RING:START")
        status = ctx.send(member, "SYSTem:TDMA:RING:STATus?").split(",")
        if not (len(status) > 8 and status[0] != "0" and status[8] != "0"):
            ctx.failures.append(f"{uid_r:016X} Step 16 ring not up: {status[:9]}")
            print(f"  {uid_r:016X} ring NOT started after START: {status[:9]}", flush=True)
        else:
            print(f"  {uid_r:016X} ring enabled/started ok"
                  f" reason {status[7] if len(status) > 7 else '?'}", flush=True)
        # The reference cursor binds as soon as its own ring is up (bit 6 is already in
        # the step 2 mask), so probe it after every member START: a peer joining is the
        # first candidate for the post-bind identity change.
        cursor_probe(ctx, src, f"after START {uid_r:016X}")
    # The origin's trial authorises the carriers the DPLL locks onto, so it belongs
    # with the ring start - BEFORE the lock wait (measured: with the trial still in
    # step 19 the lock waited on input that never came: accepted 0, state CHECKING).
    # The PWM source is NOT started here any more: it is what advances the counter
    # (t2-20261012-091730: started here, 70726 counts in 71 s at 1 kHz, all three
    # thresholds passed before the cursor could observe them; frozen by SOURce:STOP).
    # It is started in step 19 once the cursor is valid. Keeping it out of this step
    # also separates the two candidates for the reference ring stopping here (that run
    # read en 0 / adapter 0 / config 154 -> 155 right after SOURce:STARt + ORIGin:TRIAL).
    if src is not None:
        grant = ctx.send(src, f"CALibration:ORIGin:TRIAL {ctx.dep_gen},8192,256,0")
        print(f"  origin trial: {grant[:40]}"
              f" | vdc {ctx.send(src, 'SYSTem:VDC:OUTPut:RUN?')[:36]}", flush=True)
        cursor_probe(ctx, src, "after ORIGin:TRIAL (source still stopped)")
    # The DPLL lock path is independent of T2: the VDC domain only accepts input when
    # the sync-IO observer is enabled (SYSTem:SYNC:VDC:LOCK:READiness? field 8 =
    # ring_input || sync_io_input, and readiness field 2 = reason 2 on every board that
    # reported input 0). Enabling the TDMA observer is what turns sync_io_input on
    # (scpi_sync_commands.c:2066-2089 -> vdc_dpll_manager_configure_sync_io_observer_tdma).
    for uid_r, _s, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        reply = ctx.send(member, "SYSTem:SYNC:VDC:OBServer:TDMA 1,0,100,0")
        ready = ctx.send(member, "SYSTem:SYNC:VDC:LOCK:READiness?").split(",")
        print(f"  {uid_r:016X} observer:TDMA -> {reply[:16]}"
              f" | input_present {ready[8].strip() if len(ready) > 8 else '?'}"
              f" {ready[0].strip() if ready else '?'}", flush=True)
        # Field map of SYSTem:SYNC:VDC:OBServer? (scpi_sync_commands.c:2215-2260):
        # [8] accepted, [9] rejected, [15] last_gate_reject_code, [29] schedule_crc32,
        # [30] dictionary_crc32, [31] dictionary_entry_count,
        # [32] dictionary_profile_crc32, and the timestamp predicate inputs
        # [34] source, [35] resolution_ns, [36] flags.
        observer = ctx.send(member, "SYSTem:SYNC:VDC:OBServer?").split(",")

        def field(index):
            return observer[index].strip() if len(observer) > index else "?"

        print(f"    observer accepted {field(8)} rejected {field(9)}"
              f" gate_reject {field(15)} | schedule_crc {field(29)}"
              f" dict_crc {field(30)} entries {field(31)} profile_crc {field(32)}"
              f" | ts_source {field(34)} ts_res_ns {field(35)} ts_flags {field(36)}"
              f" | src_slot {field(37)} ref_slot {field(38)}"
              f" class {field(39)}", flush=True)
        # Service/word/edge counters separate "the sync-IO never delivered a word"
        # from "words arrive but no edge is classified".
        print(f"    observer words: max_per_service {field(1)} services {field(2)}"
              f" raw {field(3)} no_edge {field(4)} ambiguous {field(5)}"
              f" bad_arg {field(6)} submitted {field(7)}", flush=True)
        # The followers lock through the ring path (the origin goes through its own
        # snapshot), so print the TDMA->VDC input plan/physical/status too: the
        # observer fields show it sees frames (dictionary entries with a matching
        # profile CRC) yet never captures a sample.
        for page in ("SYSTem:SYNC:VDC:TDMA:PLAN?",
                     "SYSTem:SYNC:VDC:TDMA:PHYS?",
                     "SYSTem:SYNC:VDC:TDMA:STATus?",
                     "SYSTem:SYNC:VDC:PATH:DELay?"):
            print(f"    {page[:-1]:<28} {ctx.send(member, page)[:150]}", flush=True)
    # The VDC sync establishment transaction. HAOFV_VDC_DPLL_ARCHITECTURE.md (§9 command
    # tree, §11 four-board establishment chain) defines it as
    # NodeRoleMap -> active calibration -> ring heartbeat -> SYNC:CHECk -> SYNC:STARt
    # -> compact timestamp exchange -> per-node offset/rate -> global LOCK. This chain
    # never issued SYNC:CHECk / SYNC:STARt, so the VDC state machine was never started:
    # the followers' VDC service_count/sync_seq stayed frozen and their DCO was never
    # updated (update_seq 1), i.e. nothing was following anything.
    for uid_r, _s, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        check = ctx.send(member, "SYNC:CHECk").strip()
        start = ctx.send(member, "SYNC:STARt").strip()
        state = ctx.send(member, "READ:SYNC:STATe?").strip()
        print(f"  {uid_r:016X} SYNC:CHECk -> {check[:46]} | STARt -> {start[:14]}"
              f" | READ:SYNC:STATe? {state[:70]}", flush=True)
    cursor_probe(ctx, ctx.src, "after OBServer:TDMA + SYNC:CHECk/STARt")
    # Post-ring priority arming (TDMA_TASK_PROGRESS, 2026-10-11 21:50 root-cause note):
    # the SYNC_TRIGGER phase only fits the cycle when the priority source is active -
    # its window (279736 ns) is narrower than its declared WCET (332000 ns), so
    # without the priority ingress margin every cycle skips it via start_missed
    # (app.c:888/904-905, 930-931). FOLLow 1 needs a live session, MATCH generation
    # consistency and a successful boundary_set_stopped() (vdc_priority_follow.inc:60-71),
    # so the stopped-state write in step 15 is normally refused (FOLLow? = 0) and the
    # sync phase never runs. Re-assert here, with the ring running, and verify the
    # readback - the documented rule is not to look at publication before this passes.
    for uid_r, slot_r, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        if slot_r == ROWS[0][1]:
            ctx.send(member, f"SYSTem:VDC:PRIORity:SYNC {ctx.dep_gen}")
        else:
            ctx.send(member, f"SYSTem:VDC:FEEDback:SESSion {ctx.dep_gen}")
            ctx.send(member, f"SYSTem:VDC:PRIORity:MATCH {ctx.dep_gen}")
            ctx.send(member, "SYSTem:VDC:PRIORity:FOLLow 1")
            ctx.send(member, "SYSTem:VDC:PRIORity:FOLLow:PHASe 1")
        status = ctx.send(member, "SYSTem:VDC:PRIORity:FOLLow:STATus?").split(",")
        # Only read the follow status on followers: the reference has no follow lane and a
        # refused read costs the 8 s query timeout (measured: step 16 taking 75 s because
        # of FOLLow:STATus?/HEALth? reads). Health is derived from the same status page.
        status = status if slot_r != ROWS[0][1] else []
        readable = bool(status) and not any("<timeout>" in value for value in status)
        armed = readable and status[0].strip() not in ("0", "")
        print(f"  post-ring priority {uid_r:016X} slot {slot_r}:"
              f" follow {'ARMED' if armed else 'not armed' if readable else 'n/a (reference)'}"
              f" status {','.join(x.strip() for x in status[:6])}", flush=True)
        if slot_r != ROWS[0][1] and readable and not armed:
            ctx.failures.append(f"{uid_r:016X} Step 16 post-ring FOLLow not armed"
                                f" (status {','.join(x.strip() for x in status[:4])}); the"
                                f" SYNC_TRIGGER phase cannot run without priority ingress")
    cursor_probe(ctx, ctx.src, "after post-ring priority")
    # EXPERIMENT (--pre-lock-mask): assert the sync-trigger bit BEFORE waiting for the
    # lock. The chain normally withholds bit 6 until the lock, which would deadlock if
    # the sync captures the VDC domain accepts are themselves produced by that phase
    # (the old passing run had mask 91, and the failing ones read back 27).
    if getattr(ctx, "pre_lock_mask", None):
        for uid_r, _s, _i, _o, _f in ROWS:
            member = ctx.boards.get(f"{uid_r:016X}")
            if member is not None:
                ctx.set_load_mask(member, ctx.pre_lock_mask,
                                  f"{uid_r:016X} pre-lock experiment")
        print(f"  pre-lock mask {ctx.pre_lock_mask} asserted on all boards", flush=True)


@step(17)
def step_17_lock_and_dynamic_start(ctx, slot, board, uid):
    """Observe the VDC phase lock, then perform the DYNAMIC T2 start (mask bit 6).

    Reference and followers are judged differently because the firmware says so: a
    non-reference board is a FOLLOWER and deliberately bypasses the local evidence
    servo - "do not touch the local PI, integrator, DCO command or lock state"
    (vdc_domain.c:4076-4095). LOCK:READINESS? also demands accepted_sample_count != 0
    unconditionally (scpi_sync_commands.c:2029), and a follower never increments that
    counter. Requiring ready[1] on all four boards is therefore unsatisfiable and was
    the reason this step could never pass, while the scope showed a live lock.
    So: the reference must report LOCKED; each follower must report VDC ready with a
    growing sync_seq - its observation is running against the locked reference.
    """
    print("  VDC lock detection (up to 30 s: the DPLL preparation alone takes ~18 s;"
          " authoritative state = READ:SYNC:STATe?)", flush=True)
    locked = {}
    reference_uid = f"{ROWS[0][0]:016X}"
    for attempt in range(10):
        for uid_r, _s, _i, _o, _f in ROWS:
            member = ctx.boards.get(f"{uid_r:016X}")
            if member is None or locked.get(uid_r):
                continue
            ready = ctx.send(member, "SYSTem:SYNC:VDC:LOCK:READiness?").split(",")
            run = ctx.send(member, "SYSTem:VDC:OUTPut:RUN?").split(",")
            state_page = ctx.send(member, "READ:SYNC:STATe?")
            is_reference = f"{uid_r:016X}" == reference_uid
            # READ:SYNC:STATe? is the product-level, authoritative answer and it is
            # LOCKED on every board (followers included) once SYNC:STARt has run - the
            # maintenance readiness page stays at "not locked" for a follower by design.
            product_locked = '"LOCKED"' in state_page
            if product_locked:
                locked[uid_r] = True
            print(f"  lock t+{3 * (attempt + 1)}s {uid_r:016X}"
                  f"{' [ref]' if is_reference else ' [fol]'}:"
                  f" SYNC:STATe {'LOCKED' if product_locked else state_page[:22]}"
                  f" | readiness input_ready {ready[0] if ready else '?'}"
                  f" locked {ready[1] if len(ready) > 1 else '?'}"
                  f" (dpll.state {ready[3] if len(ready) > 3 else '?'},"
                  f" reason {ready[2] if len(ready) > 2 else '?'})"
                  f" accepted {ready[5] if len(ready) > 5 else '?'}"
                  f" run_blocks {run[4] if len(run) > 4 else '?'}", flush=True)
        if len(locked) == len(ROWS):
            break
        time.sleep(3.0)
    if len(locked) != len(ROWS):
        # Self-diagnosis for the boards that never accepted a sample: the readiness
        # page says what the gate wants, the schedule phase-7 counter says whether the
        # sync-trigger phase runs there at all, and the role says whether the board
        # even considers itself a follower. (Measured: the origin locks, the followers
        # stay at accepted 0 / rejected 0 - i.e. nothing arrives, not "rejected".)
        for uid_r, _s, _i, _o, _f in ROWS:
            member = ctx.boards.get(f"{uid_r:016X}")
            if member is None:
                continue
            ready = ctx.send(member, "SYSTem:SYNC:VDC:LOCK:READiness?").split(",")
            mask = ctx.send(member, "SYSTem:TDMA:LOAD:MASK?").split(",")[0].strip()
            role = ctx.send(member, "SYSTem:SYNC:VDC:DPLL:ROLE?").strip()
            sched = ctx.send(member, "SYSTem:TDMA:SCHEDule?").split(",")
            index = SCHEDULE_HEADER + SCHEDULE_STRIDE * SCHEDULE_PHASE_SYNC_TRIGGER + 6
            runs = sched[index].strip() if len(sched) > index else "?"
            print(f"  diag {uid_r:016X}{' LOCKED' if uid_r in locked else ' (no lock)'}:"
                  f" role {role[:24]} mask {mask} sync_phase_runs {runs}"
                  f" ready[{len(ready)}] {','.join(x.strip() for x in ready[:12])}",
                  flush=True)
            # The DPLL lock does not depend on T2: what matters is whether this board's
            # VDC input path sees the ring at all. These pages answer that directly
            # (measured: followers report accepted 0 AND rejected 0 - nothing arrives).
            for page in ("SYSTem:SYNC:VDC:STATus?",
                         "SYSTem:SYNC:VDC:DPLL:STATus?",
                         "SYSTem:SYNC:VDC:DCO?",
                         "SYSTem:SYNC:VDC:DPLL:TRACe:STATus?",
                         "SYSTem:SYNC:VDC:DPLL:PROVisional?",
                         "SYSTem:SYNC:VDC:TDMA:RING?",
                         "SYSTem:SYNC:VDC:OBServer:RING?"):
                value = ctx.send(member, page)
                print(f"    {page[:-1]:<28} {value[:120]}", flush=True)
        missing = [f"{uid:016X}" for uid, _s, _i, _o, _f in ROWS if uid not in locked]
        ctx.failures.append(f"Step 17 not locked on {missing}")
        ctx.lock_failed = True
        print(f"  lock detection: NOT locked on {missing} - dynamic T2 start withheld",
              flush=True)
        cursor_probe(ctx, ctx.src, "lock failed")
        return
    for uid_r, _s, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is not None:
            ctx.set_load_mask(member, 91, f"{uid_r:016X} dynamic T2 start")
    print("  dynamic T2 start: LOAD:MASK bit 6 asserted on all boards", flush=True)
    cursor_probe(ctx, ctx.src, "after lock + mask 91")


@step(18)
def step_18_preflight_source_trial(ctx, slot, board, uid):
    """Preflight verdict, then SOURce:STARt and the origin trial."""
    time.sleep(2.0)
    frozen = []
    for uid_r, slot_r, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        mask = ctx.send(member, "SYSTem:TDMA:LOAD:MASK?").split(",")[0].strip()
        state = ctx.send(member, "TRIGger:REServation:T2?").split(",")
        ring = ctx.send(member, "SYSTem:TDMA:RING:STATus?").split(",")
        schedule = ctx.send(member, "SYSTem:TDMA:SCHEDule?").split(",")
        runs1 = schedule_field(schedule, SCHEDULE_PHASE_SYNC_TRIGGER, SCHEDULE_RUN_COUNT)
        time.sleep(2.0)
        schedule2 = ctx.send(member, "SYSTem:TDMA:SCHEDule?").split(",")
        runs2 = schedule_field(schedule2, SCHEDULE_PHASE_SYNC_TRIGGER, SCHEDULE_RUN_COUNT)
        miss2 = schedule_field(schedule2, SCHEDULE_PHASE_SYNC_TRIGGER, SCHEDULE_START_MISS)
        sync = mask.isdigit() and (int(mask) & 64) == 64
        segments = "/".join(state[index] if len(state) > index else "?"
                            for index in T2_SEGMENTS)
        readable_segments = all(value not in ("?",) for value in segments.split("/"))
        if readable_segments and not any(value not in ("0", "?")
                                         for value in segments.split("/")):
            # segment_max_cycles is written by the T2 service on every call, so a
            # non-zero entry is the service actually running. (The old indices
            # 47..49 read freeze_resend_count and friends, which are legitimately 0.)
            ctx.failures.append(f"slot {slot_r} Step 18 service never ran (segments {segments})")
        elif not readable_segments:
            print(f"  slot {slot_r} segments unreadable this pass ({segments});"
                  f" not treated as 'never ran'", flush=True)
        up = len(ring) > 8 and ring[0] != "0" and ring[8] != "0"
        print(f"  preflight slot {slot_r}: mask {mask} (bit6 {'set' if sync else 'CLEAR'})"
              f" quarantine {schedule[5] if len(schedule) > 5 else '?'}"
              f" ring {'ok' if up else 'NOT UP'}"
              f" | phase6 runs {runs1}->{runs2}"
              f" ({'running' if runs1 != runs2 else 'NOT RUNNING'})"
              f" | T2 cfg {state[2] if len(state) > 2 else '?'}"
              f" act {state[3] if len(state) > 3 else '?'} segments {segments}"
              f" => {'service running' if segments not in ('0', '?') else 'SERVICE NEVER RAN'}",
              flush=True)
        frozen.append(runs1 == runs2 and segments in ("0", "?"))
    ctx.preflight_frozen = bool(frozen) and all(frozen)
    if ctx.preflight_frozen:
        ctx.failures.append("Step 18 preflight: SYNC_TRIGGER phase frozen everywhere")
    src = ctx.src
    # Time-base readiness: the T2 cursor must be valid and bound to THIS run's
    # session before the pulse flow starts (step 19). Otherwise the counters pass
    # the position thresholds while nothing is observing them, Core0 never builds a
    # preview and publication is impossible no matter how often it is retried.
    for uid_r, slot_r, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
    # Wait for the cursor instead of sampling once: publication needs it. Measured
    # contrast - the run whose cursor read valid 1 / seq 88288 published 20 positions,
    # while runs that read valid 0 / err 7 announced nothing even though the hardware
    # counter was advancing (51505 -> 64235) and the model/session matched.
    wait_deadline = time.time() + 24.0
    reference_ready = False
    attempts = 0
    while time.time() < wait_deadline:
        cursor = ctx.send(src, "TRIGger:REServation:T2:CURSor?").split(",")
        err = cursor[1] if len(cursor) > 1 else "?"
        valid = cursor[3] if len(cursor) > 3 else "?"
        epoch = cursor[4] if len(cursor) > 4 else "?"
        session = cursor[6] if len(cursor) > 6 else "?"
        seq = cursor[7] if len(cursor) > 7 else "?"
        ring = ctx.send(src, "SYSTem:TDMA:RING:STATus?").split(",")
        ring_ok = len(ring) > 8 and ring[0] != "0" and ring[8] != "0"
        if err == "0" and valid == "1" and epoch == str(ctx.dep_gen):
            reference_ready = True
            print(f"  reference cursor READY after wait: err 0 valid 1 epoch {epoch}"
                  f" session {session} seq {seq} ring {'ok' if ring_ok else 'NOT UP'}",
                  flush=True)
            break
        # Identity reconciliation. err 7 with seq frozen is a sticky IDENTITY fault, which
        # the firmware can only set after one successful bind (trigger_time_cursor.c
        # 241/257/265: sticky == s_binding.bound); the probes in steps 16/17 bracket when.
        # Note T2:CURSor?[9] is the legacy identity_crc32 (always 0): the bound ring
        # config_seq is [10] and the bound model token [11].
        cursor_probe(ctx, src, f"step 18 wait {attempts + 1}")
        attempts += 1
        if attempts == 3:
            # Expected to be refused while the ring runs (GRID:CONFigure goes through
            # tdma_service_update_stopped_metadata); kept as evidence of that refusal.
            reply = ctx.send(src, f"TRIGger:REServation:GRID:CONFigure "
                                  f"{ctx.dep_gen},{ctx.dep_gen + 1},0,1000000,2000")
            print(f"  cursor recovery attempt: GRID:CONFigure -> {reply[:40]}", flush=True)
        time.sleep(2.0)
    ctx.reference_ready = reference_ready
    if not reference_ready:
        ctx.failures.append(f"Step 18 reference cursor never became valid within 24 s"
                            f" (last err {err} valid {valid} seq {seq}); without a valid"
                            f" cursor Core0 saves no preview and nothing is published")
        print(f"  reference cursor NOT READY: err {err} valid {valid} epoch {epoch}"
              f" session {session} seq {seq}", flush=True)


@step(19)
def step_19_trial_and_pulse_start(ctx, slot, board, uid):
    """Start the pulse flow - gated on a valid cursor and the first threshold.

    The origin trial stays in step 16 (the DPLL needs its carriers before it can lock).
    The PWM source is what advances the counter, so it is started only here, after the
    reference cursor is valid and while the count is still below the first threshold.
    """
    src = ctx.src
    if src is None:
        ctx.failures.append("Step 19: source NO1 missing")
        return
    state = ctx.send(src, "TRIGger:REServation:T2?").split(",")
    thresholds = [state[index] if len(state) > index else "?" for index in T2_THRESHOLDS]
    counter = ctx.send(src, "TRIGger:REServation:COUNter?").split(",")
    current = counter[16] if len(counter) > 16 else "?"
    print(f"  thresholds (target,threshold) "
          f"{[(state[i] if len(state) > i else '?', state[j] if len(state) > j else '?') for i, j in zip(T2_TARGETS, T2_THRESHOLDS)]}"
          f" current count {current}", flush=True)
    if not ctx.reference_ready:
        # Without a valid cursor this run cannot publish whatever happens next (the
        # count would pass every threshold unobserved). The source is still started,
        # as diagnosis only, to settle whether SOURce:STARt alone stops the ring.
        ctx.failures.append("Step 19 reference cursor not valid (step 18): this run"
                            " cannot publish; PWM source started for diagnosis only")
        reply = ctx.send(src, "TRIGger:REServation:SOURce:STARt")
        print(f"  cursor not valid - SOURce:STARt for diagnosis only -> {reply[:44]}",
              flush=True)
        cursor_probe(ctx, src, "after SOURce:STARt (diagnosis)")
        time.sleep(1.0)
        cursor_probe(ctx, src, "after SOURce:STARt +1s (diagnosis)")
        return
    first = thresholds[0]
    if current.isdigit() and first.isdigit() and int(current) >= int(first):
        # Fatal: Core0 builds no preview for a position whose threshold the count has
        # already reached (trigger_t2_runtime.c prepare loop: last_counter_count >=
        # threshold_count -> continue), so nothing of this run can be published. The
        # source has not been started yet, so something else advanced the count.
        ctx.failures.append(f"Step 19 counter {current} already at/past the first"
                            f" threshold {first} before the PWM source was started;"
                            f" no position of this run can be published")
        print(f"  counter {current} already past the first threshold {first}"
              f" before SOURce:STARt - pulse flow not started", flush=True)
        return
    reply = ctx.send(src, "TRIGger:REServation:SOURce:STARt")
    print(f"  TRIGger:REServation:SOURce:STARt          -> {reply[:44]}", flush=True)
    # Second half of the step 16 split: does SOURce:STARt by itself stop the ring?
    cursor_probe(ctx, src, "after SOURce:STARt")
    # The turntable has to actually move or no pulse ever reaches the counter: measured
    # READ:MODEl:TURNtable:LOAD? = 1,0,3 (not armed) and MOTion? = 360.0,360.0 (sitting on
    # its end angle) with the counter flat at 0 for the whole sampling window. Load/arm
    # the model, start it again, and verify the angle advances.
    load_page = ctx.send(src, "READ:MODEl:TURNtable:LOAD?").split(",")
    motion_page = ctx.send(src, "READ:MODEl:TURNtable:MOTion?").split(",")
    print(f"  turntable before: LOAD {','.join(x.strip() for x in load_page[:4])}"
          f" MOTion {','.join(x.strip() for x in motion_page[:3])}", flush=True)
    if len(load_page) > 1 and load_page[1].strip() in ("0", ""):
        print(f"  turntable not armed -> LOAD:"
              f" {ctx.send(src, 'CONFigure:MODEl:TURNtable:LOAD 1')[:24]}", flush=True)
    ctx.send(src, "MODEl:TURNtable:STARt")
    # READ:MODEl:TURNtable:STATe? = loaded, configured, running, phase, slot, output_index,
    # dimension, total_pulses, emitted, accel, cruise, decel, min_interval_us,
    # max_interval_us, pulse_width_us, last_interval_us, fault_code, current_position
    # (scpi_model_commands.c:130-153). fault_code 3 with phase FAULT means
    # sync_io_model_pulse_schedule_arm() refused (model_turntable.c:281-288).
    for poll in range(3):
        time.sleep(1.0)
        motion_page = ctx.send(src, "READ:MODEl:TURNtable:MOTion?").split(",")
        state_page = ctx.send(src, "READ:MODEl:TURNtable:STATe?").split(",")
        live = ctx.send(src, "TRIGger:REServation:COUNter?").split(",")

        def state_field(index):
            return state_page[index].strip() if len(state_page) > index else "?"

        print(f"  motion poll +{poll + 1}s:"
              f" angle {motion_page[0].strip() if motion_page else '?'}"
              f" | model loaded {state_field(0)} cfg {state_field(1)}"
              f" running {state_field(2)} phase {state_field(3)}"
              f" emitted {state_field(8)}/{state_field(7)}"
              f" fault {state_field(16)}"
              f" | count {live[16] if len(live) > 16 else '?'}", flush=True)
    counter = ctx.send(src, "TRIGger:REServation:COUNter?").split(",")
    print(f"  pulse flow started: count {counter[16] if len(counter) > 16 else '?'}"
          f" state {counter[13] if len(counter) > 13 else '?'}"
          f" gen {counter[3] if len(counter) > 3 else '?'}"
          f" crc {counter[4] if len(counter) > 4 else '?'}"
          f" io.state {counter[13] if len(counter) > 13 else '?'}"
          f" | vdc {ctx.send(src, 'SYSTem:VDC:OUTPut:RUN?')[:40]}", flush=True)
    print(f"  counter page: {','.join(counter[:17])}", flush=True)
    print(f"  source page : {ctx.send(src, 'TRIGger:REServation:SOURce?')[:90]}", flush=True)
    # MODEl:TURNtable? is not a command: the valid readbacks are the READ:MODEl:TURNtable:*
    # pages (scpi_model_commands.h:17-24), and the counter is polled briefly because the
    # turntable model may need a moment before pulses appear.
    for label, page in (("LOAD", "READ:MODEl:TURNtable:LOAD?"),
                        ("TRIGger", "READ:MODEl:TURNtable:TRIGger?"),
                        ("MOTion", "READ:MODEl:TURNtable:MOTion?")):
        print(f"  turntable {label:<8}: {ctx.send(src, page)[:86]}", flush=True)
    # Verify the pulse path instead of publishing zeros silently: the counter must be
    # RUNNING and its count must actually advance. Measured failure mode: counter state 2
    # and the PWM source "running 1" at 1000 Hz, yet count stayed 0 - nothing reached the
    # counter input - and the run then announced nothing (prep .../0, published_mask 0)
    # while looking healthy step by step.
    counts = []
    for poll in range(3):
        time.sleep(1.0)
        live = ctx.send(src, "TRIGger:REServation:COUNter?").split(",")
        value = live[16].strip() if len(live) > 16 else "?"
        counts.append(value)
        print(f"  count poll +{poll + 1}s: loaded {live[2] if len(live) > 2 else '?'}"
              f" state {live[13] if len(live) > 13 else '?'}"
              f" count {value}", flush=True)
    numeric = [int(value) for value in counts if value.isdigit()]
    if len(numeric) >= 2 and numeric[-1] == numeric[0]:
        ctx.failures.append(f"Step 19 counter not advancing (count {counts}); pulses are"
                            f" not reaching the counter input, so no position can be"
                            f" observed or published (check the sync-out 3 -> sync-in 1"
                            f" loopback and the PWM source pin)")


@step(20)
def step_20_sampling(ctx, slot, board, uid):
    """Sample the T2 runtime and the position transport for ~10 s."""
    if ctx.preflight_frozen:
        print("  sampling skipped: SYNC_TRIGGER phase frozen on every board", flush=True)
        return
    published = "?"
    for tick in range(5):
        time.sleep(2.0)
        for uid_r, slot_r, _i, _o, _f in ROWS:
            member = ctx.boards.get(f"{uid_r:016X}")
            if member is None:
                continue
            state = ctx.send(member, "TRIGger:REServation:T2?").split(",")
            position = ctx.send(member, "SYSTem:REFMEM:SYNC:FLIGHT:POSition?")
            counter = ctx.send(member, "TRIGger:REServation:COUNter?").split(",")
            # Realtime health of the SYNC_TRIGGER load on every sample: a quarantined bit 6
            # stops the T2 service, the cursor and the counter read together (measured
            # t2-20261012-093219: count/cursor seq/sync_seq all frozen 2 s into sampling,
            # right after the second threshold, with Core0 still preparing). Header [4]
            # enabled_mask, [5] quarantined_mask, [7] schedule_miss_count; per phase
            # [5] max_runtime, [6] run, [8] start_miss, [9] overrun, [10] deadline_miss.
            schedule = ctx.send(member, "SYSTem:TDMA:SCHEDule?").split(",")

            def sched(offset):
                return schedule_field(schedule, SCHEDULE_PHASE_SYNC_TRIGGER, offset)

            quarantine = schedule[5].strip() if len(schedule) > 5 else "?"
            print(f"t+{2 * (tick + 1)}s slot {slot_r} sched: enabled"
                  f" {schedule[4].strip() if len(schedule) > 4 else '?'}"
                  f" quarantined {quarantine}"
                  f"{' (SYNC_TRIGGER QUARANTINED)' if quarantine.isdigit() and int(quarantine) & 64 else ''}"
                  f" misses {schedule[7].strip() if len(schedule) > 7 else '?'}"
                  f" | phase7 run {sched(6)} max_rt {sched(5)} start_miss {sched(8)}"
                  f" overrun {sched(9)} deadline_miss {sched(10)}", flush=True)
            if (quarantine.isdigit() and int(quarantine) & 64
                    and f"slot {slot_r} quarantined" not in " ".join(ctx.failures)):
                ctx.failures.append(f"Step 20 slot {slot_r} quarantined SYNC_TRIGGER"
                                    f" (mask {quarantine}, overrun {sched(9)},"
                                    f" deadline_miss {sched(10)}, max_rt {sched(5)})")
            if slot_r == 0:
                cursor = ctx.send(member, "TRIGger:REServation:T2:CURSor?").split(",")
                model = ctx.send(member, "SYSTem:VDC:FEEDback:MODel?").split(",")
                ring = ctx.send(member, "SYSTem:TDMA:RING:STATus?").split(",")
                print(f"t+{2 * (tick + 1)}s vdc session "
                      f"{ctx.send(member, 'SYSTem:VDC:FEEDback:SESSion?')[:12]}"
                      f" | model ok {model[0] if model else '?'}"
                      f" token {model[1] if len(model) > 1 else '?'}"
                      f" session {model[2] if len(model) > 2 else '?'}"
                      f" | ring enabled {ring[0] if ring else '?'}"
                      f" started {ring[8] if len(ring) > 8 else '?'}", flush=True)
                segments = "/".join(state[index] if len(state) > index else "?"
                                    for index in T2_SEGMENTS)
                print(f"t+{2 * (tick + 1)}s cursor err "
                      f"{cursor[1] if len(cursor) > 1 else '?'}"
                      f" cfg {cursor[2] if len(cursor) > 2 else '?'}"
                      f" valid {cursor[3] if len(cursor) > 3 else '?'}"
                      f" epoch {cursor[4] if len(cursor) > 4 else '?'}"
                      f" session {cursor[6] if len(cursor) > 6 else '?'}"
                      f" seq {cursor[7] if len(cursor) > 7 else '?'}"
                      f" pulse {t2_field(state, 'pulse_index')}"
                      f"/{t2_field(state, 'fine_slice_index')}"
                      f" model_token {cursor[11] if len(cursor) > 11 else '?'}"
                      # segment_max_cycles lives at [42..44] and is written by the T2
                      # service on every call; the old [47..49] read freeze counters.
                      f" | segments {segments}"
                      f" sync_seq {state[8] if len(state) > 8 else '?'}", flush=True)
                fields = position.split(",")
                published = t2_field(state, "published_mask")
                print(f"t+{2 * (tick + 1)}s counter loaded "
                      f"{counter[2] if len(counter) > 2 else '?'} count "
                      f"{counter[16] if len(counter) > 16 else '?'}"
                      f" | T2 counter {t2_field(state, 'last_counter_count')}"
                      f" published_mask {t2_field(state, 'published_mask')}"
                      f" announced {t2_field(state, 'announced_mask')}"
                      f" targets {[state[i] if len(state) > i else '?' for i in T2_TARGETS]}"
                      f" thresholds {[state[i] if len(state) > i else '?' for i in T2_THRESHOLDS]}"
                      f" prep {t2_field(state, 'prepare_calls')}"
                      f"/{t2_field(state, 'preview_saved')}"
                      f" reason {t2_field(state, 'prepared_last_reason')}"
                      f" | POS pub {fields[5] if len(fields) > 5 else '?'}"
                      f" rx {fields[12] if len(fields) > 12 else '?'}"
                      # The rolling window's health facts are the TAIL of T2? (the block
                      # appended after the event log; its order is guarded by
                      # tests/python/test_t2_page_field_order.py), so they are read by
                      # slicing from the end. The earlier copy of this print pattern in a
                      # different step does not run here - which is why the bench log had
                      # no window fields at all until this block was updated.
                      f" | window announced {state[-9:][1] if len(state) >= 9 else '?'}"
                      f"/entered {state[-9:][0] if len(state) >= 9 else '?'}"
                      f" overwrote_unannounced {state[-9:][2] if len(state) >= 9 else '?'}"
                      f" late {state[-9:][3] if len(state) >= 9 else '?'}"
                      f" step_mdeg {state[-9:][6] if len(state) >= 9 else '?'}"
                      f" geometry {state[-9:][7] if len(state) >= 9 else '?'}pos"
                      f"/{state[-9:][8] if len(state) >= 9 else '?'}pul",
                      flush=True)
                # T2? ends with the origin FREEZE path maxima (clk_sys cycles):
                # pending check, threshold stage, resend/ack pass, whole call. They
                # split segments[1] (RECEIVE) to show where a threshold slice spends.
                freeze = [value.strip() for value in state[-4:]]
                freeze += ["?"] * (4 - len(freeze))  # a <timeout> reply has one field
                print(f"t+{2 * (tick + 1)}s freeze path max cycles: pending {freeze[0]}"
                      f" stage {freeze[1]} resend {freeze[2]} total {freeze[3]}"
                      f" | RECEIVE {state[T2_SEGMENTS[1]] if len(state) > T2_SEGMENTS[1] else '?'}",
                      flush=True)
            print(f"t+{2 * (tick + 1)}s {uid_r:016X} slot {slot_r}"
                  f" err {state[1] if len(state) > 1 else '?'}"
                  f" cfg {state[2] if len(state) > 2 else '?'}"
                  f" act {state[3] if len(state) > 3 else '?'}"
                  f" ev {state[13] if len(state) > 13 else '?'}"
                  # Every board's segment maxima: the follower phase ran close to its
                  # WCET (t2-20261012-104142: slot 2 max_rt 28916 > 26800).
                  f" | segments {'/'.join(state[i] if len(state) > i else '?' for i in T2_SEGMENTS)}"
                  f" | POS {position[:40]}", flush=True)
    # The run publishes when every position the reference announces is published: all
    # three thresholds are crossed inside the window (count runs ~1 kHz from step 19).
    # A run that published fewer, or none, is a failure - not "publishing > 0".
    if published != str(ALL_POSITIONS_MASK):
        ctx.failures.append(f"Step 20 reference published_mask {published} at the end of"
                            f" sampling (expected {ALL_POSITIONS_MASK}: all three positions)")


@step(21)
def step_21_teardown(ctx, slot, board, uid):
    """Complete stop, with the stopped-state priority readout, and the summary."""
    src = ctx.src
    if src is not None:
        ctx.send(src, "MODEl:TURNtable:STOP")
        ctx.send(src, "TRIGger:REServation:SOURce:STOP")
    for uid_r, _s, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        ctx.send(member, "SYSTem:TDMA:RING:STOP")
        ctx.send(member, "TRIGger:REServation:T2:STOP")
        # Priority-lane diagnostics are readable only while stopped: this is the one
        # moment that shows whether the bring-up armed the class that gives the
        # SYNC_TRIGGER phase its ingress slack.
        for page in ("SYSTem:VDC:PRIORity:RX?", "SYSTem:VDC:PRIORity:TX?"):
            print(f"  {uid_r:016X} {page} -> {ctx.send(member, page)[:80]}", flush=True)
        ctx.send(member, "SYSTem:VDC:OUTPut:RUN:STOP")
        ctx.send(member, "SYSTem:VDC:FEEDback:SESSion 0")
        ctx.send(member, "TRIGger:REServation:T2:STOP")
        ctx.send(member, "TRIGger:REServation:COUNter:UNLoad")
        ctx.set_load_mask(member, 27, f"{uid_r:016X} teardown")
        ctx.send(member, "SYSTem:TDMA:RING:STOP")
    close_persistent_connections()
    print(f"  teardown complete; recorded failures: {ctx.failures or 'none'}", flush=True)


def parse_selection(text):
    """'1-6' / '1,3,6-8' / 'all' -> sorted list of step numbers."""
    if text is None or text.strip().lower() in ("all", "full", "*"):
        return list(range(0, 22))
    selection = set()
    for part in text.replace(" ", "").split(","):
        if not part:
            continue
        if "-" in part:
            low, _, high = part.partition("-")
            selection.update(range(int(low), int(high) + 1))
        else:
            selection.add(int(part))
    return sorted(selection)


class StepTee:
    """Duplicate everything written to stdout into the per-run step log.

    Step functions print their evidence with plain print(), so teeing stdout is what
    makes the durable log complete instead of containing only the engine's own lines.
    """

    def __init__(self, *streams):
        self.streams = streams

    def write(self, data):
        for stream in self.streams:
            try:
                stream.write(data)
            except Exception:                                             # noqa: BLE001
                pass
        return len(data)

    def flush(self):
        for stream in self.streams:
            try:
                stream.flush()
            except Exception:                                             # noqa: BLE001
                pass


def run_steps(board_ids, selection, interactive=False, teardown=None,
              generation=None, pre_lock_mask=None) -> int:
    """Run the requested steps in order, reporting once per step.

    Any step from 1 to 20 can be selected on its own, in ranges ('--steps 4-9') or as
    a list ('--steps 3,7,12'); '--full' is the whole chain, steps 1..20 in order.
    """
    unknown = [number for number in selection if number not in STEP_TITLES]
    if unknown:
        print(f"unknown steps: {unknown}", flush=True)
        return 2
    missing_impl = [number for number in selection if number not in STEP_FUNCTIONS]
    if missing_impl:
        print(f"steps without an implementation: {missing_impl}", flush=True)
        return 2
    ctx = StepContext(board_ids, generation)
    if pre_lock_mask:
        ctx.pre_lock_mask = pre_lock_mask
        print(f"pre-lock mask experiment enabled: {pre_lock_mask}", flush=True)
    original_stdout = sys.stdout
    log_handle = open(ctx.log_path, "a", encoding="utf-8")
    sys.stdout = StepTee(original_stdout, log_handle)
    ctx.log(f"steps: {selection}")
    try:
        return _run_selected_steps(ctx, selection, interactive, teardown)
    finally:
        sys.stdout = original_stdout
        log_handle.close()


def _run_selected_steps(ctx, selection, interactive, teardown) -> int:
    for number in selection:
        function = STEP_FUNCTIONS[number]
        ctx.log(f"=== Step {number} START  {STEP_TITLES[number]}")
        started = time.time()
        failures_before = len(ctx.failures)
        commands_before = len(ctx.command_times)
        try:
            if STEP_SCOPE.get(number, "boards") == "once":
                function(ctx, None, None, None)
            else:
                ctx.each(function)
            error = ""
        except Exception as exc:                                          # noqa: BLE001
            error = f"{type(exc).__name__}: {exc}"
            ctx.failures.append(f"Step {number} raised {error}")
        duration = time.time() - started
        new_failures = ctx.failures[failures_before:]
        status = "FAIL" if new_failures else "OK"
        reason = new_failures[0] if new_failures else ""
        # Per-step cost report: which commands dominated this step's wall clock.
        step_commands = ctx.command_times[commands_before:]
        slowest = sorted(step_commands, reverse=True)[:3]
        for elapsed, command, reply in slowest:
            if elapsed >= 1.0:
                ctx.log(f"  step {number} cost {elapsed:5.1f}s  {command[:56]}"
                        f"  -> {reply[:20]}")
        ctx.records.append({"step": number, "title": STEP_TITLES[number],
                            "seconds": duration, "status": status, "reason": reason,
                            "slowest": slowest})
        ctx.log(f"=== Step {number} END    {status}  {duration:.1f}s"
                f"{'  reason: ' + reason if reason else ''}")
        if interactive:
            try:
                answer = input(f"Step {number} done ({status}, {duration:.1f}s). "
                               f"Enter=continue, q=stop: ")
            except EOFError:
                answer = ""
            if answer.strip().lower().startswith("q"):
                break
    if teardown is None:
        # Only a ring start leaves the bench running; stopped-state steps do not.
        teardown = any(number >= 16 for number in selection) and 21 not in selection
    if teardown:
        ctx.log(f"=== Step 21 START  {STEP_TITLES[21]} (automatic)")
        started = time.time()
        step_21_teardown(ctx, None, None, None)
        ctx.records.append({"step": 21, "title": STEP_TITLES[21],
                            "seconds": time.time() - started, "status": "OK",
                            "reason": "automatic"})
        ctx.log(f"=== Step 21 END    OK  {time.time() - started:.1f}s")
    ctx.log("step summary (step, seconds, status, key reason):")
    for record in ctx.records:
        ctx.log(f"  {record['step']:>3}  {record['seconds']:>6.1f}s  {record['status']:<4}"
                f"  {record['title'][:38]:<38} {record['reason'][:60]}")
    ctx.log(f"failed steps: "
            f"{[r['step'] for r in ctx.records if r['status'] == 'FAIL'] or 'none'}"
            f"  log: {ctx.log_path}")
    emit_flow_data(ctx, selection)
    return 1 if any(r["status"] == "FAIL" for r in ctx.records) else 0


@step(0)
def step_0_stage_repair(ctx, slot, board, uid):
    """Guarantee a complete calibration stage, replaying it from a healthy board.

    CALibration:TRAINing:STAGe:CLEar (or a lost NVS stage) leaves the board without
    schedule/profile/path-delay data: the provisional DPLL training then answers 0
    and the ring adapter refuses to start. The calibration data only depends on the
    physical link, so an incomplete stage is repaired by replaying a complete one
    (BEGin/LINK/COMMit, persisted to NVS) instead of re-running the calibration flow.
    """
    page = ctx.send(board, "READ:CALibration:TRAINing:STAGe?").replace('"', "")
    if stage_complete(page):
        print(f"  slot {slot} calibration stage complete: {page[:64]}", flush=True)
        if ctx.stage_donor is None:
            ctx.stage_donor = (page.split(",")[1:], read_stage_links(ctx, board))
        return
    # Boot restore is context-bound (calibration_manager.c, calibration_manager_service):
    # an intact NVS record stays unloaded until TDMA holds the topology/schedule/profile
    # it was made for, so EMPTY is expected after another flow (e.g. the DPLL60 capture
    # adapter's 2-node level-0 bring-up) left a different ring identity. Step 3 replays
    # that context; the board then loads its own record. Nothing to repair here.
    persistence = ctx.send(board, "READ:CALibration:TRAINing:STAGe:PERSistence?")
    fields = [value.strip().strip('"') for value in persistence.split(",")]
    if (len(fields) >= 4 and fields[0] == "TRN03NVS" and fields[1] == "1"
            and fields[3] == "1"):
        print(f"  slot {slot} stage EMPTY but the NVS record is valid and restore-pending"
              f" (generation {fields[8] if len(fields) > 8 else '?'}): it loads once step 3"
              f" restores its TDMA context", flush=True)
        ctx.restore_pending_slots = getattr(ctx, "restore_pending_slots", set()) | {slot}
        return
    print(f"  slot {slot} calibration stage INCOMPLETE ({page[:32]}) - replaying",
          flush=True)
    if ctx.stage_donor is None:
        for uid_r, slot_r, _i, _o, _f in ROWS:
            donor = ctx.boards.get(f"{uid_r:016X}")
            if donor is None or donor is board:
                continue
            donor_page = ctx.send(donor, "READ:CALibration:TRAINing:STAGe?").replace('"', "")
            if stage_complete(donor_page):
                ctx.stage_donor = (donor_page.split(",")[1:], read_stage_links(ctx, donor))
                print(f"  donor stage taken from slot {slot_r} ({uid_r:016X})", flush=True)
                break
    if ctx.stage_donor is None:
        ctx.failures.append(f"slot {slot} Step 0: no healthy calibration stage to copy")
        return
    header, links = ctx.stage_donor
    begin = (f"CALibration:TRAINing:STAGe:BEGin {header[1]},{header[2]},"
             f"{header[4]},{header[5]},{header[6]},{header[7]},{header[8]}")
    ctx.fire(board, begin)
    for index, values in sorted(links.items()):
        body = [values[i] for i in STAGE_LINK_WRITE_ORDER]
        tail = [values[i] for i in STAGE_LINK_TAIL_ORDER]
        ctx.fire(board, "CALibration:TRAINing:STAGe:LINK "
                        + ",".join(str(value) for value in body + tail))
    ctx.fire(board, "CALibration:TRAINing:STAGe:COMMit")
    after = ctx.send(board, "READ:CALibration:TRAINing:STAGe?").replace('"', "")
    ok = stage_complete(after) and after.split(",")[1:] == header
    if not ok:
        ctx.failures.append(f"slot {slot} Step 0 stage replay did not verify "
                            f"({after[:40]})")
    print(f"  slot {slot} stage replayed -> {after[:64]} {'OK' if ok else 'MISMATCH'}",
          flush=True)


def inject_html_view(payload) -> str:
    """Write the payload into the T2 HTML view's inline data block.

    The page reads this block first and falls back to the .js file, so refreshing the
    report is a data write even where a file:// subresource is refused.
    """
    path = os.path.join("docs", "trigger", "t2", "T2_FLOW.html")
    if not os.path.exists(path):
        return ""
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    start, end = "<!-- T2-DATA-START -->", "<!-- T2-DATA-END -->"
    if start not in text or end not in text:
        return ""
    block = (f'{start}\n<script id="t2-flow-data" type="application/json">'
             f'{json.dumps(payload, ensure_ascii=False)}</script>\n{end}')
    head, _, rest = text.partition(start)
    _, _, tail = rest.partition(end)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(head + block + tail)
    return path.replace("\\", "/")


def refresh_html_view() -> int:
    """Re-inject the newest run's data (no bench access, no steps)."""
    path = os.path.join("out", "tdma_t2", "t2_flow.json")
    if not os.path.exists(path):
        print(f"no data yet: {path} - run some steps first")
        return 2
    with open(path, encoding="utf-8") as handle:
        payload = json.load(handle)
    target = inject_html_view(payload)
    print(f"refreshed {target or '(HTML view not found)'} from t2_flow.json"
          f" (generated {payload.get('generated')},"
          f" failed {payload.get('failed') or 'none'})")
    return 0


def emit_flow_data(ctx, selection):
    """Write this run as data for the A4 flow view (docs/trigger/t2/T2_FLOW.html).

    The view renders from JSON, so a status or a duration is a data change instead
    of a redrawn picture. Both a plain .json and a .js wrapper are written: the
    wrapper loads from file:// (fetch() is blocked there), the .json is for tooling.
    """
    by_id = {record["step"]: record for record in ctx.records}
    steps = []
    for number in sorted(STEP_TITLES):
        record = by_id.get(number)
        steps.append({
            "id": number,
            "title": STEP_TITLES[number],
            "scope": STEP_SCOPE.get(number, "boards"),
            "planned": number in selection,
            "status": record["status"] if record else ("pending" if number in selection
                                                       else "not-run"),
            "seconds": round(record["seconds"], 1) if record else None,
            "reason": (record.get("reason") or "") if record else "",
        })
    payload = {
        "schema": "HDRT100_T2_FLOW_V1",
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "generation": ctx.dep_gen,
        "selection": list(selection),
        "failed": [record["step"] for record in ctx.records
                   if record["status"] == "FAIL"],
        "log": ctx.log_path,
        "steps": steps,
        # Reference ring stops by the origin lifetime path, with their latched cause.
        "origin_stops": getattr(ctx, "origin_stop_samples", []),
    }
    if payload["origin_stops"]:
        # One file per run, so stop samples accumulate across runs for comparison.
        stops_path = os.path.join(ctx.log_dir, f"origin-stops-{ctx.dep_gen}.json")
        with open(stops_path, "w", encoding="utf-8") as handle:
            json.dump(payload["origin_stops"], handle, ensure_ascii=False, indent=2)
        ctx.log(f"origin stop samples: {stops_path}")
    json_path = os.path.join(ctx.log_dir, "t2_flow.json")
    js_path = os.path.join(ctx.log_dir, "t2_flow_data.js")
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
    with open(js_path, "w", encoding="utf-8") as handle:
        handle.write("window.T2_FLOW = "
                     + json.dumps(payload, ensure_ascii=False, indent=2) + ";\n")
    ctx.log(f"flow data: {json_path} · {js_path}")
    target = inject_html_view(payload)
    ctx.log(f"html view refreshed: {target or '(not found)'}")
    return payload


def template_crc(generation: int, rows) -> int:
    crc = crc_word(0xFFFFFFFF, generation & 0xFFFFFFFF)
    crc = crc_word(crc, len(rows))
    for template_id, gen, role, kind, arg, span, delta in rows:
        for word in (template_id, gen, role, kind, arg, span,
                     delta & 0xFFFFFFFF, (delta >> 32) & 0xFFFFFFFF):
            crc = crc_word(crc, word & 0xFFFFFFFF)
    return crc ^ 0xFFFFFFFF


if __name__ == "__main__":
    raise SystemExit(main())
