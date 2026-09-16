"""Execute production feedback integration over real FIFO, codec and RefMem RX.

Only hardware/owner snapshots and the independent CRC port are controlled.
This is host transport evidence, not same-event qualification or DCO closure.
"""
import re
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable


DEFAULT_MANAGER_STUBS = r'''
int vdc_dpll_manager_copy_boundary_command_offer(uint32_t config,uint32_t role,
    uint32_t source,uint32_t schedule,uint32_t sess,
    refmem_sync_vdc_boundary_command_t *command,uint32_t *offer_id)
{ (void)config;(void)role;(void)source;(void)schedule;(void)sess;(void)command;(void)offer_id;return 0; }
void vdc_dpll_manager_boundary_command_tx_done_core0(uint32_t offer_id) { (void)offer_id; }
'''


def make_feedback_harness(manager_stubs=DEFAULT_MANAGER_STUBS):
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    service = (ROOT / "components/tdma/src/tdma_service.c").read_text(encoding="utf-8")
    state = re.search(r"typedef struct \{[^}]*\} distributed_refmem_tdma_flight_sync_t;", source, re.S)
    assert state
    ordinary_binding = re.search(r"typedef struct \{[^}]*\} distributed_refmem_vdc_flight_rx_binding_t;", source, re.S)
    assert ordinary_binding
    physical = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    event_types = []
    for name in ("tdma_pio_spi_event_tap_config_t", "tdma_pio_spi_event_tap_snapshot_t", "tdma_pio_spi_event_live_snapshot_t"):
        declaration = re.search(r"typedef struct \{[^}]*\} " + name + ";", physical, re.S)
        assert declaration
        event_types.append(declaration.group(0))
    flags = re.search(r"enum \{\s*TDMA_EVENT_LIVE_RETAINED[^}]*\};", physical, re.S)
    assert flags
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "distributed_refmem.h"
#include "refmem_application_model.h"
#include "refmem_sync_vdc_feedback.h"
#include "vdc_dpll_manager.h"
#include "tdma_event_observer.h"
#include "tdma_origin_plan.h"
#include "tdma_process_image_layout.h"
#define DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED 0u
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE TDMA_FLIGHT_SHORT_SLOT_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE TDMA_FLIGHT_SHORT_PAYLOAD_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT TDMA_FLIGHT_SHORT_SLOT_COUNT
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC TDMA_FLIGHT_MAILBOX_MAGIC
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION TDMA_FLIGHT_MAILBOX_VERSION
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH 1u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN 1u
static tdma_service_service_t owner;
static tdma_ring_runtime_snapshot_t ring;
static vdc_dpll_manager_refmem_snapshot_t profile;
static tdma_pio_spi_event_live_snapshot_t live;
static tdma_pio_spi_event_tap_snapshot_t tap;
static uint32_t now_ms=100, s_service_count=123;
static unsigned full_ring_reads;
static uint32_t *race_guard;
static bool binding_available=true, live_available=true, tap_available=true;
static uint32_t session;
static bool auto_mode;
static bool reference_mode, origin_available=true;
static tdma_origin_raw_reference_t origin_reference;
static bool mode_available=true;
bool vdc_dpll_manager_try_boundary_auto_enabled(bool *out)
{ if(!mode_available)return false;*out=auto_mode;return true; }
bool vdc_dpll_manager_boundary_auto_enabled(void) { return auto_mode; }
bool vdc_dpll_manager_try_reference_publish_enabled(bool *out)
{ if(!mode_available)return false;*out=reference_mode;return true; }
static bool tdma_runtime_owner_get_origin_reference_epoch(uint32_t *out)
{ if(!origin_available)return false;*out=origin_reference.epoch;return true; }
static bool tdma_runtime_owner_get_origin_raw_reference(tdma_origin_raw_reference_t *out)
{ if(!origin_available)return false;*out=origin_reference;return true; }
uint32_t vdc_dpll_manager_feedback_session(void) { return session; }
bool vdc_dpll_manager_project_feedback_event(uint32_t ses,uint32_t role,uint32_t epoch,uint32_t run,
    uint32_t local,uint32_t schedule,uint32_t hz,uint64_t lo,uint64_t hi,
    vdc_dpll_manager_projected_event_t *out)
{
    assert(ses==session && role==9 && epoch==3 && run==4 && local==ring.local_slot_id && schedule==0xabc && hz==250000000);
    *out=(vdc_dpll_manager_projected_event_t){.output_ns_lo=lo*4,.output_ns_hi=hi*4+999,.model_token=7};
    return true;
}
bool vdc_dpll_manager_project_rate_feedback_event(uint32_t ses,uint32_t role,uint32_t epoch,uint32_t run,
    uint32_t local,uint32_t schedule,uint32_t hz,uint64_t lo,uint64_t hi,uint64_t elapsed,
    vdc_dpll_manager_rate_event_t *out)
{
    vdc_dpll_manager_projected_event_t event;
    assert(auto_mode);
    if(!vdc_dpll_manager_project_feedback_event(ses,role,epoch,run,local,schedule,hz,lo,hi,&event))return false;
    *out=(vdc_dpll_manager_rate_event_t){.absolute_output_ns_lo=event.output_ns_lo,
        .coordinate_ns=elapsed*4,.model_token=event.model_token,.applied_command_seq=0};return true;
}
static tdma_service_service_t *tdma_runtime_owner_get(void) { return &owner; }
bool tdma_ring_runtime_get_snapshot(const tdma_ring_runtime_t *r, tdma_ring_runtime_snapshot_t *out)
{ assert(r==&owner.ring_runtime); ++full_ring_reads; *out=ring; return binding_available; }
bool vdc_dpll_manager_get_refmem_snapshot(vdc_dpll_manager_refmem_snapshot_t *out)
{ *out=profile; if(race_guard) *race_guard+=2; return binding_available; }
static bool tdma_runtime_owner_get_event_live_snapshot(tdma_pio_spi_event_live_snapshot_t *out)
{ *out=live; return live_available; }
static bool tdma_runtime_owner_get_event_tap(tdma_pio_spi_event_tap_snapshot_t *out)
{ *out=tap; return tap_available; }
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out)
{
    *out=(tdma_ring_clock_snapshot_t){.enabled=ring.enabled,.adapter_started=ring.adapter_started,
        .config_seq=ring.config_seq,.applied_config_seq=ring.applied_config_seq,
        .node_count=ring.node_count,.local_slot_id=ring.local_slot_id,.schedule_crc32=ring.schedule_crc32};
    return binding_available;
}
bool vdc_dpll_manager_get_snapshot(vdc_domain_snapshot_t *out)
{ memset(out,0,sizeof(*out)); out->dco.phase_offset_ns=80; out->dco.period_adjust_ppb=-40; return true; }
static uint32_t osal_tick_ms(void) { return now_ms; }
/* Frame CRC port uses the production feedback CRC implementation with identical polynomial. */
uint32_t ota_crc32_compute(const uint8_t *p,size_t n) { return refmem_sync_vdc_feedback_crc32(p,n); }
uint32_t ota_crc32_update(uint32_t c,const uint8_t *p,size_t n)
{ for(size_t i=0;i<n;i++){ c^=p[i]; for(unsigned j=0;j<8;j++) c=(c>>1)^((c&1)?0xedb88320u:0); } return c; }
static void distributed_refmem_vdc_flight_rx_accept(uint32_t slot,const uint8_t *m,const tdma_flight_rx_view_t *v)
{ (void)slot; (void)m; (void)v; }
''' + state.group(0) + "\nstatic distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;\n"
    harness += ordinary_binding.group(0) + r'''
static volatile uint32_t s_vdc_flight_rx_guard;
static distributed_refmem_vdc_flight_rx_binding_t s_vdc_flight_rx_binding;
static bool s_vdc_flight_rx_admission_ready;
'''
    harness = harness.replace('#include "tdma_event_observer.h"', '#include "tdma_event_observer.h"\n' + "\n".join(event_types) + "\n" + flags.group(0))
    harness += "\n".join(ingress_definition(service, name) for name in (
        "tdma_service_core0_advance_flight_rx_admission_epoch", "tdma_service_acquire_flight_rx",
        "tdma_service_release_flight_rx", "tdma_service_publish_flight_tx", "tdma_service_get_flight_fifo_snapshot"))
    harness += "\n" + "\n".join(ingress_definition(source, name) for name in (
        "distributed_refmem_put_le16", "distributed_refmem_put_i16", "distributed_refmem_put_le32",
        "distributed_refmem_get_le16", "distributed_refmem_get_i16", "distributed_refmem_get_le32",
        "distributed_refmem_flight_input_offset_for_slot", "distributed_refmem_flight_publish_mask_for_slot"))
    harness += "\n" + "\n".join(ingress_definition(source, name) for name in (
        "distributed_refmem_vdc_flight_rx_read_binding", "distributed_refmem_vdc_flight_rx_same_binding",
        "distributed_refmem_vdc_flight_rx_refresh"))
    harness += '\n' + manager_stubs
    harness += '\n' + (ROOT / "components/distributed_refmem/src/distributed_refmem_vdc_feedback.inc").read_text(encoding="utf-8")
    harness += "\n" + "\n".join(ingress_definition(source, name) for name in (
        "distributed_refmem_tdma_flight_build_compact_mailbox", "distributed_refmem_tdma_flight_sync_store_mailbox",
        "distributed_refmem_tdma_flight_expand_compact_delta", "distributed_refmem_tdma_flight_parse_mailbox",
        "distributed_refmem_tdma_flight_sync_publish", "distributed_refmem_tdma_flight_sync_receive"))
    harness += r'''
static void setup(uint32_t local)
{
    assert(tdma_flight_fifo_init(&owner.flight_fifo));
    ring=(tdma_ring_runtime_snapshot_t){.enabled=1,.adapter_started=1,.data_enabled=1,
        .config_seq=7,.applied_config_seq=7,.node_count=6,.local_slot_id=local,
        .reference_slot_id=0,.schedule_crc32=0xabc};
    profile.control_profile=(vdc_dpll_control_profile_t){.valid=1,.generation=9,
        .mode=local?VDC_DPLL_CONTROL_MODE_FOLLOWER:VDC_DPLL_CONTROL_MODE_MASTER,.follow_master_slot_id=0};
    profile.schedule.local_slot_id=local; profile.schedule.schedule_crc32=ring.schedule_crc32;
    profile.clock_epoch_id=3; profile.clock_run_id=4;
    tap.applied_valid=1; tap.applied.enabled=1; tap.actual_valid=1;
    tap.applied.prefix_bits=tap.actual_prefix_bits=120;
    tap.applied.sample_delay_cycles=tap.actual_sample_delay_cycles=15;
    live=(tdma_pio_spi_event_live_snapshot_t){.flags=TDMA_EVENT_LIVE_RETAINED|TDMA_EVENT_LIVE_ACTIVE|TDMA_EVENT_LIVE_ANCHOR_VALID,
        .arm_epoch=0x100000002ull,.tick_hz=250000000,.timer1_enable_before=0x1234fffffffeull,
        .timer1_enable_after=0x123500000020ull,.record={.epoch=7,.sequence=10,.rx_elapsed_cycles=10000,.tx_elapsed_cycles=11000}};
    s_tdma_flight_sync.local_slot=local; s_tdma_flight_sync.node_count=6;
    s_tdma_flight_sync.active_mask=0x3f; s_tdma_flight_sync.next_seq32=1;
    assert(refmem_sync_delta_init(&s_tdma_flight_sync.context,local,1,1));
    distributed_refmem_vdc_flight_rx_refresh(&owner);
    distributed_refmem_feedback_refresh(&owner); assert(s_feedback_ready);
}
static distributed_refmem_vdc_feedback_tx_snapshot_t txread(void)
{ distributed_refmem_vdc_feedback_tx_snapshot_t out; assert(distributed_refmem_get_vdc_feedback_tx(&out)); return out; }
static distributed_refmem_vdc_feedback_rx_snapshot_t rxread(uint32_t source)
{ distributed_refmem_vdc_feedback_rx_snapshot_t out; assert(distributed_refmem_get_vdc_feedback_rx(source,&out)); return out; }
/* Existing codec/lifecycle cases explicitly advance one eligible publication.
 * Spacing boundary tests call the production publisher without this helper. */
static void publish(void) {
    now_ms += reference_mode ? DISTRIBUTED_REFMEM_REFERENCE_PUBLISH_INTERVAL_MS : 1u;
    distributed_refmem_tdma_flight_sync_publish(&owner,&ring);
}
static void consume(uint8_t *mailbox)
{
    tdma_flight_tx_view_t view; assert(tdma_flight_fifo_core1_acquire_tx(&owner.flight_fifo,&view));
    memcpy(mailbox,view.data+ring.local_slot_id*TDMA_FLIGHT_SHORT_SLOT_SIZE,TDMA_FLIGHT_SHORT_SLOT_SIZE);
    tdma_flight_fifo_core1_release_tx(&owner.flight_fifo);
}
static void group(uint8_t *complete)
{
    for(unsigned i=0;i<16;i++) {
        uint8_t m[32]; publish(); consume(m);
        assert(m[3]==TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS && m[8]==i && m[9]==16);
        memcpy(complete+i*4,m+10,4);
    }
}
static void advance_live(void)
{ live.record.sequence++; live.record.rx_elapsed_cycles+=100; live.record.tx_elapsed_cycles+=100; }
static refmem_sync_vdc_feedback_record_t record(uint32_t source,uint32_t measurement)
{
    return (refmem_sync_vdc_feedback_record_t){.schema_version=1,.domain_flags=7,.source_slot=source,.target_slot=0,
        .source_clock_epoch_id=3,.source_clock_run_id=4,.source_arm_epoch=2,.observer_epoch=7,
        .measurement_sequence=measurement,.tick_hz=250000000,.rx_elapsed_cycles=10000+measurement,
        .tx_elapsed_cycles=11000+measurement,.timer1_enable_before=0x12345678fffffffeull,
        .timer1_enable_after=0x1234567900000020ull};
}
static void crc(uint8_t *m)
{ distributed_refmem_put_le16(m+30,tdma_process_image_crc16_ccitt(m,30)); }
static void enqueue(const uint8_t wire[64],uint32_t source,uint32_t seq,uint32_t index,uint32_t flaw)
{
    uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE]={0}; uint8_t *m=image+source*32;
    distributed_refmem_put_le16(m,TDMA_FLIGHT_MAILBOX_MAGIC); m[2]=TDMA_FLIGHT_MAILBOX_VERSION;
    m[3]=TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS; m[4]=source; m[5]=0x3f;
    distributed_refmem_put_le16(m+6,(uint16_t)seq); m[8]=index; m[9]=16; memcpy(m+10,wire+index*4,4);
    distributed_refmem_put_le32(m+14,seq); distributed_refmem_put_le16(m+18,1); distributed_refmem_put_le32(m+20,123+seq);
    if(flaw==1) m[4]=source==1?2:1;
    if(flaw==2) m[5]=2;
    if(flaw==3) m[6]^=1;
    crc(m); if(flaw==4) m[12]^=1;
    assert(tdma_flight_fifo_core1_publish_rx(&owner.flight_fifo,image,tdma_flight_payload_size(ring.node_count),seq,seq,1u<<source,0,0));
}
static void receive(void) { now_ms++; distributed_refmem_tdma_flight_sync_receive(&owner,&ring); }
static void receive_group(const uint8_t wire[64],uint32_t source,uint32_t seq)
{ for(unsigned i=0;i<16;i++) { enqueue(wire,source,seq+i,i,0); receive(); } }
int main(int argc,char **argv)
{
    assert(argc==2); const char *test=argv[1];
    bool sender=!strncmp(test,"tx_",3); setup(sender || !strcmp(test,"rx_follower")?2:0);
    uint8_t wire[64], m[32];
    if(!strcmp(test,"tx_rate_mode_busy")) {
        session=123;auto_mode=true;distributed_refmem_feedback_refresh(&owner);
        publish();consume(m);assert(m[8]==0);memcpy(wire,m+10,4);
        const distributed_refmem_feedback_binding_t binding=s_feedback_binding;
        const uint32_t cancel=txread().cancel_count;
        const uint32_t next_sequence=s_tdma_flight_sync.next_seq32;
        mode_available=false;distributed_refmem_feedback_refresh(&owner);publish();
        assert(s_feedback_tx_active && s_feedback_tx_index==1 && !s_feedback_ready);
        assert(s_tdma_flight_sync.next_seq32==next_sequence);
        tdma_flight_tx_view_t pending;assert(!tdma_flight_fifo_core1_acquire_tx(&owner.flight_fifo,&pending));
        assert(!memcmp(&binding,&s_feedback_binding,sizeof(binding)));
        assert(s_feedback_tx_record[0]==4);
        mode_available=true;distributed_refmem_feedback_refresh(&owner);
        assert(txread().cancel_count==cancel && txread().fragment_index==1);
        for(unsigned i=1;i<16;i++){publish();consume(m);assert(m[8]==i);memcpy(wire+i*4,m+10,4);}
        refmem_sync_vdc_feedback_record_t out;assert(refmem_sync_vdc_feedback_decode(wire,6,2,0,&out));
        assert(out.schema_version==4 && txread().groups_published==1);
    } else if(!strcmp(test,"tx_rate_freeze")) {
        session=123;auto_mode=true;distributed_refmem_feedback_refresh(&owner);
        const uint64_t absolute=4*(live.timer1_enable_before+live.record.rx_elapsed_cycles);
        const uint64_t coordinate=4*live.record.rx_elapsed_cycles;
        publish();consume(m);memcpy(wire,m+10,4);advance_live();
        for(unsigned i=1;i<16;i++){publish();consume(m);assert(m[8]==i);memcpy(wire+i*4,m+10,4);}
        refmem_sync_vdc_feedback_record_t out;assert(refmem_sync_vdc_feedback_decode(wire,6,2,0,&out));
        assert(out.schema_version==4 && out.domain_flags==31 && out.rate.control_session==123);
        assert(out.rate.absolute_output_ns_lo==absolute && out.rate.coordinate_ns==coordinate);
        publish();consume(m);publish();consume(m);assert(txread().active);
        auto_mode=false;distributed_refmem_feedback_refresh(&owner);assert(!txread().active);
    } else if(!strcmp(test,"rx_rate_session")) {
        session=123;auto_mode=true;distributed_refmem_feedback_refresh(&owner);
        refmem_sync_vdc_feedback_record_t r=record(1,10);
        r.schema_version=4;r.domain_flags=31;memset(&r.rate,0,sizeof(r.rate));
        r.rate.absolute_output_ns_lo=123000;r.rate.coordinate_ns=20;r.rate.model_token=7;r.rate.control_session=123;
        assert(refmem_sync_vdc_feedback_encode(&r,6,wire));receive_group(wire,1,1);
        assert(rxread(1).active && rxread(1).sample.rate.coordinate_ns==20);
        r.rate.control_session=124;r.measurement_sequence++;
        assert(refmem_sync_vdc_feedback_encode(&r,6,wire));receive_group(wire,1,17);
        assert(rxread(1).receive_count==1 && rxread(1).reject_count==1);
        auto_mode=false;assert(!rxread(1).active);distributed_refmem_feedback_refresh(&owner);
    } else if(!strcmp(test,"tx_model_freeze")) {
        session=123;distributed_refmem_feedback_refresh(&owner);
        const uint64_t original=4*(live.timer1_enable_before+live.record.rx_elapsed_cycles);
        publish();consume(m);memcpy(wire,m+10,4);advance_live();
        for(unsigned i=1;i<16;i++){publish();consume(m);assert(m[8]==i);memcpy(wire+i*4,m+10,4);}
        refmem_sync_vdc_feedback_record_t out;assert(refmem_sync_vdc_feedback_decode(wire,6,2,0,&out));
        assert(out.schema_version==2 && out.model.control_session==123 && out.measurement_sequence==10);
        assert(out.model.output_ns_lo==original && out.model.model_token==7);
        publish();consume(m);publish();consume(m);assert(txread().active);
        session=124;distributed_refmem_feedback_refresh(&owner);assert(!txread().active && txread().cancel_count==1);
    } else if(!strcmp(test,"rx_model_session")) {
        session=123;distributed_refmem_feedback_refresh(&owner);
        refmem_sync_vdc_feedback_record_t r=record(1,10);
        r.schema_version=2;r.domain_flags=15;memset(&r.model,0,sizeof(r.model));
        r.model.output_ns_lo=123000;r.model.output_ns_hi=124000;r.model.model_token=7;r.model.control_session=123;
        assert(refmem_sync_vdc_feedback_encode(&r,6,wire));receive_group(wire,1,1);
        assert(rxread(1).active && rxread(1).receive_count==1);
        r.model.control_session=124;r.measurement_sequence++;
        assert(refmem_sync_vdc_feedback_encode(&r,6,wire));receive_group(wire,1,17);
        assert(rxread(1).receive_count==1 && rxread(1).reject_count==1);
        assert(rxread(1).sample.model.control_session==123 && rxread(1).active);
        session=124;assert(!rxread(1).active);distributed_refmem_feedback_refresh(&owner);
        receive_group(wire,1,33);assert(rxread(1).active && rxread(1).receive_count==2);
    } else if(!strcmp(test,"tx_freeze")) {
        publish(); consume(m); memcpy(wire,m+10,4); advance_live();
        for(unsigned i=1;i<16;i++) { publish(); consume(m); assert(m[8]==i); memcpy(wire+i*4,m+10,4); }
        refmem_sync_vdc_feedback_record_t out; assert(refmem_sync_vdc_feedback_decode(wire,6,2,0,&out));
        assert(out.measurement_sequence==10 && out.rx_elapsed_cycles==10000);
        assert(txread().groups_published==1 && !memcmp(txread().history[0],wire,64));
        publish(); consume(m); assert(m[3]==TDMA_PROCESS_IMAGE_MESSAGE_CLASS);
        group(wire); assert(txread().groups_published==2 && txread().history_count==2);
        assert(refmem_sync_vdc_feedback_decode(wire,6,2,0,&out) && out.measurement_sequence==11);
        publish(); consume(m); publish(); consume(m); assert(m[3]==TDMA_PROCESS_IMAGE_MESSAGE_CLASS);
    } else if(!strcmp(test,"tx_backpressure")) {
        publish(); assert(txread().fragment_index==1 && s_tdma_flight_sync.next_seq32==2);
        publish(); assert(txread().fragment_index==1 && s_tdma_flight_sync.next_seq32==2);
        consume(m); assert(m[8]==0); publish(); consume(m); assert(m[8]==1);
        /* A held Core0 RX view makes the real FIFO publication return busy. */
        uint8_t image[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE]={0}; tdma_flight_rx_view_t rx;
        assert(tdma_flight_fifo_core1_publish_rx(&owner.flight_fifo,image,tdma_flight_payload_size(ring.node_count),1,1,1,0,0));
        assert(tdma_flight_fifo_core0_acquire_rx(&owner.flight_fifo,&rx));
        uint32_t seq=s_tdma_flight_sync.next_seq32, rejects=s_tdma_flight_sync.tx_reject_count;
        publish(); assert(s_tdma_flight_sync.next_seq32==seq && txread().fragment_index==2);
        assert(s_tdma_flight_sync.tx_reject_count==rejects+1);
        assert(tdma_flight_fifo_core0_release_rx(&owner.flight_fifo,rx.slot_index));
        publish(); consume(m); assert(m[8]==2 && distributed_refmem_get_le32(m+14)==seq);
    } else if(!strcmp(test,"tx_busy")) {
        live_available=false; publish(); consume(m); assert(m[3]==TDMA_PROCESS_IMAGE_MESSAGE_CLASS);
        live_available=true; publish(); consume(m); assert(m[8]==0);
        uint32_t seq=s_tdma_flight_sync.next_seq32; live_available=false; publish();
        assert(s_tdma_flight_sync.next_seq32==seq && txread().fragment_index==1);
        live_available=true; publish(); consume(m); assert(m[8]==1);
        binding_available=false; distributed_refmem_feedback_refresh(&owner); publish();
        assert(txread().active==0 && txread().fragment_index==2);
        binding_available=true; distributed_refmem_feedback_refresh(&owner); publish(); consume(m); assert(m[8]==2);
    } else if(!strcmp(test,"tx_cancel")) {
        for(unsigned reason=0;reason<11;reason++) {
            publish(); consume(m); assert(txread().active);
            if(reason==0) ring.enabled=0;
            if(reason==1) ring.data_enabled=0;
            if(reason==2) { ring.config_seq++; ring.applied_config_seq++; }
            if(reason==3) { profile.control_profile.generation++; }
            if(reason==4) { profile.clock_run_id++; }
            if(reason==5) { live.arm_epoch++; }
            if(reason==6) { live.record.epoch++; }
            if(reason==7) tap.applied.enabled=0;
            if(reason==8) profile.control_profile.mode=VDC_DPLL_CONTROL_MODE_MASTER;
            if(reason==9) { ring.reference_slot_id=1; profile.control_profile.follow_master_slot_id=1; }
            if(reason==10) live.flags=TDMA_EVENT_LIVE_RETAINED|TDMA_EVENT_LIVE_ANCHOR_VALID;
            distributed_refmem_feedback_refresh(&owner);
            if((reason>=5 && reason<=7) || reason==10) { assert(distributed_refmem_feedback_prepare(&owner)==0); }
            assert(!txread().active && txread().cancel_count==reason+1);
            ring.enabled=ring.data_enabled=1; tap.applied.enabled=1;
            profile.control_profile.mode=VDC_DPLL_CONTROL_MODE_FOLLOWER;
            live.flags=TDMA_EVENT_LIVE_RETAINED|TDMA_EVENT_LIVE_ACTIVE|TDMA_EVENT_LIVE_ANCHOR_VALID;
            distributed_refmem_feedback_refresh(&owner);
            publish(); consume(m); assert(m[3]==TDMA_PROCESS_IMAGE_MESSAGE_CLASS);
        }
    } else if(!strcmp(test,"tx_identity_history")) {
        group(wire); publish(); consume(m); // ordinary separator
        ring.local_slot_id=3; profile.schedule.local_slot_id=3; profile.control_profile.generation++;
        distributed_refmem_feedback_refresh(&owner); publish(); consume(m); assert(txread().active);
        ring.enabled=0; distributed_refmem_feedback_refresh(&owner); assert(!txread().active);
        ring.enabled=1; distributed_refmem_feedback_refresh(&owner);
        publish(); consume(m); assert(m[3]==TDMA_PROCESS_IMAGE_MESSAGE_CLASS);
        group(wire); assert(txread().groups_published==2);
        refmem_sync_vdc_feedback_record_t out; assert(refmem_sync_vdc_feedback_decode(wire,6,3,0,&out));
    } else if(!strcmp(test,"rx_interleaved")) {
        uint8_t records[3][64];
        for(unsigned s=1;s<=3;s++){ refmem_sync_vdc_feedback_record_t r=record(s,10+s); assert(refmem_sync_vdc_feedback_encode(&r,6,records[s-1])); }
        for(unsigned i=0;i<16;i++) for(unsigned s=1;s<=3;s++) {
            enqueue(records[s-1],s,100+i,i,0); receive();
            assert(rxread(s).receive_count==(i==15));
            assert(s_tdma_flight_sync.context.mirror[s].value_u32==223+i);
            assert(s_tdma_flight_sync.last_vdc_phase_offset_ns==0 && s_tdma_flight_sync.last_vdc_rate_adjust_ppb==0);
        }
        for(unsigned s=1;s<=3;s++) {
            const distributed_refmem_vdc_feedback_rx_snapshot_t retained=rxread(s);
            refmem_sync_vdc_feedback_record_t decoded;
            assert(retained.active && !memcmp(retained.record,records[s-1],64));
            assert(refmem_sync_vdc_feedback_decode(records[s-1],6,s,0,&decoded));
            assert(!memcmp(&retained.sample,&decoded,sizeof(decoded)));
            assert(retained.sample.measurement_sequence==10+s && retained.sample.source_slot==s);
        }
    } else if(!strcmp(test,"rx_duplicate")) {
        refmem_sync_vdc_feedback_record_t r=record(1,10); assert(refmem_sync_vdc_feedback_encode(&r,6,wire));
        receive_group(wire,1,100); distributed_refmem_vdc_feedback_rx_snapshot_t before=rxread(1);
        receive_group(wire,1,200); distributed_refmem_vdc_feedback_rx_snapshot_t after=rxread(1);
        assert(after.receive_count==1 && after.duplicate_count==1 && before.last_rx_ms==after.last_rx_ms && before.last_transport_seq==after.last_transport_seq);
        assert(!memcmp(&after.sample,&before.sample,sizeof(before.sample)));
    } else if(!strcmp(test,"rx_follower")) {
        refmem_sync_vdc_feedback_record_t r=record(1,10); assert(refmem_sync_vdc_feedback_encode(&r,6,wire));
        receive_group(wire,1,100); assert(rxread(1).receive_count==0);
        assert(s_tdma_flight_sync.context.mirror[1].visible && s_tdma_flight_sync.context.mirror[1].value_u32==238);
        assert(s_tdma_flight_sync.last_vdc_phase_offset_ns==0 && s_tdma_flight_sync.last_vdc_rate_adjust_ppb==0);
        assert(distributed_refmem_tdma_flight_build_compact_mailbox(1,0x3f,200,789,TDMA_PROCESS_IMAGE_MESSAGE_CLASS,m,sizeof(m)));
        distributed_refmem_tdma_flight_parse_mailbox(m,sizeof(m));
        assert(s_tdma_flight_sync.context.mirror[1].value_u32==789);
        assert(s_tdma_flight_sync.last_vdc_phase_offset_ns==80 && s_tdma_flight_sync.last_vdc_rate_adjust_ppb==-40);
    } else if(!strcmp(test,"rx_bad")) {
        refmem_sync_vdc_feedback_record_t r=record(1,10); assert(refmem_sync_vdc_feedback_encode(&r,6,wire));
        receive_group(wire,1,100); distributed_refmem_vdc_feedback_rx_snapshot_t before=rxread(1);
        for(unsigned flaw=1;flaw<=4;flaw++) { enqueue(wire,1,200+flaw,0,flaw); receive(); }
        wire[55]^=1; receive_group(wire,1,300);
        r.source_slot=2; assert(refmem_sync_vdc_feedback_encode(&r,6,wire)); receive_group(wire,1,400);
        r.source_slot=1; r.target_slot=2; assert(refmem_sync_vdc_feedback_encode(&r,6,wire)); receive_group(wire,1,500);
        assert(rxread(1).receive_count==1 && !memcmp(rxread(1).record,before.record,64));
        const distributed_refmem_vdc_feedback_rx_snapshot_t rejected=rxread(1);
        assert(!memcmp(&rejected.sample,&before.sample,sizeof(before.sample)));
        assert(rxread(2).receive_count==0);
    } else if(!strcmp(test,"rx_lifecycle")) {
        refmem_sync_vdc_feedback_record_t r=record(1,10); assert(refmem_sync_vdc_feedback_encode(&r,6,wire));
        receive_group(wire,1,100); distributed_refmem_vdc_feedback_rx_snapshot_t before=rxread(1);
        binding_available=false; distributed_refmem_feedback_refresh(&owner); assert(!rxread(1).active);
        receive_group(wire,1,200); assert(!memcmp(rxread(1).record,before.record,64));
        binding_available=true; distributed_refmem_feedback_refresh(&owner); assert(rxread(1).active);
        enqueue(wire,1,300,0,0); ring.config_seq++; ring.applied_config_seq++;
        distributed_refmem_feedback_refresh(&owner); receive();
        assert(!rxread(1).active && rxread(1).receive_count==1);
        r.observer_epoch++; assert(refmem_sync_vdc_feedback_encode(&r,6,wire)); receive_group(wire,1,1);
        assert(rxread(1).active && rxread(1).receive_count==2);
        const distributed_refmem_vdc_feedback_rx_snapshot_t latest=rxread(1);
        ring.data_enabled=0; distributed_refmem_feedback_refresh(&owner); assert(!rxread(1).active && rxread(1).retained);
        ring.enabled=0; distributed_refmem_feedback_refresh(&owner);
        const distributed_refmem_vdc_feedback_rx_snapshot_t stopped=rxread(1);
        assert(!stopped.active && stopped.retained && !memcmp(stopped.record,latest.record,sizeof(latest.record)));
        assert(!memcmp(&stopped.sample,&latest.sample,sizeof(latest.sample)));
    } else if(!strcmp(test,"rx_expiry")) {
        refmem_sync_vdc_feedback_record_t r=record(1,10); assert(refmem_sync_vdc_feedback_encode(&r,6,wire));
        enqueue(wire,1,100,0,0); receive(); now_ms+=REFMEM_VDC_FEEDBACK_ASSEMBLY_TIMEOUT_MS;
        distributed_refmem_feedback_refresh(&owner); assert(rxread(1).timeout_count==1 && !rxread(1).retained);
        distributed_refmem_feedback_refresh(&owner); assert(rxread(1).timeout_count==1);
        receive_group(wire,1,200); assert(rxread(1).receive_count==1);
    } else if(!strcmp(test,"rx_copy_getter")) {
        refmem_sync_vdc_feedback_record_t r=record(1,10); assert(refmem_sync_vdc_feedback_encode(&r,6,wire));
        receive_group(wire,1,100);
        const unsigned before_reads=full_ring_reads;
        distributed_refmem_vdc_feedback_rx_snapshot_t out,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));out=sentinel;
        assert(distributed_refmem_copy_vdc_feedback_rx(1,&out));
        assert(out.active && out.retained && out.sample.measurement_sequence==10 && !memcmp(out.record,wire,64));
        assert(full_ring_reads==before_reads);
        binding_available=false;assert(distributed_refmem_copy_vdc_feedback_rx(1,&out));
        assert(out.active && out.retained && full_ring_reads==before_reads);
        out=sentinel;s_feedback_rx[1].guard=1;
        assert(!distributed_refmem_copy_vdc_feedback_rx(1,&out));assert(!memcmp(&out,&sentinel,sizeof(out)));
        assert(!distributed_refmem_copy_vdc_feedback_rx(REFMEM_SYNC_NODE_COUNT,&out));
        assert(!distributed_refmem_copy_vdc_feedback_rx(1,NULL));assert(full_ring_reads==before_reads);
    } else if(!strcmp(test,"rx_getter")) {
        distributed_refmem_vdc_feedback_rx_snapshot_t out; memset(&out,0xa5,sizeof(out));
        const distributed_refmem_vdc_feedback_rx_snapshot_t sentinel=out;
        assert(!distributed_refmem_get_vdc_feedback_rx(REFMEM_SYNC_NODE_COUNT,&out));
        s_feedback_rx[1].guard=1; assert(!distributed_refmem_get_vdc_feedback_rx(1,&out));
        assert(!memcmp(&out,&sentinel,sizeof(out))); assert(!distributed_refmem_get_vdc_feedback_rx(1,NULL));
        s_feedback_rx[1].guard=2; race_guard=&s_feedback_rx[1].guard;
        assert(!distributed_refmem_get_vdc_feedback_rx(1,&out)); assert(!memcmp(&out,&sentinel,sizeof(out)));
        s_feedback_rx[1].guard=UINT32_MAX-1;
        assert(!distributed_refmem_get_vdc_feedback_rx(1,&out)); assert(!memcmp(&out,&sentinel,sizeof(out)));
        race_guard=NULL;
        distributed_refmem_vdc_feedback_tx_snapshot_t tx; memset(&tx,0xa5,sizeof(tx));
        const distributed_refmem_vdc_feedback_tx_snapshot_t expected=tx;
        s_feedback_tx_guard=1; assert(!distributed_refmem_get_vdc_feedback_tx(&tx));
        assert(!memcmp(&tx,&expected,sizeof(tx))); assert(!distributed_refmem_get_vdc_feedback_tx(NULL));
        s_feedback_tx_guard=2; race_guard=&s_feedback_tx_guard;
        assert(!distributed_refmem_get_vdc_feedback_tx(&tx)); assert(!memcmp(&tx,&expected,sizeof(tx)));
    } else { assert(!"unknown scenario"); }
    printf("feedback integration %s passed\n",test); return 0;
}
'''
    return harness


FEEDBACK_SOURCES = [ROOT / path for path in (
        "components/tdma/src/tdma_flight_fifo.c", "components/distributed_refmem/src/refmem_sync_vdc_feedback.c",
        "components/distributed_refmem/src/refmem_sync.c", "components/distributed_refmem/src/refmem_sync_frame.c")]


@pytest.fixture(scope="module")
def feedback_executable(tmp_path_factory):
    return compile_executable(tmp_path_factory.mktemp("feedback-transport"), "feedback_transport",
                              make_feedback_harness(), FEEDBACK_SOURCES)


@pytest.mark.parametrize("case", [
    "tx_model_freeze", "rx_model_session", "tx_rate_freeze", "rx_rate_session", "tx_rate_mode_busy",
    "tx_freeze", "tx_backpressure", "tx_busy", "tx_cancel", "tx_identity_history",
    "rx_interleaved", "rx_duplicate", "rx_follower", "rx_bad", "rx_lifecycle", "rx_expiry", "rx_getter", "rx_copy_getter",
])
def test_feedback_transport(feedback_executable, case):
    result = subprocess.run([str(feedback_executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
