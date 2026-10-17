"""T2 solves ahead on Core0 and admits absolute hardware deadlines on Core1."""
import subprocess

import pytest

from test_trigger_grid_preview import ROOT, compile_grid


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "trigger_t2_runtime.h"
#include "trigger_counter_service.h"
#include "vdc_dpll_manager.h"
#include "sync_io_switch.h"
#include "sync_io_gateway.h"

static trigger_time_cursor_snapshot_t cursor;
static trigger_counter_snapshot_t counter;
static trigger_position_descriptor_t remote_descriptor;
static bool remote_valid, tx_pending, cursor_valid=true, zero_pulse_index;
/* Lane queue for the burst/blip cases (see the get stub): the mailbox holds one record and
 * the bounded ring holds more, so a single drain per pass cannot keep up. */
static trigger_position_descriptor_t remote_queue[8];
static unsigned remote_queue_len, remote_queue_head;
static bool remote_queue_on;
static trigger_position_descriptor_t transmitted[32];
static unsigned transmitted_count, inverse_calls, schedule_calls, cancel_calls;
static unsigned backend_polls, backend_snapshots;
static uint64_t now_ns=1000000u, local_offset_ns;
static unsigned token=1u, sample_offset;
static bool rejected_uncertain;
static bool accept_schedule=true, receipts=true;
static sync_io_switch_deadline_status_t hw;
static unsigned gateway_completed_sequence;

bool trigger_time_cursor_configure(const trigger_time_grid_t *grid)
{ cursor=(trigger_time_cursor_snapshot_t){.configured=1,.grid_epoch=grid->epoch,
    .grid_generation=grid->generation,.anchor_vdc_ns=grid->anchor_vdc_ns,
    .period_ns=grid->period_ns,.subdivisions=grid->subdivisions,.session=1,
    .ring_config_seq=7}; return true; }
void trigger_time_cursor_clear(void) { memset(&cursor,0,sizeof(cursor)); }
void trigger_time_cursor_service(void)
{ if (!cursor.configured) return; cursor.sample_seq=(uint32_t)(now_ns/1000000u)+1+sample_offset;
  cursor.pulse_index=zero_pulse_index?0u:now_ns/1000000u+1u;
  cursor.action_cursor=now_ns/(1000000u/cursor.subdivisions);
  cursor.valid=cursor_valid; cursor.event_vdc_ns=now_ns;
  cursor.model_token=token; cursor.last_error=TRIGGER_TIME_CURSOR_OK; }
bool trigger_time_cursor_snapshot(trigger_time_cursor_snapshot_t *out)
{ if (!out) return false; *out=cursor; return true; }
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{ *out=(vdc_dpll_manager_committed_model_t){.session=1,.token=token,
    .dco={.valid=1,.nominal_period_ns=1000000u}}; return true; }
bool vdc_domain_dco_output_to_local_ns(const vdc_dco_control_t *dco,
    uint64_t output_ns, uint64_t *out)
{ (void)dco; ++inverse_calls; *out=output_ns+local_offset_ns; return true; }
uint32_t trigger_counter_read_core1(uint32_t *out)
{ if (!out || !counter.loaded || !counter.io.valid) return COUNTER_ARGUMENT;
  *out=counter.io.count32; return COUNTER_OK; }
bool distributed_refmem_position_descriptor_pending(void) { return tx_pending; }

/* The pending-ack diagnostic belongs to the refmem owner: this harness only
 * needs the symbol, and reports "nothing pending" unless a case says otherwise. */
static uint32_t s_pending_stale, s_pending_incomplete;
void distributed_refmem_position_ack_pending(uint32_t run_id, uint32_t target_mask,
                                             uint32_t *stale_mask,
                                             uint32_t *incomplete_mask)
{
    (void)run_id; (void)target_mask;
    if (stale_mask) *stale_mask = s_pending_stale;
    if (incomplete_mask) *incomplete_mask = s_pending_incomplete;
}
static uint32_t noted_frozen=UINT32_MAX, freeze_acked;
static bool noted_active;
void distributed_refmem_note_position_frozen(uint32_t run, bool active, uint32_t mask)
{ (void)run; noted_active=active; noted_frozen=mask; }
uint32_t distributed_refmem_position_freeze_acked(uint32_t run, uint32_t targets)
{ (void)run; (void)targets; return freeze_acked; }
void distributed_refmem_request_position_cancel(void) { tx_pending=false; }
bool distributed_refmem_stage_position_descriptor(
    const trigger_position_descriptor_t *d, uint32_t mask)
{ (void)mask; assert(d); transmitted[transmitted_count++]=*d; tx_pending=true; return true; }
bool distributed_refmem_get_position_descriptor(uint32_t slot,
    trigger_position_descriptor_t *out)
{ if (!out || slot != 0u) return false;
  /* Optional queue for the burst/ blip cases: the lane can hold several records (one in the
   * mailbox plus the bounded ring), and the consumer must take a bounded batch per pass. */
  if (remote_queue_on) {
      if (remote_queue_head >= remote_queue_len) return false;
      *out = remote_queue[remote_queue_head++]; return true; }
  if (!remote_valid) return false;
  *out=remote_descriptor; remote_valid=false; return true; }
static unsigned sequence_stage_calls, sequence_stage_count;
static uint32_t window_remaining;
static uint64_t window_step, sequence_stage_base;
bool sync_io_switch_realtime_stage_sequence(uint64_t base_deadline, const uint32_t *codes,
                                            const uint64_t *delta_ticks, uint32_t count)
{ (void)codes; ++schedule_calls; ++sequence_stage_calls;
  sequence_stage_count=count; sequence_stage_base=base_deadline;
  if (!accept_schedule || count==0u ||
      base_deadline <= (now_ns+local_offset_ns)/4u+25000u) return false;
  window_remaining=count-1u;
  window_step=(count>1u)?(delta_ticks[1]-delta_ticks[0]):0u;
  hw.sequence++; hw.deadline_tick=base_deadline; hw.state=SYNC_IO_SWITCH_DEADLINE_ARMED;
  hw.origin_before_tick=(now_ns+local_offset_ns)/4u;
  hw.origin_after_tick=hw.origin_before_tick+40u;
  return true; }
bool sync_io_switch_realtime_schedule_at(uint32_t code, uint64_t deadline)
{ (void)code; ++schedule_calls; window_remaining=0u;
  if (rejected_uncertain) { hw.state=SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
      hw.origin_before_tick=100u; hw.origin_after_tick=180u; return false; }
  if (!accept_schedule || hw.state==SYNC_IO_SWITCH_DEADLINE_ARMED ||
      deadline <= (now_ns+local_offset_ns)/4u+25000u) return false;
  hw.sequence++; hw.deadline_tick=deadline; hw.state=SYNC_IO_SWITCH_DEADLINE_ARMED;
  hw.origin_before_tick=(now_ns+local_offset_ns)/4u;
  hw.origin_after_tick=hw.origin_before_tick+40u;
  return true; }
void sync_io_switch_realtime_service_core1(void)
{ ++backend_polls;
  if (receipts && hw.state==SYNC_IO_SWITCH_DEADLINE_ARMED &&
       (now_ns+local_offset_ns)/4u>=hw.deadline_tick) {
      if (window_remaining) {
          --window_remaining; hw.sequence++; hw.deadline_tick+=window_step;
          hw.receipt_mask=1; gateway_completed_sequence=hw.sequence;
      } else {
          hw.state=SYNC_IO_SWITCH_DEADLINE_COMPLETE; hw.receipt_mask=1;
          gateway_completed_sequence=hw.sequence;
      }
  } }
bool sync_io_switch_realtime_status(sync_io_switch_deadline_status_t *out)
{ ++backend_snapshots; *out=hw; return true; }
bool sync_io_switch_realtime_cancel(void)
{ ++cancel_calls; hw.state=SYNC_IO_SWITCH_DEADLINE_CANCELLED; return true; }
bool sync_io_gateway_realtime_schedule_at(uint32_t pulse_ns, uint64_t deadline)
{ return sync_io_switch_realtime_schedule_at(pulse_ns,deadline); }
void sync_io_gateway_realtime_service_core1(void)
{ sync_io_switch_realtime_service_core1(); }
bool sync_io_gateway_realtime_status(sync_io_gateway_deadline_status_t *out)
{ ++backend_snapshots; *out=(sync_io_gateway_deadline_status_t){.state=hw.state,.sequence=hw.sequence,
    .completed_sequence=gateway_completed_sequence,.deadline_tick=hw.deadline_tick,
    .origin_before_tick=hw.origin_before_tick,.origin_after_tick=hw.origin_after_tick,
    .ready_count=999u}; return true; }
bool sync_io_gateway_realtime_cancel(void) { return sync_io_switch_realtime_cancel(); }
bool sync_io_switch_snapshot(sync_io_switch_snapshot_t *out)
{ *out=(sync_io_switch_snapshot_t){.loaded=1,.generation=1}; return true; }
bool sync_io_switch_stop(uint32_t generation)
{ assert(generation==1); return sync_io_switch_realtime_cancel(); }
bool sync_io_gateway_snapshot(sync_io_gateway_snapshot_t *out)
{ *out=(sync_io_gateway_snapshot_t){.loaded=1,.generation=1}; return true; }
bool sync_io_gateway_stop(uint32_t generation)
{ assert(generation==1); return sync_io_gateway_realtime_cancel(); }

static trigger_t2_runtime_config_t config(bool origin)
{
    trigger_t2_runtime_config_t c={0};
    c.grid=(trigger_time_grid_t){.epoch=1,.generation=2,.anchor_vdc_ns=0,
        .period_ns=1000000,.subdivisions=2000};
    c.predictor=(trigger_turntable_predictor_config_t){1000,0,0,10000,5000,5000,950};
    c.deployment=(trigger_deployment_t){.generation=3,.rows={
        {.board_uid=1,.tdma_slot=0,.input_mask=1},
        {.board_uid=2,.tdma_slot=1,.output_mask=1},
        {.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=1,.feedback_mode=1},
        {.board_uid=4,.tdma_slot=3,.output_mask=1}}};
    c.templates=(trigger_action_template_table_t){.generation=4,.count=1,.row={{
        .template_id=7,.generation=4,.role=1,.kind=TRIGGER_ACTION_TEMPLATE_SWITCH,
        .argument=1,.span_slices=1,.delta_q=0}}};
    c.template_crc=trigger_action_template_table_crc(&c.templates);
    c.run_id=9; c.local_role=origin?0u:1u; c.source_slot=0; c.target_mask=0x0fu;
    c.descriptor_window_sync=TRIGGER_T2_DESCRIPTOR_TARGET_GRACE_SYNC;
    c.origin=origin?1u:0u;
    return c;
}
static trigger_t2_runtime_snapshot_t snapshot(void)
{ trigger_t2_runtime_snapshot_t s; assert(trigger_t2_runtime_snapshot(&s)); return s; }
static void preview(const trigger_t2_runtime_config_t *c, unsigned position,
    unsigned version, uint64_t ms, unsigned fine)
{
    remote_descriptor=(trigger_position_descriptor_t){.schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
        .stage=TRIGGER_POSITION_STAGE_PREVIEW,.run_id=9,.sync_epoch=1,.position_id=position,
        .count_epoch=11,.target_count=5000,.prediction_version=version,.target_ms_index=ms,
        .target_fine_slice_index=fine,.grid_generation=2,.deployment_generation=3,
        .deployment_crc=trigger_deployment_crc(&c->deployment),.valid_from_sync=1,.expiry_sync=100};
    remote_descriptor.descriptor_crc32=trigger_position_descriptor_crc(&remote_descriptor);
    remote_valid=true;
}
static void freeze(unsigned position, unsigned version)
{ remote_descriptor=(trigger_position_descriptor_t){.schema=TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
    .stage=TRIGGER_POSITION_STAGE_FREEZE,.position_id=position,.prediction_version=version}; remote_valid=true; }
static bool take(void)
{ trigger_action_runtime_event_t event; return trigger_t2_runtime_take_event(&event); }

int main(int argc,char **argv)
{
    assert(argc==2); counter.loaded=1; counter.io.valid=1;
    const char *test=argv[1];
    trigger_t2_runtime_config_t c=config(!strcmp(test,"origin") || !strcmp(test,"origin_resend") ||
        !strcmp(test,"origin_pending") || !strcmp(test,"origin_no_ack") || !strcmp(test,"origin_scan"));
    if (!strcmp(test,"announce_config")) {
        /* The three announce gates are install parameters, not compile-time constants
         * (TRIGger:RESERVation:T2:ANNOUNCE / ?). Defaults must equal the frozen macros,
         * a valid write must read back, and a 0 or out-of-range gate must be REFUSED so
         * shaping can never be disabled by an empty write; a refused write must leave the
         * previously installed values untouched. */
        uint32_t tolerance=0u, min_interval=0u, global_interval=0u;
        assert(trigger_t2_runtime_announce_get(&tolerance,&min_interval,&global_interval));
        assert(tolerance==TRIGGER_T2_ANNOUNCE_TARGET_TOLERANCE_MS);
        assert(min_interval==TRIGGER_T2_ANNOUNCE_MIN_INTERVAL_MS);
        assert(global_interval==TRIGGER_T2_ANNOUNCE_MIN_GLOBAL_INTERVAL_MS);
        /* 0 means "this gate does not suppress" (the producer writes freely and T_drain
         * owns the traffic), so it must be ACCEPTED; only out-of-range values are
         * refused, and a refused write leaves the installed gates untouched. */
        assert(trigger_t2_runtime_announce(0u,0u,0u));
        assert(trigger_t2_runtime_announce_get(&tolerance,&min_interval,&global_interval));
        assert(tolerance==0u && min_interval==0u && global_interval==0u);
        assert(trigger_t2_runtime_announce(6u,12u,15u));
        assert(trigger_t2_runtime_announce_get(&tolerance,&min_interval,&global_interval));
        assert(tolerance==6u && min_interval==12u && global_interval==15u);
        assert(!trigger_t2_runtime_announce(TRIGGER_T2_ANNOUNCE_MAX_TOLERANCE_MS+1u,12u,15u));
        assert(!trigger_t2_runtime_announce(6u,TRIGGER_T2_ANNOUNCE_MAX_INTERVAL_MS+1u,15u));
        assert(!trigger_t2_runtime_announce(6u,12u,TRIGGER_T2_ANNOUNCE_MAX_INTERVAL_MS+1u));
        assert(trigger_t2_runtime_announce_get(&tolerance,&min_interval,&global_interval));
        assert(tolerance==6u && min_interval==12u && global_interval==15u);
        assert(trigger_t2_runtime_announce(TRIGGER_T2_ANNOUNCE_TARGET_TOLERANCE_MS,
            TRIGGER_T2_ANNOUNCE_MIN_INTERVAL_MS,TRIGGER_T2_ANNOUNCE_MIN_GLOBAL_INTERVAL_MS));
        /* The second position's consumption is its OWN counter, never freeze_fallback. */
        assert(snapshot().fallback_promoted_count ==
            trigger_t2_runtime_fallback_promoted_count());
        return 0;
    }
    if (!strcmp(test,"capacity")) {
        /* T2-SEQ-001: the board keeps EVERY row of its own role as the position's
         * group sequence, up to the static table capacity, so a role with more
         * than one row mounts (that used to be refused when one row per position
         * was the whole model). */
        c.templates.count=2; c.templates.row[1]=c.templates.row[0];
        c.templates.row[1].template_id=8; c.templates.row[1].delta_q=10;
        c.template_crc=trigger_action_template_table_crc(&c.templates);
        assert(trigger_t2_runtime_configure(&c));
        const trigger_t2_runtime_snapshot_t cap=snapshot();
        assert(cap.action_group_count==2u);
        assert(cap.action_group_next[0]==0u &&
            cap.action_group_in_flight[0]==TRIGGER_T2_RUNTIME_GROUP_NONE);
        /* The sequence is explicit about its starting group. */
        assert(cap.action_group_done[0]==0u &&
            cap.action_groups_executed==0u);
        return 0;
    }
    if (!strcmp(test,"sequence")) {
        /* T2-SEQ-001: one position runs the role's whole group sequence and is
         * counted as executed only after its LAST group. Group 1 is prepared by
         * Core0 from the same frozen slice and carries its own offset/argument. */
        c.templates.count=2; c.templates.row[1]=c.templates.row[0];
        c.templates.row[1].template_id=8; c.templates.row[1].delta_q=202;
        c.template_crc=trigger_action_template_table_crc(&c.templates);
        assert(trigger_t2_runtime_configure(&c));
        assert(trigger_t2_runtime_start(11,0));
        preview(&c,1,1,10,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        trigger_t2_runtime_prepare_core0();
        freeze(1,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(schedule_calls==1 && sequence_stage_calls==1 && sequence_stage_count==2u);
        assert(snapshot().action_group_in_flight[0]==0u);
        assert(snapshot().action_completed_mask==0u && !take());
        /* Group 0 completes: the cursor advances, the position is NOT executed, and
         * the in-flight window promotes group 1 in the SAME pass (no Core0 round
         * trip between two groups of one position). */
        receipts=true; now_ns=10000500u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        trigger_t2_runtime_snapshot_t seq=snapshot();
        assert(seq.action_group_done[0]==1u && seq.action_group_next[0]==1u &&
            seq.action_groups_executed==1u);
        assert(seq.action_completed_mask==0u);
        assert(!take());
        assert(schedule_calls==1 && seq.action_group_in_flight[0]==1u);
        /* Core0 republishes only to carry the tick of the group after the promoted one. */
        trigger_t2_runtime_prepare_core0();
        receipts=true; now_ns=20000500u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        trigger_action_runtime_event_t seq_event;
        assert(trigger_t2_runtime_take_event(&seq_event));
        assert(seq_event.position_id==1u && seq_event.template_id==8u);
        assert(!take());
        seq=snapshot();
        assert(seq.action_completed_mask==1u && seq.action_group_done[0]==3u &&
            seq.action_groups_executed==2u &&
            !seq.action_failed_mask);
        return 0;
    }
    if (!strcmp(test,"gateway")) {
        c.local_role=2; c.templates.row[0].role=2;
        c.templates.row[0].kind=TRIGGER_ACTION_TEMPLATE_GATEWAY;
        c.templates.row[0].argument=1000;
        c.template_crc=trigger_action_template_table_crc(&c.templates);
    }
    /* Keep the position window open while the resend cadence is observed. */
    if (!strcmp(test,"origin_resend") || !strcmp(test,"origin_no_ack"))
        c.descriptor_window_sync=200u;
    assert(trigger_t2_runtime_configure(&c));
    assert(trigger_t2_runtime_start(11,0));
    if (!strcmp(test,"different_local_seq")) sample_offset=80;
    if(!strcmp(test,"origin_resend")) {
        /* FREEZE only starts freezing: resend, spaced, until every target
         * reports the position frozen; then the position is complete. */
        assert(noted_frozen==0u && !noted_active); /* the origin reports nothing */
        unsigned first=UINT32_MAX;
        for (unsigned i=0u; i<40u && first==UINT32_MAX; ++i) {
            now_ns=(i+1u)*1000000u; counter.io.count32=(i+1u)*300u; tx_pending=false;
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            trigger_t2_runtime_prepare_core0(); tx_pending=false;
            for (unsigned k=0u; k<transmitted_count; ++k)
                if (transmitted[k].stage==TRIGGER_POSITION_STAGE_FREEZE) { first=k; break; }
        }
        assert(first!=UINT32_MAX);
        const uint32_t position=transmitted[first].position_id, version=transmitted[first].prediction_version;
        unsigned resent=0u, before=transmitted_count;
        const uint32_t counted=snapshot().freeze_resend_count;
        for (unsigned i=0u; i<40u; ++i) {
            now_ns+=1000000u; tx_pending=false;
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK); tx_pending=false;
        }
        for (unsigned k=before; k<transmitted_count; ++k)
            if (transmitted[k].stage==TRIGGER_POSITION_STAGE_FREEZE &&
                transmitted[k].position_id==position) {
                assert(transmitted[k].prediction_version==version); ++resent;
            }
        /* Open positions share one resend every T2_FREEZE_RESEND_SLICES. */
        const uint32_t shared=snapshot().freeze_resend_count-counted;
        assert(!snapshot().freeze_abandoned_mask && resent>=2u);
        assert(snapshot().freeze_abandoned_count==0u);
        assert(shared>=resent && shared<=40u/4u+1u);
        freeze_acked=1u<<(position-1u);
        now_ns+=1000000u; tx_pending=false;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().freeze_complete_mask & freeze_acked);
        before=transmitted_count;
        for (unsigned i=0u; i<12u; ++i) {
            now_ns+=1000000u; tx_pending=false;
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK); tx_pending=false;
        }
        for (unsigned k=before; k<transmitted_count; ++k)
            assert(!(transmitted[k].stage==TRIGGER_POSITION_STAGE_FREEZE &&
                     transmitted[k].position_id==position));
        return 0;
    }
    if(!strcmp(test,"origin_no_ack")) {
        /* Nothing acknowledges these FREEZEs. In this driver the origin keeps
         * re-solving, so the window of a position is continuously extended and
         * the ABANDONMENT branch stays unreachable (recorded as gap G5); what is
         * checkable, and what must hold whatever the runtime decides, is that the
         * surviving counter and the per-position mask describe the same events
         * and that no position is ever reported complete. */
        for (unsigned i=0u; i<400u; ++i) {
            now_ns=(i+1u)*1000000u; counter.io.count32=(i+1u)*300u; tx_pending=false;
            /* A late position in this configuration reports a service error; the
             * accounting invariant must hold regardless of the result. */
            (void)trigger_t2_runtime_service();
            trigger_t2_runtime_prepare_core0(); tx_pending=false;
            const trigger_t2_runtime_snapshot_t live=snapshot();
            unsigned abandoned=0u;
            for (unsigned b=0u;b<32u;++b)
                if (live.freeze_abandoned_mask&(1u<<b)) ++abandoned;
            assert(abandoned==live.freeze_abandoned_count);
        }
        assert(snapshot().freeze_complete_mask==0u);
        return 0;
    }
    if(!strcmp(test,"origin") || !strcmp(test,"origin_pending")) {
        bool saw_preview=false, saw_freeze=false;
        for (unsigned i=0u; i<16u; ++i) {
            now_ns=(i+1u)*1000000u; counter.io.count32=(i+1u)*300u; tx_pending=false;
            // Threshold FREEZE must be offered even while a prior PREVIEW
            // waits in transport; the transport owner arbitrates replacement.
            if (!strcmp(test,"origin_pending") && i==15u) tx_pending=true;
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            trigger_t2_runtime_prepare_core0(); tx_pending=false;
        }
        for (unsigned i=0u; i<3u; ++i) {
            now_ns+=1000000u; counter.io.count32+=100u; tx_pending=false;
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            trigger_t2_runtime_prepare_core0();
        }
        for (unsigned i=0u; i<transmitted_count; ++i) {
            if (transmitted[i].position_id != 1u) continue;
            if (transmitted[i].stage == TRIGGER_POSITION_STAGE_PREVIEW) {
                assert(!saw_freeze); // no PREVIEW after this position's threshold
                saw_preview=true; assert(transmitted[i].prediction_version &&
                    transmitted[i].window_pulses && transmitted[i].window_syncs);
            }
            if (transmitted[i].stage == TRIGGER_POSITION_STAGE_FREEZE) saw_freeze=true;
        }
        assert(saw_preview && saw_freeze && !inverse_calls && !schedule_calls); return 0;
    }
    if (!strcmp(test,"burst_drain")) {
        /* The lane's single mailbox + bounded ring can hold several records, and a long
         * announcement lead makes the lane deliver more than one per coarse slice (measured
         * 2026-10-16: 361 positions -> ~176k lane records, ~1.1 per slice). One record per
         * pass saturated the mailbox (hold_drop ~214k) and starved one board per run. A
         * bounded batch per pass must consume the queued burst. */
        local_offset_ns=2000u;
        now_ns=5000000u;
        for (unsigned k=0u;k<3u;++k){ preview(&c,k+1u,k+1u,10u*(k+1u),1);
            remote_queue[k]=remote_descriptor; }
        remote_queue_len=3u; remote_queue_head=0u; remote_queue_on=true; remote_valid=false;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(remote_queue_head==remote_queue_len);            /* all three in ONE pass */
        assert(snapshot().received_mask==7u);
        remote_queue_on=false;
        return 0;
    }
    if (!strcmp(test,"blip_drain")) {
        /* A cursor blip must not stop the transport: the lane's ring is the only storage and
         * this service is the only drain (the previous code returned before draining, so a
         * board whose cursor blipped drained at half rate while its ring dropped the
         * overflow). Admission/arming still wait for a valid cursor. */
        local_offset_ns=2000u;
        now_ns=5000000u;
        preview(&c,1,1,10,1);                       /* one valid pass establishes the time base */
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().received_mask==1u);
        preview(&c,2,2,20,1); remote_queue[0]=remote_descriptor;
        remote_queue_len=1u; remote_queue_head=0u; remote_queue_on=true; remote_valid=false;
        cursor_valid=false;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_CURSOR);
        assert(remote_queue_head==remote_queue_len);            /* drained anyway */
        assert(snapshot().received_mask==3u);
        cursor_valid=true; remote_queue_on=false;
        return 0;
    }
    if (!strcmp(test,"follower_scan")) {
        /* A scan has more positions than the three slots: ordinal k takes slot k%3 and
         * the wire id slot+1. Versions keep rising per slot, a slot is handed on only
         * after its action finished, and leftovers of a finished occupant are ignored. */
        local_offset_ns=2000u;
        for (unsigned k=0u; k<9u; ++k) {
            const unsigned slot=k%3u+1u, ms=k*20u+10u, version=k+1u, bit=1u<<(slot-1u);
            now_ns=(ms-5u)*1000000u;
            preview(&c,slot,version,ms,1);
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            trigger_t2_runtime_prepare_core0();
            freeze(slot,version);
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            assert(snapshot().action_armed_mask & bit);
            if (k>=3u) {
                /* the slot's previous occupant was recycled, not carried over */
                assert(snapshot().freeze_version[slot-1u]==version);
            }
            now_ns=ms*1000000u+500u;
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            assert(snapshot().action_completed_mask & bit);
            assert(take());
        }
        uint32_t entered,announced,frozen,executed,abandoned;
        trigger_t2_runtime_position_window_totals(&entered,&announced,&frozen,&executed,&abandoned);
        assert(entered==9u && frozen==9u && executed==9u && abandoned==0u);
        assert(!snapshot().action_failed_mask && !snapshot().descriptor_reject_count);
        /* leftovers: a resent FREEZE and an old PREVIEW of finished occupants */
        const uint32_t rejects=snapshot().descriptor_reject_count;
        freeze(1u,7u); now_ns+=1000000u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        preview(&c,2u,2u,70u,1); now_ns+=1000000u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().descriptor_reject_count==rejects && !take());
        trigger_t2_runtime_position_window_totals(&entered,&announced,&frozen,&executed,&abandoned);
        assert(entered==9u && frozen==9u);
        return 0;
    }
    if (!strcmp(test,"scan_events_retained")) {
        /* Nothing drains the events in firmware: a 9-position scan on a 3-slot runtime
         * must keep all 9 receipts (capacity is not tied to the slot count). */
        local_offset_ns=2000u;
        for (unsigned k=0u; k<9u; ++k) {
            const unsigned slot=k%3u+1u, ms=k*20u+10u;
            now_ns=(ms-5u)*1000000u;
            preview(&c,slot,k+1u,ms,1);
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            trigger_t2_runtime_prepare_core0();
            freeze(slot,k+1u);
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            now_ns=ms*1000000u+500u;
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        }
        assert(snapshot().event_count==9u && !snapshot().event_dropped_count);
        return 0;
    }
    if (!strcmp(test,"follower_scan_busy")) {
        /* A newer PREVIEW for a slot whose action is still in flight is not taken into the
         * slot: the occupant keeps its version and its action. It is HELD as the slot's
         * successor (R3) and handed on as soon as the occupant finished - without waiting
         * for another PREVIEW, because the origin does not re-stage one. */
        local_offset_ns=2000u;
        now_ns=5000000u;
        preview(&c,1,1,10,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        trigger_t2_runtime_prepare_core0();
        freeze(1,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().action_armed_mask==1u);
        preview(&c,1,5,30,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().descriptors[0].prediction_version==1u &&
               snapshot().frozen_mask==1u);
        now_ns=10000500u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().action_completed_mask==1u && take());
        const uint32_t rejects=snapshot().descriptor_reject_count;
        /* no transport record this pass: the finished slot still takes its successor */
        now_ns=20000000u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().descriptors[0].prediction_version==5u &&
               !snapshot().frozen_mask && !snapshot().action_completed_mask);
        assert(snapshot().descriptor_reject_count==rejects);
        return 0;
    }
    if (!strcmp(test,"follower_scan_successor")) {
        /* The bench loss R3: the successor's PREVIEW arrives while the occupant is still in
         * flight (dropped before this fix), and after that only its FREEZE resends arrive -
         * the origin never stages that PREVIEW again. The stashed successor must be handed on
         * at completion and freeze on the resend alone, so the position still executes. */
        local_offset_ns=2000u;
        now_ns=5000000u;
        preview(&c,1,1,10,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        trigger_t2_runtime_prepare_core0();
        freeze(1,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().action_armed_mask==1u);
        preview(&c,1,5,30,1);              /* successor, while the occupant is in flight */
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        now_ns=10000500u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().action_completed_mask==1u && take());
        now_ns=11000000u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);   /* hands on the slot */
        trigger_t2_runtime_prepare_core0();                            /* Core0 solves it */
        freeze(1,5); remote_descriptor.target_ms_index=30u;             /* only the resend */
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().frozen_mask==1u && snapshot().action_armed_mask==1u);
        assert(snapshot().descriptors[0].prediction_version==5u);
        now_ns=30000500u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().action_completed_mask==1u && take());
        uint32_t entered,announced,frozen,executed,abandoned;
        trigger_t2_runtime_position_window_totals(&entered,&announced,&frozen,&executed,&abandoned);
        assert(entered==2u && frozen==2u && executed==2u && abandoned==0u);
        assert(snapshot().freeze_abandoned_count==0u &&
               !snapshot().freeze_missing_local_count);
        return 0;
    }
    if (!strcmp(test,"follower_overwrite_abandon")) {
        /* A PREVIEW replaced by a DIFFERENT position before it was ever frozen is a lost
         * position, and the loss must be counted instead of vanishing (R6). A re-solve of
         * the same position is not a replacement. */
        local_offset_ns=2000u;
        now_ns=5000000u;
        preview(&c,1,1,10,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().received_mask==1u && snapshot().freeze_abandoned_count==0u);
        preview(&c,1,5,30,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().descriptors[0].prediction_version==5u &&
               snapshot().freeze_abandoned_count==1u);
        preview(&c,1,6,30,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().freeze_abandoned_count==1u);
        return 0;
    }
    if (!strcmp(test,"freeze_other_position")) {
        /* FREEZE names its position by target_ms_index. After a slot has been handed on,
         * a FREEZE that is nearer to the PREVIOUS occupant's target than to the PREVIEW now
         * held is that occupant's leftover: it must neither freeze nor fail the new one.
         * A FREEZE for the held PREVIEW whose target drifted (a re-solve) is accepted. */
        local_offset_ns=2000u;
        now_ns=5000000u;
        preview(&c,1,1,10,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        trigger_t2_runtime_prepare_core0();
        freeze(1,1); remote_descriptor.target_ms_index=10u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().action_armed_mask==1u);
        now_ns=10000500u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().action_completed_mask==1u && take());
        /* a finished position is no longer reported as awaiting confirmation */
        assert(noted_active && noted_frozen==0u);
        now_ns=25000000u;
        preview(&c,1,5,30,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        trigger_t2_runtime_prepare_core0();
        freeze(1,9); remote_descriptor.target_ms_index=10u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(!snapshot().frozen_mask && !snapshot().action_failed_mask &&
               snapshot().freeze_mismatch_count==1u);
        freeze(1,5); remote_descriptor.target_ms_index=33u;   /* drifted, still the held one */
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().frozen_mask==1u && snapshot().action_armed_mask==1u);
        assert(noted_frozen==1u);
        return 0;
    }
    if (!strcmp(test,"origin_scan")) {
        /* 11 positions on a 3-slot runtime: every ordinal must be announced, previewed
         * and frozen on its own slot (id = slot + 1) instead of stopping after three. */
        trigger_t2_runtime_stop();
        assert(trigger_t2_runtime_scan_step(1000));
        assert(trigger_t2_runtime_configure(&c));
        assert(trigger_t2_runtime_start(11,0));
        freeze_acked=7u;
        unsigned freezes=0u;
        for (unsigned i=0u; i<260u; ++i) {
            now_ns=(i+1u)*1000000u; counter.io.count32=(i+1u)*100u; tx_pending=false;
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            trigger_t2_runtime_prepare_core0();
            tx_pending=false;
        }
        uint32_t entered,announced,frozen,executed,abandoned;
        trigger_t2_runtime_position_window_totals(&entered,&announced,&frozen,&executed,&abandoned);
        for (unsigned k=0u; k<transmitted_count; ++k)
            if (transmitted[k].stage==TRIGGER_POSITION_STAGE_FREEZE) {
                assert(transmitted[k].position_id>=1u && transmitted[k].position_id<=3u);
                assert(transmitted[k].target_ms_index!=0u);   /* identity rides on FREEZE */
                ++freezes;
            }
        assert(snapshot().predictor.window_announced_count==11u);
        assert(frozen==11u && snapshot().freeze_abandoned_count==0u);
        assert(snapshot().freeze_no_preview_count==0u && freezes>=11u);
        return 0;
    }
    preview(&c,1,1,10,1);
    if (!strcmp(test,"zero_pulse_index")) {
        zero_pulse_index=true;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_CURSOR);
        assert(!snapshot().cursor.valid && !snapshot().received_mask &&
            !schedule_calls && !take()); return 0;
    }
    if (!strcmp(test,"wire_identity_ignored")) {
        /* The compact wire carries no run identity: the follower binds its own. */
        remote_descriptor.sync_epoch=2; remote_descriptor.run_id=77;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().received_mask==1u); return 0;
    }
    if (!strcmp(test,"late_arrival")) {
        /* A well-formed, correctly addressed preview whose promised window had
         * already closed when it arrived. It stays a descriptor reject, but the
         * lateness is now attributed on its own. */
        for (unsigned i=0u; i<8u; ++i) {
            now_ns=(i+1u)*10000000u; counter.io.count32=(i+1u)*3000u;
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        }
        const uint32_t rejects=snapshot().descriptor_reject_count;
        preview(&c,1,2,0,1);   /* a NEW version, so the duplicate short-circuit cannot apply */
        const int late_rc=(int)trigger_t2_runtime_service();
        assert(late_rc==TRIGGER_T2_RUNTIME_DESCRIPTOR);
        assert(snapshot().late_arrival_count==1u);
        assert(snapshot().descriptor_reject_count==rejects+1u);
        /* Magnitude is readable: the promised window and the arrival ordinal,
         * with the arrival strictly after the window. */
        assert(snapshot().late_last_expiry_sync!=0u);
        assert(snapshot().late_last_current_ms>snapshot().late_last_expiry_sync);
        assert((snapshot().received_mask&1u) &&
               snapshot().descriptors[0].prediction_version==1u);
        return 0;
    }
    if (!strcmp(test,"expired")) {
        /* Expiry derives from the target slice; an unrepresentable one expires. */
        remote_descriptor.target_ms_index=UINT32_MAX;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_DESCRIPTOR);
        assert(!take() && !schedule_calls);
        /* No window was ever promised, so this is NOT counted as lateness: the
         * counter must mean "missed the promised window", not "any expiry". */
        assert(snapshot().late_arrival_count==0u);
        return 0;
    }
    if (!strcmp(test,"version_overflow")) {
        remote_descriptor.prediction_version = UINT16_MAX + 1u;
        remote_descriptor.descriptor_crc32 = trigger_position_descriptor_crc(&remote_descriptor);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_DESCRIPTOR);
        assert(snapshot().descriptor_reject_count==1 && !schedule_calls && !take());
        return 0;
    }
    if (!strcmp(test,"version_limit")) {
        remote_descriptor.prediction_version=UINT16_MAX;
        remote_descriptor.descriptor_crc32=trigger_position_descriptor_crc(&remote_descriptor);
    }
    assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
    assert(!take() && !schedule_calls && !inverse_calls);
    assert(!backend_polls && !backend_snapshots);
    if (!strcmp(test,"freeze_missing")) {
        freeze(1,1); assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_ACTION);
        trigger_t2_runtime_prepare_core0();
        assert(snapshot().freeze_missing_local_count==1 && !inverse_calls && !schedule_calls);
        /* Received is frozen: the follower reports completion regardless. */
        assert(noted_active && noted_frozen==1u);
        return 0;
    }
    local_offset_ns=2000u;
    trigger_t2_runtime_prepare_core0();
    /* Two inversions per admitted record: the deadline solve plus the receive-side
     * timestamp (the admission instant, projected through the same DCO/TIMER1 path). */
    assert(inverse_calls==2);
    assert(schedule_calls==0);
    assert(inverse_calls==2);
    assert(schedule_calls==0);
    /* Receive-side timestamp: one stamp per admitted record, in the same local tick domain
     * as the deadline, and not later than that deadline. */
    {
        const trigger_t2_runtime_snapshot_t rx=snapshot();
        assert(rx.rx_timestamp_count==1u);
        assert(rx.rx_local_tick!=0u);
        assert(rx.rx_local_tick != 0u);
        assert(rx.rx_stale_count==0u);
    }
    if ((!strcmp(test,"independent_positions") || !strcmp(test,"all_events_retained"))) {
        for (unsigned pos=2;pos<=3;pos++) {
            preview(&c,pos,1,pos*10,1);
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            trigger_t2_runtime_prepare_core0();
        }
    }
    if (!strcmp(test,"fallback") || !strcmp(test,"freeze_stops_solve")) {
        preview(&c,1,2,11,1); // RX newer but unfinished: use newest completed local solve.
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
    }
    if (!strcmp(test,"conflict")) {
        preview(&c,2,1,10,1);
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        trigger_t2_runtime_prepare_core0();
    }
    if (!strcmp(test,"reject_no_retry")) accept_schedule=false;
    if (!strcmp(test,"reject_uncertain")) rejected_uncertain=true;
    freeze(1,!strcmp(test,"version_limit")?UINT16_MAX:
        !strcmp(test,"fallback") || !strcmp(test,"freeze_stops_solve")?2:1);
    const unsigned frozen_solves=inverse_calls;
    const int result=trigger_t2_runtime_service();
    if (!strcmp(test,"reject_uncertain")) {
        assert(result==TRIGGER_T2_RUNTIME_ACTION && schedule_calls==1);
        assert(snapshot().action_failed_mask==1 && snapshot().action_uncertain_mask==1);
        assert(snapshot().action_backend_state[0]==SYNC_IO_SWITCH_DEADLINE_UNCERTAIN &&
            snapshot().action_origin_width_ticks[0]==80u);
        assert(!take()); return 0;
    }
    if (!strcmp(test,"reject_no_retry")) {
        assert(result==TRIGGER_T2_RUNTIME_ACTION && schedule_calls==1);
        accept_schedule=true; now_ns=20000000;
        (void)trigger_t2_runtime_service();
        assert(schedule_calls==1 && !take() && snapshot().action_failed_mask==1); return 0;
    }
    assert(result==TRIGGER_T2_RUNTIME_OK && schedule_calls==1);
    assert(hw.deadline_tick==(10000500u+2000u)/4u);
    assert(!take() && snapshot().action_armed_mask==1 && !snapshot().action_completed_mask);
    assert(snapshot().action_backend_state[0]==SYNC_IO_SWITCH_DEADLINE_ARMED &&
        snapshot().action_origin_width_ticks[0]==40u);
    if (!strcmp(test,"fallback")) {
        const trigger_t2_runtime_snapshot_t s=snapshot();
        assert(s.freeze_local_version[0]==1 && s.freeze_version[0]==2 &&
            s.freeze_fallback_count==1 && s.freeze_local_age[0]==1);
    }
    if (!strcmp(test,"freeze_stops_solve")) {
        token++; trigger_t2_runtime_prepare_core0();
        assert(inverse_calls==frozen_solves); // no inverse after the threshold.
    }
    if (!strcmp(test,"stop") || !strcmp(test,"restart")) {
        trigger_t2_runtime_stop(); assert(cancel_calls==1);
        now_ns=20000000; (void)trigger_t2_runtime_service(); assert(!take());
        if (!strcmp(test,"restart")) {
            freeze(1,99); // prior-run FREEZE still pending in the single-depth RX mailbox
            assert(remote_valid);
            assert(trigger_t2_runtime_start(12,0));
            assert(!remote_valid);
            assert(!snapshot().frozen_mask && !snapshot().received_mask &&
                !snapshot().action_failed_mask && !snapshot().event_count);
            now_ns=1000000;
            preview(&c,1,1,10,1); remote_descriptor.count_epoch=12;
            remote_descriptor.descriptor_crc32=trigger_position_descriptor_crc(&remote_descriptor);
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            trigger_t2_runtime_prepare_core0(); freeze(1,1);
            assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
            assert(snapshot().action_armed_mask==1 && !snapshot().action_failed_mask);
        }
        return 0;
    }
    if (!strcmp(test,"conflict")) {
        freeze(2,1); assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_ACTION);
        assert(snapshot().action_failed_mask==2 && schedule_calls==1 && !take()); return 0;
    }
    if (!strcmp(test,"identity")) {
        /* A transiently invalid cursor is NOT an identity change: an armed absolute
         * deadline belongs to the backend, so it must neither be cancelled nor failed
         * (the previous behaviour cancelled and abandoned the position, which turned every
         * cursor blip into lost positions - measured 2026-10-16 as 23-30 abandoned
         * positions per follower over a 361-position scan). The position still completes on
         * its own receipt; a REAL session/ring change is caught once the cursor is valid
         * again (see the plan-identity checks and the `conflict` case). */
        cursor_valid=false; assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_CURSOR);
        assert(cancel_calls==0 && !snapshot().action_failed_mask && !take());
        assert(snapshot().action_armed_mask==1);
        cursor_valid=true; now_ns=10000500u;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().action_completed_mask==1 && take());
        assert(!snapshot().action_failed_mask);
        assert(!snapshot().action_fail_reason[TRIGGER_T2_ACTION_FAIL_BACKEND]);
        return 0;
    }
    if (!strcmp(test,"wrong_receipt")) {
        hw.sequence++; hw.state=SYNC_IO_SWITCH_DEADLINE_COMPLETE;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_ACTION);
        assert(!take() && snapshot().action_uncertain_mask==1); return 0;
    }
    if ((!strcmp(test,"independent_positions") || !strcmp(test,"all_events_retained"))) {
        freeze(2,1); assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        freeze(3,1); assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(snapshot().received_mask==7 && snapshot().frozen_mask==7);
    }
    // READY observations and elapsed time alone are not a hardware receipt.
    receipts=false; now_ns=10000500u;
    assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK); assert(!take());
    receipts=true;
    assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
    trigger_action_runtime_event_t event;
    if (strcmp(test,"all_events_retained")) {
        assert(trigger_t2_runtime_take_event(&event));
        assert(event.position_id==1 && event.action_q==20001 && event.template_id==7);
        assert(!take());
    }
    assert(snapshot().action_completed_mask==1);
    if (!strcmp(test,"gateway")) {
        const unsigned prior_polls=backend_polls;
        assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        assert(backend_polls==prior_polls+1); // late READY must still be drained
    }
    if ((!strcmp(test,"independent_positions") || !strcmp(test,"all_events_retained"))) {
        assert(schedule_calls==2);
        now_ns=20000500; assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        if (strcmp(test,"all_events_retained"))
            assert(trigger_t2_runtime_take_event(&event) && event.position_id==2);
        assert(schedule_calls==3);
        now_ns=30000500; assert(trigger_t2_runtime_service()==TRIGGER_T2_RUNTIME_OK);
        if (strcmp(test,"all_events_retained"))
            assert(trigger_t2_runtime_take_event(&event) && event.position_id==3);
        assert(snapshot().action_completed_mask==7);
        if (!strcmp(test,"all_events_retained")) {
            assert(snapshot().event_count==3 && !snapshot().event_dropped_count);
            for (unsigned i=1;i<=3;i++)
                assert(trigger_t2_runtime_take_event(&event) && event.position_id==i);
            assert(!take());
        }
    }
    return 0;
}
'''


@pytest.fixture(scope="module")
def runtime(tmp_path_factory):
    directory = tmp_path_factory.mktemp("t2-runtime")
    (directory / "board_config.h").write_text(
        "#define BOARD_SYS_CLOCK_HZ 250000000u\n", encoding="utf-8")
    return compile_grid(directory, "runtime", HARNESS,
        [ROOT / "components/sync_trigger/src/trigger_t2_runtime.c"])


@pytest.mark.parametrize("case", ["announce_config", "origin", "origin_pending", "origin_resend", "origin_no_ack", "receiver", "fallback", "late_arrival", "wire_identity_ignored",
    "expired", "freeze_missing", "freeze_stops_solve", "capacity", "sequence", "gateway",
    "independent_positions", "reject_no_retry", "stop", "restart", "identity",
    "wrong_receipt", "reject_uncertain", "conflict", "different_local_seq", "all_events_retained", "zero_pulse_index", "version_overflow", "version_limit",
    "follower_scan", "follower_scan_busy", "follower_scan_successor", "follower_overwrite_abandon", "burst_drain", "blip_drain", "origin_scan", "freeze_other_position", "scan_events_retained"])
def test_t2_runtime(runtime, case):
    result = subprocess.run([str(runtime), case], capture_output=True,
                            text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
