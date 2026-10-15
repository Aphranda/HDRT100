"""Read the T2 361-position status pages on all four boards (read-only)."""
import sys
from types import SimpleNamespace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.tdma_ring_monitor.tdma_start_ring import (  # noqa: E402
    board_command, discover, close_persistent_connections)

UIDS = ("0010071E65B5CB38", "FB276192BEF9CCE1", "2BD5090FE009FA2A", "A1E549202D18ED6A")
ROLES = ("COUNTER", "SWITCH_A", "GATEWAY", "SWITCH_B")
PAGES = (
    "TRIGger:RESERVation:T2:SCAN?",
    "TRIGger:RESERVation:T2?",
    "TRIGger:RESERVation:T2:ACTion?",
    "TRIGger:RESERVation:ACTion?",
)

ctx = SimpleNamespace(board_ids=list(UIDS), baud=115200, timeout=8.0,
                      read_timeout=0.02, settle=0.02, keep_open=True, short_open=False)
boards = discover(ctx)
try:
    for index, uid in enumerate(UIDS):
        board = boards.get(uid)
        print(f"=== slot {index} {ROLES[index]} {uid}")
        if board is None:
            print("   not discovered")
            continue
        for page in PAGES:
            try:
                raw = board_command(board, page, ctx).strip()
            except Exception as exc:  # noqa: BLE001
                raw = f"<{type(exc).__name__}>"
            print(f"   {page:34s} {raw[:200]}")
finally:
    close_persistent_connections()
