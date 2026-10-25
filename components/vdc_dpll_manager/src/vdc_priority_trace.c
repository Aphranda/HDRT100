#include "vdc_dpll_manager.h"
#include "vdc_dpll_manager_internal.h"
#include "vdc_priority_shared.h"
#include "vdc_time_mapping.h"
#include "vdc_model_projection.h"
#include "vdc_priority_tx.h"
#include "vdc_priority_rx.h"
#include "vdc_priority_match.h"
#include "vdc_priority_follow.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_config.h"
#include "board_identity.h"
#include "app.h"
#include "distributed_refmem.h"
#include "ota_crc32.h"
#include "osal.h"
#include "ota_ao.h"
#include "storage_manager.h"
#include "product_config.h"
#include "refmem_sync_frame.h"
#include "sync_io.h"
#include "tdma_runtime_owner.h"
#include "tdma_service.h"
#include "tdma_service_timing.h"
#include "vdc_domain.h"
#include "vdc_ring_observer.h"
#include "vdc_sync_io_adapter.h"
#include "vdc_tdma_payload.h"
#include "vdc_timestamp_clock.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#include "pico/time.h"
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) name
#endif

#define VDC_DPLL_MANAGER_SELF_TEST_CLEANUP_MARGIN_MS 250u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_MAGIC 0x4C504444u /* DDPL */
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_SCHEMA 6u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_MASTER 1u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_COMMAND 2u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_STATE 3u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_EVIDENCE 4u
#define VDC_DPLL_MANAGER_WAVEFORM_MAGIC 0x57524D53u /* SMRW */
#define VDC_DPLL_MANAGER_WAVEFORM_SCHEMA 4u
#define VDC_DPLL_MANAGER_WAVEFORM_BUFFER_COUNT 3u
#define VDC_DPLL_MANAGER_PHASE_ARM_AHEAD_PERIODS 2u


#include "vdc_dpll_manager_internal.h"

/* Shared 76 x 100-byte maintenance pool, no second record ring. Core0 only
 * reserves the pool and publishes a stopped command. Core1 processes that
 * command between producer calls; its ACK is the ownership-transfer proof.
 * FROZEN bytes and metadata remain immutable until another acknowledged
 * command. READ holds a lease that excludes both submission and consumption. */
static uint32_t s_priority_trace_request_guard, s_priority_trace_ack;
static vdc_priority_trace_request_t s_priority_trace_request;
static uint32_t s_priority_trace_last_capture; /* Core0, never reused. */
static uint32_t s_priority_trace_status_guard;
static uint32_t s_priority_trace_status_words[sizeof(vdc_priority_trace_status_t) / 4u];

_Static_assert(sizeof(vdc_priority_summary_work_t) <= VDC_PRIORITY_TRACE_ORIGIN_EXTENSION_BYTES,
    "summary scratch must fit existing origin extension");
vdc_priority_trace_work_t s_priority_trace_work;
static bool priority_trace_origin_family(uint32_t schema)
{ return schema == VDC_PRIORITY_TRACE_ORIGIN_SCHEMA || schema == VDC_PRIORITY_TRACE_SUMMARY_ORIGIN_SCHEMA ||
    schema == VDC_PRIORITY_TRACE_SUMMARY_WINDOW_ORIGIN_SCHEMA; }
static bool priority_trace_summary_family(uint32_t schema)
{ return schema == VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA || schema == VDC_PRIORITY_TRACE_SUMMARY_ORIGIN_SCHEMA ||
    schema == VDC_PRIORITY_TRACE_SUMMARY_WINDOW_PHASE_SCHEMA || schema == VDC_PRIORITY_TRACE_SUMMARY_WINDOW_ORIGIN_SCHEMA; }
static bool priority_trace_summary_interval_valid(uint32_t schema, uint32_t interval_ms)
{
    if (!priority_trace_summary_family(schema)) return interval_ms == 0u;
    if (interval_ms < VDC_PRIORITY_TRACE_SUMMARY_INTERVAL_MS ||
        interval_ms > VDC_PRIORITY_TRACE_SUMMARY_MAX_INTERVAL_MS ||
        interval_ms % VDC_PRIORITY_TRACE_SUMMARY_INTERVAL_MS != 0u) return false;
    if ((schema == VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA ||
         schema == VDC_PRIORITY_TRACE_SUMMARY_ORIGIN_SCHEMA) &&
        interval_ms != VDC_PRIORITY_TRACE_SUMMARY_INTERVAL_MS) return false;
    const uint64_t ticks = (uint64_t)BOARD_SYS_CLOCK_HZ * interval_ms / 1000u;
    return ticks != 0u && ticks <= UINT32_MAX;
}
static void priority_summary_terminal(void);
void priority_summary_service(bool end);
static void priority_guard_reset_core1(uint32_t duration_s);
static void priority_guard_note_bin(uint32_t flags, uint32_t success_gap, bool nonempty);
static bool priority_guard_steady_gaps(void);
void priority_guard_service_core1(void);
static void priority_guard_ring_core1(uint32_t config_seq);
_Static_assert(sizeof(vdc_priority_trace_status_t) == 148u, "typed trace status ABI");
_Static_assert(sizeof(vdc_priority_trace_header_t) == 168u, "typed trace header ABI");
_Static_assert(VDC_PRIORITY_TRACE_ORIGIN_SCHEMA == 5u, "origin schema 5 uses TIMER1 directly");
_Static_assert(sizeof(vdc_priority_trace_record_t) == sizeof(s_dpll_capture_records[0]),
    "typed trace reuses each complete legacy slot");

static bool priority_trace_request_copy(vdc_priority_trace_request_t *out)
{
    const uint32_t guard = __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    const vdc_priority_trace_request_t copy = {
        .sequence = __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_RELAXED),
        .command = __atomic_load_n(&s_priority_trace_request.command, __ATOMIC_RELAXED),
        .capture_id = __atomic_load_n(&s_priority_trace_request.capture_id, __ATOMIC_RELAXED),
        .session = __atomic_load_n(&s_priority_trace_request.session, __ATOMIC_RELAXED),
        .generation = __atomic_load_n(&s_priority_trace_request.generation, __ATOMIC_RELAXED),
        .mode = __atomic_load_n(&s_priority_trace_request.mode, __ATOMIC_RELAXED),
        .schema = __atomic_load_n(&s_priority_trace_request.schema, __ATOMIC_RELAXED),
        .interval_ms = __atomic_load_n(&s_priority_trace_request.interval_ms, __ATOMIC_RELAXED),
        .guard_s = __atomic_load_n(&s_priority_trace_request.guard_s, __ATOMIC_RELAXED),
        .boundary_request = __atomic_load_n(&s_priority_trace_request.boundary_request, __ATOMIC_RELAXED)};
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE)) return false;
    *out = copy; return true;
}

static void priority_trace_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_trace_status_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_trace_status_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    boundary_store(s_priority_trace_status_words, &s_priority_trace_work.status,
        sizeof(s_priority_trace_work.status));
    __atomic_store_n(&s_priority_trace_status_guard, guard + 2u, __ATOMIC_RELEASE);
}

static bool priority_trace_status_copy(vdc_priority_trace_status_t *out)
{
    const uint32_t guard = __atomic_load_n(&s_priority_trace_status_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_trace_status_t copy;
    match_load_words(s_priority_trace_status_words, &copy, sizeof(copy));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_trace_status_guard, __ATOMIC_ACQUIRE)) return false;
    *out = copy; return true;
}

bool vdc_dpll_manager_get_priority_trace(vdc_priority_trace_status_t *out)
{
    if (!out) return false;
    vdc_priority_trace_request_t request;
    vdc_priority_trace_status_t copy;
    if (!priority_trace_request_copy(&request) || !priority_trace_status_copy(&copy)) return false;
    /* The requested identity is distinct from the owner's acknowledged one. */
    if (!copy.schema) copy.schema = VDC_PRIORITY_TRACE_SCHEMA;
    copy.request_seq = request.sequence;
    copy.command = request.command;
    if (copy.ack_seq != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE)) return false;
    if (copy.ack_seq == request.sequence &&
        (copy.state == VDC_PRIORITY_TRACE_REJECTED ||
         (copy.state == VDC_PRIORITY_TRACE_IDLE && copy.reason == VDC_PRIORITY_TRACE_RELEASED))) {
        const uint32_t owner = __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE);
        if (owner != DPLL_CAPTURE_POOL_LEGACY && owner != DPLL_CAPTURE_POOL_LEGACY_ACCESS) return false;
    }
    if (request.sequence != __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_ACQUIRE) ||
        (__atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE) & 1u)) return false;
    *out = copy; return true;
}

typedef struct { uint32_t command, capture_id, schema, interval_ms, guard_s; } vdc_priority_trace_submit_t;
static bool priority_trace_submit_stopped(void *context)
{
    const vdc_priority_trace_submit_t *input = context;
    if (input->command == VDC_PRIORITY_TRACE_COMMAND_ARM &&
        !priority_trace_summary_interval_valid(input->schema, input->interval_ms)) return false;
    if (input->guard_s && (!priority_trace_summary_family(input->schema) ||
        input->interval_ms != VDC_PRIORITY_TRACE_SUMMARY_MAX_INTERVAL_MS ||
        input->guard_s > VDC_PRIORITY_GUARD_MAX_SECONDS ||
        input->guard_s % VDC_PRIORITY_GUARD_CHECKPOINT_S != 0u)) return false;
    uint32_t guard = __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE);
    if ((guard & 1u) || guard > UINT32_MAX - 2u ||
        !__atomic_compare_exchange_n(&s_priority_trace_request_guard, &guard, guard + 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    bool accepted = false, reserved = false;
    const uint32_t previous = __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_RELAXED);
    const uint32_t owner = __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE);
    vdc_priority_trace_status_t status;
    uint32_t request = 0u, boundary_session = 0u, mode = 0u, generation = 0u;
    const uint32_t session = vdc_dpll_manager_feedback_session();
    int32_t delta;
    if (previous == UINT32_MAX || previous != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE) ||
        (owner != DPLL_CAPTURE_POOL_LEGACY && owner != DPLL_CAPTURE_POOL_TYPED) ||
        !priority_trace_status_copy(&status)) goto done;
    if (input->command == VDC_PRIORITY_TRACE_COMMAND_ARM) {
        bool phase_enabled;
        if (!priority_trace_origin_family(input->schema) &&
            (!vdc_dpll_manager_try_priority_follow_phase_enabled(&phase_enabled) ||
             (input->schema == VDC_PRIORITY_TRACE_SCHEMA && phase_enabled) ||
             ((input->schema == VDC_PRIORITY_TRACE_PHASE_SCHEMA || priority_trace_summary_family(input->schema)) && !phase_enabled))) goto done;
        if (!input->capture_id || input->capture_id <= s_priority_trace_last_capture ||
            (owner == DPLL_CAPTURE_POOL_TYPED && status.state != VDC_PRIORITY_TRACE_FROZEN) ||
            !session || session != vdc_dpll_manager_feedback_session()) goto done;
        if (priority_trace_origin_family(input->schema)) {
            generation = vdc_dpll_manager_priority_sync_generation();
            if (!generation || session != vdc_dpll_manager_feedback_session() ||
                generation != vdc_dpll_manager_priority_sync_generation()) goto done;
        } else {
            generation = vdc_dpll_manager_priority_match_generation();
            if (!boundary_request(&request, &boundary_session, &delta, &mode) || !generation ||
                __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) != session) goto done;
        }
        if (owner == DPLL_CAPTURE_POOL_LEGACY) {
            uint32_t expected = DPLL_CAPTURE_POOL_LEGACY;
            if (!__atomic_compare_exchange_n(&s_dpll_capture_pool_owner, &expected,
                    DPLL_CAPTURE_POOL_TYPED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) goto done;
            reserved = true;
        }
        __atomic_store_n(&s_priority_trace_request.capture_id, input->capture_id, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.session, session, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.generation,
            generation, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.schema, input->schema, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.interval_ms, input->interval_ms, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.guard_s, input->guard_s, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.mode, mode, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.boundary_request, request, __ATOMIC_RELAXED);
        s_priority_trace_last_capture = input->capture_id;
    } else if (owner != DPLL_CAPTURE_POOL_TYPED) goto done;
    __atomic_store_n(&s_priority_trace_request.command, input->command, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_trace_request.sequence, previous + 1u, __ATOMIC_RELAXED);
    accepted = true;
done:
    if (reserved && !accepted)
        __atomic_store_n(&s_dpll_capture_pool_owner, DPLL_CAPTURE_POOL_LEGACY, __ATOMIC_RELEASE);
    __atomic_store_n(&s_priority_trace_request_guard, guard + 2u, __ATOMIC_RELEASE);
    return accepted;
}

static bool priority_trace_submit(uint32_t command, uint32_t capture_id, uint32_t schema, uint32_t interval_ms)
{
    vdc_priority_trace_submit_t input = {command, capture_id, schema, interval_ms, 0u};
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service, priority_trace_submit_stopped, &input);
}
bool vdc_dpll_manager_priority_trace_arm(uint32_t capture_id)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id, VDC_PRIORITY_TRACE_SCHEMA, 0u); }
bool vdc_dpll_manager_priority_trace_phase_arm(uint32_t capture_id)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id, VDC_PRIORITY_TRACE_PHASE_SCHEMA, 0u); }
bool vdc_dpll_manager_priority_trace_origin_arm(uint32_t capture_id)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id, VDC_PRIORITY_TRACE_ORIGIN_SCHEMA, 0u); }
bool vdc_dpll_manager_priority_trace_summary_arm(uint32_t capture_id, bool origin)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id, origin ? VDC_PRIORITY_TRACE_SUMMARY_ORIGIN_SCHEMA : VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA, VDC_PRIORITY_TRACE_SUMMARY_INTERVAL_MS); }
bool vdc_dpll_manager_priority_trace_summary_window_arm(uint32_t capture_id, bool origin, uint32_t interval_ms)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id, origin ? VDC_PRIORITY_TRACE_SUMMARY_WINDOW_ORIGIN_SCHEMA : VDC_PRIORITY_TRACE_SUMMARY_WINDOW_PHASE_SCHEMA, interval_ms); }
bool vdc_dpll_manager_priority_trace_guard_arm(uint32_t capture_id, bool origin, uint32_t duration_s)
{
    if (!duration_s) return false;
    vdc_priority_trace_submit_t input = {VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id,
        origin ? VDC_PRIORITY_TRACE_SUMMARY_WINDOW_ORIGIN_SCHEMA : VDC_PRIORITY_TRACE_SUMMARY_WINDOW_PHASE_SCHEMA,
        VDC_PRIORITY_TRACE_SUMMARY_MAX_INTERVAL_MS, duration_s};
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service, priority_trace_submit_stopped, &input);
}
bool vdc_dpll_manager_priority_trace_stop(void)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_STOP, 0u, 0u, 0u); }
bool vdc_dpll_manager_priority_trace_release(void)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_RELEASE, 0u, 0u, 0u); }

static bool priority_trace_active(void)
{
    const uint32_t state = s_priority_trace_work.status.state;
    return state == VDC_PRIORITY_TRACE_ARMED || state == VDC_PRIORITY_TRACE_RUNNING;
}
static void priority_trace_freeze(uint32_t reason)
{
    vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    if (!priority_trace_active()) return;
    priority_summary_terminal();
    s->state = VDC_PRIORITY_TRACE_FROZEN; s->reason = reason; s->freeze_ms = board_uptime_ms();
    priority_trace_publish();
}

/* Core1-only diagnostics. The current unpublished shared-pool slot is the
 * accumulator; committing a bin makes that slot immutable. No second ring. */
enum {
    SUMMARY_PARTIAL=1u, SUMMARY_NO_SUCCESS=2u, SUMMARY_SERVICE_GAP=4u,
    SUMMARY_COUNTER_RESET=8u, SUMMARY_COUNTER_SATURATED=16u,
    SUMMARY_FIELD_SATURATED=32u, SUMMARY_CLOCK_INVALID=64u,
    SUMMARY_UNBOUND=128u, SUMMARY_TERMINAL=256u,
    /* Semantic marker, not an anomaly. Old unmarked records retain the
     * START-to-first-success interval in their maximum. */
    SUMMARY_STEADY_SUCCESS_GAP=512u
};
_Static_assert(sizeof(vdc_priority_summary_record_t)==VDC_PRIORITY_TRACE_RECORD_BYTES,
    "summary native ABI is exactly one legacy slot");
_Static_assert(__builtin_offsetof(vdc_priority_summary_record_t,residual_min_ns)==60u,
    "summary residual ABI offset");

static vdc_priority_summary_record_t *priority_summary_record(void)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    if (!priority_trace_summary_family(w->status.schema) || !priority_trace_active() ||
        !w->summary.open || w->status.record_count>=w->status.capacity) return NULL;
    return (vdc_priority_summary_record_t *)&s_dpll_capture_records[w->status.record_count];
}
static uint16_t priority_summary_add16(uint16_t a, uint32_t b, vdc_priority_summary_record_t *p)
{
    if (b>(uint32_t)UINT16_MAX-a) {p->flags|=SUMMARY_FIELD_SATURATED;return UINT16_MAX;}
    return (uint16_t)(a+b);
}
static uint32_t priority_summary_u32(uint64_t value, vdc_priority_summary_record_t *p)
{
    if (value>UINT32_MAX) {p->flags|=SUMMARY_FIELD_SATURATED;return UINT32_MAX;}
    return (uint32_t)value;
}
static void priority_summary_counters(bool baseline)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!p) return;
    vdc_priority_tx_snapshot_t tx={0};
    if (priority_trace_origin_family(w->status.schema) &&
        !vdc_dpll_manager_get_priority_tx(&tx)) {
        /* The service observation was incomplete. Preserve the last actual
         * counter value; a failed snapshot is neither zero nor a reset. */
        p->flags|=SUMMARY_SERVICE_GAP;return;
    }
    if (priority_trace_origin_family(w->status.schema) &&
        tx.generation!=w->status.generation) {
        /* START can open the first bin before the new TX owner is called.
         * Its published status still belongs to the preceding generation;
         * do not adopt those counters as the new capture's baseline. Once
         * the current owner has been observed, a mismatch is a real gap. */
        if (w->summary.counter_valid_mask & (1u<<6))
            p->flags|=SUMMARY_SERVICE_GAP;
        return;
    }
    const uint32_t values[7]={s_priority_match_work.status.rejected,
        s_priority_follow_work.status.rejected,s_priority_follow_work.status.cancelled,
        s_priority_phase_work.status.rejected,s_priority_phase_work.status.cancelled,
        s_priority_phase_work.status.held,tx.rejected};
    const uint16_t masks[7]={1u,2u,4u,8u,16u,0u,32u};
    for (unsigned i=0;i<7u;++i) {
        if ((priority_trace_origin_family(w->status.schema) && i!=6u) ||
            (!priority_trace_origin_family(w->status.schema) && i==6u)) continue;
        uint32_t old=w->summary.counters[i];
        const uint32_t value=values[i];
        w->summary.counters[i]=value;
        const bool known=(w->summary.counter_valid_mask & (1u<<i))!=0u;
        w->summary.counter_valid_mask|=1u<<i;
        if (baseline || !known) {
            if (baseline) continue;
            if (i!=6u) {p->flags|=SUMMARY_SERVICE_GAP;continue;}
            /* TX initializes the current generation from zero. Include its
             * first rejects, even when its first published snapshot is not
             * observed until after several provider calls. A failed read
             * above already records SERVICE_GAP and is never forgiven. */
            old=0u;
        }
        if (value==UINT32_MAX || old==UINT32_MAX) p->flags|=SUMMARY_COUNTER_SATURATED;
        if (value<old) p->flags|=SUMMARY_COUNTER_RESET;
        const uint32_t delta=value>=old?value-old:value;
        if (!delta) continue;
        p->outcome_mask|=masks[i];
        if (i==2u || i==4u) p->cancelled_count=priority_summary_add16(p->cancelled_count,delta,p);
        else if (i==5u) p->phase_held_count=priority_summary_add16(p->phase_held_count,delta,p);
        else p->rejected_count=priority_summary_add16(p->rejected_count,delta,p);
    }
}
static bool priority_summary_now(uint64_t *raw)
{
    return vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ,raw);
}
static bool priority_summary_observe(uint64_t raw)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!p) return false;
    if (raw<p->observed_end_raw) {p->flags|=SUMMARY_CLOCK_INVALID;return false;}
    p->observed_end_raw=raw;w->summary.last_ms=board_uptime_ms();
    /* The first accepted event starts GUARD's success-gap clock. Its wait
     * remains in first_success_offset_ticks; neither later gaps in this bin
     * nor cross-bin/tail gaps are exempt. Ordinary unmarked summaries keep
     * their original semantics. No extra clock read, state or publication. */
    if (!(p->flags & SUMMARY_STEADY_SUCCESS_GAP) || w->summary.have_success) {
        const uint64_t since=w->summary.last_success;
        const uint32_t gap=priority_summary_u32(raw-since,p);
        if (gap>p->max_success_gap_ticks) p->max_success_gap_ticks=gap;
    }
    return true;
}
static void priority_summary_commit(bool terminal)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    vdc_priority_trace_status_t *s=&w->status;
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!p) return;
    if (terminal) p->flags|=SUMMARY_PARTIAL|SUMMARY_TERMINAL;
    if (!p->success_count) p->flags|=SUMMARY_NO_SUCCESS;
    if (!w->bound) p->flags|=SUMMARY_UNBOUND;
    priority_guard_note_bin(p->flags, p->max_success_gap_ticks,
        p->observed_end_raw > p->observed_start_raw);
    if (!s->record_count) s->first_ms=w->summary.first_ms;
    s->last_ms=w->summary.last_ms;
    s->match_count=UINT32_MAX-s->match_count<p->success_count?UINT32_MAX:s->match_count+p->success_count;
    s->decision_count=UINT32_MAX-s->decision_count<p->decision_count?UINT32_MAX:s->decision_count+p->decision_count;
    ++s->record_count;w->summary.open=0u;
    if (s->record_count==s->capacity && !terminal) {
        s->state=VDC_PRIORITY_TRACE_FROZEN;s->reason=VDC_PRIORITY_TRACE_FULL;s->freeze_ms=board_uptime_ms();
    }
    priority_trace_publish();
}
static void priority_summary_terminal(void)
{
    if (get_core_num()!=1u ||
        __atomic_load_n(&s_dpll_capture_pool_owner,__ATOMIC_ACQUIRE)!=DPLL_CAPTURE_POOL_TYPED ||
        !priority_trace_summary_family(s_priority_trace_work.status.schema)) return;
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!p) return;
    uint64_t raw;
    if (s_priority_trace_work.summary.clock_epoch!=s_vdc_domain.clock.epoch_id ||
        s_priority_trace_work.summary.clock_run!=s_vdc_domain.clock.run_id) {
        p->flags|=SUMMARY_CLOCK_INVALID;
    } else if (priority_summary_now(&raw)) {
        const bool ordered=priority_summary_observe(raw);
        if (ordered && raw>=s_priority_trace_work.summary.last_service) {
            const uint64_t gap=raw-s_priority_trace_work.summary.last_service;
            const uint32_t g=priority_summary_u32(gap,p);
            if (g>p->max_service_gap_ticks) p->max_service_gap_ticks=g;
            if (gap>=BOARD_SYS_CLOCK_HZ) p->flags|=SUMMARY_SERVICE_GAP;
        }
    }
    else p->flags|=SUMMARY_CLOCK_INVALID;
    priority_summary_counters(false);priority_summary_commit(true);
}
void priority_summary_service(bool end)
{
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    if (get_core_num()!=1u ||
        __atomic_load_n(&s_dpll_capture_pool_owner,__ATOMIC_ACQUIRE)!=DPLL_CAPTURE_POOL_TYPED ||
        !priority_trace_summary_family(w->status.schema) || !priority_trace_active() || !w->seen_running) return;
    if (w->summary.open && (w->summary.clock_epoch!=s_vdc_domain.clock.epoch_id ||
        w->summary.clock_run!=s_vdc_domain.clock.run_id)) {
        priority_summary_record()->flags|=SUMMARY_CLOCK_INVALID;
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING);return;
    }
    uint64_t raw=0u;
    const bool valid=priority_summary_now(&raw);
    vdc_priority_summary_record_t *p=priority_summary_record();
    if (!valid || (p && !priority_summary_observe(raw))) {
        if (p) p->flags|=SUMMARY_CLOCK_INVALID;
        /* No bin may open without an actual clock observation. On rollback,
         * retain the last ordered observation and freeze before rollover;
         * never splice a new lower start onto the previous larger end. */
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING);return;
    }
    if (!end && p && valid && raw>=p->observed_start_raw &&
        raw-p->observed_start_raw>=w->summary.interval_ticks) {
        const uint64_t gap=raw>=w->summary.last_service?raw-w->summary.last_service:0u;
        const uint32_t g=priority_summary_u32(gap,p);
        if (g>p->max_service_gap_ticks) p->max_service_gap_ticks=g;
        if (gap>=BOARD_SYS_CLOCK_HZ || raw-p->observed_start_raw>=w->summary.interval_ticks+BOARD_SYS_CLOCK_HZ)
            p->flags|=SUMMARY_SERVICE_GAP;
        priority_summary_counters(false);priority_summary_commit(false);p=NULL;
    }
    if (!priority_trace_active()) return;
    if (!p) {
        if (end) return;
        w->summary.open=1u;
        p=priority_summary_record();if (!p) return;
        memset(p,0,sizeof(*p));p->bin_index=(uint16_t)w->status.record_count;
        if (priority_guard_steady_gaps()) p->flags|=SUMMARY_STEADY_SUCCESS_GAP;
        p->observed_start_raw=p->observed_end_raw=valid?raw:w->summary.last_service;
        w->summary.first_ms=w->summary.last_ms=board_uptime_ms();
        if (!w->status.record_count) {
            w->summary.last_service=w->summary.last_success=p->observed_start_raw;
            w->summary.clock_epoch=s_vdc_domain.clock.epoch_id;
            w->summary.clock_run=s_vdc_domain.clock.run_id;
            priority_summary_counters(true);
        }
        w->status.tick_hz=BOARD_SYS_CLOCK_HZ;
        w->status.state=VDC_PRIORITY_TRACE_RUNNING;
    }
    if (!valid) p->flags|=SUMMARY_CLOCK_INVALID;
    if (end) {priority_summary_counters(false);return;}
    if (valid && raw>=w->summary.last_service) {
        const uint64_t gap=raw-w->summary.last_service;
        const uint32_t g=priority_summary_u32(gap,p);
        if (g>p->max_service_gap_ticks) p->max_service_gap_ticks=g;
        if (gap>=BOARD_SYS_CLOCK_HZ) p->flags|=SUMMARY_SERVICE_GAP;
        w->summary.last_service=raw;
    } else if (valid) p->flags|=SUMMARY_CLOCK_INVALID;
    p->service_count=priority_summary_add16(p->service_count,1u,p);
}
static void priority_summary_success(uint32_t event,uint32_t model,int32_t ppb,
    int64_t lo,int64_t hi,uint64_t width)
{
    vdc_priority_summary_record_t *p=priority_summary_record();
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    if (!p) return;
    uint64_t raw;
    if (!priority_summary_now(&raw) || raw<p->observed_start_raw || raw<p->observed_end_raw) {
        p->flags|=SUMMARY_CLOCK_INVALID;
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING);return;
    }
    priority_summary_observe(raw);
    const uint32_t offset=priority_summary_u32(raw-p->observed_start_raw,p);
    if (!p->success_count) {
        p->first_event=event;p->first_model=model;p->first_success_offset_ticks=offset;
        p->residual_min_ns=lo;p->residual_max_ns=hi;p->min_ppb=p->max_ppb=ppb;
    } else {
        if (lo<p->residual_min_ns) p->residual_min_ns=lo;
        if (hi>p->residual_max_ns) p->residual_max_ns=hi;
        if (ppb<p->min_ppb) p->min_ppb=ppb;
        if (ppb>p->max_ppb) p->max_ppb=ppb;

    }
    if (w->summary.have_success && model!=w->summary.last_model)
        p->model_changes=priority_summary_add16(p->model_changes,1u,p);
    w->summary.last_model=model;
    p->last_event=event;p->last_model=model;p->last_success_offset_ticks=offset;
    const uint32_t compressed=priority_summary_u32(width,p);
    if (compressed>p->max_width_ns) p->max_width_ns=compressed;
    p->success_count=priority_summary_add16(p->success_count,1u,p);
    w->summary.last_success=raw;w->summary.have_success=1u;
}


__attribute__((noinline)) void priority_trace_service_core1(void)
{
    if (get_core_num() != 1u ||
        __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE) != DPLL_CAPTURE_POOL_TYPED) return;
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    vdc_priority_trace_status_t *s = &w->status;
    vdc_priority_trace_request_t command;
    if (priority_trace_request_copy(&command) &&
        command.sequence != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_RELAXED)) {
        if (command.command == VDC_PRIORITY_TRACE_COMMAND_ARM) {
            /* Same Core1 boundary follows every earlier legacy append. The
             * reservation never cancelled a live legacy capture on Core0. */
            const bool origin_invalid = priority_trace_origin_family(command.schema) &&
                !vdc_priority_tx_origin_trace_eligible_core1(command.generation, command.session);
            const bool interval_invalid = !priority_trace_summary_interval_valid(command.schema, command.interval_ms);
            if (s_dpll_capture_armed || origin_invalid || interval_invalid) {
                memset(w, 0, sizeof(*w));
                s->schema = command.schema; s->capture_id = command.capture_id;
                s->state = VDC_PRIORITY_TRACE_REJECTED; s->reason = (origin_invalid || interval_invalid) ? VDC_PRIORITY_TRACE_BINDING : VDC_PRIORITY_TRACE_LEGACY_BUSY;
            } else {
                /* No pool memset: only the next committed record becomes
                 * visible, so ARM work does not scale with capture capacity. */
                /* The pool's old bytes will change format. Retire legacy
                 * completion metadata here, after its sole producer has
                 * quiesced, so RELEASE cannot export typed bytes as schema 6. */
                s_dpll_capture_complete = false;
                s_dpll_capture_count = s_dpll_capture_dropped = 0u;
                s_dpll_capture_first_update_seq = s_dpll_capture_last_update_seq = 0u;
                s_dpll_capture_start_ms = s_dpll_capture_end_ms = 0u;
                memset(w, 0, sizeof(*w));
                s->schema = command.schema; s->capture_id = command.capture_id;
                s->session = command.session; s->generation = command.generation; s->mode = command.mode;
                s->capacity = VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES;
                s->sample_interval_ms = priority_trace_summary_family(command.schema) ? command.interval_ms : (priority_trace_origin_family(command.schema) ? 0u : VDC_PRIORITY_TRACE_MATCH_INTERVAL_MS);
                if (priority_trace_summary_family(command.schema))
                    w->summary.interval_ticks = (uint64_t)BOARD_SYS_CLOCK_HZ * command.interval_ms / 1000u;
                s->state = VDC_PRIORITY_TRACE_ARMED;
                w->boundary_request = command.boundary_request;
            }
            priority_guard_reset_core1(s->state == VDC_PRIORITY_TRACE_ARMED ? command.guard_s : 0u);
        } else if (command.command == VDC_PRIORITY_TRACE_COMMAND_STOP) {
            priority_trace_freeze(VDC_PRIORITY_TRACE_STOP);
        } else if (command.command == VDC_PRIORITY_TRACE_COMMAND_RELEASE) {
            priority_trace_freeze(VDC_PRIORITY_TRACE_RELEASED);
            s->state = VDC_PRIORITY_TRACE_IDLE; s->reason = VDC_PRIORITY_TRACE_RELEASED;
            priority_guard_reset_core1(0u);
        }
        s->request_seq = s->ack_seq = command.sequence; s->command = command.command;
        const bool release_pool = s->state == VDC_PRIORITY_TRACE_REJECTED ||
            command.command == VDC_PRIORITY_TRACE_COMMAND_RELEASE;
        /* No new command can enter between ACK publication and pool return.
         * The public getter withholds this ACK until return is complete. */
        if (release_pool)
            __atomic_store_n(&s_dpll_capture_pool_owner, DPLL_CAPTURE_POOL_RELEASING, __ATOMIC_RELEASE);
        priority_trace_publish();
        /* ACK release makes every record/status write precede Core0 reuse. */
        __atomic_store_n(&s_priority_trace_ack, command.sequence, __ATOMIC_RELEASE);
        if (release_pool)
            __atomic_store_n(&s_dpll_capture_pool_owner, DPLL_CAPTURE_POOL_LEGACY, __ATOMIC_RELEASE);
    }
    if (!priority_trace_active()) return;
    uint32_t request, session, mode;
    int32_t delta;
    if (priority_trace_origin_family(s->schema)) {
        if (vdc_dpll_manager_feedback_session() != s->session ||
            vdc_dpll_manager_priority_sync_generation() != s->generation) {
            priority_trace_freeze(VDC_PRIORITY_TRACE_SESSION); return;
        }
    } else if (vdc_dpll_manager_feedback_session() != s->session ||
        vdc_dpll_manager_priority_match_generation() != s->generation ||
        __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) != s->session) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_SESSION); return;
    }
    if (!priority_trace_origin_family(s->schema) &&
        boundary_request(&request, &session, &delta, &mode) &&
        (request != w->boundary_request || mode != s->mode)) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_MODE); return;
    }
    tdma_ring_clock_snapshot_t *ring = &s_priority_match_work.ring_after;
    if (tdma_runtime_owner_get_ring_clock_snapshot(ring)) {
        if (ring->enabled && ring->adapter_started) {
            if (!w->seen_running) priority_guard_ring_core1(ring->config_seq);
            w->seen_running = 1u; priority_summary_service(false);
        }
        else if (w->seen_running || w->bound) priority_trace_freeze(VDC_PRIORITY_TRACE_STOP);
    }
}

static bool priority_trace_binding(const vdc_priority_match_snapshot_t *m)
{
    const vdc_priority_trace_work_t *w = &s_priority_trace_work;
    const vdc_priority_trace_status_t *s = &w->status;
    const uint64_t arm = ((uint64_t)s->arm_epoch_hi << 32u) | s->arm_epoch_lo;
    return m->session == s->session && m->generation == s->generation &&
        m->ring_config_seq == s->ring_config_seq && m->ring_applied_seq == s->ring_applied_seq &&
        m->local_slot == s->local_slot && m->reference_slot == s->reference_slot &&
        m->node_count == s->node_count && m->schedule_crc32 == s->schedule_crc32 &&
        m->profile_crc32 == s->profile_crc32 && m->path_crc32 == s->path_crc32 &&
        m->delay_ns == s->delay_ns && m->clock_epoch == s->clock_epoch && m->clock_run == s->clock_run &&
        m->arm_epoch == arm && m->observer_epoch == s->observer_epoch && m->rx_epoch == s->rx_epoch &&
        m->tick_hz == s->tick_hz && m->role_generation == w->role_generation &&
        s_vdc_domain.path_delay.table_crc32 == s->path_table_crc32;
}

static void priority_trace_bind(const vdc_priority_match_snapshot_t *m)
{
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    vdc_priority_trace_status_t *s = &w->status;
    s->ring_config_seq = m->ring_config_seq; s->ring_applied_seq = m->ring_applied_seq;
    s->local_slot = m->local_slot; s->reference_slot = m->reference_slot; s->node_count = m->node_count;
    s->schedule_crc32 = m->schedule_crc32; s->profile_crc32 = m->profile_crc32;
    s->path_table_crc32 = s_vdc_domain.path_delay.table_crc32;
    s->path_crc32 = m->path_crc32; s->delay_ns = m->delay_ns;
    s->clock_epoch = m->clock_epoch; s->clock_run = m->clock_run;
    s->arm_epoch_lo = (uint32_t)m->arm_epoch; s->arm_epoch_hi = (uint32_t)(m->arm_epoch >> 32u);
    s->observer_epoch = m->observer_epoch; s->rx_epoch = m->rx_epoch; s->tick_hz = m->tick_hz;
    w->role_generation = m->role_generation; w->bound = 1u; s->state = VDC_PRIORITY_TRACE_RUNNING;
}

static uint8_t *priority_trace_record_begin(uint32_t kind, uint32_t event, uint32_t carrier)
{
    vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    if (!priority_trace_active() ||
        __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE) != DPLL_CAPTURE_POOL_TYPED) return NULL;
    if (s->record_count >= VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES) {
        s->dropped_count = priority_match_inc(s->dropped_count);
        priority_trace_freeze(VDC_PRIORITY_TRACE_FULL); return NULL;
    }
    uint8_t *record = (uint8_t *)&s_dpll_capture_records[s->record_count];
    memset(record, 0, VDC_PRIORITY_TRACE_RECORD_BYTES);
    boundary_capture_u32(record, s->record_count); boundary_capture_u32(record + 4u, kind);
    boundary_capture_u32(record + 8u, board_uptime_ms());
    boundary_capture_u32(record + 12u, event); boundary_capture_u32(record + 16u, carrier);
    return record;
}

static void priority_trace_record_commit(const uint8_t *record, bool decision)
{
    vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    uint32_t now;
    memcpy(&now, record + 8u, 4u);
    if (!s->record_count) s->first_ms = now;
    s->last_ms = now; ++s->record_count;
    if (decision) s->decision_count = priority_match_inc(s->decision_count);
    else s->match_count = priority_match_inc(s->match_count);
    priority_trace_publish();
}

__attribute__((noinline)) void priority_trace_match_core1(void)
{
    if (get_core_num() != 1u || !priority_trace_active() ||
        (s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_PHASE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SUMMARY_WINDOW_PHASE_SCHEMA)) return;
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    const vdc_priority_match_work_t *match = &s_priority_match_work;
    const vdc_priority_match_snapshot_t *m = &match->status;
    if (m->generation == w->status.generation && m->retired) {
        priority_trace_freeze(m->last_reason == VDC_PRIORITY_MATCH_STOP ?
            VDC_PRIORITY_TRACE_STOP : VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (!match->fresh) return;
    if (!m->active || m->retired || !m->have_match || m->last_reason != VDC_PRIORITY_MATCH_OK ||
        m->session != w->status.session || m->generation != w->status.generation) return;
    if (!w->bound) priority_trace_bind(m);
    else if (!priority_trace_binding(m)) { priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return; }
    if (w->have_match && m->event_sequence <= w->last_match_sequence) return;
    w->last_match_sequence = m->event_sequence;
    if (priority_trace_summary_family(w->status.schema)) {
        priority_summary_success(m->event_sequence,m->model_token,match->model.dco.period_adjust_ppb,
            m->residual_lo,m->residual_hi,(uint64_t)m->residual_hi-(uint64_t)m->residual_lo);
        w->have_match=1u; return;
    }
    const uint64_t period = (uint64_t)m->tick_hz * VDC_PRIORITY_TRACE_MATCH_INTERVAL_MS / 1000u;
    if (w->have_match && (m->raw_lo < w->last_sample_raw || m->raw_lo - w->last_sample_raw < period)) {
        w->status.skipped_count = priority_match_inc(w->status.skipped_count); return;
    }
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_MATCH, m->event_sequence, m->carrier_sequence);
    if (!p) return;
    boundary_capture_u64(p + 20u, m->raw_lo); boundary_capture_u64(p + 28u, m->raw_hi);
    boundary_capture_u64(p + 36u, (uint64_t)m->residual_lo);
    boundary_capture_u64(p + 44u, (uint64_t)m->residual_hi);
    boundary_capture_u64(p + 52u, m->local_lo); boundary_capture_u64(p + 60u, m->local_hi);
    boundary_capture_u64(p + 68u, m->remote_lo); boundary_capture_u64(p + 76u, m->remote_hi);
    boundary_capture_u32(p + 84u, m->model_token);
    boundary_capture_u32(p + 88u, match->model.dco.dco_update_seq);
    boundary_capture_u32(p + 92u, (uint32_t)match->model.dco.period_adjust_ppb);
    boundary_capture_u32(p + 96u, m->last_reason);
    w->have_match = 1u; w->last_sample_raw = m->raw_lo;
    priority_trace_record_commit(p, false);
}

void priority_trace_decision_core1(const vdc_priority_follow_snapshot_t *decision)
{
    if (get_core_num() != 1u || !priority_trace_active() || !decision ||
        (s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_PHASE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA &&
         s_priority_trace_work.status.schema != VDC_PRIORITY_TRACE_SUMMARY_WINDOW_PHASE_SCHEMA)) return;
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    if (!w->bound || decision->session != w->status.session || decision->generation != w->status.generation ||
        !priority_trace_binding(&s_priority_follow_work.ticket.match)) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (w->have_decision && decision->event_sequence <= w->last_decision_sequence) return;
    if (priority_trace_summary_family(w->status.schema)) {
        vdc_priority_summary_record_t *p=priority_summary_record();
        if (p) {
            p->decision_count=priority_summary_add16(p->decision_count,1u,p);
            if (decision->after_dco_seq!=decision->before_dco_seq)
                p->frequency_applied_count=priority_summary_add16(p->frequency_applied_count,1u,p);
        }
        w->have_decision=1u;w->last_decision_sequence=decision->event_sequence;return;
    }
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_DECISION,
        decision->event_sequence, decision->carrier_sequence);
    if (!p) return;
    boundary_capture_u32(p + 20u, decision->baseline_sequence);
    boundary_capture_u32(p + 24u, decision->model_token);
    boundary_capture_u32(p + 28u, decision->before_dco_seq);
    boundary_capture_u32(p + 32u, decision->after_dco_seq);
    boundary_capture_u32(p + 36u, (uint32_t)decision->before_ppb);
    boundary_capture_u32(p + 40u, (uint32_t)decision->after_ppb);
    boundary_capture_u32(p + 44u, (uint32_t)decision->selected_delta_ppb);
    boundary_capture_u32(p + 48u, decision->last_reason);
    boundary_capture_u64(p + 52u, (uint64_t)decision->error_lo_ppb);
    boundary_capture_u64(p + 60u, (uint64_t)decision->error_hi_ppb);
    boundary_capture_u64(p + 68u, decision->interval_lo); boundary_capture_u64(p + 76u, decision->interval_hi);
    boundary_capture_u64(p + 84u, decision->local_interval_lo); boundary_capture_u64(p + 92u, decision->local_interval_hi);
    w->have_decision = 1u; w->last_decision_sequence = decision->event_sequence;
    priority_trace_record_commit(p, true);
}


/* A complete consecutive origin history, beginning at an acknowledged empty
 * sync generation. This hook runs only after successful encoding. */
__attribute__((noinline)) void priority_trace_origin_core1(
    const vdc_priority_tx_origin_evidence_t *e)
{
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    vdc_priority_trace_status_t *s = &w->status;
    if (get_core_num() != 1u || !e || !priority_trace_active() ||
        !priority_trace_origin_family(s->schema)) return;
    if (e->session != s->session || e->generation != s->generation ||
        (w->bound && (e->role_generation != w->role_generation ||
            e->source_epoch != w->boundary_request || e->clock_epoch != s->clock_epoch ||
            e->clock_run != s->clock_run || e->tick_hz != s->tick_hz ||
            e->local_slot != s->local_slot || e->node_count != s->node_count ||
            e->schedule_crc32 != s->schedule_crc32 || e->profile_crc32 != s->profile_crc32))) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (!w->bound) {
        w->role_generation = e->role_generation; w->boundary_request = e->source_epoch;
        s->clock_epoch = e->clock_epoch; s->clock_run = e->clock_run; s->tick_hz = e->tick_hz;
        s->local_slot = e->local_slot; s->node_count = e->node_count;
        s->schedule_crc32 = e->schedule_crc32; s->profile_crc32 = e->profile_crc32;
        w->bound = 1u; s->state = VDC_PRIORITY_TRACE_RUNNING;
    } else if (e->event_sequence <= w->last_match_sequence) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (priority_trace_summary_family(s->schema)) {
        priority_summary_success(e->event_sequence,e->projection.model.token,
            e->projection.model.dco.period_adjust_ppb,0,0,e->encoded_width);
        w->last_match_sequence=e->event_sequence;return;
    }
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_ORIGIN, 0u, 0u);
    if (!p) return;
    const vdc_dco_control_t *dco = &e->projection.model.dco;
    boundary_capture_u32(p + 8u, e->event_sequence);
    boundary_capture_u32(p + 12u, e->projection.model.token);
    boundary_capture_u32(p + 16u, e->tick_hz);
    boundary_capture_u64(p + 20u, e->source_raw_lo); boundary_capture_u64(p + 28u, e->source_raw_hi);
    boundary_capture_u64(p + 36u, e->projection.raw_now);
    boundary_capture_u64(p + 44u, e->projection.local_lo);
    boundary_capture_u64(p + 52u, e->projection.local_hi);
    boundary_capture_u64(p + 60u, dco->base_local_tick64);
    boundary_capture_u64(p + 68u, dco->base_vdc_time64_ns);
    boundary_capture_u32(p + 76u, (uint32_t)dco->period_adjust_ppb);
    boundary_capture_u32(p + 80u, (uint32_t)dco->phase_offset_ns);
    boundary_capture_u32(p + 84u, dco->dco_update_seq);
    boundary_capture_u64(p + 88u, e->encoded_output_lo);
    boundary_capture_u32(p + 96u, e->encoded_width);
    uint8_t *h = w->origin;
    if (!s->record_count) {
        boundary_capture_u32(h, e->role_generation); boundary_capture_u32(h + 4u, e->source_epoch);
        boundary_capture_u32(h + 8u, e->source_identity);
        boundary_capture_u32(h + 16u, e->published_version);
        s->first_ms = board_uptime_ms();
    }
    boundary_capture_u32(h + 12u, e->source_identity);
    boundary_capture_u32(h + 20u, e->published_version);
    boundary_capture_u32(h + 24u, e->event_sequence);
    w->last_match_sequence = e->event_sequence;
    s->last_ms = board_uptime_ms(); ++s->record_count;
    if (s->record_count == VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES)
        priority_trace_freeze(VDC_PRIORITY_TRACE_FULL);
    else priority_trace_publish();
}

/* Only a receipt-confirmed full model publication may produce PHASE. */
void priority_trace_phase_core1(const vdc_priority_phase_snapshot_t *phase)
{
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    if (get_core_num() != 1u || !phase || !priority_trace_active()) return;
    if (w->status.schema == VDC_PRIORITY_TRACE_SCHEMA) {
        /* A STOP reconfiguration after trace ARM cannot silently reinterpret
         * legacy frequency records once a real phase translation occurs. */
        priority_trace_freeze(VDC_PRIORITY_TRACE_MODE); return;
    }
    if (w->status.schema != VDC_PRIORITY_TRACE_PHASE_SCHEMA &&
        w->status.schema != VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA &&
        w->status.schema != VDC_PRIORITY_TRACE_SUMMARY_WINDOW_PHASE_SCHEMA) return;
    if (!w->bound || phase->session != w->status.session ||
        phase->generation != w->status.generation ||
        !priority_trace_binding(&s_priority_follow_work.ticket.match)) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (priority_trace_summary_family(w->status.schema)) {
        vdc_priority_summary_record_t *p=priority_summary_record();
        if (p) p->phase_applied_count=priority_summary_add16(p->phase_applied_count,1u,p);
        return;
    }
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_PHASE,
        phase->event_sequence, phase->carrier_sequence);
    if (!p) return;
    boundary_capture_u32(p + 20u, phase->before_model);
    boundary_capture_u32(p + 24u, phase->after_model);
    boundary_capture_u32(p + 28u, phase->before_dco);
    boundary_capture_u32(p + 32u, phase->after_dco);
    boundary_capture_u32(p + 36u, phase->rate_epoch);
    boundary_capture_u32(p + 40u, phase->reason);
    boundary_capture_u64(p + 44u, (uint64_t)phase->delta_ns);
    boundary_capture_u64(p + 52u, (uint64_t)phase->cumulative_ns);
    boundary_capture_u64(p + 60u, (uint64_t)phase->residual_lo);
    boundary_capture_u64(p + 68u, (uint64_t)phase->residual_hi);
    boundary_capture_u64(p + 76u, phase->before_base_vdc);
    boundary_capture_u64(p + 84u, phase->after_base_vdc);
    boundary_capture_u64(p + 92u, phase->raw_lo);
    priority_trace_record_commit(p, true);
}

static bool priority_trace_read_frozen(uint32_t capture_id, uint32_t offset,
    uint8_t *data, uint32_t size, uint32_t *total_bytes, uint32_t *file_crc32)
{
    struct { vdc_priority_trace_header_t base; uint8_t origin[VDC_PRIORITY_TRACE_ORIGIN_EXTENSION_BYTES]; } file;
    vdc_priority_trace_header_t *header = &file.base;
    tdma_ring_clock_snapshot_t ring;
    if (!vdc_dpll_manager_get_priority_trace(&header->status) ||
        header->status.state != VDC_PRIORITY_TRACE_FROZEN || header->status.capture_id != capture_id ||
        !capture_id || header->status.request_seq != header->status.ack_seq ||
        header->status.ack_seq != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE) ||
        header->status.record_count > VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled || ring.adapter_started ||
        ring.config_seq != ring.applied_config_seq) return false;
    const uint32_t config = ring.config_seq;
    const uint32_t header_size = sizeof(*header) + (header->status.schema == VDC_PRIORITY_TRACE_ORIGIN_SCHEMA ? sizeof(file.origin) : 0u);
    if (header->status.schema == VDC_PRIORITY_TRACE_ORIGIN_SCHEMA)
        memcpy(file.origin, s_priority_trace_work.origin, sizeof(file.origin));
    const uint32_t bytes = header->status.record_count * VDC_PRIORITY_TRACE_RECORD_BYTES;
    const uint32_t total = header_size + bytes;
    if (offset > total || size > total - offset) return false;
    header->magic = VDC_PRIORITY_TRACE_MAGIC; header->schema = header->status.schema;
    header->header_size = header_size; header->record_size = VDC_PRIORITY_TRACE_RECORD_BYTES;
    header->payload_crc32 = ota_crc32_compute((const uint8_t *)s_dpll_capture_records, bytes);
    uint32_t crc = ota_crc32_update(0u, (const uint8_t *)&file, header_size);
    crc = ota_crc32_update(crc, (const uint8_t *)s_dpll_capture_records, bytes);
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled || ring.adapter_started ||
        ring.config_seq != config || ring.applied_config_seq != config) return false;
    uint32_t copied = 0u;
    if (offset < header_size) {
        copied = header_size - offset;
        if (copied > size) copied = size;
        memcpy(data, (const uint8_t *)&file + offset, copied);
    }
    if (copied < size) memcpy(data + copied, (const uint8_t *)s_dpll_capture_records +
        offset + copied - header_size, size - copied);
    *total_bytes = total; *file_crc32 = crc; return true;
}

bool vdc_dpll_manager_priority_trace_read(uint32_t capture_id, uint32_t offset,
    uint8_t *data, uint32_t size, uint32_t *total_bytes, uint32_t *file_crc32)
{
    if (get_core_num() != 0u || !data || !total_bytes || !file_crc32 || !size ||
        size > VDC_PRIORITY_TRACE_READ_MAX_BYTES) return false;
    /* Exclude a submission while proving no command is in flight. Taking a
     * READ lease from RUNNING/pending state could otherwise cross a Core1
     * command that already passed its owner check and is about to RELEASE. */
    uint32_t guard = __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE);
    if ((guard & 1u) || guard > UINT32_MAX - 2u ||
        !__atomic_compare_exchange_n(&s_priority_trace_request_guard, &guard, guard + 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    vdc_priority_trace_status_t status;
    uint32_t expected = DPLL_CAPTURE_POOL_TYPED;
    const bool leased = priority_trace_status_copy(&status) &&
        status.state == VDC_PRIORITY_TRACE_FROZEN && status.capture_id == capture_id &&
        status.ack_seq == __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE) &&
        status.ack_seq == __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_RELAXED) &&
        __atomic_compare_exchange_n(&s_dpll_capture_pool_owner, &expected,
            DPLL_CAPTURE_POOL_TYPED_READ, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    __atomic_store_n(&s_priority_trace_request_guard, guard + 2u, __ATOMIC_RELEASE);
    if (!leased) return false;
    const bool result = priority_trace_read_frozen(capture_id, offset, data, size, total_bytes, file_crc32);
    expected = DPLL_CAPTURE_POOL_TYPED_READ;
    return __atomic_compare_exchange_n(&s_dpll_capture_pool_owner, &expected,
        DPLL_CAPTURE_POOL_TYPED, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED) && result;
}

/* Opt-in diagnostic supervision. Core1 alone classifies observations; Core0
 * requests local retirement. No live record export or realtime host traffic. */
static vdc_priority_guard_status_t s_priority_guard_work;
static uint64_t s_priority_guard_start;
static uint32_t s_priority_guard_started, s_priority_guard_sequence;
static uint32_t s_priority_guard_words[sizeof(vdc_priority_guard_status_t)/4u];
/* Core0-only bookkeeping, always matched to capture identity. */
static uint32_t s_priority_guard_stop_capture, s_priority_guard_stop_config;
static uint32_t s_priority_guard_ring_retired, s_priority_guard_output_retired;

static void priority_guard_output(uint64_t now, bool initial)
{
    vdc_priority_guard_status_t *g=&s_priority_guard_work;
    vdc_run_output_observation_t o;
    if (!vdc_run_output_observe_core1(&o)) {
        g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_READ;
        return;
    }
    const uint64_t prior=((uint64_t)g->output_ordinal_hi<<32u)|g->output_ordinal_lo;
    g->output_state=o.state; g->output_reason=o.reason;
    g->output_ordinal_lo=(uint32_t)o.last_ordinal;
    g->output_ordinal_hi=(uint32_t)(o.last_ordinal>>32u);
    if (initial) g->output_request=o.request;
    if (!o.request || o.request!=g->output_request || o.session!=g->session ||
        (!initial && o.ring_config!=g->ring_config_seq))
        g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_IDENTITY;
    /* The first observation may precede first-block admission. Minute
     * checkpoints require running output, a fresh owner and real admission
     * progress. Never drive/service PIO from the diagnostic observer. */
    if (initial) return;
    if (o.state!=SYNC_IO_RUN_OUTPUT_RUNNING || o.reason!=SYNC_IO_RUN_OUTPUT_OK)
        g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_STOPPED;
    if (!o.service_observations || o.service_last_tick>now || o.submit_last_tick>now ||
        now-o.service_last_tick>BOARD_SYS_CLOCK_HZ || now-o.submit_last_tick>BOARD_SYS_CLOCK_HZ)
        g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_STALE;
    if (o.last_ordinal<=prior) g->reason_mask |= VDC_PRIORITY_GUARD_OUTPUT_PROGRESS;
}

static void priority_guard_publish(void)
{
    const uint32_t seq = __atomic_load_n(&s_priority_guard_sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_guard_sequence, seq+1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    boundary_store(s_priority_guard_words, &s_priority_guard_work, sizeof(s_priority_guard_work));
    __atomic_store_n(&s_priority_guard_sequence, seq+2u, __ATOMIC_RELEASE);
}

static bool priority_guard_copy(vdc_priority_guard_status_t *out)
{
    const uint32_t seq = __atomic_load_n(&s_priority_guard_sequence, __ATOMIC_ACQUIRE);
    if (seq & 1u) return false;
    match_load_words(s_priority_guard_words, out, sizeof(*out));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return seq == __atomic_load_n(&s_priority_guard_sequence, __ATOMIC_ACQUIRE);
}

static void priority_guard_reset_core1(uint32_t duration_s)
{
    const vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    s_priority_guard_work = (vdc_priority_guard_status_t){
        .schema=VDC_PRIORITY_GUARD_SCHEMA, .capture_id=s->capture_id,
        .session=s->session, .generation=s->generation, .target_s=duration_s,
        .state=duration_s ? VDC_PRIORITY_GUARD_ARMED : VDC_PRIORITY_GUARD_DISABLED};
    s_priority_guard_start=0u; s_priority_guard_started=0u;
    priority_guard_publish();
}

static bool priority_guard_steady_gaps(void)
{
    return s_priority_guard_work.target_s != 0u;
}

static void priority_guard_note_bin(uint32_t flags, uint32_t success_gap, bool nonempty)
{
    vdc_priority_guard_status_t *g=&s_priority_guard_work;
    if (!g->target_s || g->state>=VDC_PRIORITY_GUARD_PASS) return;
    /* A zero-length terminal record claims no new coverage. Existing clock,
     * service, saturation and counter failures remain failures even there. */
    uint32_t reasons=flags & (SUMMARY_SERVICE_GAP|SUMMARY_COUNTER_RESET|
        SUMMARY_COUNTER_SATURATED|SUMMARY_FIELD_SATURATED|SUMMARY_CLOCK_INVALID);
    if (nonempty) reasons |= flags & (SUMMARY_NO_SUCCESS|SUMMARY_UNBOUND);
    if (success_gap>BOARD_SYS_CLOCK_HZ) reasons |= VDC_PRIORITY_GUARD_SUCCESS_GAP;
    g->reason_mask |= reasons;
}

static void priority_guard_ring_core1(uint32_t config_seq)
{
    /* Called exactly once before trace seen_running; config zero is valid. */
    if (s_priority_guard_work.state==VDC_PRIORITY_GUARD_ARMED) {
        s_priority_guard_work.ring_config_seq=config_seq;
        priority_guard_publish();
    }
}

void priority_guard_service_core1(void)
{
    vdc_priority_guard_status_t *g=&s_priority_guard_work;
    vdc_priority_trace_work_t *w=&s_priority_trace_work;
    if (get_core_num()!=1u || !g->target_s || g->state>=VDC_PRIORITY_GUARD_PASS) return;
    if (!w->seen_running) return; /* ARM alone is not elapsed run time. */
    if (!s_priority_guard_started) {
        /* First bin may already be frozen by a startup failure. */
        const vdc_priority_summary_record_t *first=(const void *)&s_dpll_capture_records[0];
        if (!w->summary.open && !w->status.record_count) {
            g->reason_mask |= VDC_PRIORITY_GUARD_COVERAGE;
            g->state=VDC_PRIORITY_GUARD_FAIL; priority_guard_publish(); return;
        }
        s_priority_guard_start=first->observed_start_raw;
        s_priority_guard_started=1u; g->state=VDC_PRIORITY_GUARD_RUNNING;
        priority_guard_output(s_priority_guard_start,true);
        priority_guard_publish();
    }
    tdma_ring_clock_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) {
        g->reason_mask |= VDC_PRIORITY_GUARD_RING_READ;
    } else if (ring.config_seq!=g->ring_config_seq) {
        g->reason_mask |= VDC_PRIORITY_GUARD_COVERAGE;
    }
    uint64_t now;
    if (!priority_summary_now(&now) || now<s_priority_guard_start) {
        g->reason_mask |= SUMMARY_CLOCK_INVALID;
        g->state=VDC_PRIORITY_GUARD_FAIL;
    } else {
        const uint64_t ms=(now-s_priority_guard_start)/(BOARD_SYS_CLOCK_HZ/1000u);
        g->elapsed_ms=ms>UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
        if (w->status.state==VDC_PRIORITY_TRACE_FROZEN) {
            g->reason_mask |= VDC_PRIORITY_GUARD_EARLY_FREEZE;
            g->state=VDC_PRIORITY_GUARD_FAIL;
        } else if (g->elapsed_ms >= (g->checked_s+VDC_PRIORITY_GUARD_CHECKPOINT_S)*1000u) {
            priority_guard_output(now,false);
            /* Include the open partial bin: bin closure is not a minute
             * boundary and cannot hide a current gap or missing input. */
            vdc_priority_summary_record_t *p=priority_summary_record();
            if (p) priority_guard_note_bin(p->flags | (!p->success_count ? SUMMARY_NO_SUCCESS : 0u) |
                (!w->bound ? SUMMARY_UNBOUND : 0u), p->max_success_gap_ticks,
                p->observed_end_raw>p->observed_start_raw);
            else g->reason_mask |= VDC_PRIORITY_GUARD_COVERAGE;
            if (g->elapsed_ms >= (g->checked_s+2u*VDC_PRIORITY_GUARD_CHECKPOINT_S)*1000u)
                g->reason_mask |= VDC_PRIORITY_GUARD_COVERAGE;
            g->checked_s += VDC_PRIORITY_GUARD_CHECKPOINT_S;
            if (!g->reason_mask && g->checked_s>=g->target_s) {
                /* Seal while still RUNNING so the terminal clock/counter
                 * observations can veto PASS through note_bin. This is a
                 * local recorder boundary, not a ring/output STOP. */
                priority_summary_terminal();
                const uint32_t count=w->status.record_count;
                if (w->summary.open || !count || count>w->status.capacity ||
                    !(((const vdc_priority_summary_record_t *)&s_dpll_capture_records[count-1u])->flags & SUMMARY_TERMINAL))
                    g->reason_mask |= VDC_PRIORITY_GUARD_COVERAGE;
                priority_trace_freeze(g->reason_mask ? VDC_PRIORITY_TRACE_BINDING :
                    VDC_PRIORITY_TRACE_TARGET_COMPLETE);
            }
            if (g->reason_mask) g->state=VDC_PRIORITY_GUARD_FAIL;
            else {
                g->passed_mask |= 1u << (g->checked_s/VDC_PRIORITY_GUARD_CHECKPOINT_S-1u);
                if (g->checked_s>=g->target_s) g->state=VDC_PRIORITY_GUARD_PASS;
            }
            if (g->state==VDC_PRIORITY_GUARD_FAIL) g->first_failure_ms=g->elapsed_ms;
            priority_guard_publish();
        }
    }
    if (g->state==VDC_PRIORITY_GUARD_FAIL) {
        if (!g->first_failure_ms) g->first_failure_ms=g->elapsed_ms;
        priority_guard_publish();
    }
}

/* Called inside the TDMA control guard: no new ARM can slip between identity
 * validation, output cancellation and the ring disable publication. */
static bool priority_guard_cancel_current(void *context)
{
    const vdc_priority_guard_status_t *g=context;
    vdc_priority_trace_status_t trace;
    if (!vdc_dpll_manager_get_priority_trace(&trace) || trace.request_seq!=trace.ack_seq ||
        trace.capture_id!=g->capture_id || trace.session!=g->session ||
        trace.generation!=g->generation || vdc_dpll_manager_feedback_session()!=g->session) return false;
    vdc_run_output_cancel();
    return true;
}

void priority_guard_service_core0(void)
{
    vdc_priority_guard_status_t g;
    if (get_core_num()!=0u || !s_vdc_tdma_service || !priority_guard_copy(&g) ||
        g.state!=VDC_PRIORITY_GUARD_FAIL) return;
    if (g.capture_id==s_priority_guard_stop_capture) {
        /* Retain proof that this exact STOP retired, even after an ordinary
         * cleanup STOP advances config again. Never borrow a later run. */
        tdma_ring_clock_snapshot_t ring;
        if (tdma_runtime_owner_get_ring_clock_snapshot(&ring) &&
            ring.config_seq==s_priority_guard_stop_config && !ring.enabled) {
            if (!ring.adapter_started && ring.applied_config_seq==ring.config_seq)
                s_priority_guard_ring_retired=1u;
            if (vdc_run_output_configuration_idle()) s_priority_guard_output_retired=1u;
        }
        return;
    }
    uint32_t stopped=0u;
    if (tdma_service_ring_stop_if_current(s_vdc_tdma_service, g.ring_config_seq,
            priority_guard_cancel_current, &g, &stopped)) {
        s_priority_guard_stop_config=stopped;
        s_priority_guard_stop_capture=g.capture_id;
        s_priority_guard_ring_retired=s_priority_guard_output_retired=0u;
    }
}

bool vdc_dpll_manager_get_priority_guard(vdc_priority_guard_status_t *out)
{
    if (get_core_num()!=0u || !out) return false;
    vdc_priority_guard_status_t g;
    vdc_priority_trace_status_t trace;
    if (!priority_guard_copy(&g) || !vdc_dpll_manager_get_priority_trace(&trace) ||
        trace.request_seq!=trace.ack_seq || trace.capture_id!=g.capture_id ||
        trace.session!=g.session || trace.generation!=g.generation) return false;
    if (g.capture_id && g.capture_id==s_priority_guard_stop_capture) {
        g.stop_accepted=1u; g.stop_config_seq=s_priority_guard_stop_config;
        g.ring_retired=s_priority_guard_ring_retired;
        g.output_retired=s_priority_guard_output_retired;
        tdma_ring_clock_snapshot_t ring;
        if (tdma_runtime_owner_get_ring_clock_snapshot(&ring) &&
            ring.config_seq==g.stop_config_seq && !ring.enabled) {
            g.ring_retired|=!ring.adapter_started && ring.applied_config_seq==ring.config_seq;
            g.output_retired|=vdc_run_output_configuration_idle();
        }
    }
    *out=g; return true;
}
