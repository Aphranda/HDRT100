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
import pathlib
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


# Per-role group sequence (T2-SEQ-001 / T2-EXEC-002). The role's rows ARE the
# sequence: group g of a position is row g, with its own channel code and its own
# static offset delta_q, and the position counts as executed only after its last
# group.
#
# TIMING IS A STOPPED-STATE INSTALL PARAMETER, NOT A BUILD PARAMETER: every group's
# offset travels in its own TEMPlate row, so a new spacing/code set is a re-run of
# the install (and of the scan) - it never needs a firmware build or a re-flash.
# Precedence: command line > environment > --sequence-profile file > built-in default.
#   --seq-groups 1,8,8,8          groups per role (COUNTER, SWITCH_A, GATEWAY, SWITCH_B)
#   --seq-period-slices 20        group period P in fine slices (delta_q = offset + g*P)
#   --seq-offset-slices 0,0,0,0   per-role base offset O_r in fine slices
#   --seq-codes-a / --seq-codes-b per-group channel code / polarization level
#   --seq-arg-g 20                GATEWAY pulse-width argument, used by every group
# Environment names are the same with a T2_SEQ_ prefix (T2_SEQ_GROUPS, ...) and the
# profile keys are groups, group_period_slices, group_offset_slices, codes,
# gateway_pulse_argument (see config/trigger/t2_sequence_profile.json).
# The built-in default is ONE row per role - the behaviour before the sequence
# landed - because a sequence commands real switch and polarization lines.
SEQ_ROLES = ("COUNTER", "SWITCH_A", "GATEWAY", "SWITCH_B")
SEQUENCE_PROFILE_DEFAULT = "config/trigger/t2_sequence_profile.json"
# Filled from the command line by main(); module level so the row builder stays pure.
_sequence_cli: dict[str, str] = {}


def set_sequence_options(profile=None, groups=None, period_slices=None, offsets=None,
                         codes_a=None, codes_b=None, arg_g=None) -> None:
    """Command-line overrides for the group program (None = not given)."""
    _sequence_cli.clear()
    _sequence_cli.update({
        "profile": profile, "groups": groups, "period_slices": period_slices,
        "offsets": offsets, "codes_a": codes_a, "codes_b": codes_b, "arg_g": arg_g})


def _sequence_profile() -> dict:
    path = (_sequence_cli.get("profile") or os.environ.get("T2_SEQ_PROFILE")
            or SEQUENCE_PROFILE_DEFAULT)
    try:
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)
    except OSError:
        return {}
    except ValueError as error:
        raise SystemExit(f"sequence profile {path} is not valid JSON: {error}")


def _seq_env(name: str, default: str) -> list[str]:
    return [item.strip() for item in os.environ.get(name, default).split(",")]


def _seq_ints(value, what: str) -> list[int]:
    if isinstance(value, list):
        return [int(item) for item in value]
    return [int(item.strip()) for item in str(value).split(",") if item.strip()]


def sequence_program() -> tuple[list[int], int, list[int], list[int], list[int], int]:
    profile = _sequence_profile()
    codes = profile.get("codes") or {}
    groups_value = (_sequence_cli.get("groups") or os.environ.get("T2_SEQ_GROUPS")
                    or profile.get("groups") or "1,1,1,1")
    groups = _seq_ints(groups_value, "groups")
    if len(groups) != 4 or any(value < 1 for value in groups):
        raise SystemExit("groups must be four positive counts (COUNTER,SWITCH_A,GATEWAY,SWITCH_B)")
    period_value = (_sequence_cli.get("period_slices")
                    if _sequence_cli.get("period_slices") is not None
                    else os.environ.get("T2_SEQ_PERIOD_SLICES",
                                        profile.get("group_period_slices", 12)))
    period = int(period_value)
    offsets_value = (_sequence_cli.get("offsets") or os.environ.get("T2_SEQ_OFFSET_SLICES")
                     or profile.get("group_offset_slices") or "0,0,0,0")
    offsets = _seq_ints(offsets_value, "offsets")
    codes_a = _seq_ints(_sequence_cli.get("codes_a") or os.environ.get("T2_SEQ_CODES_A")
                        or codes.get("SWITCH_A") or "0", "codes_a")
    codes_b = _seq_ints(_sequence_cli.get("codes_b") or os.environ.get("T2_SEQ_CODES_B")
                        or codes.get("SWITCH_B") or "0", "codes_b")
    arg_value = (_sequence_cli.get("arg_g") if _sequence_cli.get("arg_g") is not None
                 else os.environ.get("T2_SEQ_ARG_G", profile.get("gateway_pulse_argument", 20)))
    arg_g = int(arg_value)
    if period < 0:
        raise SystemExit("group period must not be negative")
    if len(offsets) != 4:
        raise SystemExit("group offsets must be four values")
    return groups, period, offsets, codes_a, codes_b, arg_g


def sequence_rows(slot: int) -> list[tuple[int, int, int, int, int]]:
    """(role, kind, argument, span_slices, delta_q) for each group of this role.

    Codes follow the reservation sequence of the node-sequence branch
    (trigger_reservation_build_plan): one per-position sequence of
    TRIGGER_RESERVATION_ITEM_COUNT items, item i carrying
    output_code = (i // 2) | (level << 2) - the SP8T channel steps every second
    item and the polarization is the item's parity. Per role that is:
      SWITCH_A (channel):   i // 2                 -> 0,0,1,1,2,2,3,3
      SWITCH_B (polarize):  i % 2  == h_level/v_level -> 0,1,0,1,...
      GATEWAY  (VNA):       pulse width, 2 pulses per position
    T2_SEQ_CODES_A / T2_SEQ_CODES_B override the codes (bench diagnostics), and a
    shorter list repeats its last entry.
    """
    groups, period, offsets, codes_a, codes_b, arg_g = sequence_program()
    role, kind, arg = action_row(slot)
    count = groups[slot]
    base = offsets[slot]
    rows: list[tuple[int, int, int, int, int]] = []
    for group in range(count):
        if kind == 2:                                   # GATEWAY pulse
            argument = arg if arg else arg_g
        elif role == 1:                                 # SWITCH_A: channel
            codes = codes_a if codes_a != [0] else [group_index // 2 for group_index in range(count)]
            argument = codes[min(group, len(codes) - 1)]
        else:                                           # SWITCH_B: polarization
            codes = codes_b if codes_b != [0] else [group_index % 2 for group_index in range(count)]
            argument = codes[min(group, len(codes) - 1)]
        rows.append((role, kind, argument, 1, base + group * period))
    return rows


def template_rows(slot: int, generation: int) -> list[tuple[int, int, int, int, int, int, int]]:
    """Template table rows for this board's role: one row per group, in order."""
    return [(group + 1, generation, role, kind, argument, span, delta)
            for group, (role, kind, argument, span, delta)
            in enumerate(sequence_rows(slot))]


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
    # Sequence timing lives in the STOPPED-state install (one TEMPlate row per group,
    # each row carrying its own argument and delta_q), so tuning it is a re-run of the
    # install - never a re-flash. Precedence: CLI > environment > --sequence-profile > default.
    parser.add_argument("--sequence-profile", default=None,
                        help="JSON file with the per-role group program (groups, "
                             "group_period_slices, group_offset_slices, codes, "
                             "gateway_pulse_argument); see config/trigger/t2_sequence_profile.json")
    parser.add_argument("--seq-groups", default=None,
                        help="groups per role as COUNTER,SWITCH_A,GATEWAY,SWITCH_B (e.g. 1,8,8,8)")
    parser.add_argument("--seq-period-slices", type=int, default=None,
                        help="group period P in fine slices (delta_q advances by P per group)")
    parser.add_argument("--seq-offset-slices", default=None,
                        help="per-role base offset in fine slices, e.g. 0,0,0,0")
    parser.add_argument("--seq-codes-a", default=None,
                        help="SWITCH_A channel code per group, e.g. 0,0,1,1,2,2,3,3")
    parser.add_argument("--seq-codes-b", default=None,
                        help="SWITCH_B polarization level per group, e.g. 0,1,0,1,0,1,0,1")
    parser.add_argument("--seq-arg-g", type=int, default=None,
                        help="GATEWAY pulse-width argument used by every group")
    parser.add_argument("--list", action="store_true",
                        help="print the step table and exit")
    parser.add_argument("--step", type=int, default=None,
                        help="run exactly one step (1..20) for the whole chain")
    parser.add_argument("--steps", default=None,
                        help="run a step selection, e.g. '1-6', '3,7', '4-9,16'; "
                             "'all' or --full runs the continuous chain")
    parser.add_argument("--interactive", action="store_true",
                        help="with --steps/--full: wait for Enter between steps")
    parser.add_argument("--leave-running", action="store_true",
                        help="with --steps/--full: keep the ring/T2/VDC running "
                             "instead of the automatic step-21 teardown")
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
    parser.add_argument("--release-quarantine", action="store_true",
                        help="EXPERIMENT: in step 2 drive LOAD:MASK 0 -> foundation so every "
                             "foundation load is seen as a disabled->enabled transition and "
                             "released from quarantine (the base only clears a bit's "
                             "quarantine on that transition, app.c:594-610)")
    args = parser.parse_args()
    set_sequence_options(
        profile=args.sequence_profile, groups=args.seq_groups,
        period_slices=args.seq_period_slices, offsets=args.seq_offset_slices,
        codes_a=args.seq_codes_a, codes_b=args.seq_codes_b, arg_g=args.seq_arg_g)

    if args.refresh_html:
        return refresh_html_view()

    if args.list:
        for number in sorted(STEP_TITLES):
            print(f"{number:>3}  {STEP_TITLES[number]}")
        return 0

    if args.step is not None:
        return run_steps([f"{row[0]:016X}" for row in ROWS], [args.step],
                         interactive=args.interactive,
                         teardown=False if args.leave_running else None,
                         generation=args.dep_generation,
                         pre_lock_mask=args.pre_lock_mask,
                         release_quarantine=args.release_quarantine)
    if args.steps is not None:
        return run_steps([f"{row[0]:016X}" for row in ROWS], parse_selection(args.steps),
                         interactive=args.interactive,
                         teardown=False if args.leave_running else None,
                         generation=args.dep_generation,
                         pre_lock_mask=args.pre_lock_mask,
                         release_quarantine=args.release_quarantine)
    if args.full:
        # One code path: the step engine, run continuously over all 21 steps (the
        # legacy run_full remains only as a reference implementation).
        return run_steps([f"{row[0]:016X}" for row in ROWS], parse_selection("all"),
                         interactive=args.interactive,
                         teardown=False if args.leave_running else None,
                         pre_lock_mask=args.pre_lock_mask,
                         release_quarantine=args.release_quarantine)
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
        print("T2:MOUNT ->", q("TRIGger:REServation:T2:MOUNT")[:24])
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
        send(board, "TRIGger:REServation:T2:UNMOUNT")
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
        # Seat rotation (STOP-only, per board). Today the firmware rotates TWO seats
        # (priority, first-class): "SYSTem:TDMA:SEAT:ROTation <priority>,<first_class>",
        # each <= 8 and their sum <= 12. Owner decision 2026-10-16: 7:3 for the two-seat
        # form, and a THREE-way form (priority:first:second = 7:2:1 with VDC running,
        # 1:2:7 without VDC) once the second-class share exists - the refmem image/ack
        # path must get its own guaranteed time, which the two-seat rotation cannot give.
        _rotation = os.environ.get("T2_SEAT_ROTATION")
        if _rotation:
            print(f"  slot {slot} seat rotation {_rotation} -> "
                  f"{send(board, f'SYSTem:TDMA:SEAT:ROTation {_rotation}')[:40]}"
                  f" | readback {send(board, 'SYSTem:TDMA:SEAT:ROTation?')[:24]}", flush=True)
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
        rows = template_rows(slot, dep_gen)
        tcrc = template_crc(dep_gen, rows)
        reply = send(board, f"TRIGger:REServation:TEMPLate:INSTall {dep_gen},{tcrc},{len(rows)},"
                                + ",".join(str(v) for row in rows for v in row))
        print(f"  slot {slot} TEMPlate:INSTall -> {reply[:60]}")
        reply = send(board, "TRIGger:REServation:TEMPLate:ACTivate")
        print(f"  slot {slot} TEMPlate:ACTivate -> {reply[:60]}")
        print(f"  slot {slot} TEMPlate? -> {send(board, 'TRIGger:REServation:TEMPLate?')[:80]}")
        _action_page = send(board, "TRIGger:REServation:ACTion?").split(",")
        _template_page = send(board, "TRIGger:REServation:TEMPLate?").split(",")
        _diag = send(board, "TRIGger:REServation:SEAT:MOUNT?").split(",")
        # A rig without an analyzer cannot answer READY; T2_READY_REQUIRED=0 tells the gateway to
        # retire a pending READY (and count it) instead of refusing the next action.
        _ready_required = os.environ.get("T2_READY_REQUIRED", "0")
        if _ready_required not in ("0", "1"):
            raise SystemExit("T2_READY_REQUIRED must be 0 or 1")
        send(board, f"TRIGger:SEQuence:REAdy {_ready_required}")
        print(f"  slot {slot} SEQuence:REAdy {_ready_required} -> "
              f"{send(board, 'TRIGger:SEQuence:REAdy?')[:24]}", flush=True)
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
                    f"5000,5000,700,0,0,360000,1000,1000")   # 0..360 deg @1 deg; threshold 700 = 300-pulse declare lead (firmware floor)
        diag = send(board, "TRIGger:REServation:SEAT:MOUNT?").split(",")
        _override = _scan_override_command(dep_gen, int(slot == 0))
        if _override is not None:
            send(board, _override)
            print(f"  scan override -> {_override[:72]}", flush=True)
        print(f"  configure diag {diag[4] if len(diag) > 4 else '?'}"
              " (91/92/93 prereq, 100/101 inner, 140-148 validate, 111-137 stopped)")
        send(board, f"TRIGger:REServation:T2:MOUNT {dep_gen},0")
        # The gateway module is re-initialised by the configure/start path, so the READY requirement is
        # sent again here - after the start - and verified on the spot.
        _ready_required = os.environ.get("T2_READY_REQUIRED", "0")
        send(board, f"TRIGger:SEQuence:REAdy {_ready_required}")
        print(f"  slot {slot} SEQuence:REAdy {_ready_required} (after start) -> "
              f"{send(board, 'TRIGger:SEQuence:REAdy?')[:24]}", flush=True)
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
        # Seat rotation (STOP-only, per board): two-seat form today
        # ("<priority>,<first_class>", each <= 8, sum <= 12); owner decision 2026-10-16
        # is 7:3 for this form, with a three-share form (7:2:1 with VDC running, 1:2:7
        # without) once the reserved second-class share exists in the rotation counter.
        _rotation = os.environ.get("T2_SEAT_ROTATION")
        if _rotation:
            print(f"  slot {slot} seat rotation {_rotation} -> "
                  f"{send(board, f'SYSTem:TDMA:SEAT:ROTation {_rotation}')[:40]}"
                  f" | readback {send(board, 'SYSTem:TDMA:SEAT:ROTation?')[:24]}", flush=True)
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
    # T2 start: every stopped-state write (T2:MOUNT, SESSion, OUTPut:RUN,
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
        send(_board, "TRIGger:REServation:T2:UNMOUNT")
        # The priority-lane diagnostics are stopped-state only (they refuse while
        # ring.enabled/adapter_started or while the lane is active), so this is the
        # one moment they can be read: it shows whether the bring-up actually armed
        # the priority class that gives the SYNC_TRIGGER phase its ingress slack.
        for _page in ("SYSTem:VDC:PRIORity:RX?", "SYSTem:VDC:PRIORity:TX?"):
            print(f"  {uid:016X} {_page} -> {send(_board, _page)[:80]}", flush=True)
        send(_board, "SYSTem:VDC:OUTPut:RUN:STOP")
        send(_board, "SYSTem:VDC:FEEDback:SESSion 0")
        send(_board, "TRIGger:REServation:T2:UNMOUNT")
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
        send(board, "TRIGger:REServation:T2:UNMOUNT")
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
             "descriptor_reject": 11, "transport_reject": 12,
             "event_count": 13, "event_dropped": 14, "frozen_mask": 15,
             "freeze_mismatch": 16, "freeze_fallback": 17, "freeze_missing_local": 18,
             "last_counter_count": 28, "pulse_index": 29, "fine_slice_index": 30,
             "announced_mask": 31, "prepared_last_reason": 41, "predictor_last_error": 38,
             "prepare_calls": 39, "preview_saved": 40}
T2_SEGMENTS = (42, 43, 44)
ALL_POSITIONS_MASK = 7             # TRIGGER_T2_RUNTIME_POSITION_COUNT == 3

# Optional scan override (TRIGGER_T2_TODO.md A.3.50): when
# T2_SCAN_OVERRIDE_TERMINAL_MDEG is set, the run re-sends T2:CONFigure right after the
# standard one with this geometry, so the angle matrix and the short-span BOUNDARY run need
# no code edit. Additive on purpose: the standard call sites keep their literal argument
# list, and this helper's own line is registered in the call-site table below alongside them.
def _scan_override_command(run_id, origin):
    terminal = os.environ.get("T2_SCAN_OVERRIDE_TERMINAL_MDEG")
    if not terminal:
        return None
    start = os.environ.get("T2_SCAN_OVERRIDE_START_MDEG", "0")
    step = os.environ.get("T2_SCAN_OVERRIDE_STEP_MDEG", "1000")
    # threshold_per_mille sets the ANNOUNCEMENT LEAD (lead = (1000 - threshold) per mille of
    # the position interval). Measured 2026-10-16 with 950: every residual follower loss over
    # a 361-position scan was T2_ACTION_FAIL_LATE (38/39/36 per board, nothing else), i.e. the
    # deadline was already behind the cursor when it came to arm - a latency-budget question,
    # so the lead belongs to the scan configuration and is exposed here.
    # 700 => 300 pulses of lead, the measured floor (a 100-pulse lead still lost
    # 6-8% of a 361-position scan to LATE, 300 and 500 lost none); the firmware
    # refuses a configure whose lead cannot reach TRIGGER_T2_MIN_DECLARE_LEAD_PULSES.
    threshold = os.environ.get("T2_SCAN_OVERRIDE_THRESHOLD_PERMILLE", "700")
    # Target mask override for TRANSPORT experiments: the origin's transmit volume and
    # its FREEZE resend both scale with the number of peers it must wait for, so running
    # the same scan against one peer separates "the origin's fan-out volume eats the
    # lead" from "the lane duplicates/loses records regardless of fan-out".
    target_mask = os.environ.get("T2_SCAN_OVERRIDE_TARGET_MASK", "14")
    command = ("TRIGger:REServation:" + "T2:CONFigure " + f"{run_id},0,{target_mask},{origin},"
               f"5000,5000,{threshold},{start},{start},{terminal},{step},1000")
    return command
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


def field_at(fields, index):
    """Positional field from a page (the sampling loop keeps a local alias)."""
    return fields[index] if len(fields) > index else "?"


def query_page(ctx, board, command, min_fields=5, tries=3, delay_s=0.05):
    """Bounded retry for a status page; a CDC timeout answers '<timeout>'.

    The sampling loop queries every board once per tick, so a single slow board
    (observed on the GATEWAY) can drop one reply and turn a whole page into '?'.
    """
    last = ""
    for attempt in range(max(1, tries)):
        last = ctx.send(board, command)
        if "<timeout>" not in last and len(last.split(",")) >= min_fields:
            return last
        if attempt + 1 < tries:
            time.sleep(delay_s)
    ctx._query_retries = getattr(ctx, "_query_retries", 0) + 1
    return last


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


GENERATION_MARKER = os.path.join("out", "tdma_t2", "last_generation.json")


def load_last_generation(path=None):
    """Last generation this tool wrote, or None when no marker exists."""
    path = path or GENERATION_MARKER
    try:
        with open(path, encoding="utf-8") as handle:
            value = int(json.load(handle).get("last_generation", 0))
    except (OSError, ValueError, TypeError):
        return None
    return value if value > 0 else None


def save_last_generation(generation, path=None):
    path = path or GENERATION_MARKER
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump({"last_generation": int(generation)}, handle)
    except OSError:
        pass


def reuses_recorded_generation(selection):
    """True when the range cannot write a new grid epoch and must reuse one."""
    steps = set(selection or [])
    return bool(steps) and not ({8, 9} & steps)


def generation_for_selection(selection, requested=None, marker=None):
    """Pick the generation a run must use.

    Steps 8/9 write the grid epoch and bind T2 to it, so a fresh generation
    every invocation makes a later subset (e.g. '--steps 11-20') impossible to
    reconcile with the grid already on the boards.  Ranges that cannot re-write
    the grid/T2 binding reuse the recorded generation instead; a range that does
    contain those steps starts a new one.
    """
    if requested is not None:
        return int(requested) & 0xFFFFFFFF
    if reuses_recorded_generation(selection):
        reused = load_last_generation(marker)
        if reused:
            return reused
    return int(time.time()) & 0xFFFFFFFF


class StepContext:
    """Shared state for one step run."""

    def __init__(self, board_ids, generation=None, reuse_generation=False):
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
        marker = GENERATION_MARKER
        last_accepted = load_last_generation(marker) or 0
        for uid, _s, _i, _o, _f in ROWS:
            member = self.boards.get(f"{uid:016X}")
            if member is None:
                continue
            page = self.send(member, "SYSTem:VDC:FEEDback:SESSion?")
            if page.strip().isdigit():
                last_accepted = max(last_accepted, int(page.strip()))
        requested = generation & 0xFFFFFFFF if generation else int(time.time()) & 0xFFFFFFFF
        if requested <= last_accepted and not reuse_generation:
            print(f"  note: generation {requested} is not above the last accepted "
                  f"session {last_accepted}; using {last_accepted + 1}", flush=True)
            requested = last_accepted + 1
        self.dep_gen = requested
        save_last_generation(self.dep_gen, marker)
        self._epoch_mismatch_reported = False
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
    ctx.send(board, "TRIGger:REServation:T2:UNMOUNT")
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
    # Seat rotation (STOP-only, per board) - THIS is the step-2 the --full chain runs
    # (run_steps -> the engine's @step(2)); the legacy chain and the session-reopen
    # helpers have their own copies of the TIMing write and are not on this path.
    # Two-seat form today: "<priority>,<first_class>", each <= 8, sum <= 12; owner
    # decision 2026-10-16 is 7:3 for it, with a three-share form (7:2:1 with VDC
    # running, 1:2:7 without) once the reserved second-class share exists.
    _rotation = os.environ.get("T2_SEAT_ROTATION")
    if _rotation:
        print(f"  slot {slot} seat rotation {_rotation} -> "
              f"{ctx.send(board, f'SYSTem:TDMA:SEAT:ROTation {_rotation}')[:40]}"
              f" | readback {ctx.send(board, 'SYSTem:TDMA:SEAT:ROTation?')[:24]}", flush=True)
    # T1 (HAOFV review): the priority lanes are the VDC owner's (SYNC/MATCH/FOLLow/
    # FOLLow:PHASe). The load no longer zeroes them - a zero write is still the load
    # configuring the base, and the owner handles a stale session's request itself
    # (672edf0b). SESSion stays: it is identity, not lane configuration.
    ctx.send(board, "SYSTem:VDC:FEEDback:REFerence 0")
    ctx.send(board, "SYSTem:VDC:FEEDback:LOCALfollow 0")
    # The foundation mask is VDC|DPLL|SYNC_CAPTURE|REFMEM|SYNC_TRIGGER = 91
    # (application/inc/app.h:25-29, APP_REALTIME_LOAD_FOUNDATION_MASK). 27 was not
    # a valid baseline: without the SYNC_TRIGGER bit the VDC domain never accepts a
    # sample, so the reference could never lock (measured).
    if getattr(ctx, "release_quarantine", False):
        # EXPERIMENT: a load bit is isolated for good after one WCET/deadline
        # violation and the base only releases it on a disabled->enabled
        # transition of that bit (app.c:594-610). 91 -> 91 is not a transition,
        # so a quarantined foundation load (observed: mask 8 = SYNC_CAPTURE on
        # all four boards) stays dead across runs. Drive 0 -> 91 and read the
        # quarantine mask before and after.
        before = ctx.send(board, "SYSTem:TDMA:SCHEDule?").split(",")
        ctx.set_load_mask(board, 0, f"slot {slot} quarantine release: all loads off")
        ctx.set_load_mask(board, 91, f"slot {slot} quarantine release: foundation on")
        after = ctx.send(board, "SYSTem:TDMA:SCHEDule?").split(",")
        print(f"  slot {slot} quarantine release: quarantined "
              f"{before[5].strip() if len(before) > 5 else '?'} -> "
              f"{after[5].strip() if len(after) > 5 else '?'}", flush=True)
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
    # Verify the configure instead of assuming it. The reply names its own refusal, and
    # an unverified grid used to surface four steps later as a T2 mount refusal (seat
    # reject 91 "grid not configured") on one board while the other three mounted -
    # measured 2026-10-13, which cost the whole run.
    state = ctx.send(board, "TRIGger:REServation:GRID?").split(",")
    result = state[1].strip() if len(state) > 1 else "?"
    configured = state[2].strip() if len(state) > 2 else "?"
    epoch = state[4].strip() if len(state) > 4 else "?"
    print(f"  slot {slot} grid readback -> result {result}"
          f" configured {configured} epoch {epoch}", flush=True)
    if result != "0" or configured != "1" or epoch != str(ctx.dep_gen):
        ctx.failures.append(
            f"slot {slot} Step 8 grid not configured (result {result}"
            f" configured {configured} epoch {epoch} asked {ctx.dep_gen})")


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
    rows = template_rows(slot, ctx.dep_gen)
    tcrc = template_crc(ctx.dep_gen, rows)
    template_reply = ctx.send(board, f"TRIGger:REServation:TEMPLate:INSTall "
                                     f"{ctx.dep_gen},{tcrc},{len(rows)},"
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
    # A previous run that aborted before teardown leaves the runtime active, and both CONFigure and MOUNT
    # refuse while it is: clear it here so consecutive runs do not need a board reboot.
    ctx.send(board, "TRIGger:REServation:T2:UNMOUNT")
    for _ in range(10):
        state = ctx.send(board, "TRIGger:REServation:T2?").split(",")
        if len(state) > 3 and state[3] == "0":
            break
        time.sleep(0.1)
    for attempt in range(4):
        ctx.send(board, "TRIGger:REServation:T2:UNMOUNT")
        ctx.send(board, "SYSTem:TDMA:FLIGHT:FIFO:RESet")
        # Threshold (= announcement lead), FROZEN 2026-10-17 at 800: lead in pulses =
        # 1000 - permille, so 800 => 200 pulses (~77 ms). A measured four-point sweep with
        # the announce throttle in place gave 300 pulses: clean, 200: clean (adopted),
        # 150: GATEWAY lost 1 to LATE, 100: rejected by the firmware floor. The previous
        # default was 700 (300 pulses); 300 was the un-throttled floor, where 100 pulses
        # lost 6-8%. NOTE the override block below is gated on T2_SCAN_OVERRIDE_TERMINAL_MDEG:
        # setting only T2_SCAN_OVERRIDE_THRESHOLD_PERMILLE silently does nothing, which is
        # why this production literal must stay settable on its own.
        _t2_threshold_permille = os.environ.get("T2_CONFIGURE_THRESHOLD_PERMILLE", "800")
        ctx.send(board, f"TRIGger:REServation:T2:CONFigure {ctx.dep_gen},0,14,"
                        f"{int(slot == 0)},5000,5000,{_t2_threshold_permille},0,0,360000,1000,1000")
        diag = ctx.send(board, "TRIGger:REServation:SEAT:MOUNT?").split(",")
        _override = _scan_override_command(ctx.dep_gen, int(slot == 0))
        if _override is not None:
            ctx.send(board, _override)
            print(f"  scan override -> {_override[:72]}", flush=True)
        ctx.send(board, f"TRIGger:REServation:T2:MOUNT {ctx.dep_gen},0")
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
    # T2:MOUNT re-initialises the gateway module, which restores the firmware
    # default ready_required=1 (trigger_sequence_timing.c:29). A rig without an
    # analyzer cannot answer READY, and an unanswered READY refuses the next
    # position table (sync_io_gateway.c:587), so assert the operator's choice here
    # and verify it on the spot; step 17 re-asserts it after the dynamic start.
    _ready_required = os.environ.get("T2_READY_REQUIRED", "0")
    if _ready_required not in ("0", "1"):
        raise SystemExit("T2_READY_REQUIRED must be 0 or 1")
    ctx.send(board, f"TRIGger:SEQuence:REAdy {_ready_required}")
    print(f"  slot {slot} SEQuence:REAdy {_ready_required} (after mount) -> "
          f"{ctx.send(board, 'TRIGger:SEQuence:REAdy?')[:24]}", flush=True)
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
        # Option (a) of TRIGGER_T2_TODO.md A.3.114: the pulse path has a single owner, so it
        # must be released before the model is loaded and armed. Otherwise the model's arm is
        # refused and it stays at fault_code 3 (measured: running 0 phase 5 emitted 0/181).
        # Later steps restart the counter, which then only observes the model.
        print(f"  counter stop before model arm -> {ctx.send(src, 'TRIGger:REServation:COUNter:STOP')}",
              flush=True)
        # The PWM/motion source is the pulse-path owner (A.3.117: count kept advancing
        # after the counter stop), so stop it too before the model is loaded and armed.
        print(f"  source stop before model arm -> {ctx.send(src, 'TRIGger:REServation:SOURce:STOP')}",
              flush=True)
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
    # Optional seat rotation (STOP-only, per board): "priority,first_class" shares, each
    # <= TDMA_SEAT_ROTATION_SHARE_MAX (8) and their sum <= TDMA_SEAT_ROTATION_CYCLE_MAX
    # (12). Owner decision 2026-10-16: 7:3, and the first-class share is then split 2:1
    # internally (position slices : refmem ack/state). The refmem image/ack path is NOT
    # part of this two-seat rotation, so its queueing cannot be fixed from here - that
    # needs the first-class seat to carry the ack as well (TDMA-SEAT-CLASS-001).
    _rotation = os.environ.get("T2_SEAT_ROTATION")
    if _rotation:
        print(f"  seat rotation {_rotation} -> "
              f"{ctx.send(board, f'SYSTem:TDMA:SEAT:ROTation {_rotation}')[:40]}"
              f" | readback {ctx.send(board, 'SYSTem:TDMA:SEAT:ROTation?')[:24]}", flush=True)
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
    """Priority class: observe the VDC owner's own arm of the observation lanes.

    The load no longer configures the base (T1, HAOFV review): SESSion is
    identity and stays, but SYNC/MATCH/FOLLow/FOLLow:PHASe are the VDC owner's
    own lanes now (VDC-LOCKPUB-001①, commit 672edf0b). The workflow only reads
    back what the owner armed, and reports a mismatch instead of writing it.
    The owner arms within ~32 ms of the session opening, which step 13 opened
    seconds ago.
    """
    src = ctx.src
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
            print(f"  session {session_now} != {ctx.dep_gen}: the owner cannot arm for "
                  f"this session", flush=True)
            return
    if src is not None:
        sync_generation = ctx.send(src, "SYSTem:VDC:PRIORity:SYNC?").strip()
        print(f"  {src.address} owner SYNC generation {sync_generation}"
              f" (expected {ctx.dep_gen})", flush=True)
        if sync_generation != str(ctx.dep_gen):
            ctx.failures.append(f"Step 15: origin SYNC generation is {sync_generation}, "
                                f"expected the owner to arm {ctx.dep_gen}")
    for node, (uid_r, _s, _i, _o, _f) in enumerate(ROWS):
        peer = ctx.boards.get(f"{uid_r:016X}")
        if peer is None or node == 0:
            continue
        ctx.send(peer, f"SYSTem:VDC:FEEDback:SESSion {ctx.dep_gen}")
        match_generation = ctx.send(peer, "SYSTem:VDC:PRIORity:MATCH?").strip()
        follow = ctx.send(peer, "SYSTem:VDC:PRIORity:FOLLow?").strip()
        print(f"  {uid_r:016X} owner MATCH generation {match_generation}"
              f" (expected {ctx.dep_gen}) follow {follow}", flush=True)
        if match_generation != str(ctx.dep_gen):
            ctx.failures.append(f"Step 15: {uid_r:016X} MATCH generation is "
                                f"{match_generation}, expected the owner to arm "
                                f"{ctx.dep_gen}")


@step(16)
def step_16_ring_start(ctx, slot, board, uid):
    """Ring data path: BURSt 0, ARM the peers then the reference, TRAIN, START."""
    seq_mode = os.environ.get("T2_SEQ_MODE", "")
    if seq_mode in ("0", "1", "2"):
        # The position model must be selected while the board is stopped and
        # after T2 is configured: switching it later rewrote the ring/epoch
        # prerequisites and ORIGin:TRIAL was refused (EPOCH_SUPERSEDED).
        for uid_r, slot_r, _i, _o, _f in ROWS:
            member = ctx.boards.get(f"{uid_r:016X}")
            if member is None:
                continue
            reply = ctx.send(
                member, f"TRIGger:REServation:T2:SEQ:MODE {seq_mode}")
            print(f"  slot {slot_r} SEQ:MODE {seq_mode} (before ring) -> "
                  f"{reply[:24]}", flush=True)
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
        # The trial is a deliberate diagnostic, but the reference ring MUST be healthy the
        # moment it returns: passing runs read err 0 / valid 1 / ring_cfg == config_seq
        # exactly here, while an UNAUTHORIZED trial (ORIGIN STOP cause UNAUTHORIZED+
        # COMM_FAULT / FAULT_LATCHED) still bumps the ring config and leaves the binding one
        # behind - a sticky drift (bound N vs ring N+1) that keeps the cursor invalid for
        # the rest of the run and only surfaces three steps later as "cursor never became
        # valid" (TRIGGER_T2_TODO.md A.3.28-A.3.31). Assert the recovery HERE, in the terms
        # of the real cause, with a short retry so a slow re-arm is not a false failure.
        def _ring_wait(attempts: int = 3) -> list:
            """Poll RING:STATus? until enabled, so a slow re-arm is not a failure."""
            now = ctx.send(src, "SYSTem:TDMA:RING:STATus?").split(",")
            for _ in range(attempts):
                if now and now[0].strip() == "1":
                    return now
                time.sleep(1.0)
                now = ctx.send(src, "SYSTem:TDMA:RING:STATus?").split(",")
            return now

        ring_now = _ring_wait()
        retried = False
        if not ring_now or ring_now[0].strip() != "1":
            # Recovery attempt (A.3.31 item 2): re-issue the trial once. The drift comes from
            # a trial whose authorization failed, so a fresh grant is the cheapest recovery
            # path to try before giving up; if it does not help, the reason below carries
            # both readings for the next investigation.
            retried = True
            print(f"  ring down after trial ({','.join(ring_now[:6])}) - re-issuing the trial"
                  f" once as recovery", flush=True)
            grant_retry = ctx.send(src, f"CALibration:ORIGin:TRIAL {ctx.dep_gen},8192,256,0")
            print(f"  recovery trial: {grant_retry[:40]}", flush=True)
            ring_now = _ring_wait(4)
        if not ring_now or ring_now[0].strip() != "1":
            ctx.failures.append(
                f"Step 16: reference ring did not recover after ORIGin:TRIAL "
                f"(RING:STATus? {','.join(ring_now[:6])}"
                f"{', after a recovery trial' if retried else ''}) - an unauthorized trial "
                f"(UNAUTHORIZED+COMM_FAULT) bumps the ring config and leaves a sticky "
                f"drift, so no cursor can become valid in this run")
            print(f"  ring did NOT recover: {','.join(ring_now[:8])}", flush=True)
        elif retried:
            print(f"  ring recovered on the retry: {','.join(ring_now[:8])}", flush=True)
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
    # Post-ring observation (T1, HAOFV review): the load no longer re-issues the
    # priority lanes - a running ring refuses stopped-metadata writes anyway
    # (reject 115, tdma_service.c:1033-1034), so the old block proved nothing and
    # only masked the fact that the lane was never armed. The VDC owner keeps its
    # own lanes armed and restarts them after a retire (672edf0b); this reads the
    # result back and reports a timeout as a timeout (the old code labelled an
    # 8 s timeout "n/a (reference)" and did not record a failure).
    for uid_r, slot_r, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        if slot_r == ROWS[0][1]:
            generation = ctx.send(member, "SYSTem:VDC:PRIORity:SYNC?").strip()
            print(f"  post-ring priority {uid_r:016X} slot {slot_r}:"
                  f" SYNC generation {generation} (owner-armed {ctx.dep_gen})",
                  flush=True)
            if generation != str(ctx.dep_gen):
                ctx.failures.append(f"{uid_r:016X} Step 16 origin SYNC generation "
                                    f"{generation}, expected the owner to keep "
                                    f"{ctx.dep_gen}")
            continue
        generation = ctx.send(member, "SYSTem:VDC:PRIORity:MATCH?").strip()
        # FOLLow? refuses service while the lane is active (measured 8 s timeout), so it
        # is a best-effort read here: a timeout is reported as a timeout, never as
        # "n/a (reference)" and never as a lane failure - the armed state is already
        # carried by the MATCH generation above.
        follow_read = ctx.send(member, "SYSTem:VDC:PRIORity:FOLLow?").strip()
        follow_known = "<timeout>" not in follow_read
        print(f"  post-ring priority {uid_r:016X} slot {slot_r}:"
              f" MATCH generation {generation}"
              f" follow {'<not readable while running>' if not follow_known else follow_read}",
              flush=True)
        if "<timeout>" in generation:
            ctx.failures.append(f"{uid_r:016X} Step 16 MATCH generation readback timed out;"
                                f" the observation lane cannot be verified this way")
        elif generation != str(ctx.dep_gen):
            ctx.failures.append(f"{uid_r:016X} Step 16 MATCH generation {generation},"
                                f" expected the owner to keep {ctx.dep_gen}")
        elif follow_known and follow_read not in ("1", ""):
            ctx.failures.append(f"{uid_r:016X} Step 16 follow lane reads {follow_read},"
                                f" expected the owner's armed lane")
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
    admitted = {}     # layer 1: did this board accept the sync input at all?
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
            # READ:SYNC:STATe? is the product-level, authoritative answer for a
            # FOLLOWER, and every follower reports LOCKED once it locked onto the
            # reference. The reference itself is the SOURCE: it has nothing to
            # phase-lock onto, and this bench provides no physical reference, so
            # its own lock ladder legitimately stays OFF (measured: `1,1,0,
            # 536870913,0,0,"U"` with readiness `locked 0 / dpll.state 0` in every
            # T2 run; the known-good DPLL60 verdict is built on the internal
            # trace, never on this bit). Judging the source by its own lock bit
            # made this step unsatisfiable. The reference is judged by SERVING -
            # its readiness input is ready - and the proof that it is usable is
            # that the followers locked onto it.
            product_locked = '"LOCKED"' in state_page
            # Layered on purpose: "never admitted the sync input" and "admitted but
            # not locked" are different failures, and one "not locked" verdict could
            # not tell them apart on the bench (measured: followers reported
            # input_ready 0 / reason 8 on runs whose firmware locks on others).
            input_ok = bool(ready) and ready[0].strip() == "1"
            admitted[uid_r] = input_ok
            if not input_ok:
                print(f"    layer1 FAIL {uid_r:016X}: sync input not admitted"
                      f" (readiness reason {ready[2].strip() if len(ready) > 2 else '?'})",
                      flush=True)
            if is_reference:
                locked[uid_r] = input_ok                    # source: judged by serving
            elif input_ok and product_locked:
                locked[uid_r] = True
            print(f"  lock t+{3 * (attempt + 1)}s {uid_r:016X}"
                  f"{' [ref]' if is_reference else ' [fol]'}:"
                  f" SYNC:STATe {'LOCKED' if product_locked else state_page[:22]}"
                  f"{' (source, judged by serving)' if is_reference else ''}"
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
            # Live observation evidence (VDC-ISO-001): the readiness page's tail
            # carries what the matcher's own input predicate last saw - the
            # event-live flags, the predicate verdict and its anchor terms.
            if len(ready) > 30:
                try:
                    flags = int(ready[24].strip() or "0")
                except ValueError:
                    flags = -1
                print(f"    live_evidence flags 0x{flags & 0x1ff:02x}"
                      f" valid {ready[25].strip()} samples {ready[30].strip()}"
                      f" arm_epoch_lo {ready[26].strip()}"
                      f" observer_epoch {ready[27].strip()}"
                      f" tick_hz {ready[28].strip()}"
                      f" bracket_ok {ready[29].strip()}", flush=True)
                if len(ready) > 36:
                    print(f"    typed_lane carrier {ready[31].strip()}"
                          f" source_event {ready[32].strip()}"
                          f" binding {ready[33].strip()}"
                          f" carriers {ready[34].strip()}"
                          f" unique {ready[35].strip()}"
                          f" dup {ready[36].strip()}", flush=True)
                # Live matcher state (the matcher's own page requires STOP).
                if len(ready) > 48:
                    print(f"    match_live calls {ready[37].strip()}"
                          f" matched {ready[38].strip()}"
                          f" rejected {ready[39].strip()}"
                          f" superseded {ready[40].strip()}"
                          f" history_miss {ready[41].strip()}"
                          f" pending {ready[42].strip()}"
                          f" busy {ready[43].strip()}"
                          f" last_reason {ready[44].strip()}"
                          f" first_reject {ready[45].strip()}"
                          f" history_result {ready[46].strip()}"
                          f" anchor_fallback {ready[47].strip()}"
                          f" have_match {ready[48].strip()}", flush=True)
                if len(ready) > 53:
                    print(f"    match_arm arm_epoch_lo {ready[49].strip()}"
                          f" arm_epoch_hi {ready[50].strip()}"
                          f" observer_epoch {ready[51].strip()}"
                          f" tick_hz {ready[52].strip()}"
                          f" session {ready[53].strip()}", flush=True)
                # Which counter does the observer's witness actually track? Ring
                # status carries the adapter's own up/down/ring sequences, and the
                # RX page carries the carrier (frame) sequence of the typed lane.
                ring_status = ctx.send(member, "SYSTem:TDMA:RING:STATus?").split(",")
                if len(ring_status) > 12:
                    print(f"    ring_seq up_tx {ring_status[10].strip()}"
                          f" down_rx {ring_status[11].strip()}"
                          f" ring {ring_status[6].strip()}"
                          f" adapter_tx {ring_status[23].strip() if len(ring_status) > 23 else '-'}"
                          f" adapter_rx {ring_status[24].strip() if len(ring_status) > 24 else '-'}"
                          f" services {ring_status[9].strip()}", flush=True)
            else:
                print(f"    live_evidence <not in this build> ready[{len(ready)}]",
                      flush=True)
            # Event-observer evidence (VDC-ISO-001): the follower's matcher input
            # predicate fails on the event-live flags, so read the observer's own
            # state/reason/fault bits live. A non-ACTIVE state here is the cause
            # the flags can only report, not explain.
            event = ctx.send(member, "SYSTem:TDMA:EVENt:SNAPshot?").split(",")
            if len(event) > 15:
                try:
                    event_faults = int(event[6].strip() or "0")
                except ValueError:
                    event_faults = -1
                # Wire-sequence witness (VDC-ISO-001): forward steps are now
                # counted evidence, so a refusal must name repeat/wrap/backwards.
                evidence = ""
                if len(event) > 24:
                    evidence = (f" wire forward {event[16].strip()}"
                                f" step_max {event[17].strip()}"
                                f" repeat {event[18].strip()}"
                                f" wrap {event[19].strip()}"
                                f" backwards {event[20].strip()}"
                                f" zero {event[21].strip()}"
                                f" ones {event[22].strip()}"
                                f" refused 0x{event[23].strip()}"
                                f" last 0x{event[24].strip()}")
                if len(event) > 28:
                    evidence += (f" | sample_point prefix_bits {event[25].strip()}"
                                 f" seq_first 0x{event[26].strip()}"
                                 f" tx_first 0x{event[27].strip()}"
                                 f" rx_first 0x{event[28].strip()}")
                if len(event) > 37:
                    evidence += (f" | facade history_active {event[30].strip()}"
                                 f" hist_epoch {event[31].strip()}"
                                 f" hist_count {event[32].strip()}"
                                 f" hist_reason {event[33].strip()}"
                                 f" obs_epoch {event[34].strip()}"
                                 f" armed {event[35].strip()}"
                                 f" arm_valid {event[36].strip()}"
                                 f" arm_epoch {event[37].strip()}")
                print(f"    event_live state {event[1].strip()}"
                      f" reason {event[2].strip()} epoch {event[3].strip()}"
                      f" joined {event[4].strip()} published {event[5].strip()}"
                      f" fault_bits 0x{event_faults & 0xff:02x}"
                      f" service_count {event[10].strip()}"
                      f" gap_max_us {event[11].strip()}"
                      f" max_us {event[12].strip()}"
                      f" pending {event[13].strip()}/{event[14].strip()}"
                      f"/{event[15].strip()}{evidence}", flush=True)
            else:
                print(f"    event_live <unavailable> {','.join(event)[:80]}",
                      flush=True)
            # All ten phases, not just SYNC_TRIGGER: whether the DPLL phase (2)
            # is serviced at all decides whether the matcher/follow lane runs,
            # and the 2026-10-14 T2 runs left two followers with frozen match
            # counters (calls == rejected ~ 40k) while the origin and slot 3
            # kept counting.
            for phase_index, phase_name in enumerate(
                    ("TDMA", "VDC", "DPLL", "CALIB", "SYNC_CAP", "REFTEM",
                     "MODEL", "SYNC_TRG", "TRG_MEAS", "GUARD")):
                print(f"    phase{phase_index} {phase_name:<9}"
                      f" run {schedule_field(sched, phase_index, 6)}"
                      f" start_miss {schedule_field(sched, phase_index, 8)}"
                      f" deadline_miss {schedule_field(sched, phase_index, 10)}"
                      f" max_rt {schedule_field(sched, phase_index, 5)}"
                      f" overrun {schedule_field(sched, phase_index, 9)}", flush=True)
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
            # Live origin-handover evidence (VDC-ISO-001/T3): the typed SYNC
            # publication only exists inside the origin fast path, and the adapter
            # dispatch enters it only while origin.active != 0
            # (tdma_pio_spi_ring_adapter.c service dispatch); otherwise the
            # reference runs the resident process-image branch, which never calls
            # the priority provider. Reading the page here - while the ring is up -
            # names that branch, unlike the ORIGIN STOP sample in the stop handler
            # (which always reads active 0 because the origin just retired).
            origin = ctx.send(member, "READ:CALibration:ORIGin:RUNTIme?").split(",")
            if len(origin) > 19:
                print(f"    origin_live active {origin[1].strip()}"
                      f" comm_fsm {origin[2].strip()}/{origin[3].strip()}"
                      f" observed {origin[4].strip()} unobserved {origin[5].strip()}"
                      f" rejected {origin[6].strip()}"
                      f" published_gen {origin[7].strip()}/{origin[8].strip()}"
                      f" matched_gen {origin[9].strip()}"
                      f" admit allow/block {origin[17].strip()}/{origin[18].strip()}"
                      f" ts_flags {origin[19].strip()}", flush=True)
            else:
                print(f"    origin_live <unavailable> {','.join(origin)[:80]}", flush=True)
            # Live seat counters (TDMA-SEAT-01): the fast path's opportunities and
            # which seat boarded. This page has no stopped gate, so it separates
            # "the priority branch was never reached" (opportunities 0) from "it
            # ran but only the load's first-class seat boarded" (SEAT-01).
            seat = ctx.send(member, "SYSTem:TDMA:SEAT:COUNters?").split(",")
            if len(seat) > 11:
                print(f"    seat_live opportunities {seat[0].strip()}"
                      f" priority {seat[1].strip()} first_class {seat[2].strip()}"
                      f" yield {seat[3].strip()} fc_reject {seat[4].strip()}"
                      f" fc_wait_max {seat[5].strip()} fc_deferred {seat[11].strip()}"
                      f" starved {seat[9].strip()}/{seat[10].strip()}", flush=True)
            else:
                print(f"    seat_live <unavailable> {','.join(seat)[:80]}", flush=True)
        missing = [f"{uid:016X}" for uid, _s, _i, _o, _f in ROWS if uid not in locked]
        no_input = [f"{u[0]:016X}" for u in ROWS if admitted.get(u[0]) is False]
        if no_input:
            ctx.failures.append(
                f"Step 17 layer1: sync input not admitted on {no_input}"
                f" (not locked on {missing} is a consequence, not the cause)")
        else:
            ctx.failures.append(f"Step 17 layer2 not locked on {missing}"
                                " (input admitted everywhere)")
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
    # The dynamic start re-initialises the sync_io modules (the same reason the
    # legacy run_full path sent REAdy before *and* after the start), which returns
    # the gateway to the firmware default ready_required=1: an unanswered READY
    # then refuses the next position table (sync_io_gateway.c:587). A rig without
    # an analyzer cannot answer READY, so re-assert the operator's choice here,
    # before step 19 starts the pulse flow.
    _ready_required = os.environ.get("T2_READY_REQUIRED", "0")
    if _ready_required not in ("0", "1"):
        raise SystemExit("T2_READY_REQUIRED must be 0 or 1")
    for uid_r, slot_r, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        ctx.send(member, f"TRIGger:SEQuence:REAdy {_ready_required}")
        print(f"  slot {slot_r} SEQuence:REAdy {_ready_required} (after dynamic start) -> "
              f"{ctx.send(member, 'TRIGger:SEQuence:REAdy?')[:24]}", flush=True)
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
        if not getattr(ctx, "_t2_scan_diag_read", False):
            # Window health for the migration and the 361-position criterion
            # (TRIGGER_T2_TODO.md A.3.36): entered == announced, no unannounced
            # overwrite, no legacy/window announcement mismatch, and the derived
            # geometry. Printed once per run.
            ctx._t2_scan_diag_read = True
            print(f"T2SCAN {ctx.send(member, 'TRIGger:REServation:T2:SCAN?')}",
                  flush=True)
            # Keep the reading for the end-of-sampling criterion (A.3.38).
            _scan_fields = ctx.send(member, 'TRIGger:REServation:T2:SCAN?').split(",")
            if len(_scan_fields) >= 10:
                ctx._t2_scan_last = [f.strip() for f in _scan_fields[:15]]
            # Gateway staging refusals, the last eight GWDIAG fields:
            # NO_LEASE / BAD_COUNT / BAD_OFFSET / BAD_DELTA / BUSY / READY /
            # BAD_PULSE / SCHEDULE. READY is the one that stalls a rig without an
            # analyzer; BUSY is a previous table still pending. Sampling this at
            # the start of the window separates pre-sampling stalls from later ones.
            _gw = [f.strip() for f in ctx.send(
                member, "TRIGger:REServation:T2:ACTion:GWDIAG?").split(",")]
            if len(_gw) >= 8:
                print(f"  slot {slot_r} gw stage rejects "
                      f"NO_LEASE/BAD_COUNT/OFFSET/DELTA/BUSY/READY/PULSE/SCHEDULE = "
                      f"{','.join(_gw[-8:])}", flush=True)
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
        if err == "0" and valid == "1" and epoch != str(ctx.dep_gen) \
                and not ctx._epoch_mismatch_reported:
            ctx._epoch_mismatch_reported = True
            print(f"  epoch mismatch: T2 cursor epoch {epoch} != this run dep_gen "
                  f"{ctx.dep_gen}; the grid/T2 on the boards were bound by an "
                  f"earlier invocation. Rerun steps 8-12 in this invocation or "
                  f"use --steps 1-21.", flush=True)
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
                            f" (last err {err} valid {valid} epoch {epoch} vs dep_gen"
                            f" {ctx.dep_gen}); rerun steps 8-12 in this invocation or"
                            f" use --steps 1-21 so grid/T2 and the session share one"
                            f" generation")
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
        # Name the state as well as the symptom (TRIGGER_T2_TODO.md A.3.43/A.3.44): the
        # counter is started BEFORE the ring on purpose, because counter_binding() refuses
        # COUNter:STARt once ring.enabled/adapter_started is set (COUNTER_CHANGED,
        # trigger_counter_service.c:56-61). So state 1 (LOADED) here means it never started,
        # which is a different repair than "the pulses do not arrive" - and the two were
        # previously indistinguishable in the failure text.
        _state = live[13].strip() if len(live) > 13 else "?"
        _loaded = live[2].strip() if len(live) > 2 else "?"
        ctx.failures.append(f"Step 19 counter not advancing (count {counts}, state {_state}"
                            f" [1=LOADED = never started], loaded {_loaded}); pulses are"
                            f" not reaching the counter input, so no position can be"
                            f" observed or published (check the sync-out 3 -> sync-in 1"
                            f" loopback and the PWM source pin)")


@step(20)
def step_20_sampling(ctx, slot, board, uid):
    """Sample the T2 runtime and the position transport for T2_SAMPLE_TICKS x 2 s.

    The window has to cover the scan it is judging: the 9-position scan is done in ~10 s
    (default 5 ticks), while the 361-position scan travels ~45 s - a fixed 10 s window then
    verifies only its first ~28 positions and reports them as the whole run.
    """
    if ctx.preflight_frozen:
        print("  sampling skipped: SYNC_TRIGGER phase frozen on every board", flush=True)
        return
    published = "?"
    ticks = max(1, int(os.environ.get("T2_SAMPLE_TICKS", "5")))
    # Last chance to set the READY requirement: the TDMA start and the device load in the steps before
    # this one re-initialise the sync_io modules and leave it at the default (1 = wait for READY).
    _ready = os.environ.get("T2_READY_REQUIRED", "0")
    for uid_r, slot_r, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        ctx.send(member, f"TRIGger:SEQuence:REAdy {_ready}")
        print(f"  slot {slot_r} SEQuence:REAdy {_ready} (before sampling) -> "
              f"{ctx.send(member, 'TRIGger:SEQuence:REAdy?')[:24]}", flush=True)
    # Baseline BEFORE sampling: the runtime counters are monotonic across runs, so the verdict must subtract
    # this snapshot (see T2-PROGRESS-20261020-004). Written next to the post-run snapshot.
    try:
        import json as _json_b
        _base = {}
        for uid_r, slot_r, _i, _o, _f in ROWS:
            member = ctx.boards.get(f"{uid_r:016X}")
            if member is None:
                continue
            _base_state = query_page(ctx, member, "TRIGger:REServation:T2?").split(",")
            _base_scan = query_page(ctx, member, "TRIGger:REServation:T2:SCAN?",
                                    10).split(",")
            _base[slot_r] = {
                "why": ctx.send(member, "TRIGger:REServation:T2:ACTion:WHY?"),
                "diag": ctx.send(member, "TRIGger:REServation:T2:ACTion:DIAG?"),
                "gwdiag": ctx.send(member, "TRIGger:REServation:T2:ACTion:GWDIAG?"),
                "freeze_mismatch": t2_field(_base_state, "freeze_mismatch"),
                "freeze_fallback": t2_field(_base_state, "freeze_fallback"),
                "scan_overwrote": field_at(_base_scan, 8),
            }
        ctx._steploss_baseline = {
            slot_r: (values.get("freeze_mismatch"), values.get("freeze_fallback"),
                     values.get("scan_overwrote"))
            for slot_r, values in _base.items()}
        _base_path = pathlib.Path("out/sync_trigger/sequence_baseline.json")
        _base_path.parent.mkdir(parents=True, exist_ok=True)
        _base_path.write_text(_json_b.dumps(_base, indent=1), encoding="utf-8")
        print(f"  sequence baseline -> {_base_path}", flush=True)
    except Exception as _exc:                     # never fail a run because of a diagnostic
        print(f"  sequence baseline skipped: {type(_exc).__name__}", flush=True)
    for tick in range(ticks):
        time.sleep(2.0)
        for uid_r, slot_r, _i, _o, _f in ROWS:
            member = ctx.boards.get(f"{uid_r:016X}")
            if member is None:
                continue
            if not getattr(ctx, "_t2_scan_diag_read", False):
                # Window health for the migration and the 361-position criterion
                # (TRIGGER_T2_TODO.md A.3.36): entered == announced, no unannounced
                # overwrite, no legacy/window announcement mismatch, and the derived
                # geometry. Printed once per run.
                ctx._t2_scan_diag_read = True
                scan_text = query_page(ctx, member, "TRIGger:REServation:T2:SCAN?", 10)
                print(f"T2SCAN {scan_text}", flush=True)
                # Keep the reading for the end-of-sampling criterion (A.3.38).
                _scan_fields = scan_text.split(",")
                if len(_scan_fields) >= 10:
                    ctx._t2_scan_last = [f.strip() for f in _scan_fields[:15]]
            state = query_page(ctx, member, "TRIGger:REServation:T2?").split(",")
            position = ctx.send(member, "SYSTem:REFMEM:SYNC:FLIGHT:POSition?")
            # Keep every board's position-transport page for the end-of-sampling
            # distribution verdict below (the per-tick print is slot 0 only).
            _pos_seen = getattr(ctx, "_position_last", None)
            if _pos_seen is None:
                _pos_seen = {}
                ctx._position_last = _pos_seen
            _pos_seen[uid_r] = [f.strip() for f in position.split(",")]
            # Action-execution evidence per role (TRIGger:RESERVation:T2:ACTion?).
            # The decoded position slice becomes a local deadline plan; the role's
            # own template kind decides whether this board arms a SWITCH or a
            # GATEWAY deadline, and the masks say whether it armed/completed. Only
            # this page can answer "did each role execute its own sequence".
            action_page = ctx.send(member, "TRIGger:REServation:T2:ACTion?").split(",")
            # Window cumulative totals are the last twelve fields of T2:ACTion?: the seven
            # window totals (7) + five action_fail() reason counters + five group
            # sequence words (T2-SEQ-001) + five FREEZE-lead words, appended after the
            # variable-length events block. The same positions on T2? are unrelated
            # trailing values. A short or truncated reply (a command timeout answers
            # "<timeout>") must not raise: anything shorter than the tail is "unknown".
            # The length is coupled to the FIRMWARE's page version: 17 words are the
            # page up to INTERFACE-T2ACTION-01 v1, 22 add the group and freeze-lead
            # words. Point T2_ACTION_TAIL_FIELDS at the flashed version when running an
            # older image, otherwise every window number below is shifted.
            tail_fields = int(os.environ.get("T2_ACTION_TAIL_FIELDS", "26"))
            win_totals = ([f.strip() for f in action_page[-tail_fields:]]
                          if len(action_page) >= tail_fields else ["?"] * tail_fields)
            if tail_fields >= 7 and win_totals[0] not in ("?", "<timeout>") and any(
                    win_totals[i] not in ("?", "<timeout>")
                    for i in range(3, 5)
                    if int(win_totals[i] or 0) > int(win_totals[0] or 0)):
                print(f"  WARNING action page tail {tail_fields} disagrees with the "
                      f"flashed firmware (executed/abandoned > entered): set "
                      f"T2_ACTION_TAIL_FIELDS to the page version of the image on the "
                      f"boards", flush=True)
            _act_seen = getattr(ctx, "_action_last", None)
            if _act_seen is None:
                _act_seen = {}
                ctx._action_last = _act_seen
            _act_seen[uid_r] = [f.strip() for f in action_page]
            # Runtime preparation state per board: whether the local T2 owner ever
            # took a staged position out of its single-depth mailbox. Without this,
            # "lane_accept low" cannot be told apart from "owner never consumed".
            _rt_seen = getattr(ctx, "_runtime_last", None)
            if _rt_seen is None:
                _rt_seen = {}
                ctx._runtime_last = _rt_seen
            _rt_seen[uid_r] = [f.strip() for f in state]
            # Per-tick time series (window investigation): the window counters were
            # only read at the start and at the end of sampling, which cannot say
            # whether the window ever rolled. T2:SCAN? carries the cumulative
            # entered/announced/overwrote/mismatch at fixed indices and T2? carries
            # predictor.last_error - together they separate "advance never called"
            # from "a backward count latched the predictor".
            scan_tick = ctx.send(member, "TRIGger:REServation:T2:SCAN?").split(",")
            # Executor state DURING the run. The window counters alone cannot tell "the runtime
            # never armed" from "the executor armed and never completed": the gateway parks with
            # the runtime believing a slot is armed, and GWDIAG is the only page that carries the
            # plan cursor / pending / progress watchdog. Indices: page = field + 3.
            gw_tick = ctx.send(member, "TRIGger:REServation:T2:ACTion:GWDIAG?").split(",")
            pos_tick = _pos_seen[uid_r]

            def tick_field(page, index):
                return page[index].strip() if len(page) > index else "?"

            print(f"    WIN t+{2 * (tick + 1)}s slot {slot_r}"
                  f" {ROLES[slot_r] if slot_r < len(ROLES) else slot_r}"
                  f" entered {tick_field(scan_tick, 6)}"
                  f" announced {tick_field(scan_tick, 7)}"
                  f" overwrote {tick_field(scan_tick, 8)}"
                  f" mismatch {tick_field(scan_tick, 9)}"
                  f" pred_err {t2_field(state, 'predictor_last_error')}"
                  f" | lane acc {tick_field(pos_tick, 18)}"
                  f" dup {tick_field(pos_tick, 19)}"
                  f" rej {tick_field(pos_tick, 20)}"
                  f" drop {tick_field(pos_tick, 27)}"
                  f" deliv {tick_field(pos_tick, 30)}"
                  f" | rt recv {t2_field(state, 'received_mask')}"
                  f" frozen {t2_field(state, 'frozen_mask')}"
                  f" prep {t2_field(state, 'prepare_calls')}"
                  f" saved {t2_field(state, 'preview_saved')}"
                  f" | act armed {tick_field(action_page, 1)}"
                  f" | CUM events {t2_field(state, 'event_count')}"
                  f" dropped {t2_field(state, 'event_dropped')}"
                  f" mismatch {t2_field(state, 'freeze_mismatch')}"
                  f" fallback {t2_field(state, 'freeze_fallback')}"
                  f" miss_local {t2_field(state, 'freeze_missing_local')}"
                  f" | win {win_totals[0]}/{win_totals[1]}/{win_totals[2]}"
                  f"/{win_totals[3]}/{win_totals[4]}/{win_totals[5]}/{win_totals[6]}"
                  " (entered/announced/frozen/executed/abandoned/mark_rejected/alloc_rejected)"
                  f" | fail late/expired/no_plan/conflict/backend"
                  f" {win_totals[7]}/{win_totals[8]}/{win_totals[9]}/{win_totals[10]}/{win_totals[11]}"
                  f" | groups exec/count {win_totals[12]}/{win_totals[13]}"
                  f" slotpack next|inflight|done"
                  f" {win_totals[14]}/{win_totals[15]}/{win_totals[16]}"
                  f" | freeze lead min/last {win_totals[17]}/{win_totals[18]}"
                  f" expired {win_totals[19]} resend {win_totals[20]}"
                  f" abandoned {win_totals[21]}"
                  f" | seg lead pack declare/stage/arm/ackRtt"
                  f" {win_totals[22]}/{win_totals[23]}/{win_totals[24]}/{win_totals[25]}"
                  f" (min|last<<16: announce / freeze-staged / armed / ack round trip in ms;"
                  f" the ack value is the PAIRED now_ack - stage_at, not a lead)",
                  flush=True)
            print(f"    RT  t+{2 * (tick + 1)}s slot {slot_r}"
                  f" frozen {tick_field(pos_tick, 15)}"
                  f" published {tick_field(pos_tick, 9)}"
                  f" received {tick_field(pos_tick, 10)}"
                  f" desc_rej {tick_field(pos_tick, 11)}"
                  f" event {tick_field(pos_tick, 13)}"
                  f" drop {tick_field(pos_tick, 14)}"
                  f" prepared {tick_field(pos_tick, 39)}"
                  f" preview {tick_field(pos_tick, 40)}"
                  f" prep_reason {tick_field(pos_tick, 41)}",
                  flush=True)
            print(f"    GW  t+{2 * (tick + 1)}s slot {slot_r}"
                  f" plan {tick_field(gw_tick, 3)}"
                  f" plan_next {tick_field(gw_tick, 25)}"
                  f" pending {tick_field(gw_tick, 29)}"
                  f" sm {tick_field(gw_tick, 26)}"
                  f" svc_wait {tick_field(gw_tick, 22)}"
                  f" svc_done {tick_field(gw_tick, 24)}"
                  f" late {tick_field(gw_tick, 17)}"
                  f" prog_retired {tick_field(gw_tick, 42)}"
                  f" prog_stale {tick_field(gw_tick, 46)}"
                  f" state {tick_field(gw_tick, 47)}"
                  f" seq {tick_field(gw_tick, 48)}"
                  f" done_seq {tick_field(gw_tick, 49)}",
                  flush=True)
            counter = ctx.send(member, "TRIGger:REServation:COUNter?").split(",")
            # Rate evidence, not absolutes (VDC-ISO-001/T3): the seat and match
            # counters are cumulative since boot, so a single read cannot say
            # whether *this* load window publishes/observes. Sample the first and
            # the last tick and report the delta.
            seat = ctx.send(member, "SYSTem:TDMA:SEAT:COUNters?").split(",")
            # The priority-lane pages are STOPPED-STATE ONLY: they refuse while
            # ring.enabled/adapter_started or the lane is active (the same reason the
            # teardown read below is the one that works). Reading them mid-run made every
            # follower block for the full 8 s command timeout on each of five samples, and
            # the measured schedule shows what that cost: phase-7 start_miss/deadline_miss
            # in the tens of thousands on those boards, the cursor losing validity, and the
            # T2 service answering TRIGGER_T2_RUNTIME_CURSOR for the whole scan - so their
            # position holds never drained while the origin's did. Keep the mid-run sample
            # to the pages that answer while the ring is up; the seat counters are the rate
            # evidence (VDC-ISO-001/T3) and the priority pages are read at teardown.
            sample = {
                "seat": [f.strip() for f in seat[:3]],
            }
            first = getattr(ctx, "_rate_first", None)
            if first is None:
                ctx._rate_first = {uid_r: sample}
            else:
                first = first.setdefault(uid_r, sample)
                if tick == 4:
                    seat_delta = [f"{int(b) - int(a)}" if a.isdigit() and b.isdigit() else "?"
                                  for a, b in zip(first["seat"], sample["seat"])]
                    print(f"    rate slot {slot_r} over sampling:"
                          f" seat opp/pri/fc {seat_delta}"
                          " (priority match/rx pages are stopped-state only; read at teardown)",
                          flush=True)
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
            # Sticky cursor binding fault, read for EVERY board: the origin-only probe
            # below cannot see a follower's latch, and that latch is what makes a
            # follower's T2 service answer CURSOR(5) for the rest of the run (its lane
            # hold queue then only fills, so its position window stops growing while
            # lane_accept keeps counting). One extra read per board until it latches.
            if not hasattr(ctx, "_cursor_fault_seen"):
                ctx._cursor_fault_seen = set()
            if slot_r not in ctx._cursor_fault_seen:
                fc = ctx.send(member, "TRIGger:REServation:T2:CURSor?").split(",")
                fault_r = cursor_fault(fc)
                if fault_r is not None and fault_r["site"] not in ("0", ""):
                    ctx._cursor_fault_seen.add(slot_r)
                    mism = cursor_fault_mismatches(fault_r)
                    print(f"  t+{2 * (tick + 1)}s slot {slot_r} STICKY CURSOR FAULT:"
                          f" site {CURSOR_FAULT_SITES.get(fault_r['site'], fault_r['site'])}"
                          f" err {fault_r['error']} at seq {fault_r['sample_seq']}"
                          f" | differs: {', '.join(mism) if mism else 'nothing decoded'}"
                          f" | now valid {page_field(fc, CURSOR_FIELDS, 'valid')}"
                          f" err {page_field(fc, CURSOR_FIELDS, 'err')}"
                          f" ring_cfg {fault_r['ring_config']} model_token {fault_r['model_token']}",
                          flush=True)
            if slot_r == 0:
                cursor = ctx.send(member, "TRIGger:REServation:T2:CURSor?").split(",")
                model = ctx.send(member, "SYSTem:VDC:FEEDback:MODel?").split(",")
                ring = ctx.send(member, "SYSTem:TDMA:RING:STATus?").split(",")
                # TX image handoff. Field order = FIFO_FIELDS in
                # tools/tdma_ring_monitor/flight_bitmap_validate.py:79-88, i.e.
                #   index 6 tx_release_count = images RETIRED (turnover)
                #   index 7 tx_ready_count   = prepared-but-not-retired (0..2 = depth)
                # A position publication is refused while tx_ready_count >= 2
                # (distributed_refmem.c:1632 WAIT_RESOURCE/FIFO_READY), so these two
                # numbers are the direct measure of what blocks and what unblocks T2.
                fifo = ctx.send(member, "SYSTem:TDMA:FLIGHT:FIFO?").split(",")
                seat = ctx.send(member, "SYSTem:TDMA:SEAT:COUNters?").split(",")
                print(f"t+{2 * (tick + 1)}s vdc session "
                      f"{ctx.send(member, 'SYSTem:VDC:FEEDback:SESSion?')[:12]}"
                      f" | model ok {model[0] if model else '?'}"
                      f" token {model[1] if len(model) > 1 else '?'}"
                      f" session {model[2] if len(model) > 2 else '?'}"
                      f" | ring enabled {ring[0] if ring else '?'}"
                      f" started {ring[8] if len(ring) > 8 else '?'}"
                      f" | FIFO release {fifo[6] if len(fifo) > 6 else '?'}"
                      f" ready {fifo[7] if len(fifo) > 7 else '?'}"
                      # Identity check: if this page belongs to the same FIFO that the
                      # position publication uses (tdma_service_get_flight_fifo_snapshot,
                      # distributed_refmem.c:1618), its own tx_publish_count/reject must
                      # move with the POSITION page's tx_publish. If they stay flat, this
                      # page is a DIFFERENT instance and cannot explain WAIT_RESOURCE.
                      f" pub {fifo[1] if len(fifo) > 1 else '?'}"
                      f" rej {fifo[2] if len(fifo) > 2 else '?'}"
                      f" acq {fifo[3] if len(fifo) > 3 else '?'}"
                      f" slot {fifo[8] if len(fifo) > 8 else '?'}"
                      # The first-class seat is SINGLE DEPTH by contract
                      # (tdma_first_class.h:10-14: "the next one may be staged only
                      # after the origin boarded or retired it"), so the seat's own
                      # counters are the purpose-built evidence for the blocking that
                      # the position page reports as WAIT_RESOURCE/reason 3
                      # (distributed_refmem.c:1742-1746 reuses FAILURE_FIFO_READY for
                      # "the previous record still waits"). SYSTem:TDMA:SEAT:COUNters?
                      # indices: 6 first_class_stage_refused_count,
                      #          7 first_class_retired_waiting_count,
                      #         11 seat_first_class_deferred_count.
                      f" | SEAT refused {seat[6] if len(seat) > 6 else '?'}"
                      f" retired {seat[7] if len(seat) > 7 else '?'}"
                      f" deferred {seat[11] if len(seat) > 11 else '?'}", flush=True)
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
                      # Position transport, named by the page's own field order
                      # (scpi_system_snapshot_commands.c: scpi_cmd_refmem_sync_flight_position_q).
                      # The lane path is the one T2 uses: once
                      # distributed_refmem_position_priority_rx() owns the transport
                      # (mailbox class == T2_POSITION), the classic mailbox parser -
                      # and therefore position_rx_complete_count - is bypassed by
                      # design (distributed_refmem.c:1893-1896). lane_accept_count is
                      # incremented only on a successful decode+stage, so it is the
                      # distribution evidence; rx_complete_count only ever counts the
                      # classic path.
                      f" | POSITION tx_publish {fields[3] if len(fields) > 3 else '?'}"
                      f" tx_seq {fields[9] if len(fields) > 9 else '?'}"
                      f" tx_frag {fields[2] if len(fields) > 2 else '?'}"
                      f" tx_phase {fields[6] if len(fields) > 6 else '?'}"
                      # Per-tick wait reason/count (page indices 7/8): the phase alone
                      # says WAIT_RESOURCE, and only these two split it into
                      # FIFO_SNAPSHOT (2) vs FIFO_READY (3) vs FEEDBACK (4) vs NONE (0).
                      f" tx_wait {fields[7] if len(fields) > 7 else '?'}"
                      f"/{fields[8] if len(fields) > 8 else '?'}"
                      f" | lane accept {fields[18] if len(fields) > 18 else '?'}"
                      f" dup {fields[19] if len(fields) > 19 else '?'}"
                      f" reject {fields[20] if len(fields) > 20 else '?'}"
                      f" owned {fields[21] if len(fields) > 21 else '?'}"
                      f" hold {fields[22] if len(fields) > 22 else '?'}"
                      f" drain {fields[23] if len(fields) > 23 else '?'}"
                      f" drain_fail {fields[24] if len(fields) > 24 else '?'}"
                      f" hold_drop {fields[27] if len(fields) > 27 else '?'}"
                      f" hold_delivered {fields[30] if len(fields) > 30 else '?'}"
                      f" decode_reject {fields[29] if len(fields) > 29 else '?'}"
                      f" | classic rx_complete {fields[10] if len(fields) > 10 else '?'}"
                      f" rx_reject {fields[11] if len(fields) > 11 else '?'}"
                      f" rx_last_failure {fields[17] if len(fields) > 17 else '?'}"
                      # The rolling window's health facts are appended to T2?, but a tail
                      # slice is NOT a reliable way to read them: measured on the bench
                      # (t2-20261013-020455) state[-9:] produced self-contradictory values
                      # (entered 0 with late 18, step_mdeg 104 instead of 5000), so the page
                      # does not end with exactly those nine fields. Print them as unknown
                      # until the page describes its own block lengths or the fields get
                      # named indices - see TRIGGER_T2_TODO.md A.3.2c. A mislabelled reading
                      # is worse than no reading.
                      f" | window announced {'?'}/entered {'?'} overwrote_unannounced {'?'}"
                      f" late {'?'} step_mdeg {'?'} geometry {'?'}pos/{'?'}pul",
                      flush=True)
                # The origin FREEZE path maxima (pending check, threshold stage,
                # resend/ack pass, whole call) are NOT at the end of T2? any more: the
                # rolling-window block was appended after them, and the event block
                # (variable length) sits between, so no "count from the end" can find
                # them - state[-4:] read window fields and printed them under this
                # label. Until the page describes its own block lengths (or the maxima
                # get a named index), report them as unavailable rather than mislabelled:
                # see docs/trigger/t2/TRIGGER_T2_TODO.md A.3.2c.
                freeze = ["?", "?", "?", "?"]
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

    # The sequence counters are only valid BEFORE teardown, so they are captured here and written to a
    # file the sequence tool reads afterwards (polling the boards later always yields zeros).
    # Settle: positions still in flight when the sampling window ends are legitimate, but they are
    # not in the counters yet, so the acceptance saw 708/722 instead of 722/722. Poll each role's
    # DIAG until it stops moving (bounded) before taking the snapshot.
    try:
        for _settle in range(20):
            _moved = False
            for _row in ROWS:
                _member = ctx.boards.get(f"{_row[0]:016X}")
                if _member is None:
                    continue
                try:
                    _now = ctx.send(_member, "TRIGger:REServation:T2:ACTion:DIAG?").split(",")
                    _key = (_row[0], _now[1] if len(_now) > 1 else "", _now[2] if len(_now) > 2 else "")
                    _prev = getattr(ctx, "_settle_last", {})
                    if _prev.get(_row[0]) != _key:
                        _prev[_row[0]] = _key
                        ctx._settle_last = _prev
                        _moved = True
                except Exception:
                    pass
            if not _moved:
                break
            time.sleep(0.25)
    except Exception:
        pass

    try:
        import json as _json
        _snap = {}
        for uid_r, slot_r, _i, _o, _f in ROWS:
            member = ctx.boards.get(f"{uid_r:016X}")
            if member is None:
                continue
            # One page per read, one retry, and a recorded failure: a timed-out page must never be
            # stored as if it were a reading (round 44 saw `T2?` come back with 2 fields).
            _pages = {
                "why": "TRIGger:REServation:T2:ACTion:WHY?",
                "window": "TRIGger:REServation:T2:ACTion:WINdow?",
                "diag": "TRIGger:REServation:T2:ACTion:DIAG?",
                "t2": "TRIGger:REServation:T2?",
                "gwdiag": "TRIGger:REServation:T2:ACTion:GWDIAG?",
                # tail 7 = entered / announced / frozen / executed / abandoned / mark_rejected / alloc_rejected (the position-window accounting).
                "action": "TRIGger:REServation:T2:ACTion?",
            }
            # Minimum plausible length per page. `1,1` is the 2-word FAIL reply, and a page that
            # short used to be stored as if it were a reading (the gateway answered `1,1` to every
            # T2? while its snapshot guard was contended, so its frozen/received masks were read
            # from a 2-token string and reported as zeros).
            _min_fields = {"t2": 40, "gwdiag": 40, "why": 8, "window": 7, "diag": 20, "action": 8}
            _entry, _failed = {}, []
            for _name, _page in _pages.items():
                _value = ""
                for _attempt in range(3):
                    try:
                        _value = ctx.send(member, _page)
                        if _value and len(_value.split(",")) >= _min_fields.get(_name, 2):
                            break
                        _value = ""
                    except Exception:
                        _value = ""
                    time.sleep(0.05)
                if not _value:
                    _failed.append(_name)
                _entry[_name] = _value
            if _failed:
                _entry["failed"] = _failed
                print(f"  snapshot page(s) missing for slot {slot_r}: {','.join(_failed)}", flush=True)
            _snap[slot_r] = _entry
        _path = pathlib.Path("out/sync_trigger/sequence_snapshot.json")
        _path.parent.mkdir(parents=True, exist_ok=True)
        _path.write_text(_json.dumps(_snap, indent=2), encoding="utf-8")
        print(f"  sequence snapshot -> {_path}", flush=True)
    except Exception as _exc:                     # never fail the run because of a diagnostic
        print(f"  sequence snapshot skipped: {type(_exc).__name__}", flush=True)
    # Judge the mask against the counter's own progress (TRIGGER_T2_TODO.md A.3.25): a
    # position can only be announced after the counter crosses its threshold
    # (4750/9750/14750 = one STEP's lead before 5000/10000/15000), and this rig's counter
    # start speed is bimodal (about 3.8e3 vs 1.1e4 at t+2s). A fixed-time "all three
    # published" check therefore fails for reasons unrelated to the firmware under test -
    # it produced six rounds of misattributed regressions. Only a run whose counter DID
    # cross threshold(s) yet published fewer than that is a failure.
    try:
        counter_fields = ctx.send(member, "TRIGger:REServation:COUNter?").split(",")
        final_count = int(counter_fields[1]) if len(counter_fields) > 1 else 0
    except Exception:                                                    # noqa: BLE001
        final_count = 0
    crossed = 0
    for index in T2_THRESHOLDS:
        raw = state[index].strip() if len(state) > index else ""
        if raw.isdigit() and final_count >= int(raw):
            crossed += 1
    try:
        published_count = int(published)
    except (TypeError, ValueError):
        published_count = 0
    if published_count < crossed:
        ctx.failures.append(f"Step 20 reference published_mask {published} at the end of"
                            f" sampling (counter {final_count} had crossed {crossed} of"
                            f" {len(T2_THRESHOLDS)} thresholds)")

    # Additive window-health criterion (TRIGGER_T2_TODO.md A.3.36/A.3.38), read from
    # T2:SCAN?: the rolling window must announce every position it entered, never replace
    # an unannounced entry, and (during the migration) never disagree with the legacy
    # announced set. This is the criterion that a 361-position scan can also express,
    # which the three-bit legacy mask cannot. Deliberately ADDITIVE: the legacy mask check
    # above still runs, so this cannot weaken the current verdict.
    scan = getattr(ctx, "_t2_scan_last", None)
    # End-of-sampling scan window: the single early read only covers the first
    # ~2 s (measured: entered 2 / announced 0), so it cannot say whether the
    # publish path walked all 361 positions. Read it again now and show both.
    _scan_end_raw = ctx.send(member, "TRIGger:RESERVation:T2:SCAN?")
    print(f"  T2SCAN end {_scan_end_raw}", flush=True)
    # Self-consistency of the travel geometry reported by T2:SCAN? (A.3.72).
    # Uses only values from the page itself, so it holds for any configured
    # span: travel = pre_roll + (positions - 1) * step_pulses + post_roll.
    if scan is not None and len(scan) >= 15:
        try:
            _m_start, _m_stop, _m_pulses = int(scan[10]), int(scan[11]), int(scan[12])
            _pre_p, _post_p = int(scan[13]), int(scan[14])
            _pos, _step_p = int(scan[4]), int(scan[5])
            _expect = _pre_p + (_pos - 1) * _step_p + _post_p
            _pulses_ok = _m_pulses == _expect
            _bounds_ok = _m_stop - _m_start == _m_pulses
            print(f"  travel: start={_m_start} stop={_m_stop} pulses={_m_pulses} "
                  f"pre={_pre_p} post={_post_p} positions={_pos} step_pulses={_step_p} "
                  f"-> {'OK' if (_pulses_ok and _bounds_ok) else 'MISMATCH'}", flush=True)
            if not (_pulses_ok and _bounds_ok):
                raise RuntimeError("T2 travel geometry is not self-consistent (A.3.72)")
        except ValueError as _exc:
            print(f"  travel: unparsable scan field ({_exc})", flush=True)
    if scan and scan[6].isdigit() and scan[7].isdigit():
        entered, announced = int(scan[6]), int(scan[7])
        # NOT entered == announced: at the end of the sampling window the window has only
        # just begun (entered = WINDOW_MAX, announced = 0) - the announcements happen as
        # the counter crosses the thresholds, which is later (measured: the end-of-run
        # reading is 3/3). Asserting equality here produced false failures on two healthy
        # runs (2026-10-13), the same criterion/timing trap as A.3.16/A.3.25/A.3.28.
        # Only invariants that must hold at ANY moment are asserted: a window can never
        # announce more positions than it entered, and rolling must only consume announced
        # entries.
        if announced > entered:
            ctx.failures.append(
                f"Step 20 window health: announced {announced} > entered {entered} "
                f"(T2:SCAN? {','.join(scan)})")
        if scan[8].isdigit() and int(scan[8]) != 0:
            ctx.failures.append(
                f"Step 20 window health: overwrote_unannounced {scan[8]} != 0 "
                f"(T2:SCAN? {','.join(scan)})")
        # announce_mismatch compares the window's announced set with the legacy three-slot
        # mask, which is only meaningful while the scan's positions coincide with those
        # three named slots (A.3.13). At any other scan length - e.g. the 361-position
        # 0..360@1 scan - the two models legitimately disagree, so the check is gated on
        # that premise instead of failing (measured: mismatch = 558 during a healthy
        # 361-position run, 2026-10-13).
        legacy_premise = len(scan) > 4 and scan[4].strip() == "3"
        if legacy_premise and scan[9].isdigit() and int(scan[9]) != 0:
            ctx.failures.append(
                f"Step 20 window health: announce_mismatch {scan[9]} != 0 "
                f"(T2:SCAN? {','.join(scan)})")

    # Position-distribution verdict (TRIGGER_T2_TODO, position transport). The
    # SOURCE publishes slices; every other board receives them on the
    # first-class/priority lane that T2 owns, so the delivery evidence is the lane
    # counters, not the classic mailbox ones: while the lane owns the transport
    # (mailbox class == T2_POSITION) the classic parser is skipped by design
    # (distributed_refmem.c:1893-1896), so position_rx_complete_count stays 0 on a
    # perfectly delivered run. lane_accept_count is incremented only on a
    # successful decode+stage (distributed_refmem_position_ack.inc:287).
    seen = getattr(ctx, "_position_last", {}) or {}
    if seen:
        source_publish = 0
        follower_accepts = []
        for uid_r, slot_r, _i, _o, _f in ROWS:
            fields_p = seen.get(uid_r)
            if fields_p is None:
                continue

            def pos_field(index):
                return fields_p[index] if len(fields_p) > index else "?"

            print(f"  POSITION slot {slot_r} {uid_r:016X}"
                  f" tx_publish {pos_field(3)} tx_seq {pos_field(9)}"
                  f" tx_frag {pos_field(2)} tx_phase {pos_field(6)}"
                  # Fields 7/8 are the ONLY discriminator for what blocks a position
                  # publication; before this they were silently dropped, so every reading
                  # was blind to it. Index cross-checked against the tool's own uses of
                  # 2/3/6/9/10/11/12/17/18/19/20 in this same printout (they match the
                  # documented page order exactly).
                  #   phase:    1 STAGED 2 WAIT_VDC 3 WAIT_RESOURCE 4 WAIT_FEEDBACK
                  #             5 SUBMITTED 6 RECEIVED 7 COMPLETE 8 FAILED
                  #   wait:     0 NONE 1 BAD_DESCRIPTOR 2 FIFO_SNAPSHOT 3 FIFO_READY
                  #             4 FEEDBACK 5 PUBLISH 6 CRC 7 FRAGMENT   (reason/count)
                  f" tx_wait {pos_field(7)}/{pos_field(8)}"
                  f" | lane_accept {pos_field(18)} lane_dup {pos_field(19)}"
                  f" lane_reject {pos_field(20)} owned {pos_field(21)}"
                  f" hold {pos_field(22)} drain {pos_field(23)}"
                  f" drain_fail {pos_field(24)} hold_drop {pos_field(27)}"
                  f" hold_delivered {pos_field(30)}"
                  f" decode_reject {pos_field(29)}"
                  f" | FREEZE latched {pos_field(31)} kept {pos_field(32)}"
                  f" taken {pos_field(33)}"
                  f" | classic rx_complete {pos_field(10)}"
                  f" rx_reject {pos_field(11)} rx_pending_mask {pos_field(12)}"
                  f" rx_last_failure {pos_field(17)}", flush=True)
            try:
                if slot_r == 0:
                    source_publish = int(pos_field(3))
                else:
                    follower_accepts.append((slot_r, int(pos_field(18))))
            except (TypeError, ValueError):
                pass
        delivered = sum(1 for _slot, accept in follower_accepts if accept > 0)
        verdict = ("DISTRIBUTED" if follower_accepts and delivered == len(follower_accepts)
                   else ("PARTIAL" if delivered else "NO DELIVERY"))
        print(f"  POSITION distribution: source tx_publish {source_publish};"
              f" follower lane_accept {follower_accepts} -> {verdict}", flush=True)
        # Only the unambiguous failure is a criterion: the source published, yet no
        # follower staged a single slice.
        if source_publish > 0 and follower_accepts and delivered == 0:
            ctx.failures.append(
                f"Step 20 position distribution: source published {source_publish} slices"
                f" but no follower staged any (lane_accept {follower_accepts})")
        # A source that published NOTHING is the other unambiguous failure, and it used to
        # slip through as OK (measured 2026-10-17: tx_publish 0 for a whole run because the
        # VDC feedback path stalled the publish function, and step 20 still reported OK).
        if source_publish == 0:
            ctx.failures.append(
                "Step 20 position distribution: the source published no slice at all"
                f" (tx_publish 0, follower lane_accept {follower_accepts}); check"
                " SYSTem:REFMEM:SYNC:FLIGHT? feedback-refusal reason")

    # Role-specific action execution (TRIGger:RESERVation:T2:ACTion?): the decoded
    # position slice becomes a local deadline plan, and each board's deployment role
    # selects its template kind - SWITCH_A/SWITCH_B arm a switch deadline, GATEWAY
    # arms a gateway pulse, COUNTER only sources positions. Page order
    # (scpi_trigger_t2_runtime_commands.inc: scpi_cmd_trigger_t2_action_q):
    # [0] 1, [1] armed, [2] completed, [3] failed, [4] uncertain, then per position
    # [5+5i] sequence, [6+5i..7+5i] deadline_tick (lo,hi), [8+5i] backend_state,
    # [9+5i] origin_width_ticks, then [20] event_count and the events.
    acts = getattr(ctx, "_action_last", {}) or {}
    if acts:
        executed = 0
        for uid_r, slot_r, _i, _o, _f in ROWS:
            page = acts.get(uid_r)
            if page is None:
                continue

            def act_field(index):
                return page[index] if len(page) > index else "?"

            role = ROLES[slot_r] if slot_r < len(ROLES) else f"slot{slot_r}"
            # The per-position layout is FOUR fields (sequence, deadline as one UInt64 field,
            # backend state, origin width) - verified against scpi_cmd_trigger_t2_action_q.
            # It used to be parsed with a stride of 5, which shifted every value by one
            # ("seq 150405957821", "width 218"); the prefix (armed/completed/failed/uncertain)
            # is positional and remains the trustworthy part.
            stride = int(os.environ.get("T2_ACTION_FIELDS", "4") or "4")
            per_position = []
            if stride >= 4:
                count = min(len(T2_TARGETS), max(0, (len(page) - 5) // stride))
                for index in range(count):
                    base = 5 + stride * index
                    per_position.append(
                        f"[{index}] seq {act_field(base)}"
                        f" deadline {act_field(base + 1)}:{act_field(base + 2)}"
                        f" backend {act_field(base + 3)} width {act_field(base + 4)}")
            else:
                per_position.append(f"[per-position tail not parsed: fields {len(page)},"
                                    " set T2_ACTION_FIELDS]")
            print(f"  ACTION slot {slot_r} {role} {uid_r:016X}"
                  f" armed {act_field(1)} completed {act_field(2)}"
                  f" failed {act_field(3)} uncertain {act_field(4)}"
                  f" | {' '.join(per_position)}", flush=True)
            try:
                if int(act_field(2)) != 0:
                    executed += 1
            except (TypeError, ValueError):
                pass
        print(f"  ACTION execution: {executed} of {len(acts)} boards completed an"
              f" action (roles {[ROLES[s] if s < len(ROLES) else s for _u, s, _i, _o, _f in ROWS]})",
              flush=True)

    # Local runtime consumption per role: a role can only execute when its own T2
    # owner took a staged descriptor (prepare) and froze it. received_mask shows the
    # mailbox was populated for this board, prepare/preview that it was consumed,
    # frozen_mask that a deadline plan exists.
    rts = getattr(ctx, "_runtime_last", {}) or {}
    for uid_r, slot_r, _i, _o, _f in ROWS:
        page = rts.get(uid_r)
        if page is None:
            continue
        role = ROLES[slot_r] if slot_r < len(ROLES) else f"slot{slot_r}"
        print(f"  RUNTIME slot {slot_r} {role} {uid_r:016X}"
              f" received {t2_field(page, 'received_mask')}"
              f" frozen {t2_field(page, 'frozen_mask')}"
              f" published {t2_field(page, 'published_mask')}"
              f" announced {t2_field(page, 'announced_mask')}"
              f" prepare {t2_field(page, 'prepare_calls')}"
              f" preview_saved {t2_field(page, 'preview_saved')}"
              f" prep_reason {t2_field(page, 'prepared_last_reason')}"
              f" desc_reject {t2_field(page, 'descriptor_reject')}"
              f" trans_reject {t2_field(page, 'transport_reject')}"
              f" last_error {t2_field(page, 'last_error')}", flush=True)
    if getattr(ctx, "_query_retries", 0):
        print(f"  query retries used: {ctx._query_retries}", flush=True)


def step_loss_verdict(ctx):
    """Per-role window verdict: no lost step once the pulse flow has stopped.

    A frozen plan that is neither executed nor abandoned after the flow stops is
    a lost step.  ``slotpack inflight`` is the only legitimate reason for a
    pending plan, so pending must not exceed it; freeze mismatch/fallback must be
    zero.  Read before UNMOUNT so the counters are still live.
    """
    tail_fields = int(os.environ.get("T2_ACTION_TAIL_FIELDS", "26"))
    verdicts = []
    time.sleep(2.0)
    for uid_r, slot_r, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        role = ROLES[slot_r] if slot_r < len(ROLES) else f"slot{slot_r}"
        state = query_page(ctx, member, "TRIGger:REServation:T2?").split(",")
        action = query_page(ctx, member, "TRIGger:REServation:T2:ACTion?", 20).split(",")
        tail = ([f.strip() for f in action[-tail_fields:]]
                if len(action) >= tail_fields else ["?"] * tail_fields)

        def as_int(value):
            try:
                return int(value)
            except (TypeError, ValueError):
                return None

        entered, announced, frozen, executed, abandoned = (
            as_int(tail[index]) for index in range(5))
        base = (getattr(ctx, "_steploss_baseline", {}) or {}).get(
            slot_r, (None, None, None))
        scan = query_page(ctx, member, "TRIGger:REServation:T2:SCAN?", 10).split(",")

        def delta(now, before):
            now_value, before_value = as_int(now), as_int(before)
            if now_value is None or before_value is None:
                return None
            return now_value - before_value

        mismatch = delta(t2_field(state, "freeze_mismatch"), base[0])
        fallback = delta(t2_field(state, "freeze_fallback"), base[1])
        overwrote = delta(field_at(scan, 8), base[2])
        pending = (max(0, frozen - executed - abandoned)
                   if None not in (frozen, executed, abandoned) else None)
        # A lost step is a frozen plan that never executed/abandoned, or a window
        # overwrite before announcement. FREEZE version mismatches that fell back
        # are reported as WARN: the position still executed.
        pending_ok = True if slot_r == 0 else pending == 0
        overwrite_ok = overwrote in (0, None)
        ok = pending_ok and overwrite_ok
        verdicts.append((role, ok))
        warn = " WARN freeze mismatch/fallback" if (mismatch or fallback) else ""
        print(f"  STEPLOSS slot {slot_r} {role}: {'PASS' if ok else 'FAIL'}"
              f" entered {entered} announced {announced} frozen {frozen}"
              f" executed {executed} abandoned {abandoned} pending {pending}"
              f" overwrote +{overwrote} mismatch +{mismatch} fallback +{fallback}"
              f"{warn}", flush=True)
    if verdicts and not all(ok for _role, ok in verdicts):
        ctx.failures.append("Step 21 step-loss verdict failed: "
                            + ", ".join(role for role, ok in verdicts if not ok))
    return verdicts


@step(21)
def step_21_teardown(ctx, slot, board, uid):
    """Complete stop, with the stopped-state priority readout, and the summary."""
    src = ctx.src
    if src is not None:
        ctx.send(src, "MODEl:TURNtable:STOP")
        ctx.send(src, "TRIGger:REServation:SOURce:STOP")
    try:
        step_loss_verdict(ctx)
    except Exception as exc:  # never fail the teardown because of a verdict read
        print(f"  step-loss verdict skipped: {type(exc).__name__}: {exc}", flush=True)
    for uid_r, _s, _i, _o, _f in ROWS:
        member = ctx.boards.get(f"{uid_r:016X}")
        if member is None:
            continue
        ctx.send(member, "SYSTem:TDMA:RING:STOP")
        ctx.send(member, "TRIGger:REServation:T2:UNMOUNT")
        # Priority-lane diagnostics are readable only while stopped: this is the one
        # moment that shows whether the bring-up armed the class that gives the
        # SYNC_TRIGGER phase its ingress slack. The match pages answer the T3
        # question directly - the RX counters say typed records arrived, so the
        # remaining question is why a follower did or did not form a match
        # (generation/session/binding, or a rejection code).
        for page in ("SYSTem:VDC:PRIORity:RX?", "SYSTem:VDC:PRIORity:TX?",
                     "SYSTem:VDC:PRIORity:SYNC?", "SYSTem:VDC:PRIORity:MATCH?",
                     "SYSTem:VDC:PRIORity:MATCH:STATus?",
                     "SYSTem:VDC:PRIORity:FOLLow:STATus?",
                     "SYSTem:VDC:PRIORity:FOLLow:HEALth?",
                     "SYSTem:VDC:FEEDback:SESSion?"):
            print(f"  {uid_r:016X} {page} -> {ctx.send(member, page)[:200]}", flush=True)
        ctx.send(member, "SYSTem:VDC:OUTPut:RUN:STOP")
        ctx.send(member, "SYSTem:VDC:FEEDback:SESSion 0")
        ctx.send(member, "TRIGger:REServation:T2:UNMOUNT")
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
              generation=None, pre_lock_mask=None, release_quarantine=False) -> int:
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
    ctx = StepContext(board_ids,
                      generation_for_selection(selection, generation),
                      reuses_recorded_generation(selection) and generation is None)
    if pre_lock_mask:
        ctx.pre_lock_mask = pre_lock_mask
        print(f"pre-lock mask experiment enabled: {pre_lock_mask}", flush=True)
    if release_quarantine:
        ctx.release_quarantine = True
        print("quarantine-release experiment enabled: mask 0 -> foundation in step 2",
              flush=True)
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
            # Self-diagnosing failures: the log otherwise only says "IndexError: list
            # index out of range" and the offending line is lost (see step 20).
            _frames = []
            _tb = exc.__traceback__
            while _tb is not None:
                _name = str(_tb.tb_frame.f_code.co_filename).replace(chr(92), '/')
                _frames.append(f"{_name.split('/')[-1]}:{_tb.tb_lineno}")
                _tb = _tb.tb_next
            if _frames:
                error += " | at " + " <- ".join(_frames[-4:])
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
