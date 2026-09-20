#!/usr/bin/env python3
"""Bounded NO1..NO4 DPLL input probe; review before running.

No OTA, first-frame selection, role change, PI tuning or live query sampling.
Default --p3 binds the admitted 20260915 p3-r2 image even if workspace code changed.
--out must be a fresh dated evidence directory. --check-only never opens serial.
"""
from __future__ import annotations
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
import sys
import time
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT))
from serial.tools.list_ports import comports
from tools.calibration_ring_validate import trn03_closed_loop as trn
from tools.calibration_ring_validate.tdma_board_record import decode_record, record_status
from tools.dpll_observation_capture import dpll_observation_capture as capture
from tools.hardware_acceptance.p3_hardware_acceptance import working_source_fingerprint, validate_ota
from probe_support import (IDS, OBSERVER_FIELDS, TRACE_FIELDS, bind_receipt, fields,
                           native_input_summary, observer_delta, sha, uints)

BACKEND = sys.modules['tdma_start_ring']
PIPE_QUERIES = dict(observer='SYSTem:SYNC:VDC:OBServer:RING?',
                   dpll='SYSTem:SYNC:VDC:DPLL:STATus?',
                   role='SYSTem:SYNC:VDC:DPLL:ROLE:STATus?',
                   rx='SYSTem:REFMEM:SYNC:TDMA:VDC:RX?')
STOP = 'SYSTem:TDMA:RING:STOP'
TRACE = 'SYSTem:SYNC:VDC:DPLL:TRACe:'


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--p3', type=Path, default=HERE.parent / 'physical-frame-identity-r1/p3-r2')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--check-only', action='store_true')
    return parser.parse_args()


class Probe:
    def __init__(self, opt):
        self.opt = opt
        self.opt.out.mkdir(parents=True, exist_ok=False)
        self.args = argparse.Namespace(baud=115200, timeout=3.0, read_timeout=0.005,
            action_timeout=0.25, settle=0.02, keep_open=True, short_open=False, arm_wait=3.0)
        self.deadline = time.monotonic() + 240.0
        self.block_queries = True  # Initial state is unknown: STOP controls come first.
        self.barrier = False
        self.boards = []
        self.record_armed = set()
        self.trace_armed = set()
        self.original_command = trn.board_command
        self.report = dict(schema='HAOFV_FOUR_BOARD_DPLL_INPUT_PROBE_V1',
            utc=datetime.now(timezone.utc).isoformat(), passed=False, flow_completed=False,
            hardware_started=False, no5_operated=False, ota_operated=False,
            queries_during_acquisition=0, actions=[], errors=[], boards={},
            plan=dict(bootstrap_s=1.0, autonomous_settle_s=1.0, trace_window_s=4.5,
                trial_s=15, rearm_ticks=8192, abort_polls=256,
                record_interval_us=300000, record_count=20,
                total_work_deadline_s=240, cleanup_stop_passes=2,
                no_first_frame_gate=True, no_fixed_good_sample_gate=True),
            dpll_locked_qualified=False, output_precision_qualified=False,
            timing_gate_waived=False,
            resource_order='DPLL fixed SRAM ARM may overlap TDMA record lease. '
                           'After all STOP ACKs: TDMA save/readback and SAVED, then DPLL SAVE/readback.')
        self.report['tool_sources'] = [dict(path=str(p), sha256=sha(p)) for p in (
            Path(__file__).resolve(), HERE / 'probe_support.py', Path(capture.__file__),
            Path(trn.__file__), Path(BACKEND.__file__),
            ROOT / 'tools/calibration_ring_validate/tdma_board_record.py',
            ROOT / 'tools/dpll_observation_decode/dpll_observation_decode.py')]

    def save(self):
        (self.opt.out / 'input-probe.json').write_text(
            json.dumps(self.report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def error(self, scope, exc):
        self.report['errors'].append(dict(scope=scope, error=f'{type(exc).__name__}: {exc}'))
        self.save()

    def command(self, board, text, unused=None):
        row = dict(uid=board.address, port=board.port, command=text,
                   start_ns=time.monotonic_ns(), utc=datetime.now(timezone.utc).isoformat())
        self.report['actions'].append(row)
        try:
            if board.address not in IDS:
                raise ValueError('board outside the four-UID allowlist')
            if '?' in text and self.block_queries:
                self.report['queries_during_acquisition'] += 1
                raise RuntimeError('query forbidden before all STOP control ACKs')
            cleanup_control = text in (STOP, TRACE + 'STOP', 'CALibration:ORIGin:REVOKe')
            if not cleanup_control and (time.monotonic() > self.deadline or
                                        len(self.report['actions']) > 2000):
                raise TimeoutError('bounded probe work budget exhausted')
            conn = BACKEND._PERSISTENT_CONNECTIONS.get(board.address)
            if conn is None or conn.session is None:
                raise RuntimeError('persistent session missing; implicit IDN/reopen forbidden')
            if 'FILE:READ?' in text.upper():
                offset = int(fields(text.split('?', 1)[1])[1])
                if offset >= 65536:
                    raise RuntimeError('bounded file download exceeded 64 KiB')
            row['raw'] = self.original_command(board, text, self.args)
            return row['raw']
        except Exception as exc:
            row['error'] = f'{type(exc).__name__}: {exc}'
            raise
        finally:
            row['end_ns'] = time.monotonic_ns()
            # Append raw responses durably; includes partial SD pages/errors.
            with (self.opt.out / 'actions.jsonl').open('a', encoding='utf-8') as stream:
                stream.write(json.dumps(row, ensure_ascii=False) + '\n')

    def control(self, board, text, expected='OK'):
        raw = self.command(board, text)
        normalized = raw.strip().strip('"')
        # A bounded action timeout may be returned as a synthetic response by
        # the shared SCPI helper.  The caller must still perform the explicit
        # state readback; retaining the marker in actions.jsonl keeps this
        # diagnostic capture auditable.
        no_payload_state_readback = normalized.startswith(
            'OK(no payload; verified by state readback)')
        if normalized != str(expected) and not no_payload_state_readback:
            raise RuntimeError(f'control ACK mismatch: {text}: {raw!r}')
        return raw

    def trace_control(self, board, action):
        values = fields(self.command(board, TRACE + action))
        if len(values) != 3 or values[0] != 'OK' or any(int(v) < 0 for v in values[1:]):
            raise RuntimeError(f'trace composite ACK mismatch: {values}')
        return list(map(int, values[1:]))

    def stop_all(self, label, passes=1):
        self.barrier = False
        pending = list(self.boards)
        for attempt in range(passes):
            failed = []
            for board in pending:
                try:
                    self.control(board, STOP)
                except Exception as exc:
                    self.error(f'{label}:stop:{board.address}:attempt{attempt+1}', exc)
                    failed.append(board)
            pending = failed
            if not pending:
                break
        if pending or len(self.boards) != len(IDS):
            raise RuntimeError('STOP ACK barrier incomplete; all readback remains prohibited')
        self.block_queries = False
        for index, board in enumerate(self.boards):
            state = trn.wait_runtime_stopped(board, self.args, index)
            self.report['boards'][board.address][label] = state
        self.barrier = True
        self.save()

    def poll_record(self, board, states, seconds=3):
        end = min(self.deadline, time.monotonic() + seconds)
        last = None
        while time.monotonic() < end:
            last = record_status(self.command(board, 'SYSTem:TDMA:RECord:STATus?'))
            if last and last['state'] in states:
                return last
            if last and last['state'] == 9:
                raise RuntimeError(f'recorder failed; SRAM retained, no re-ARM: {last}')
            time.sleep(0.05)
        raise TimeoutError(f'recorder state deadline: {last}')

    def export_tdma(self, board, label, expected_epoch=None):
        if not self.barrier:
            raise RuntimeError('TDMA export requires acknowledged STOP state')
        self.control(board, 'SYSTem:TDMA:RECord:CANCel')
        status = self.poll_record(board, {0, 5, 8})
        row = dict(initial_status=status)
        self.report['boards'][board.address][label + '_tdma'] = row
        if status['state'] == 0:
            if expected_epoch is not None:
                raise RuntimeError('requested TDMA recording absent')
            row['reason'] = 'no_prior_record'
            return
        if expected_epoch is not None and status['epoch'] != expected_epoch:
            raise RuntimeError('TDMA recording epoch differs from this run')
        epoch = status['epoch']
        raw_path = self.opt.out / f'{label}_{board.address}_tdma.bin'
        if status['state'] == 5:
            # Preserve RAM before save can fail. Existing exporter validates native CRC.
            from tools.calibration_ring_validate.tdma_board_record import export_frozen
            row['ram'] = export_frozen(board, self.args, self.command, epoch=epoch,
                                      path=raw_path.with_suffix('.ram.bin'))
            self.control(board, 'SYSTem:TDMA:RECord:SAVE')
            status = self.poll_record(board, {8}, seconds=12)
        row['saved_status'] = status  # SAVED: StorageAO finished/released its write lease.
        sd_path = f'/logs/tdma_{epoch}.bin'
        data = capture.download(board, sd_path, self.args)
        raw_path.write_bytes(data)
        decoded = decode_record(data, expected_build=self.args.expected_build,
                                expected_board=int(board.address, 16), expected_epoch=epoch)
        if 'ram' in row and Path(row['ram']['path']).read_bytes() != data:
            raise RuntimeError('TDMA SD readback differs from frozen SRAM')
        row.update(sd_path=sd_path, path=str(raw_path), sha256=sha(raw_path),
                   bytes=len(data), native=decoded, input_summary=native_input_summary(decoded))
        self.save()

    def export_trace(self, board, label):
        if not self.barrier:
            raise RuntimeError('trace export requires acknowledged STOP state')
        status = uints(self.command(board, TRACE + 'STATus?'), TRACE_FIELDS)
        row = dict(status=status)
        self.report['boards'][board.address][label + '_trace'] = row
        if status['armed']:
            raise RuntimeError('trace still armed')
        if not status['sample_count']:
            row['reason'] = 'zero_update_records_preserved; no causal or lock conclusion from trace alone'
            self.save()
            return
        job_id, sd_path, count = capture.parse_save(self.command(board, TRACE + 'SAVE'))
        # capture.query is replaced below: no extra *IDN? or port reopen.
        row.update(job_id=job_id, sd_path=sd_path, sample_count=count)
        named_board = SimpleNamespace(**vars(board), name='NO' + str(IDS.index(board.address) + 1))
        row['job'] = capture.wait_job(named_board, job_id, self.args)
        data = capture.download(board, sd_path, self.args)
        path = self.opt.out / f'{label}_{board.address}_dpll.bin'
        path.write_bytes(data)
        decoded = capture.decode(path, 'NO' + str(IDS.index(board.address) + 1))
        if count != status['sample_count']:
            raise RuntimeError('trace SAVE count changed after freeze')
        row.update(path=str(path), sha256=sha(path), bytes=len(data), decoded=decoded,
            coverage_limit='TRACE ARM can first record an already-published update_seq. '
                           'Nonzero record_count alone is not a new autonomous update. '
                           'Independently compare sequence/time/evidence with baseline/end '
                           'counters and TDMA native persona samples; bootstrap is included '
                           'in pipeline deltas. Host delay is not proof of valid autonomous input.')
        self.save()

    def pipeline(self, board, label):
        raw = {key: self.command(board, text) for key, text in PIPE_QUERIES.items()}
        value = dict(raw=raw, observer=uints(raw['observer'], OBSERVER_FIELDS),
                     role=capture.parse_role_status(raw['role']),
                     rx=capture.parse_refmem_vdc_follower_rx(raw['rx']))
        self.report['boards'][board.address][label] = value
        return value

    def configure(self):
        generation = self.receipt['calibration_generation']
        BACKEND.COMPOSITE_ACK_HEADERS.add('SYSTEM:TDMA:FLIGHT:TX')
        for index, board in enumerate(self.boards):
            row = self.report['boards'][board.address]
            row['original_schedule_raw'] = self.command(board, 'SYSTem:TDMA:SCHEDule?')
            row['original_load_mask'] = int(self.command(board, 'SYSTem:TDMA:LOAD:MASK?').strip().strip('"'), 0)
            target = row['original_load_mask'] | trn.APP_REALTIME_LOAD_VDC_MASK | trn.APP_REALTIME_LOAD_DPLL_MASK
            if target != row['original_load_mask']:
                # Explicit stopped control; preserves all unrelated load bits and budgets.
                row['load_mask_change_requested'] = True
                self.set_load_mask(board, target)
            row['enabled_schedule_raw'] = self.command(board, 'SYSTem:TDMA:SCHEDule?')
            schedule = list(map(int, fields(row['enabled_schedule_raw'])))
            if schedule[4] != target or schedule[5] & 3:
                raise RuntimeError('VDC/DPLL disabled or quarantined; retain cause without changing budget')
            row['original_diagnostic'] = int(self.command(board, 'SYSTem:TDMA:RING:DIAGnostic?').strip('"'))
            if index == 0:
                row['original_burst'] = int(self.command(board, 'SYSTem:TDMA:RING:BURSt?').strip('"'))
            # Ordinary profile; zero disables selected geometry. No generation hunt/re-ARM.
            self.control(board, 'SYSTem:TDMA:RING:GEOMetry 0', 0)
            self.control(board, 'SYSTem:TDMA:FLIGHT:FIFO:RESet')
            if trn.validate_fifo_reset(trn.flight_snapshot(board, self.args)):
                raise RuntimeError('stopped FIFO reset not acknowledged')
            self.control(board, 'SYSTem:TDMA:RING:DIAGnostic 1', 1)
            if index == 0:
                self.control(board, 'SYSTem:TDMA:RING:BURSt 0', 0)
            seed = 0x40 + index * 0x10
            values = fields(self.command(board,
                f'SYSTem:TDMA:FLIGHT:TX 32,{seed},{generation},{index+1},1'))
            expected = [32, sum((seed+i)&255 for i in range(32)), seed, generation, index+1, 1]
            if values[0] != 'OK' or list(map(int, values[1:])) != expected:
                raise RuntimeError('TX prime composite ACK mismatch')
        for board in reversed(self.boards):
            index = IDS.index(board.address)
            row = self.report['boards'][board.address]
            row['arm'] = trn.arm_with_evidence(board, self.args, index)
            row['ready_config'] = trn.wait_runtime_config_applied(board, self.args, index)
        self.report['clock_hz'] = int(fields(self.command(self.boards[0], 'SYSTem:TDMA:SCHEDule?'))[1])
        if not 0 < self.report['clock_hz'] <= 500000000:
            raise ValueError('invalid schedule clock')

    def set_load_mask(self, board, mask):
        if not self.barrier:
            raise RuntimeError('load mask control requires all-board stopped state')
        response = fields(self.command(board, f'SYSTem:TDMA:LOAD:MASK {mask}'))
        if response != ['OK', str(mask)]:
            raise RuntimeError(f'load mask composite ACK mismatch: {response}')
        actual = int(self.command(board, 'SYSTem:TDMA:LOAD:MASK?').strip().strip('"'), 0)
        if actual != mask:
            raise RuntimeError('load mask readback differs from request')

    def acquire(self):
        epoch = self.report['epoch'] = int(time.time()) & 0xffffffff
        for board in self.boards:
            self.record_armed.add(board.address)  # Cleanup also handles a lost ARM response.
            self.control(board, f'SYSTem:TDMA:RECord:ARM {epoch},300000,20')
            status = self.poll_record(board, {3})
            if status['epoch'] != epoch:
                raise RuntimeError('TDMA ARM epoch mismatch')
            self.pipeline(board, 'baseline')
        self.block_queries = True
        self.barrier = False
        for board in reversed(self.boards):
            self.control(board, 'SYSTem:TDMA:RING:START')
        time.sleep(1.0)
        hz = self.report['clock_hz']
        raw = self.command(self.boards[0], f'CALibration:ORIGin:TRIAL {epoch},8192,256,{hz*15}')
        trial = int(raw.strip().strip('"'))
        if trial <= 0 or trial & 1:
            raise RuntimeError('TRIAL grant response invalid')
        self.report['trial_epoch'] = trial
        time.sleep(1.0)
        for board in self.boards:
            self.trace_armed.add(board.address)
            self.report['boards'][board.address]['trace_arm_ack'] = self.trace_control(board, 'ARM')
            self.report['boards'][board.address]['trace_arm_action'] = dict(self.report['actions'][-1])
        time.sleep(4.5)

    def collect(self):
        for board in self.boards:
            try:
                self.trace_control(board, 'STOP')
                after = self.pipeline(board, 'end')
                self.report['boards'][board.address]['end_schedule_raw'] = self.command(board, 'SYSTem:TDMA:SCHEDule?')
                before = self.report['boards'][board.address].get('baseline')
                if before:
                    self.report['boards'][board.address]['pipeline_deltas'] = dict(
                        observer=observer_delta(before['observer'], after['observer']),
                        role=capture.role_status_delta(before['role'], after['role']),
                        rx=capture.refmem_vdc_follower_rx_delta(before['rx'], after['rx']),
                        scope='Baseline-to-post-STOP cumulative deltas; includes bootstrap and cleanup. '
                              'Post-STOP last_result is not a RUN rejection cause.')
            except Exception as exc:
                self.error(f'pipeline:{board.address}', exc)
        for key, text in [('release', 'READ:CALibration:ORIGin:RELease?'),
                          ('handoff', 'READ:CALibration:ORIGin:HANDoff?'),
                          ('origin', 'READ:CALibration:ORIGin:RUNTIme?')]:
            try:
                self.report[key + '_raw'] = self.command(self.boards[0], text)
            except Exception as exc:
                self.error(key, exc)
        for board in self.boards:
            if board.address not in self.record_armed:
                continue
            try:
                self.export_tdma(board, 'run', self.report['epoch'])
            except Exception as exc:
                self.error(f'tdma_export:{board.address}', exc)
                continue  # Storage lease may remain owned; never attempt DPLL SAVE on this board.
            try:
                if board.address in self.trace_armed:
                    self.export_trace(board, 'run')
            except Exception as exc:
                self.error(f'trace_export:{board.address}', exc)

    def run(self):
        try:
            self.receipt, self.report['binding'] = bind_receipt(
                ROOT, self.opt.p3, working_source_fingerprint, validate_ota)
            self.args.expected_build = int(self.receipt['build_id'])
            if self.opt.check_only:
                self.report.update(check_only=True, passed=True)
                return
            ports = {}
            for port in comports():
                if port.serial_number in IDS:
                    if port.serial_number in ports:
                        raise RuntimeError('duplicate USB UID')
                    ports[port.serial_number] = port.device
            if set(ports) != set(IDS):
                raise RuntimeError(f'four-board USB UID scan incomplete: {ports}')
            self.boards = [trn.Board(ports[uid], uid, '', self.receipt['build_id']) for uid in IDS]
            self.report['ports'] = ports
            trn.board_command = self.command
            sys.modules['trn03_stage'].board_command = self.command
            capture.query = self.command
            # Open known-UID ports without implicit IDN; first bus actions are STOP.
            for board in self.boards:
                self.report['boards'][board.address] = {}
                connection = BACKEND.BoardConnection(board, self.args)
                BACKEND._PERSISTENT_CONNECTIONS[board.address] = connection
                connection.session = BACKEND.SerialSession(board.port, self.args.baud,
                    self.args.timeout, self.args.settle, read_timeout_s=self.args.read_timeout)
                connection.session.open()
            self.report['hardware_started'] = True
            self.stop_all('initial_stop')
            for index, board in enumerate(self.boards):
                if board.address not in self.command(board, '*IDN?'):
                    raise RuntimeError('online UID mismatch')
                if self.command(board, 'SYSTem:FW:BUILD?').strip().strip('"') != board.build:
                    raise RuntimeError('online build mismatch')
                if int(self.command(board, 'SYSTem:BOARD:NO?')) != index + 1:
                    raise RuntimeError('online board number mismatch')
                self.trace_control(board, 'STOP')
            # Preserve every board's old TDMA and trace before any new capture ARM.
            for board in self.boards:
                self.export_tdma(board, 'prior')
                self.export_trace(board, 'prior')
            self.configure()
            self.acquire()
        except Exception as exc:
            self.error('run', exc)
        finally:
            if self.report['hardware_started']:
                try:
                    self.stop_all('final_stop', passes=2)
                    self.collect()
                    for board in self.boards:
                        try:
                            row = self.report['boards'][board.address]
                            if 'original_diagnostic' in row:
                                self.control(board, f'SYSTem:TDMA:RING:DIAGnostic {row["original_diagnostic"]}', row['original_diagnostic'])
                            if 'original_burst' in row:
                                self.control(board, f'SYSTem:TDMA:RING:BURSt {row["original_burst"]}', row['original_burst'])
                            if row.get('load_mask_change_requested'):
                                self.set_load_mask(board, row['original_load_mask'])
                                row['load_mask_restored'] = row['original_load_mask']
                        except Exception as exc:
                            self.error(f'restore:{board.address}', exc)
                except Exception as exc:
                    self.error('cleanup', exc)
                try:
                    self.control(self.boards[0], 'CALibration:ORIGin:REVOKe')
                except Exception as exc:
                    self.error('revoke', exc)
            trn.close_persistent_connections()
            if not self.opt.check_only:
                self.report['flow_completed'] = all(
                    'run_tdma' in self.report['boards'].get(uid, {}) and
                    'run_trace' in self.report['boards'].get(uid, {}) for uid in IDS)
                self.report['passed'] = self.report['flow_completed'] and not self.report['errors']
            self.report['conclusion_scope'] = (
                'Acquisition/export status only. Zero trace is preserved. Native timestamps, '
                'pipeline deltas and actual trace coverage require independent diagnosis; '
                'no lock, formal accuracy, perfect window or timing qualification is granted.')
            self.save()


def main():
    probe = Probe(parse_args())
    probe.run()
    print(json.dumps(dict(passed=probe.report['passed'], out=str(probe.opt.out),
                          hardware_started=probe.report['hardware_started'])))
    return 0 if probe.report['passed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
