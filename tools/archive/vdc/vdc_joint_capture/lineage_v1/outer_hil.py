"""STOP-only output-timing SCPI/Flash HIL; main controller owns bench execution."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import csv
import hashlib
import json
from pathlib import Path
import re
import sys
import time

ROOT = next(p for p in Path(__file__).resolve().parents if (p / 'AGENTS.md').exists())
sys.path.insert(0, str(ROOT))
sys.dont_write_bytecode = True
from tools.scpi_common import scpi_serial
from tools.scpi_common.board_identity import parse_idn_response
from tools.tdma_ring_monitor.tdma_field_parse import RUNTIME_FIELDS

IDS = ('0010071E65B5CB38', 'FB276192BEF9CCE1',
       '2BD5090FE009FA2A', 'A1E549202D18ED6A')
TARGET = IDS[1]
BASE = 'SYSTem:VDC:OUTPut:TIMing'
DELAY = 'SYSTem:VDC:OUTPut:DELay?'
BASELINE = 'SYSTem:VDC:PRIORity:FOLLow:BASEline?'
STOP = 'SYSTem:TDMA:RING:STOP'
STATUS = 'SYSTem:TDMA:RING:STATus?'
RESET = 'SYSTem:BOOT:RESet'
ERROR = 'SYSTem:ERR?'
PI = 'SYSTem:SYNC:VDC:DPLL:COEFficient?'
ROLE = 'SYSTem:SYNC:VDC:DPLL:ROLE?'
DEFAULT = (12000, 16000, 6000)
TEST = (14000, 18000, 7000)
# Local response adapter: TIMing SET and query return three uint32s.
# Preserve signed DELay query support for the cross-profile invariant check.
scpi_serial.SCALAR_ONE_QUERY_HEADERS.add(DELAY.upper())
_original_response_matcher = scpi_serial.scpi_response_matches_command


def output_timing_response_matches_command(command, line):
    header = command.strip().split(maxsplit=1)[0].upper()
    raw = scpi_serial.trim_embedded_scpi_log(line).strip()
    text = scpi_serial.strip_scpi_ack_prefix(raw).strip()
    if header in (BASE.upper(), BASE.upper() + '?'):
        return bool(re.fullmatch(r'\d+\s*,\s*\d+\s*,\s*\d+', text))
    if header == DELAY.upper():
        return bool(re.fullmatch(r'-?\d+', text)) and -(1 << 31) <= int(text) < (1 << 31)
    return _original_response_matcher(command, line)


scpi_serial.scpi_response_matches_command = output_timing_response_matches_command


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def integers(raw, count, signed=False):
    fields = next(csv.reader([raw.strip()]))
    require(len(fields) == count, f'Expected {count} fields: {raw!r}')
    result = []
    for field in fields:
        require(re.fullmatch(r'-?\d+' if signed else r'\d+', field.strip()),
                f'Invalid integer: {raw!r}')
        value = int(field)
        require(-(1 << 31) <= value <= 0xffffffff if signed else
                0 <= value <= 0xffffffff, f'Integer out of range: {raw!r}')
        result.append(value)
    return tuple(result)


def pair(raw):
    value = integers(raw, 3)
    plan, commit, low = value
    require(1000 <= low < plan <= commit <= 1000000,
            f'Invalid output-timing readback: {raw!r}')
    return value


def error_code(raw):
    require(re.fullmatch(r'-?\d+(?:,.*)?', raw.strip()),
            f'Invalid error response: {raw!r}')
    return int(raw.split(',')[0])


def stopped(raw):
    row = dict(zip(RUNTIME_FIELDS, integers(raw, len(RUNTIME_FIELDS), True)))
    return (row['ring_enabled'] == row['ring_adapter_started'] == 0 and
            row['ring_config_seq'] == row['ring_applied_config_seq'])


def allowed_ports(ports):
    """Never open an unlisted USB UID, including NO5, even for discovery."""
    found = {}
    for port in ports:
        uid = (port.serial_number or '').upper()
        if uid not in IDS:
            continue
        require(uid not in found, f'Duplicate USB UID {uid}')
        found[uid] = port.device
    return found


class SerialBackend:
    def __init__(self, timeout_s=3.0):
        self.sessions = {}
        self.ports = {}
        self.timeout_s = timeout_s

    def open(self, uid):
        from serial.tools import list_ports
        require(uid in IDS, f'Unauthorized UID {uid}')
        ports = allowed_ports(list_ports.comports())
        require(uid in ports, f'USB UID missing: {uid}')
        self.ports[uid] = ports[uid]
        self.sessions[uid] = scpi_serial.SerialSession(
            ports[uid], 115200, self.timeout_s, .05,
            read_timeout_s=.02).open()
        return ports[uid]

    def execute(self, uid, command):
        return self.sessions[uid].execute(command)

    def close(self, uid):
        session = self.sessions.pop(uid, None)
        if session is not None:
            session.close()

    def sleep(self, seconds):
        time.sleep(seconds)


def validate_receipt(path):
    from tools.hardware_acceptance import p3_hardware_acceptance as gate
    receipt = json.loads(path.read_text(encoding='utf-8'))
    gate._validate_quick_diagnostic_receipt(ROOT, receipt)
    fingerprint = gate.working_source_fingerprint(ROOT)
    require(fingerprint == (receipt['source_tree_sha256'], receipt['source_file_count']),
            'Current source does not match P3 receipt')
    require(receipt['tdma_board_ids'] == list(IDS), 'Receipt board UID order differs')
    require(str(receipt['build_id']).isdigit(), 'Invalid receipt build ID')
    return receipt


class Acceptance:
    def __init__(self, backend, receipt, output, deadline_s=180.0):
        self.backend, self.receipt, self.output = backend, receipt, Path(output)
        self.output.mkdir(parents=True, exist_ok=False)
        self.report = dict(basis_script_sha256='8328ae3a868b114775f9104fa2852bb5b076c70ebe7595aed41fa66476fff06e', schema='DPLL_OUTPUT_TIMING_SCPI_FLASH_HIL_V1', passed=False,
                           complete=False, main_completed=False, boards={},
                           stages=[], errors=[], cleanup_errors=[], commands=[],
                           receipt_build=receipt['build_id'],
                           source_tree_sha256=receipt['source_tree_sha256'],
                           limitations=['STOP-only; ARM guard covered by host tests',
                                        'PI/role equality assumes fresh P3 baseline with no unsaved tune; baseline tuning also checked',
                                        'No GPIO phase-lock claim'])
        self.opened = set()
        self.verified = set()
        self.initial = {}
        self.persisted = None
        self.ram_dirty = False
        self.flash_dirty = False
        self.store_attempts = 0
        self.stage_name = 'setup'
        self.deadline = time.monotonic() + deadline_s
        self.cleanup = False

    def save(self):
        (self.output / 'report.json').write_text(
            json.dumps(self.report, indent=2) + '\n', encoding='utf-8')

    @contextmanager
    def stage(self, name):
        previous, self.stage_name = self.stage_name, name
        row = dict(name=name, start_ns=time.monotonic_ns(), passed=False)
        self.report['stages'].append(row)
        try:
            yield
            row['passed'] = True
        except Exception as exc:
            row['error'] = repr(exc)
            self.failure_stage = name
            raise
        finally:
            row['end_ns'] = time.monotonic_ns()
            row['duration_s'] = (row['end_ns'] - row['start_ns']) / 1e9
            self.stage_name = previous
            self.save()

    def command(self, uid, command):
        require(time.monotonic() < self.deadline, 'Acceptance deadline exhausted')
        require(uid in IDS, f'Unauthorized UID {uid}')
        row = dict(uid=uid, command=command, stage=self.stage_name,
                   start_ns=time.monotonic_ns())
        self.report['commands'].append(row)
        try:
            row['raw'] = self.backend.execute(uid, command)
            return row['raw'].strip()
        except Exception as exc:
            row['error'] = repr(exc)
            raise
        finally:
            row['end_ns'] = time.monotonic_ns()
            row['duration_s'] = (row['end_ns'] - row['start_ns']) / 1e9
            with (self.output / 'commands.jsonl').open('a', encoding='utf-8') as log:
                log.write(json.dumps(row) + '\n')
            self.save()

    def ok(self, uid, command):
        raw = self.command(uid, command)
        require(raw.strip('"') == 'OK', f'{command} rejected: {raw!r}')

    def open_board(self, uid):
        self.verified.discard(uid)
        port = self.backend.open(uid)
        self.opened.add(uid)
        self.report['boards'].setdefault(uid, {}).setdefault('ports', []).append(port)

    def stop_barrier(self):
        failures = []
        # Attempt every STOP even when one fails. No query before all ACKs.
        for uid in IDS:
            try:
                if uid not in self.opened:
                    self.open_board(uid)
                self.ok(uid, STOP)
            except Exception as exc:
                failures.append(f'{uid}: {exc!r}')
        require(not failures, f'STOP action failures: {failures}')
        for uid in IDS:
            self.wait_stopped(uid)
            self.identity(uid)

    def wait_stopped(self, uid):
        for _ in range(40):
            if stopped(self.command(uid, STATUS)):
                return
            self.backend.sleep(.05)
        raise RuntimeError(f'{uid}: STOP was not applied')

    def identity(self, uid):
        idn = self.command(uid, '*IDN?')
        require(parse_idn_response(idn).address == uid, f'IDN UID mismatch: {idn!r}')
        build = self.command(uid, 'SYSTem:FW:BUILD?').strip('"')
        require(build == str(self.receipt['build_id']), f'Build mismatch: {build!r}')
        number = integers(self.command(uid, 'SYSTem:BOARD:NO?'), 1)[0]
        require(number == IDS.index(uid) + 1, f'BOARD:NO mismatch: {number}')
        self.verified.add(uid)
        return dict(idn=idn, build=build, board_number=number)

    def snapshot(self, uid):
        return dict(identity=self.identity(uid),
                    output_timing=pair(self.command(uid, BASE + '?')),
                    output_delay=integers(self.command(uid, DELAY), 1, True),
                    baseline=integers(self.command(uid, BASELINE), 2),
                    pi=integers(self.command(uid, PI), 9, True),
                    role=integers(self.command(uid, ROLE), 5))

    def unchanged(self, uid, value):
        original = self.initial[uid]
        require(value['identity'] == original['identity'], f'{uid}: identity changed')
        require(value['pi'][:6] == original['pi'][:6], f'{uid}: PI profile changed')
        require(value['role'][:2] == original['role'][:2], f'{uid}: role changed')
        require(value['baseline'] == original['baseline'], f'{uid}: baseline changed')
        require(value['output_delay'] == original['output_delay'], f'{uid}: output delay changed')

    def clean_errors(self, uid):
        require(error_code(self.command(uid, ERROR)) == 0, f'{uid}: SCPI error queue not empty')

    def check_pair(self, expected):
        actual = pair(self.command(TARGET, BASE + '?'))
        require(actual == expected, f'Output timing {actual} != expected {expected}')

    def set_pair(self, value):
        require(set(IDS) == self.verified, 'All board identities must pass before mutation')
        self.ram_dirty = True
        raw = self.command(TARGET, BASE + ' ' + ','.join(map(str, value)))
        require(pair(raw) == value, f'SET acknowledgement differs: {raw!r}')
        self.check_pair(value)
        self.clean_errors(TARGET)

    def store(self):
        require(set(IDS) == self.verified, 'All current connections must pass identity before STORE')
        require(self.persisted is not None, 'Original saved profile unavailable')
        require(self.store_attempts < 2, 'Two-STORE budget exhausted; preserve unresolved rollback evidence')
        self.store_attempts += 1
        self.report['store_attempts'] = self.store_attempts
        self.flash_dirty = True  # ACK loss cannot make an attempted write harmless.
        self.ok(TARGET, BASE + ':STORe')
        self.clean_errors(TARGET)

    def invalid(self, text, before):
        self.clean_errors(TARGET)
        raw = self.command(TARGET, BASE + ' ' + text)
        errors = []
        for _ in range(8):
            code = error_code(self.command(TARGET, ERROR))
            if code == 0:
                break
            errors.append(code)
        else:
            raise RuntimeError('SCPI error queue did not drain')
        require(errors and all(code < 0 for code in errors),
                f'Invalid SET lacks explicit error evidence: {text}, {raw!r}, {errors}')
        require(raw == '<timeout>' or re.match(r'-\d+(?:,|$)', raw),
                f'Invalid SET unexpectedly acknowledged: {raw!r}')
        self.check_pair(before)

    def reboot(self, expected):
        # Distinct volatile sentinel proves boot reload, even if USB absence is
        # too brief to observe. Never resend RESET after loss of acknowledgement.
        sentinel = DEFAULT if expected != DEFAULT else TEST
        self.set_pair(sentinel)
        try:
            response = self.command(TARGET, RESET)
            require(response in ('OK', '"OK"', '<timeout>'),
                    f'RESET rejected: {response!r}')
        except OSError:
            pass  # Transport loss is only provisional until readback below.
        finally:
            self.backend.close(TARGET)
            self.opened.discard(TARGET)
            self.verified.discard(TARGET)
        errors = []
        for _ in range(30):
            self.backend.sleep(.5)
            try:
                self.open_board(TARGET)
                self.ok(TARGET, STOP)
                self.wait_stopped(TARGET)
                self.identity(TARGET)
                break
            except Exception as exc:
                errors.append(repr(exc))
                self.backend.close(TARGET)
                self.opened.discard(TARGET)
                self.verified.discard(TARGET)
        else:
            raise RuntimeError(f'Re-enumeration failed: {errors}')
        self.report.setdefault('reboots', []).append(dict(
            expected=expected, reconnect_errors=errors, identity=self.identity(TARGET)))
        self.check_pair(expected)
        value = self.snapshot(TARGET)
        self.unchanged(TARGET, value)
        self.report['reboots'][-1]['snapshot'] = value
        self.clean_errors(TARGET)

    def restore(self):
        if TARGET not in self.initial or not self.ram_dirty:
            return
        if self.flash_dirty:
            require(self.persisted is not None, 'Cannot restore unknown persisted pair')
            self.set_pair(self.persisted)
            self.store()
            self.reboot(self.persisted)
            self.flash_dirty = False
        self.set_pair(self.initial[TARGET]['output_timing'])
        self.ram_dirty = False

    def run(self):
        started = time.monotonic_ns()
        try:
            with self.stage('initial_stop_and_identity'):
                self.stop_barrier()
                for uid in IDS:
                    value = self.snapshot(uid)
                    self.initial[uid] = value
                    self.report['boards'][uid]['initial'] = value
                    self.clean_errors(uid)
            with self.stage('saved_profile_readback'):
                self.ram_dirty = True
                self.ok(TARGET, BASE + ':RECall')
                self.persisted = pair(self.command(TARGET, BASE + '?'))
                self.report['original_persisted'] = self.persisted
                self.clean_errors(TARGET)
                self.set_pair(DEFAULT)
            with self.stage('valid_boundaries'):
                for value in ((1001,1001,1000), DEFAULT, TEST, (1000000,1000000,999999)):
                    self.set_pair(value)
            with self.stage('invalid_inputs_leave_pair_unchanged'):
                self.set_pair(DEFAULT)
                for text in ('6000,16000,6000', '12000,11999,6000', '12000,16000,999',
                             '12000,1000001,6000', '4294967296,16000,6000',
                             '-1,16000,6000', '12000,16000', '12000,16000,6000,1', 'abc'):
                    self.invalid(text, DEFAULT)
            with self.stage('default_and_recall_do_not_store'):
                self.set_pair(TEST)
                self.ok(TARGET, BASE + ':DEFA')
                self.check_pair(DEFAULT)
                self.ok(TARGET, BASE + ':RECall')
                self.check_pair(self.persisted)
                self.clean_errors(TARGET)
            with self.stage('explicit_store_and_boot_reload'):
                self.set_pair(TEST)
                self.store()
                self.ok(TARGET, BASE + ':DEFA')
                self.check_pair(DEFAULT)
                self.ok(TARGET, BASE + ':RECall')
                self.check_pair(TEST)
                self.reboot(TEST)
            with self.stage('restore_original_saved_then_requested'):
                self.restore()
            self.report['main_completed'] = True
        except Exception as exc:
            self.report['errors'].append(dict(stage=getattr(self, 'failure_stage', self.stage_name),
                                              error=repr(exc)))
        finally:
            # Separate bounded cleanup budget survives expiration of main run.
            self.cleanup = True
            self.deadline = time.monotonic() + 90.0
            for name, action in (('final_stop', self.stop_barrier),
                                 ('rollback', self.restore),
                                 ('final_verification', self.verify_final)):
                try:
                    with self.stage(name):
                        action()
                except Exception as exc:
                    self.report['cleanup_errors'].append(dict(stage=name, error=repr(exc)))
                    # Restore needs a complete STOP barrier. Abort restoration
                    # after failed STOP, but still attempt every board's STOP.
                    if name == 'final_stop':
                        break
            for uid in tuple(self.opened):
                try:
                    self.backend.close(uid)
                except Exception as exc:
                    self.report['cleanup_errors'].append(dict(stage='close', uid=uid, error=repr(exc)))
            self.report['restore_needed'] = dict(ram=self.ram_dirty, flash=self.flash_dirty)
            self.report['complete'] = True
            self.report['passed'] = (self.report['main_completed'] and
                                     not self.report['errors'] and
                                     not self.report['cleanup_errors'] and
                                     not self.ram_dirty and not self.flash_dirty)
            self.report['duration_s'] = (time.monotonic_ns() - started) / 1e9
            self.save()
        return self.report

    def verify_final(self):
        failures = []
        for uid in IDS:
            try:
                self.wait_stopped(uid)
                value = self.snapshot(uid)
                self.report['boards'][uid]['final'] = value
                require(uid in self.initial, f'{uid}: no initial state for comparison')
                self.unchanged(uid, value)
                require(value['output_timing'] == self.initial[uid]['output_timing'],
                        f'{uid}: requested output timing not restored')
                self.clean_errors(uid)
            except Exception as exc:
                failures.append(f'{uid}: {exc!r}')
        require(not failures, f'Final verification failures: {failures}')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path,
                        default=ROOT / 'config/hardware_acceptance/p3_acceptance_receipt.json')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--deadline-s', type=float, default=180.0)
    args = parser.parse_args(argv)
    require(args.deadline_s > 0, 'Deadline must be positive')
    receipt = validate_receipt(args.receipt)
    if args.check_only:
        args.out.mkdir(parents=True, exist_ok=False)
        summary = dict(check_only=True, passed=True, hardware_opened=False,
                       build_id=receipt['build_id'],
                       source_tree_sha256=receipt['source_tree_sha256'],
                       board_ids=IDS, target=TARGET,
                       script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
        (args.out / 'preflight.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(summary))
        return 0
    runner = Acceptance(SerialBackend(), receipt, args.out, args.deadline_s)
    runner.report['receipt_path'] = str(args.receipt)
    runner.report['receipt_sha256'] = hashlib.sha256(args.receipt.read_bytes()).hexdigest()
    runner.report['script_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    report = runner.run()
    print(json.dumps({key: report[key] for key in ('passed', 'errors', 'cleanup_errors', 'duration_s')}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
