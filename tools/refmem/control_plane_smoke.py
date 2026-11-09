"""Manual smoke: ControlPlane.read() against a real board (single port).

Run with the board on COM5 (single-port rule). Exits non-zero when a pilot key
that must be live reports stale, or when the wire does not round-trip.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.common.scpi.scpi_serial import SerialSession  # noqa: E402
from tools.refmem.control_plane import ControlPlane  # noqa: E402

PILOT = {1: 'switch1', 2: 'switch2', 3: 'trigger_state', 4: 'guide_plan',
         5: 'boot_plan', 6: 'ring_status', 7: 'cal_status',
         8: 'node_health'}
NOT_IMPLEMENTED = {}


def main(port: str = 'COM5') -> int:
    session = SerialSession(port, 115200, 3.0, 0.05, read_timeout_s=0.05)
    plane = ControlPlane(session)
    failures = 0
    for key, name in PILOT.items():
        page = plane.read(key, 0, 4)
        print(f'{key} {name}: {page}')
        if not any(row['valid'] for row in page['rows']):
            print(f'  FAIL: {name} is a live pilot key but every row is invalid')
            failures += 1
    for key, name in NOT_IMPLEMENTED.items():
        page = plane.read(key, 0, 4)
        ok = page['valid'] == 0 and page['age_ms'] == 0xFFFFFFFF
        print(f'{key} {name}: valid={page["valid"]} age={page["age_ms"]} '
              f'{"OK" if ok else "FAIL (expected stale)"}')
        if not ok:
            failures += 1
    session.close()
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1] if len(sys.argv) > 1 else 'COM5'))
