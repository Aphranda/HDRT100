"""T2 bench enablement: grid -> deployment -> action table -> T2 runtime.

Every CRC and field order mirrors the firmware (trigger_deployment.h,
trigger_action_table.h) so the host and the device agree bit for bit.
Read-only except for the documented enable commands; prints every response.
"""
from __future__ import annotations

import argparse
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
    args = parser.parse_args()

    if args.full:
        return run_full([f"{row[0]:016X}" for row in ROWS])
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
        print("T2:CONFigure ->", q(f"TRIGger:REServation:T2:CONFigure 1,0,15,1,0,0,999,0,0,0")[:24])
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
                          read_timeout=0.02, settle=0.02, keep_open=True, short_open=False)
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
        send(board, "SYSTem:VDC:OUTPut:RUN:STOP")
        send(board, "MODEl:TURNtable:STOP")
        send(board, "TRIGger:REServation:COUNter:STOP")
        send(board, "TRIGger:REServation:COUNter:CLEar")
        send(board, "TRIGger:REServation:COUNter:UNLoad")
        send(board, "TRIGger:REServation:DEPloyment:CLEar")
        send(board, "TRIGger:REServation:GRID:CONFigure 1,1,0,1000000,2")
        send(board, f"TRIGger:REServation:DEPloyment:STAGe {dep_gen},{dep_crc},{payload}")
        send(board, f"TRIGger:REServation:DEPloyment:ACTivate {dep_gen},{dep_crc}")
        _dep = send(board, "TRIGger:REServation:DEPloyment?").split(",")
        print(f"  deploy after activate: {_dep[2:7]}"
              " (active?, last_gen, staged?, staged_crc, active_crc)")
        kind = kinds[slot]
        arg = pulses.get(slot, 0)
        if slot:
            acrc = action_table_crc(1, dep_gen, dep_crc, 1, 1, 0, 1000000, 2,
                                    [(1, slot, kind, arg, 0, 1)])
            send(board, f"TRIGger:REServation:ACTion:INSTall 1,{acrc},{dep_gen},{dep_crc},1,1,0,"
                        f"1000000,2,1,1,{slot},{kind},{arg},0,1")
        rows = [(1, 1, slot, kind, arg, 1, 0)]
        tcrc = template_crc(1, rows)
        send(board, f"TRIGger:REServation:TEMPLate:INSTall 1,{tcrc},1,"
                    + ",".join(str(v) for row in rows for v in row))
        send(board, "TRIGger:REServation:TEMPLate:ACTivate")
        send(board, "SYSTem:TDMA:FLIGHT:FIFO:RESet")
        # NO1 (slot 0) is the only position source; every board names source slot 0.
        send(board, f"TRIGger:REServation:T2:CONFigure 1,0,14,{int(slot == 0)},10,10,2,0,0,180000")
        diag = send(board, "TRIGger:REServation:SEAT:MOUNT?").split(",")
        print(f"  configure diag {diag[4] if len(diag) > 4 else '?'}"
              " (91/92/93 prereq, 100/101 inner, 140-148 validate, 111-137 stopped)")
        send(board, "TRIGger:REServation:T2:STARt 1,0")
        state = send(board, "TRIGger:REServation:T2?").split(",")
        print(f"{uid:016X} slot {slot} | T2 configured {state[2] if len(state) > 2 else '?'}"
              f" active {state[3] if len(state) > 3 else '?'}"
              f" last_error {state[1] if len(state) > 1 else '?'}")

    # (1b) VDC feedback session (monotonic) + output run with valid ranges,
    #      prepared while stopped so the ring start can enable the hardware output.
    session = (int(time.time()) + 5000) & 0xFFFFFFFF
    for uid, _s, _i, _o, _f in ROWS:
        board = boards.get(f"{uid:016X}")
        if board is None:
            continue
        send(board, f"SYSTem:VDC:FEEDback:SESSion {session}")
        send(board, "SYSTem:VDC:OUTPut:RUN:STOP")
        send(board, "SYSTem:VDC:OUTPut:RUN 1000000,400000,10000")
    print("vdc prepared, session", session)
    if src is not None:
        print("vdc session?", send(src, "SYSTem:VDC:FEEDback:SESSion?")[:16])

    # (2) T2 is up; now start the turntable source (NO1 drives its own IN1 through OUT4)
    src = boards.get("0010071E65B5CB38")
    if src is None:
        print("turntable: NO1 NOT DISCOVERED - source not started")
    if src is not None:
        for command in ("CONFigure:MODEl:TURNtable:LOAD 0,3",
                        "CONFigure:MODEl:TURNtable:MOTion 360,360",
                        "CONFigure:MODEl:TURNtable:TRIGger 1,0,360,1,100,1,1000",
                        "MODEl:TURNtable:STARt"):
            send(src, command)
        print("turntable:", send(src, "READ:MODEl:TURNtable:STATe?")[:60])

    # (3) VDC run supplies the cursor time base, then the ring is started
    for node, (uid, _s, _i, _o, _f) in enumerate(ROWS):
        board = boards.get(f"{uid:016X}")
        if board is None:
            continue
        send(board, f"SYSTem:TDMA:RING:TOPology 4,{node},0")
        send(board, "SYSTem:TDMA:RING:ARM")
        send(board, "SYSTem:TDMA:RING:START")
    print("ring start requested")
    src = boards.get("0010071E65B5CB38")
    if src is not None:
        try:
            epoch = int(time.time()) & 0xFFFFFFFF
            grant = send(src, f"CALibration:ORIGin:TRIAL {epoch},8192,256,0")
            print("origin trial:", grant[:40])
        except Exception as exc:  # noqa: BLE001
            print("origin trial failed", type(exc).__name__)
    print("vdc run requested")
    if src is not None:
        print("vdc state:", send(src, "SYSTem:VDC:OUTPut:RUN?")[:70])

    # (4) sample the T2 runtime and the position transport
    for tick in range(5):
        time.sleep(2.0)
        for uid, slot, _i, _o, _f in ROWS:
            board = boards.get(f"{uid:016X}")
            if board is None:
                continue
            state = send(board, "TRIGger:REServation:T2?").split(",")
            position = send(board, "SYSTem:REFMEM:SYNC:FLIGHT:POSition?")
            err = state[1] if len(state) > 1 else "?"
            cfg = state[2] if len(state) > 2 else "?"
            act = state[3] if len(state) > 3 else "?"
            ev = state[13] if len(state) > 13 else "?"
            print(f"t+{2 * (tick + 1)}s {uid:016X} slot {slot} err {err} cfg {cfg}"
                  f" act {act} ev {ev} | POS {position[:40]}")

    # (5) teardown: turntable, VDC run, T2
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
    return 0


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
