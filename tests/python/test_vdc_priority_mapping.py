"""Real typed TX + committed model + Domain on the TIMER1 coordinate.

External raw-event publications and clock reads are controlled inputs. The
direct projector admission, final revalidation, codec, and cached
mailbox provider are production code. This is not hardware acceptance.
"""
import json
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_priority_follow import domain_sources


@pytest.fixture(scope="module")
def mapping_tx_executable(tmp_path_factory):
    manager = ROOT / "components/vdc_dpll_manager/src"
    source = PRELUDE + (manager / "vdc_model_feedback.inc").read_text(encoding="utf-8")
    source += "\n" + (manager / "vdc_priority_tx.inc").read_text(encoding="utf-8")
    source += CASES
    return compile_executable(tmp_path_factory.mktemp("priority-mapping"), "priority_mapping", source,
        domain_sources() + [manager / "vdc_priority_codec.c"])


def run(executable, case):
    command = [str(executable), case]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    (executable.parent / f"{case}.json").write_text(json.dumps(dict(command=command,
        returncode=result.returncode, stdout=result.stdout, stderr=result.stderr), indent=2), encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


@pytest.mark.parametrize("case", ["direct_interval", "repeat", "model_same_event", "model_fresh",
    "final_reject_retry", "epoch_retire", "config_retire", "timer0_independent",
    "many_models", "stop", "new_generation", "lower_rollback", "upper_rollback",
    "old_event", "bridge_unavailable", "original_admission", "capture_eligible"])
def test_real_mapping_is_committed_only_with_final_encoded_offer(mapping_tx_executable, case):
    run(mapping_tx_executable, case)


PRELUDE = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define tdma_service_update_stopped_metadata mapping_test_stopped_metadata
#include "vdc_dpll_manager.h"
#include "vdc_model_projection.h"
#include "vdc_priority_tx.h"
#include "vdc_priority_codec.h"
#include "tdma_origin_plan.h"
#define BOARD_SYS_CLOCK_HZ 250000000u
#undef assert
#define assert(condition) do { if(!(condition)) { \
    fprintf(stderr,"assertion failed at %s:%d: %s\n",__FILE__,__LINE__,#condition);exit(1); \
} } while(0)
static unsigned core;
static unsigned get_core_num(void) { return core; }
static vdc_domain_context_t s_vdc_domain;
static tdma_service_service_t owner;
static tdma_service_service_t *s_vdc_tdma_service=&owner;
static tdma_origin_raw_reference_t raw;
static tdma_ring_runtime_config_t config;
static vdc_timestamp_clock_bridge_t bridge;
static bool stopped=true,clock_ok=true,bridge_ok=true,source_ok=true,epoch_ok=true;
static uint64_t raw_now;
static unsigned bridge_calls,epoch_action;
bool mapping_test_stopped_metadata(tdma_service_service_t *p,bool(*publish)(void*),void *context)
{ assert(p==&owner);return stopped && publish(context); }
bool vdc_dpll_manager_try_boundary_auto_enabled(bool *out) { *out=false;return true; }
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t *out)
{ assert(hz==BOARD_SYS_CLOCK_HZ);if(!clock_ok)return false;*out=raw_now;return true; }
bool vdc_timestamp_clock_try_read_bridge(uint32_t hz,vdc_timestamp_clock_bridge_t *out)
{ assert(hz==BOARD_SYS_CLOCK_HZ);++bridge_calls;if(!bridge_ok)return false;*out=bridge;return true; }
bool tdma_runtime_owner_get_origin_raw_reference(tdma_origin_raw_reference_t *out)
{ if(!source_ok)return false;*out=raw;return true; }
bool tdma_runtime_owner_get_origin_reference_epoch(uint32_t *out)
{
    if(!epoch_ok)return false;
    if(epoch_action==1u)++raw.epoch;
    if(epoch_action==2u)++config.owner_config_seq;
    *out=raw.epoch;return true;
}
'''


CASES = r'''
static void model_publish(void)
{
    ++s_committed_model_guard;
    model_feedback_end_core1(vdc_dpll_manager_feedback_session());
}
static vdc_priority_tx_snapshot_t snapshot(void)
{
    vdc_priority_tx_snapshot_t out;
    assert(vdc_dpll_manager_get_priority_tx(&out));return out;
}
static void setup(void)
{
    config=(tdma_ring_runtime_config_t){.enabled=1u,.node_count=6u,
        .local_slot_id=0u,.reference_slot_id=0u,.up_group_id=1u,.down_group_id=2u,
        .ring_profile_crc32=0x456u,.schedule_crc32=0x123u,.operating_profile_crc32=0x789u,
        .cycle_period_ns=1500000u,.geometry_generation=12u,.owner_config_seq=23u};
    s_vdc_domain.dco=(vdc_dco_control_t){.valid=1u,.nominal_period_ns=1500000u,
        .tdma_schedule_crc32=0x123u,.base_local_tick64=UINT64_C(3000000),
        .base_vdc_time64_ns=UINT64_C(12000000000),.period_adjust_ppb=777,.phase_offset_ns=-17};
    s_vdc_domain.control.profile.generation=3u;
    s_vdc_domain.clock.epoch_id=4u;s_vdc_domain.clock.run_id=5u;
    s_vdc_domain.schedule.local_slot_id=0u;
    assert(vdc_dpll_manager_set_feedback_session(17u));
    assert(vdc_dpll_manager_set_priority_sync(101u));
    raw_now=999000u;core=1u;model_publish();
    raw=(tdma_origin_raw_reference_t){.epoch=29u,.sequence=47u,.identity=0x2468u,
        .published_version=90u,.tick_hz=BOARD_SYS_CLOCK_HZ,.timer_lower=1000000u,.timer_upper=1000004u};
    bridge=(vdc_timestamp_clock_bridge_t){.raw_before=1000125u,.raw_after=1000129u,
        .local_ns=UINT64_C(10000000000),.tick_hz=BOARD_SYS_CLOCK_HZ};
    raw_now=bridge.raw_after;stopped=false;
}
static vdc_priority_codec_record_t offer(uint8_t *mailbox)
{
    assert(vdc_priority_tx_core1(&config,mailbox)==TDMA_PRIORITY_TX_READY);
    vdc_priority_codec_record_t decoded;
    assert(vdc_priority_codec_decode(mailbox+TDMA_PROCESS_IMAGE_PRIORITY_SYNC_BODY_OFFSET,&decoded));
    assert(decoded.event_sequence==raw.sequence && decoded.binding_generation==101u);
    return decoded;
}
static void fresh(uint64_t ticks)
{
    ++raw.sequence;raw.identity+=9u;raw.published_version+=2u;
    raw.timer_lower+=ticks;raw.timer_upper+=ticks;
    bridge.raw_before+=ticks;bridge.raw_after+=ticks;raw_now=bridge.raw_after;
    /* Unrelated TIMER0 observations must have no effect on the projection. */
    bridge.local_ns=(UINT64_C(9996000000)+bridge.raw_before*4u)/1000u*1000u;
}
static void empty(uint32_t reason,bool retired)
{
    uint8_t mailbox[32],saved[32];memset(mailbox,0xa5,sizeof(mailbox));memcpy(saved,mailbox,sizeof(saved));
    assert(vdc_priority_tx_core1(&config,mailbox)==TDMA_PRIORITY_TX_EMPTY);
    assert(!memcmp(mailbox,saved,sizeof(mailbox)));
    assert(snapshot().last_reject==reason && (snapshot().retired!=0u)==retired);
}
static void assert_actual_inside(const vdc_priority_codec_record_t *r)
{
    uint64_t lo,hi;
    assert(vdc_domain_dco_local_to_output_ns(&s_vdc_domain.dco,
        raw.timer_lower*4u,&lo));
    assert(vdc_domain_dco_local_to_output_ns(&s_vdc_domain.dco,
        raw.timer_upper*4u,&hi));
    assert(r->event_time_lower<=lo && hi<=r->event_time_lower+r->uncertainty_width);
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *name=argv[1];setup();
    uint8_t first_bytes[32],bytes[32];
    if(!strcmp(name,"capture_eligible")) {
        assert(vdc_priority_tx_origin_trace_eligible_core1(101u,17u));
        assert(!vdc_priority_tx_origin_trace_eligible_core1(100u,17u));
        assert(!vdc_priority_tx_origin_trace_eligible_core1(101u,18u));
        core=0u;assert(!vdc_priority_tx_origin_trace_eligible_core1(101u,17u));core=1u;
    }
    const vdc_priority_codec_record_t first=offer(first_bytes);
    assert_actual_inside(&first);
    if(!strcmp(name,"capture_eligible")) {
        assert(!vdc_priority_tx_origin_trace_eligible_core1(101u,17u));return 0;
    }
    const unsigned calls=bridge_calls;
    if(!strcmp(name,"repeat")) {
        bridge_ok=false;bridge.local_ns+=100000u;
        (void)offer(bytes);
        assert(!memcmp(bytes,first_bytes,32u) && bridge_calls==calls);
        assert(snapshot().encoded==1u && snapshot().repeated==1u);return 0;
    }
    if(!strcmp(name,"stop")) {
        assert(vdc_priority_tx_core1(NULL,NULL)==TDMA_PRIORITY_TX_EMPTY);
        assert(snapshot().retired);empty(VDC_PRIORITY_TX_REJECT_RETIRED,true);
        return 0;
    }
    if(!strcmp(name,"model_same_event")) {
        raw_now=raw.timer_lower;++s_vdc_domain.dco.period_adjust_ppb;model_publish();raw_now=bridge.raw_after;
        empty(VDC_PRIORITY_TX_REJECT_MODEL,false);
        assert(bridge_calls==calls);
        assert(!memcmp(snapshot().mailbox,first_bytes,32u));return 0;
    }
    if(!strcmp(name,"old_event")) {
        raw_now=raw.timer_lower+(uint64_t)BOARD_SYS_CLOCK_HZ*2u+1u;
        empty(VDC_PRIORITY_TX_REJECT_MODEL,false);assert(bridge_calls==calls);return 0;
    }
    fresh(125u);
    if(!strcmp(name,"new_generation")) {
        core=0u;stopped=true;assert(vdc_dpll_manager_set_priority_sync(102u));core=1u;stopped=false;
        assert(vdc_priority_tx_core1(&config,bytes)==TDMA_PRIORITY_TX_READY);
        assert(bridge_calls==0);
        assert(snapshot().generation==102u && snapshot().encoded==1u);return 0;
    }
    if(!strcmp(name,"model_fresh")) {
        raw_now=raw.timer_lower;++s_vdc_domain.dco.period_adjust_ppb;model_publish();raw_now=bridge.raw_after;
        (void)offer(bytes);
        assert(s_priority_tx_work.evidence.projection.model.token==2u);return 0;
    }
    if(!strcmp(name,"final_reject_retry")) {
        epoch_ok=false;empty(VDC_PRIORITY_TX_REJECT_CHANGED,false);
        assert(snapshot().encoded==1u && !memcmp(snapshot().mailbox,first_bytes,32u));
        epoch_ok=true;(void)offer(bytes);
        assert(snapshot().encoded==2u && bridge_calls==0);return 0;
    }
    if(!strcmp(name,"direct_interval")) {
        const vdc_priority_codec_record_t second=offer(bytes);assert_actual_inside(&second);
        assert(second.uncertainty_width==first.uncertainty_width && memcmp(first_bytes,bytes,32u));
        assert(snapshot().encoded==2u && bridge_calls==0);return 0;
    }
    if(!strcmp(name,"epoch_retire") || !strcmp(name,"config_retire")) {
        epoch_action=!strcmp(name,"epoch_retire")?1u:2u;
        empty(VDC_PRIORITY_TX_REJECT_BINDING,true);
    } else if(!strcmp(name,"timer0_independent")) {
        bridge.local_ns=UINT64_MAX;bridge_ok=false;
        const vdc_priority_codec_record_t result=offer(bytes);assert_actual_inside(&result);
        assert(bridge_calls==0);return 0;
    } else if(!strcmp(name,"many_models")) {
        for(unsigned i=0;i<70u;++i) {
            raw_now=raw.timer_lower;++s_vdc_domain.dco.period_adjust_ppb;model_publish();
            raw_now=bridge.raw_after;(void)offer(bytes);fresh(125u);
        }
        assert(snapshot().encoded==71u && bridge_calls==0);return 0;
    } else if(!strcmp(name,"lower_rollback")) {
        raw.timer_lower-=126u;empty(VDC_PRIORITY_TX_REJECT_SEQUENCE,true);
    } else if(!strcmp(name,"upper_rollback")) {
        raw.timer_lower-=125u;raw.timer_upper-=126u;empty(VDC_PRIORITY_TX_REJECT_SEQUENCE,true);
    } else if(!strcmp(name,"bridge_unavailable")) {
        bridge_ok=false;(void)offer(bytes);assert(bridge_calls==0);return 0;
    } else if(!strcmp(name,"original_admission")) {
        raw_now=raw.timer_upper-1u;empty(VDC_PRIORITY_TX_REJECT_MODEL,false);
    } else assert(0);
    assert(snapshot().encoded==1u && !memcmp(snapshot().mailbox,first_bytes,32u));return 0;
}
'''
