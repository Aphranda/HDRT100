"""Run the production summary recorder with real matcher/Domain/TX owners.

The bin oracle reads the ABI independently. These tests establish diagnostics
and lifecycle semantics, never physical GPIO accuracy or lock qualification.
"""
import struct
import zlib

import pytest

from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_priority_follow import domain_sources
from test_vdc_priority_trace import trace_executable, execute, STATUS_FIELDS  # noqa: F401
from test_vdc_priority_origin_trace import origin_executable  # noqa: F401

FORMAT = '<HHQQIIIIIIHHHHHHHHqqIIIiiHH'
FIELDS = ('bin_index flags observed_start_raw observed_end_raw max_service_gap_ticks '
          'max_success_gap_ticks first_event last_event first_success_offset_ticks '
          'last_success_offset_ticks service_count success_count rejected_count cancelled_count '
          'phase_held_count decision_count frequency_applied_count phase_applied_count '
          'residual_min_ns residual_max_ns max_width_ns first_model last_model min_ppb max_ppb '
          'model_changes outcome_mask').split()


def native(raw, schema=7):
    magic, version, header, size, crc = struct.unpack_from('<5I', raw)
    assert (magic, version, header, size) == (0x52545056, schema, 168, 100)
    status = dict(zip(STATUS_FIELDS, struct.unpack_from('<37I', raw, 20), strict=True))
    assert status['schema'] == schema and status['sample_interval_ms'] == 1000
    assert status['state'] == 3 and status['request_seq'] == status['ack_seq']
    assert len(raw) == header + size * status['record_count']
    assert zlib.crc32(raw[header:]) == crc
    rows = [dict(zip(FIELDS, struct.unpack_from(FORMAT, raw, i), strict=True))
            for i in range(header, len(raw), size)]
    assert status['match_count'] == sum(r['success_count'] for r in rows)
    assert status['decision_count'] == sum(r['decision_count'] for r in rows)
    assert status['skipped_count'] == 0
    return status, rows


@pytest.fixture(scope='module')
def summary_executable(trace_executable, tmp_path_factory):
    source = trace_executable.with_suffix('.c').read_text(encoding='utf-8')
    source = 'static int summary_clock_read_available=1;\n' + source.replace(
        '{ assert(hz==BOARD_SYS_CLOCK_HZ);*out=raw_now;return true; }',
        '{ assert(hz==BOARD_SYS_CLOCK_HZ);if(!summary_clock_read_available)return false;'
        '*out=raw_now;return true; }')
    source = source.replace('int main(int argc,char **argv)', 'int old_trace_main(int argc,char **argv)', 1)
    source += CASES
    return compile_executable(tmp_path_factory.mktemp('summary-trace'), 'summary', source,
        domain_sources() + [ROOT / 'components/vdc_dpll_manager/src/vdc_feedback_match.c',
                           ROOT / 'components/distributed_refmem/src/refmem_sync_vdc_feedback.c'])


@pytest.mark.parametrize('case', ['empty', 'tail', 'gap', 'success', 'reject', 'counter',
                                'session', 'capacity', 'raw_rollback', 'wide', 'uptime_wrap',
                                'held', 'cancel', 'stop_gap', 'sixty', 'ownership', 'clock_epoch',
                                'clock_boundary', 'clock_success', 'clock_initial_invalid'])
def test_production_bins(summary_executable, case):
    data = execute(summary_executable, case)
    status, rows = native(data)
    if case.startswith('clock_') or case == 'raw_rollback':
        from tools.vdc_priority_trace.vdc_priority_trace import decode
        decoded = decode(data, 1)
        assert decoded['status'] == status
        assert len(decoded['records']) == len(rows)
    if case == 'clock_initial_invalid':
        assert rows == [] and status['reason'] == 3
        return
    if case == 'empty':
        assert rows == []
        return
    for index, row in enumerate(rows):
        assert row['bin_index'] == index
        assert row['observed_end_raw'] >= row['observed_start_raw']
        if not row['success_count']:
            assert row['flags'] & 2
            assert not any(row[k] for k in ('residual_min_ns', 'residual_max_ns',
                'first_model', 'last_model', 'min_ppb', 'max_ppb', 'max_width_ns'))
    if case == 'tail':
        assert len(rows) == 4 and rows[-1]['flags'] & 256
        assert rows[-1]['max_success_gap_ticks'] == 750_000_000
    if case == 'gap':
        assert rows[0]['flags'] & 4 and rows[0]['max_service_gap_ticks'] == 750_000_000
        assert rows[0]['observed_end_raw'] - rows[0]['observed_start_raw'] == 750_000_000
        assert len(rows) == 2
    if case == 'success':
        assert sum(r['success_count'] for r in rows) == 12  # No 200 ms decimation.
        assert sum(r['phase_applied_count'] for r in rows) > 0
    if case == 'reject':
        assert sum(r['rejected_count'] for r in rows) > 0
        assert rows[-1]['phase_held_count'] == 3 and rows[-1]['cancelled_count'] == 2
    if case == 'counter':
        assert rows[-1]['flags'] & 8 and rows[-1]['flags'] & 16 and rows[-1]['flags'] & 32
        assert rows[-1]['rejected_count'] == 65535
    if case == 'session':
        assert status['reason'] == 2 and rows[-1]['flags'] & 256
    if case == 'capacity':
        assert len(rows) == 76 and status['reason'] == 4
    if case == 'raw_rollback':
        assert rows[-1]['flags'] & 64
    if case == 'wide':
        assert rows[-1]['max_width_ns'] == 2**32-1 and rows[-1]['flags'] & 32
    if case == 'uptime_wrap':
        assert status['last_ms'] < status['first_ms'] and len(rows) == 2
    if case == 'held':
        assert rows[-1]['phase_held_count'] == 1 and rows[-1]['phase_applied_count'] == 0
    if case == 'cancel':
        assert rows[-1]['cancelled_count'] > 0 and rows[-1]['outcome_mask'] & (4 | 16)
    if case == 'sixty':
        assert len(rows) == 61 and sum(r['success_count'] for r in rows) == 600
        assert all(r['success_count'] == 10 for r in rows[:-1])
        assert rows[-1]['observed_end_raw'] - rows[0]['observed_start_raw'] == 15_000_000_000
        assert all(rows[i]['observed_end_raw'] == rows[i+1]['observed_start_raw'] for i in range(60))
        assert status['reason'] == 1 and status['dropped_count'] == 0
    if case == 'clock_epoch':
        assert status['reason'] == 3 and rows[-1]['flags'] & 64
    if case in ('clock_boundary', 'clock_success'):
        assert status['reason'] == 3 and len(rows) == 1
        assert rows[0]['flags'] & 64 and rows[0]['flags'] & 256
        assert rows[0]['observed_end_raw'] == 1_050_000_000
        assert rows[0]['success_count'] == 0
    if case == 'stop_gap':
        assert len(rows) == 1 and rows[0]['flags'] & 4
        assert rows[0]['max_service_gap_ticks'] == 500_000_000
        assert rows[0]['max_success_gap_ticks'] == 500_000_000


@pytest.fixture(scope='module')
def summary_origin_executable(origin_executable, tmp_path_factory):
    source = origin_executable.with_suffix('.c').read_text(encoding='utf-8')
    source = source.replace('int main(int argc,char **argv)', 'int old_origin_main(int argc,char **argv)', 1)
    source += ORIGIN_CASES
    return compile_executable(tmp_path_factory.mktemp('summary-origin'), 'summary_origin', source,
        domain_sources() + [ROOT / 'components/vdc_dpll_manager/src/vdc_feedback_match.c',
                           ROOT / 'components/distributed_refmem/src/refmem_sync_vdc_feedback.c',
                           ROOT / 'components/vdc_dpll_manager/src/vdc_priority_codec.c'])


@pytest.mark.parametrize('case', ['origin_flow', 'origin_reject', 'origin_binding', 'origin_warm', 'origin_busy'])
def test_origin_summary_real_provider(summary_origin_executable, case):
    raw = execute(summary_origin_executable, case)
    if case == 'origin_warm':
        assert not raw
        return
    status, rows = native(raw, 8)
    assert all(not r[k] for r in rows for k in ('residual_min_ns', 'residual_max_ns',
        'phase_held_count', 'phase_applied_count', 'decision_count', 'frequency_applied_count'))
    assert sum(r['success_count'] for r in rows) >= 1
    if case == 'origin_flow':
        assert sum(r['success_count'] for r in rows) == 12
    elif case == 'origin_busy':
        assert rows[-1]['flags'] & 4 and rows[-1]['rejected_count'] == 0
        assert not rows[-1]['flags'] & 8
    elif case == 'origin_reject':
        assert rows[-1]['outcome_mask'] & 32 and rows[-1]['rejected_count'] > 0
    else:
        assert status['reason'] == 3 and rows[-1]['flags'] & 256


CASES = r'''
static void summary_arm(void)
{
    stopped_ring();assert(vdc_dpll_manager_set_priority_follow_phase(true));
    assert(vdc_dpll_manager_priority_trace_summary_arm(1u,false));trace_service();
    assert(trace_status().schema==7u && trace_status().sample_interval_ms==1000u);
}
static void advance_summary(uint64_t elapsed)
{
    raw_now=750000000u+elapsed/4u;now_ns=3000000000ull+elapsed;
    now_ms=(uint32_t)(now_ns/1000000u);
    trace_service();priority_summary_service(true);
}
int main(int argc,char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    assert(argc==2);const char *name=argv[1];setup(6000);summary_arm();
    if (!strcmp(name,"empty")) {frozen_trace();export_trace();return 0;}
    if (!strcmp(name,"clock_initial_invalid")) {
        summary_clock_read_available=0;running_ring();trace_service();
        assert(trace_status().state==VDC_PRIORITY_TRACE_FROZEN);
        summary_clock_read_available=1;frozen_trace();export_trace();return 0;
    }
    running_ring();
    if (!strcmp(name,"uptime_wrap")) {
        raw_now=((UINT64_C(1)<<32)-500u)*250000u;
        now_ms=UINT32_MAX-499u;trace_service();
        raw_now+=250000000u;now_ms=500u;trace_service();
        frozen_trace();export_trace();return 0;
    }
    advance_summary(0u);
    if (!strcmp(name,"tail")) {
        for (unsigned i=1;i<=30u;++i) advance_summary((uint64_t)i*100000000u);
    } else if (!strcmp(name,"gap")) advance_summary(3000000000ull);
    else if (!strcmp(name,"success")) {
        for (unsigned i=0;i<12u;++i) {event(100u+i,(uint64_t)i*100000000u);tick();}
    } else if (!strcmp(name,"sixty")) {
        for (unsigned i=0;i<600u;++i) {event(100u+i,(uint64_t)i*100000000u);tick();}
        advance_summary(60000000000ull);frozen_trace();
        uint8_t saved[sizeof(s_dpll_capture_records)];memcpy(saved,s_dpll_capture_records,sizeof(saved));
        ++s_model_feedback_session;++s_vdc_domain.clock.epoch_id;
        running_ring();event(900u,61000000000ull);tick();
        assert(!memcmp(saved,s_dpll_capture_records,sizeof(saved)));stopped_ring();
        export_trace();return 0;
    } else if (!strcmp(name,"ownership")) {
        const vdc_priority_trace_work_t saved_work=s_priority_trace_work;
        uint8_t saved[sizeof(s_dpll_capture_records)];memcpy(saved,s_dpll_capture_records,sizeof(saved));
        ++raw_now;core=0u;priority_summary_service(true);priority_summary_terminal();core=1u;
        s_dpll_capture_pool_owner=DPLL_CAPTURE_POOL_LEGACY;priority_summary_service(true);
        s_dpll_capture_pool_owner=DPLL_CAPTURE_POOL_TYPED;
        assert(!memcmp(saved,s_dpll_capture_records,sizeof(saved)));
        assert(!memcmp(&saved_work,&s_priority_trace_work,sizeof(saved_work)));
    } else if (!strcmp(name,"clock_epoch")) {
        ++s_vdc_domain.clock.epoch_id;raw_now+=250u;trace_service();
    } else if (!strcmp(name,"clock_boundary") || !strcmp(name,"clock_success")) {
        raw_now+=300000000u;priority_summary_service(true);
        raw_now-=25000000u;
        if (!strcmp(name,"clock_boundary")) trace_service();
        else {
            event(100u,1100000000u);vdc_priority_match_core1();
            assert(s_priority_match_work.fresh);priority_trace_match_core1();
        }
        assert(trace_status().state==VDC_PRIORITY_TRACE_FROZEN);
        uint8_t saved[sizeof(s_dpll_capture_records)];memcpy(saved,s_dpll_capture_records,sizeof(saved));
        raw_now+=1000000000u;priority_summary_service(false);priority_summary_service(true);
        assert(!memcmp(saved,s_dpll_capture_records,sizeof(saved)));
    } else if (!strcmp(name,"reject")) {
        priority_rx_available=false;event(100u,100000000u);tick();
        s_priority_phase_work.status.held+=3u;s_priority_follow_work.status.cancelled+=2u;
        priority_summary_service(true);
    } else if (!strcmp(name,"counter")) {
        s_priority_match_work.status.rejected=UINT32_MAX;priority_summary_service(true);
        s_priority_match_work.status.rejected=2u;priority_summary_service(true);
    } else if (!strcmp(name,"held")) {
        event(100u,100000000u);prepare();
        s_priority_follow_work.ticket.match.residual_lo=-10;
        s_priority_follow_work.ticket.match.residual_hi=10;
        apply();assert(s_priority_phase_work.status.held==1u);
    } else if (!strcmp(name,"cancel")) {
        event(100u,100000000u);prepare();
        priority_follow_cancel(VDC_PRIORITY_FOLLOW_BINDING,false);
        priority_summary_service(true);
    } else if (!strcmp(name,"session")) {
        ++s_model_feedback_session;trace_service();--s_model_feedback_session;
        assert(trace_status().state==VDC_PRIORITY_TRACE_FROZEN);
    } else if (!strcmp(name,"capacity")) {
        for (unsigned i=1;i<=76u;++i) advance_summary((uint64_t)i*1000000000u);
        assert(trace_status().state==VDC_PRIORITY_TRACE_FROZEN);
        uint8_t saved[sizeof(s_dpll_capture_records)];memcpy(saved,s_dpll_capture_records,sizeof(saved));
        advance_summary(80000000000ull);
        assert(!memcmp(saved,s_dpll_capture_records,sizeof(saved)));
    } else if (!strcmp(name,"raw_rollback")) {
        --raw_now;priority_summary_service(true);
    } else if (!strcmp(name,"stop_gap")) {
        raw_now+=500000000u;now_ms+=2000u;
    } else if (!strcmp(name,"wide")) {
        priority_summary_success(100u,4u,5,INT64_MIN,INT64_MAX,UINT64_MAX);
    } else assert(0);
    frozen_trace();export_trace();return 0;
}
'''

ORIGIN_CASES = r'''
int main(int argc,char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    assert(argc==2);const char *name=argv[1];origin_setup();
    if (!strcmp(name,"origin_warm")) {
        running_ring();origin_offer();stopped_ring();
        assert(vdc_dpll_manager_priority_trace_summary_arm(1u,true));trace_service();
        assert(trace_status().state==VDC_PRIORITY_TRACE_REJECTED);return 0;
    }
    assert(vdc_dpll_manager_priority_trace_summary_arm(1u,true));trace_service();
    running_ring();trace_service();origin_offer();priority_summary_service(true);
    if (!strcmp(name,"origin_flow")) {
        for (unsigned i=1;i<12u;++i) {
            origin_fresh(25000000u);trace_service();origin_offer();priority_summary_service(true);
        }
    } else if (!strcmp(name,"origin_reject")) {
        origin_fresh(125u);origin_epoch_ok=false;uint8_t mailbox[32];
        assert(vdc_priority_tx_core1(&origin_config,mailbox)==TDMA_PRIORITY_TX_EMPTY);
        priority_summary_service(true);
    } else if (!strcmp(name,"origin_busy")) {
        ++s_priority_tx_guard;priority_summary_service(true);--s_priority_tx_guard;
    } else if (!strcmp(name,"origin_binding")) {
        vdc_priority_tx_origin_evidence_t other=s_priority_tx_work.evidence;++other.source_epoch;
        priority_trace_origin_core1(&other);
    } else assert(0);
    frozen_trace();export_trace();return 0;
}
'''
