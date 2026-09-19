#!/usr/bin/env python3
"""Start the existing sequence once over VISA and record passive observations."""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.hardware_acceptance.sequence_trigger_acceptance import (
    Bench, open_visa_resource, require, visa_command,
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--visa-resource', required=True)
    parser.add_argument('--serial-number', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--gui-observer', action='store_true',
                        help='exercise the GUI observer against the existing independent configuration')
    args = parser.parse_args()
    args.timeout = 3
    report = {'started_at': datetime.now(timezone.utc).isoformat(),
              'transcript': [], 'samples': [], 'failure': None,
              'configuration_modified': False, 'software_next_sent': 0}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x', encoding='utf-8') as evidence:
        try:
            with open_visa_resource(args.visa_resource, args.timeout) as port:
                bench = Bench(port, args, report,
                              lambda cmd, timeout: visa_command(port, cmd, timeout))
                identity = next(csv.reader([bench.command('*IDN?')]))
                require(len(identity) == 4 and identity[2] == args.serial_number,
                        'unexpected board identity')
                report['identity'] = identity
                report['build'] = bench.command('SYST:FW:BUILD?')
                report['configuration'] = {cmd: bench.command(cmd) for cmd in (
                    'READ:SEQ:SOUR?', 'READ:SEQ:OUTPUT?', 'READ:SEQ:REP?',
                    'READ:SEQ:LINK?')}
                source = next(csv.reader([report['configuration']['READ:SEQ:SOUR?']]))
                output = next(csv.reader([report['configuration']['READ:SEQ:OUTPUT?']]))
                require(source[0] in {'IN1', 'IN2', 'IN3', 'IN4'} and
                        len(output) == 7 and output[2] == 'PULSE' and output[-1] == '1',
                        'no valid external-feedback pulse configuration; START not sent')
                if args.gui_observer:
                    from tools.sequence_trigger_debug_ui.sequence_trigger_debug_ui import (
                        MODE_INDEPENDENT, observe_loopback,
                    )
                    report['observation_result'] = observe_loopback(
                        MODE_INDEPENDENT, bench.command, lambda *args: None, duration=6)
                    print(report['observation_result'], flush=True)
                else:
                    report['before'] = bench.status()
                    require(report['before']['state'] == 'IDLE',
                            'sequence already active; no START sent')
                    bench.write('TRIG:START')
                    for _ in range(12):
                        row = bench.status()
                        report['samples'].append(row)
                        print(json.dumps(row), flush=True)
                        time.sleep(.5)
                    # Leave execution unchanged: this probe sends only START.
                    report['left_running'] = report['samples'][-1]['state'] != 'IDLE'
                report['repeat_after'] = bench.command('READ:SEQ:REP?')
                report['io_after'] = bench.command('READ:IO:STAT?')
                report['error_after'] = bench.command('SYST:ERR?')
        except (Exception, KeyboardInterrupt) as exc:
            report['failure'] = f'{type(exc).__name__}: {exc}'
        finally:
            json.dump(report, evidence, ensure_ascii=True, indent=2)
            evidence.write('\n')
    print(json.dumps({'failure': report['failure'], 'evidence': str(args.out)}))
    return int(report['failure'] is not None)


if __name__ == '__main__':
    raise SystemExit(main())
