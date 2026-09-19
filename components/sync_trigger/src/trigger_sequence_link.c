#include "trigger_sequence_link.h"
#include <stddef.h>
#include <string.h>
#include "osal.h"
#include "refmem_application_model.h"
#include "refmem_realtime_contract.h"
#include "tdma_runtime_owner.h"
#include "sync_io_sequence.h"
#include "board_config.h"
#include "vdc_timestamp_clock.h"

enum { LINK_OFF, LINK_WAIT_START, LINK_WAIT_APPLIED, LINK_WAIT_READY,
       LINK_WAIT_RETURN, LINK_WAIT_STEP, LINK_PAUSED, LINK_FAULT, LINK_DONE,
       LINK_WAIT_COUNT, LINK_WAIT_COUNTER_RETURN, LINK_WAIT_REARM };
enum { LINK_OK, LINK_CONFIG_CHANGED, LINK_TIMEOUT, LINK_PROTOCOL, LINK_OWNER,
       LINK_COUNTER_BUSY, LINK_COUNTER_OVERFLOW, LINK_MAILBOX_FULL, LINK_CLOCK,
       LINK_TRANSPORT };
static trigger_sequence_link_status_t s_link;
static trigger_sequence_link_tx_t s_tx;
/* The Core1 flight phase is the only transport producer/consumer. LINK
 * runtime never edits its assembler/cursor. The copied offer is immutable
 * until the next publish. */
static trigger_sequence_link_tx_t s_tx_offer, s_transport_tx;
static bool s_tx_offer_enabled;
static trigger_sequence_link_rx_t s_transport_rx;
static uint32_t s_transport_run, s_transport_generation, s_transport_binding;
_Static_assert(TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY > 0u &&
    TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY < UINT32_MAX / 2u, "bounded inbox");
static trigger_sequence_link_message_t s_inbox[TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY];
static uint32_t s_inbox_written, s_inbox_read, s_inbox_overflows;
static uint32_t s_transport_tx_count, s_transport_rejected;
static uint32_t s_seen_overflows, s_overflow_run, s_overflow_generation, s_overflow_binding;
static uint32_t s_tx_count_baseline;
static bool s_action_submitted;
static bool s_retry_valid;
static trigger_sequence_link_message_t s_retry_message;
static uint16_t s_token;
static uint64_t s_phase_at, s_now_ticks;
static uint32_t s_ready_baseline;
/* Core0 publishes bounded READY credits. Core1 is the sole consumer and the
 * only caller of the realtime sequence owner action. */
static uint32_t s_ready_credit_count;
static uint32_t s_ready_credit_run, s_ready_credit_generation, s_ready_credit_binding;
static uint32_t s_resume_phase;
static uint32_t s_rearm_baseline;
static bool s_tx_enabled;
static uint32_t s_guard;
static uint32_t s_configuring;
static trigger_sequence_link_status_t s_published;
/* Run identity and threshold are immutable for the retained window. Avoid
 * duplicating them per record in the board's constrained static SRAM. */
typedef struct {
    uint32_t ordinal, position, observed_pulses, exchange_id;
    uint32_t trigger_ordinal, ready_ordinal;
    uint32_t position_admitted_tick_ms, sample_done_tick_ms, cycle_elapsed_ms;
    uint16_t sequence_index, sequence_state, output_code, outcome_flags;
    uint32_t timing_flags;
    uint64_t timing_ticks[TRIGGER_SEQUENCE_LINK_TIME_COUNT];
} history_entry_t;
_Static_assert(TRIGGER_SEQUENCE_STATE_MAX <= UINT16_MAX, "history sequence index");
_Static_assert(sizeof(history_entry_t) == 96u, "compact history SRAM budget");
_Static_assert(sizeof(trigger_sequence_link_history_status_t) == 40u, "history vector metadata");
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0), "history atomics must not use locks");
enum { HISTORY_WORDS = sizeof(history_entry_t) / sizeof(uint32_t),
       HISTORY_STATUS_WORDS = sizeof(trigger_sequence_link_history_status_t) / sizeof(uint32_t) };
/* HAOFV Trigger diagnostic vector: Core1 sole writer; atomic32 payload words
 * avoid data races and torn uint64 marks. Values are TIMER1 offsets and run
 * identities, never physical-edge timestamps. No reader handshake/lock.
 * Core0 configuration does not mutate this vector. New Core1 run resets the
 * visible window; STOP/fault preserve partial flags and completed records. */
static struct {
    uint32_t sequence;
    uint32_t status[HISTORY_STATUS_WORDS];
    uint32_t records[TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY][HISTORY_WORDS];
} s_history_vector = {.status = {TRIGGER_SEQUENCE_LINK_HISTORY_VECTOR_VERSION,
    BOARD_SYS_CLOCK_HZ, TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY}};

static void history_load_words(void *out, const uint32_t *words, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        const uint32_t word = __atomic_load_n(&words[i], __ATOMIC_RELAXED);
        memcpy((unsigned char *)out + i * sizeof(word), &word, sizeof(word));
    }
}
static void history_store_words(uint32_t *words, const void *value, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        uint32_t word;
        memcpy(&word, (const unsigned char *)value + i * sizeof(word), sizeof(word));
        __atomic_store_n(&words[i], word, __ATOMIC_RELAXED);
    }
}
static void history_write_begin(void)
{
    const uint32_t seq = __atomic_load_n(&s_history_vector.sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&s_history_vector.sequence, seq + 1u, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
}
static void history_write_end(void)
{
    const uint32_t seq = __atomic_load_n(&s_history_vector.sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&s_history_vector.sequence, seq + 1u, __ATOMIC_RELEASE);
}
static void history_store_status(void)
{
    const trigger_sequence_link_history_status_t status = {
        TRIGGER_SEQUENCE_LINK_HISTORY_VECTOR_VERSION, BOARD_SYS_CLOCK_HZ,
        TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY, s_link.run_id, s_link.generation,
        s_link.binding_epoch, s_link.config.counter_threshold, s_link.history_total,
        s_link.history_retained, s_link.history_total - s_link.history_retained};
    history_store_words(s_history_vector.status, &status, HISTORY_STATUS_WORDS);
}
static void history_reset(void)
{
    s_link.history_total = s_link.history_retained = 0u;
    history_write_begin();
    history_store_status();
    history_write_end();
}
static void history_store_entry(const history_entry_t *entry)
{
    history_write_begin();
    history_store_words(s_history_vector.records[(entry->ordinal - 1u) %
        TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY], entry, HISTORY_WORDS);
    /* Run identity remains the one admitted by history_reset(), including
     * across stopped configuration changes. Only occupancy changes here. */
    const uint32_t occupancy[] = {s_link.history_total, s_link.history_retained,
        s_link.history_total - s_link.history_retained};
    history_store_words(s_history_vector.status +
        offsetof(trigger_sequence_link_history_status_t, total) / sizeof(uint32_t),
        occupancy, sizeof(occupancy) / sizeof(occupancy[0]));
    history_write_end();
}
static history_entry_t history_current_entry(void)
{
    history_entry_t entry;
    history_load_words(&entry, s_history_vector.records[(s_link.history_total - 1u) %
        TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY], HISTORY_WORDS);
    return entry;
}
static uint64_t s_position_admitted_ticks;
static uint64_t s_message_started_ticks, s_message_tx_ticks, s_message_rx_ticks;
static bool s_clock_fault;
typedef struct {
    uint32_t config_seq, node_count, local_slot, reference_slot;
    uint32_t ring_crc, schedule_crc, operating_crc, cycle_ns, baud_hz;
    uint32_t up_group, down_group, flags, adapter_start_count;
} transport_binding_t;
/* START publishes one immutable transport identity with the role binding.
 * Sequence run/step and physical ring sequence remain separate identities. */
static transport_binding_t s_start_transport;
static uint32_t s_start_transport_binding;
static bool s_transport_ready;
static bool s_transport_snapshot_waiting;
static uint64_t s_transport_unavailable_since;

static bool transport_snapshot_valid(const tdma_ring_runtime_snapshot_t *ring)
{
    return ring->enabled && ring->adapter_started && ring->data_enabled &&
        ring->up_running && ring->down_running &&
        ring->config_seq == ring->applied_config_seq && ring->node_count &&
        ring->node_count <= REFMEM_APP_MODEL_NODE_COUNT &&
        ring->local_slot_id < ring->node_count &&
        ring->reference_slot_id < ring->node_count && ring->ring_profile_crc32 &&
        ring->schedule_crc32 && ring->operating_profile_crc32 &&
        ring->cycle_period_ns && ring->baud_hz;
}

static transport_binding_t transport_binding(const tdma_ring_runtime_snapshot_t *ring)
{
    return (transport_binding_t){ring->config_seq, ring->node_count,
        ring->local_slot_id, ring->reference_slot_id, ring->ring_profile_crc32,
        ring->schedule_crc32, ring->operating_profile_crc32, ring->cycle_period_ns,
        ring->baud_hz, ring->up_group_id, ring->down_group_id, ring->flags,
        ring->adapter_start_count};
}

/* A temporarily unavailable seqlock snapshot defers work, never authorizes it. */
enum { TRANSPORT_BUSY, TRANSPORT_CURRENT, TRANSPORT_CHANGED };
static unsigned transport_current(uint32_t binding_epoch, uint32_t *local_slot)
{
    transport_binding_t expected;
    osal_critical_enter();
    expected = s_start_transport;
    const bool prepared = s_start_transport_binding == binding_epoch;
    osal_critical_exit();
    if (!prepared) return TRANSPORT_CHANGED;
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring)) return TRANSPORT_BUSY;
    const transport_binding_t current = transport_binding(&ring);
    if (!transport_snapshot_valid(&ring) ||
        memcmp(&expected, &current, sizeof(expected)) != 0) return TRANSPORT_CHANGED;
    if (local_slot) *local_slot = current.local_slot;
    return TRANSPORT_CURRENT;
}

_Static_assert(BOARD_SYS_CLOCK_HZ % 1000u == 0u, "TIMER1 ticks per millisecond");
static uint32_t elapsed_ms(uint64_t end, uint64_t start)
{
    const uint64_t ms = (end - start) / (BOARD_SYS_CLOCK_HZ / 1000u);
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}
static uint32_t timestamp_ms(uint64_t ticks)
{ return (uint32_t)(ticks / (BOARD_SYS_CLOCK_HZ / 1000u)); }

static bool take(void)
{ return __atomic_exchange_n(&s_guard, 1u, __ATOMIC_ACQUIRE) == 0u; }
static void publish(void)
{
    osal_critical_enter();
    s_published = s_link;
    s_tx_offer = s_tx;
    s_tx_offer_enabled = s_tx_enabled;
    osal_critical_exit();
}
static void release(void)
{
    publish();
    __atomic_store_n(&s_guard, 0u, __ATOMIC_RELEASE);
}

static bool role_present(uint32_t slot, uint32_t role, uint32_t type)
{
    const refmem_node_load_table_t *loads = refmem_application_model_get_node_load_table();
    const refmem_fb_instance_table_t *instances = refmem_application_model_get_fb_instance_table();
    uint32_t found = 0u;
    for (uint32_t i = 0; i < loads->load_count; ++i) {
        const refmem_node_load_entry_t *l = &loads->load[i];
        if (!l->enabled || l->node_id != slot || !(l->role_mask & role)) continue;
        bool valid = false;
        for (uint32_t j = 0; j < instances->instance_count; ++j) {
            const refmem_fb_instance_entry_t *fb = &instances->instance[j];
            if (fb->instance_id == l->instance_id && fb->fb_type == type && fb->enable_condition)
                valid = true;
        }
        if (!valid || ++found != 1u) return false;
    }
    return found == 1u;
}

static bool binding_model_valid(const trigger_sequence_link_config_t *config, uint32_t model_epoch)
{
    return model_epoch == refmem_realtime_contract_origin_model_epoch() &&
        role_present(config->dut_slot, REFMEM_APP_ROLE_LINK_SWITCHER, REFMEM_APP_FB_LINK_SWITCHER) &&
       role_present(config->vna_slot, REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER,
                     REFMEM_APP_FB_INSTRUMENT_CONTROLLER) &&
        (!config->counter_enabled || role_present(config->counter_slot,
            REFMEM_APP_ROLE_PULSE_DISTRIBUTOR, REFMEM_APP_FB_PULSE_COUNTER));
}

/* Full model/role validation belongs to the Core0 START guard. During RUN
 * only the atomic epoch is inspected; no mutable model pointers cross cores. */
static bool model_valid(void)
{ return s_link.model_epoch == refmem_realtime_contract_origin_model_epoch(); }

static void discard_inbox(void)
{
    __atomic_store_n(&s_inbox_read,
        __atomic_load_n(&s_inbox_written, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
    s_seen_overflows = __atomic_load_n(&s_inbox_overflows, __ATOMIC_ACQUIRE);
    s_retry_valid = false;
}

static void discard_ready_credits(void)
{ __atomic_store_n(&s_ready_credit_count, 0u, __ATOMIC_RELEASE); }

static bool start_guard(void)
{
    /* Runtime service may be preempted with s_guard held. START only needs
     * the frozen binding, published synchronously before configure returns.
     * USB and RS485 can enter SCPI from different Core0 tasks: reject an
     * in-progress configuration, without claiming a guard-to-enqueue lock. */
    if (__atomic_load_n(&s_configuring, __ATOMIC_ACQUIRE)) return false;
    trigger_sequence_link_config_t config;
    uint32_t model_epoch, binding_epoch;
    osal_critical_enter();
    config = s_published.config;
    model_epoch = s_published.model_epoch;
    binding_epoch = s_published.binding_epoch;
    osal_critical_exit();
    tdma_ring_runtime_snapshot_t ring;
    uint64_t clock_ticks;
    bool valid = vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &clock_ticks) &&
        config.enabled && binding_model_valid(&config, model_epoch) &&
        tdma_runtime_owner_get_ring_snapshot(&ring) && transport_snapshot_valid(&ring);
    osal_critical_enter();
    valid = valid && binding_epoch == s_published.binding_epoch &&
        model_epoch == refmem_realtime_contract_origin_model_epoch();
    if (valid) {
        s_start_transport = transport_binding(&ring);
        s_start_transport_binding = binding_epoch;
    }
    osal_critical_exit();
    return valid && !__atomic_load_n(&s_configuring, __ATOMIC_ACQUIRE) &&
        model_epoch == refmem_realtime_contract_origin_model_epoch();
}

static bool dispatch_transport_action(bool (*action)(void), bool *result)
{
    transport_binding_t expected;
    osal_critical_enter();
    expected = s_start_transport;
    const bool prepared = s_start_transport_binding == s_published.binding_epoch;
    osal_critical_exit();
    return prepared && tdma_runtime_owner_run_bound_action(expected.config_seq,
        expected.adapter_start_count, action, result);
}

static trigger_sequence_link_config_result_t configure(
    const trigger_sequence_link_config_t *config, trigger_sequence_link_config_diagnostic_t *diagnostic)
{
    if (!config || (config->counter_enabled && !config->enabled))
        return TRIGGER_SEQUENCE_LINK_CONFIG_INVALID;
    if (trigger_sequence_service_is_active()) return TRIGGER_SEQUENCE_LINK_CONFIG_ACTIVE;
    if (s_link.binding_epoch == UINT32_MAX) return TRIGGER_SEQUENCE_LINK_CONFIG_EPOCH_EXHAUSTED;
    if (config->enabled &&
        (config->dut_slot >= REFMEM_APP_MODEL_NODE_COUNT ||
         config->vna_slot >= REFMEM_APP_MODEL_NODE_COUNT || config->dut_slot == config->vna_slot ||
         config->timeout_ms == 0u || config->timeout_ms > INT32_MAX ||
         !role_present(config->dut_slot, REFMEM_APP_ROLE_LINK_SWITCHER, REFMEM_APP_FB_LINK_SWITCHER) ||
         !role_present(config->vna_slot, REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER,
                       REFMEM_APP_FB_INSTRUMENT_CONTROLLER))) return TRIGGER_SEQUENCE_LINK_CONFIG_ROLE_INVALID;
    if (config->counter_enabled &&
        (config->counter_slot >= REFMEM_APP_MODEL_NODE_COUNT ||
         config->counter_slot == config->dut_slot || config->counter_slot == config->vna_slot ||
         config->counter_input == 0u || config->counter_input > 4u ||
         config->counter_input == config->ready_input || config->counter_threshold == 0u ||
         config->counter_threshold >= SYNC_IO_SEQUENCE_COUNTER_LIMIT ||
         !role_present(config->counter_slot, REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
                       REFMEM_APP_FB_PULSE_COUNTER))) return TRIGGER_SEQUENCE_LINK_CONFIG_COUNTER_INVALID;
    trigger_sequence_gateway_config_t gateway = {
        .enabled = config->enabled, .ready_input = config->ready_input,
        .counter_input = config->counter_enabled ? config->counter_input : 0u,
        .counter_threshold = config->counter_enabled ? config->counter_threshold : 0u,
        .falling = config->falling, .trigger_output_mask = config->trigger_output_mask,
        .pulse_us = config->pulse_us
    };
    /* Validate before changing the transport's stopped configuration. */
    if (config->enabled && (config->ready_input > 4u ||
        config->trigger_output_mask == 0u || config->trigger_output_mask > 15u ||
        (config->trigger_output_mask & (config->trigger_output_mask - 1u)) != 0u ||
        config->pulse_us == 0u || config->pulse_us > SYNC_IO_SEQUENCE_TIME_MAX_US))
        return TRIGGER_SEQUENCE_LINK_CONFIG_GATEWAY_INVALID;
    diagnostic->tdma_result = tdma_runtime_owner_set_local_return_delivery_checked(config->enabled);
    if (diagnostic->tdma_result != TDMA_STOPPED_CONFIG_OK)
        return TRIGGER_SEQUENCE_LINK_CONFIG_TDMA_REJECTED;
    diagnostic->gateway_result = trigger_sequence_service_set_gateway_locked(&gateway, start_guard);
    if (diagnostic->gateway_result != TRIGGER_SEQUENCE_SERVICE_OK) {
        diagnostic->rollback_result = tdma_runtime_owner_set_local_return_delivery_checked(s_link.config.enabled);
        return TRIGGER_SEQUENCE_LINK_CONFIG_GATEWAY_REJECTED;
    }
    uint32_t epoch = s_link.binding_epoch + 1u;
    trigger_sequence_service_set_transport_action_locked(
        config->enabled ? dispatch_transport_action : NULL);
    memset(&s_link, 0, sizeof(s_link));
    s_link.config = *config;
    s_link.binding_epoch = epoch;
    s_link.model_epoch = refmem_realtime_contract_origin_model_epoch();
    s_link.phase = config->enabled ? LINK_WAIT_START : LINK_OFF;
    s_tx_count_baseline = __atomic_load_n(&s_transport_tx_count, __ATOMIC_ACQUIRE);
    (void)__atomic_exchange_n(&s_transport_rejected, 0u, __ATOMIC_ACQ_REL);
    s_tx_enabled = false;
    discard_inbox();
    discard_ready_credits();
    return TRIGGER_SEQUENCE_LINK_CONFIG_OK;
}

static void fail(uint32_t error)
{
    s_link.error = error;
    s_link.phase = LINK_FAULT;
    s_tx_enabled = false;
    discard_ready_credits();
    if (trigger_sequence_service_stop() == TRIGGER_SEQUENCE_SERVICE_OK) s_action_submitted = true;
}

static void publish_message(uint32_t kind)
{
    if ((kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED ||
         kind == TRIGGER_SEQUENCE_LINK_COUNTER_NEXT) && ++s_link.exchange_id == 0u)
        ++s_link.exchange_id;
    trigger_sequence_link_message_t message = {
        .kind = kind, .run_id = s_link.run_id, .generation = s_link.generation,
        .binding_epoch = s_link.binding_epoch, .step_ordinal = s_link.step,
        .source_slot = kind == TRIGGER_SEQUENCE_LINK_COUNTER_NEXT ? s_link.config.counter_slot :
            (kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED ? s_link.config.dut_slot : s_link.config.vna_slot),
        .target_slot = kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED ? s_link.config.vna_slot : s_link.config.dut_slot,
        .exchange_id = s_link.exchange_id
    };
    if (++s_token == 0u) ++s_token; /* Assembly hint only; full identity guards execution. */
    if (!trigger_sequence_link_tx_begin(&s_tx, &message, s_token)) { fail(LINK_PROTOCOL); return; }
    s_tx_enabled = true;
    s_phase_at = s_now_ticks;
    s_message_started_ticks = s_phase_at;
    s_message_tx_ticks = s_message_rx_ticks = 0u;
}

static bool record_position_step(const trigger_sequence_service_status_t *owner,
    uint32_t target_step, uint32_t exchange_id)
{
    if (!s_link.config.counter_enabled) return true;
    if (s_link.history_total == UINT32_MAX) { fail(LINK_COUNTER_OVERFLOW); return false; }
    const uint32_t ordinal = ++s_link.history_total;
    if (s_link.history_retained < TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY) ++s_link.history_retained;
    uint64_t request_ticks;
    const bool timed = vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &request_ticks) &&
        request_ticks >= s_position_admitted_ticks;
    const history_entry_t record = {
        .ordinal = ordinal,
        .position = s_link.counter_consumed,
        .sequence_index = owner->count ? target_step % owner->count : 0u,
        .observed_pulses = owner->counter_events,
        .exchange_id = exchange_id,
        .position_admitted_tick_ms = timestamp_ms(s_position_admitted_ticks),
        .sequence_state = UINT16_MAX,
        .output_code = UINT16_MAX,
        .outcome_flags = TRIGGER_SEQUENCE_LINK_HISTORY_REQUESTED,
        .timing_flags = timed ? 1u << TRIGGER_SEQUENCE_LINK_TIME_REQUEST : 0u,
        .timing_ticks = {timed ? request_ticks - s_position_admitted_ticks : 0u},
    };
    history_store_entry(&record);
    return true;
}

static void record_time(history_entry_t *record, trigger_sequence_link_time_t stage,
    uint64_t ticks)
{
    /* Caller edits a private Core1 record before publication. A zero offset
     * is valid; flags, rather than timestamp values, identify missing marks. */
    if (ticks < s_position_admitted_ticks) return;
    record->timing_ticks[stage] = ticks - s_position_admitted_ticks;
    record->timing_flags |= 1u << stage;
}

static void record_fire_queued(void)
{
    if (!s_link.config.counter_enabled || !s_link.history_total) return;
    uint64_t queued_ticks;
    const bool timed = vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &queued_ticks);
    history_entry_t record = history_current_entry();
    record_time(&record, TRIGGER_SEQUENCE_LINK_TIME_OFFERED, s_message_tx_ticks);
    record_time(&record, TRIGGER_SEQUENCE_LINK_TIME_RETURNED, s_message_rx_ticks);
    if (timed) record_time(&record, TRIGGER_SEQUENCE_LINK_TIME_FIRE_QUEUED, queued_ticks);
    history_store_entry(&record);
}

static void record_outcome(uint32_t flags,
    const trigger_sequence_service_status_t *owner)
{
    if (!s_link.config.counter_enabled || !s_link.history_total) return;
    uint64_t observed_ticks;
    const bool timed = vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &observed_ticks);
    history_entry_t entry = history_current_entry();
    history_entry_t *record = &entry;
    record->outcome_flags |= flags;
    if ((flags & TRIGGER_SEQUENCE_LINK_HISTORY_APPLIED) && owner != NULL) {
        if (timed) record_time(record, TRIGGER_SEQUENCE_LINK_TIME_APPLIED, observed_ticks);
        uint32_t output_code;
        record->sequence_state = owner->current_state <= UINT16_MAX ?
            (uint16_t)owner->current_state : UINT16_MAX;
        record->output_code = trigger_sequence_service_get_code(
            owner->current_state, &output_code) && output_code <= UINT16_MAX ?
            (uint16_t)output_code : UINT16_MAX;
    }
    if ((flags & TRIGGER_SEQUENCE_LINK_HISTORY_SAMPLE_DONE) && owner != NULL) {
        if (timed) record_time(record, TRIGGER_SEQUENCE_LINK_TIME_DONE, observed_ticks);
        record->trigger_ordinal = owner->gateway_trigger_count;
        record->ready_ordinal = owner->gateway_ready_count;
        const uint64_t completed_ticks = timed && observed_ticks >= s_position_admitted_ticks ?
            observed_ticks : s_now_ticks;
        record->sample_done_tick_ms = timestamp_ms(completed_ticks);
        record->cycle_elapsed_ms = elapsed_ms(completed_ticks, s_position_admitted_ticks);
    }
    history_store_entry(record);
}

static uint32_t next_link_exchange_id(void)
{
    const uint32_t next = s_link.exchange_id + 1u;
    return next == 0u ? 1u : next;
}

static bool submit_next_step(const trigger_sequence_service_status_t *owner)
{
    const trigger_sequence_service_result_t result = trigger_sequence_service_cycle_step(
        s_link.run_id, s_link.generation, s_link.step);
    if (result == TRIGGER_SEQUENCE_SERVICE_BUSY) return false;
    if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
        fail(LINK_OWNER);
        return false;
    }
    s_action_submitted = true;
    if (!record_position_step(owner, s_link.step + 1u, next_link_exchange_id()))
        return false;
    s_link.phase = LINK_WAIT_STEP;
    s_phase_at = s_now_ticks;
    return true;
}

static void advance_after_sample(const trigger_sequence_service_status_t *owner)
{
    if (s_link.phase != LINK_WAIT_RETURN) return;
    if (s_link.repeat_count != 0u && (uint64_t)s_link.step + 1u >=
        (uint64_t)owner->count * s_link.repeat_count) {
        const trigger_sequence_service_result_t result = trigger_sequence_service_cycle_finish(
            s_link.run_id, s_link.generation, s_link.step);
        if (result == TRIGGER_SEQUENCE_SERVICE_BUSY) return;
        if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
            fail(LINK_OWNER);
            return;
        }
        s_action_submitted = true;
        s_link.phase = LINK_DONE;
        s_tx_enabled = false;
        discard_ready_credits();
        return;
    }
    if (s_link.config.counter_enabled && owner->count != 0u &&
        ((uint64_t)s_link.step + 1u) % owner->count == 0u) {
        const trigger_sequence_service_result_t result = trigger_sequence_service_counter_rearm(
            s_link.run_id, s_link.generation, s_link.step);
        if (result == TRIGGER_SEQUENCE_SERVICE_BUSY) return;
        if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
            fail(LINK_OWNER);
            return;
        }
        s_action_submitted = true;
        s_rearm_baseline = owner->counter_rearm_count;
        s_link.phase = LINK_WAIT_REARM;
        s_phase_at = s_now_ticks;
        s_tx_enabled = false;
        return;
    }
    (void)submit_next_step(owner);
}

/* Pulses continue accumulating during measurement. Reaching the next
 * position before this full plan completes is an error, never a queue. */
static bool observe_counter(const trigger_sequence_service_status_t *owner)
{
    if (!s_link.config.counter_enabled) return true;
    const uint32_t previous = s_link.counter_events;
    s_link.counter_events = owner->counter_events;
    if (owner->counter_events < previous) {
        s_link.counter_fault_events = owner->counter_events;
        fail(LINK_COUNTER_OVERFLOW);
        return false;
    }
    const uint32_t delta = owner->counter_events - previous;
    if (!delta) return true;
    const bool waiting = s_link.phase == LINK_WAIT_COUNT;
    const uint32_t remaining = s_link.config.counter_threshold - s_link.counter_partial;
    if ((!waiting && delta >= remaining) ||
        (waiting && (uint64_t)delta >= (uint64_t)remaining + s_link.config.counter_threshold)) {
        s_link.counter_fault_events = owner->counter_events;
        fail(LINK_COUNTER_BUSY);
        return false;
    }
    s_link.counter_partial += delta;
    return true;
}

static void service(void)
{
    if (!s_link.config.enabled) return;
    trigger_sequence_service_status_t owner;
    trigger_sequence_service_get_status(&owner);
    s_link.triggers = owner.gateway_trigger_count;
    s_link.ready = owner.gateway_ready_count;
    s_link.completed = owner.completed;
    if (s_link.config.counter_enabled && owner.state == TRIGGER_SEQUENCE_SERVICE_FAULT) {
        if (owner.run_id != s_link.run_id) {
            s_link.run_id = owner.run_id;
            s_link.generation = owner.generation;
            s_link.repeat_count = owner.repeat_count;
            s_link.step = owner.completed;
            s_link.counter_consumed = s_link.counter_partial = 0u;
            history_reset();
        }
        s_link.counter_events = s_link.counter_fault_events = owner.counter_events;
        s_link.error = owner.backend_fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY ?
            LINK_COUNTER_BUSY : (owner.backend_fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_OVERFLOW ||
            owner.backend_fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_REGRESSION ? LINK_COUNTER_OVERFLOW : LINK_OWNER);
        s_link.phase = LINK_FAULT;
        s_tx_enabled = false;
        return;
    }
    if (trigger_sequence_service_stop_pending() ||
        owner.state == TRIGGER_SEQUENCE_SERVICE_IDLE || owner.state == TRIGGER_SEQUENCE_SERVICE_STOPPING ||
        owner.state == TRIGGER_SEQUENCE_SERVICE_FAULT) {
        discard_ready_credits();
        if (s_link.config.counter_enabled && owner.run_id == s_link.run_id &&
            owner.counter_events >= s_link.counter_events) {
            s_link.counter_events = owner.counter_events;
            s_link.counter_partial = owner.counter_events % s_link.config.counter_threshold;
        }
        s_tx_enabled = false;
        discard_inbox();
        s_clock_fault = false;
        s_transport_snapshot_waiting = false;
        s_now_ticks = 0u;
        if (s_link.phase != LINK_FAULT && s_link.phase != LINK_DONE) s_link.phase = LINK_WAIT_START;
        return;
    }
    if (!model_valid()) { fail(LINK_CONFIG_CHANGED); return; }
    if ((s_link.phase == LINK_FAULT || s_link.phase == LINK_DONE) && owner.run_id == s_link.run_id) return;
    uint64_t now;
    if (s_clock_fault || !vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &now) ||
        now < s_now_ticks || (s_tx_enabled &&
            (now < s_message_tx_ticks || now < s_message_rx_ticks))) {
        fail(LINK_CLOCK);
        return;
    }
    s_now_ticks = now;
    const unsigned transport = transport_current(s_link.binding_epoch, NULL);
    if (transport != TRANSPORT_CURRENT) {
        if (!s_transport_snapshot_waiting) {
            s_transport_snapshot_waiting = true;
            s_transport_unavailable_since = s_now_ticks;
        }
        if (transport == TRANSPORT_CHANGED || s_now_ticks - s_transport_unavailable_since >
                (uint64_t)s_link.config.timeout_ms * (BOARD_SYS_CLOCK_HZ / 1000u))
            fail(LINK_TRANSPORT);
        return;
    }
    s_transport_snapshot_waiting = false;
    s_transport_ready = true;
    if (s_link.phase == LINK_WAIT_REARM && owner.counter_rearm_count != s_rearm_baseline) {
        s_link.phase = LINK_WAIT_COUNT;
        s_tx_enabled = false;
    }
    if (owner.state == TRIGGER_SEQUENCE_SERVICE_PAUSING || owner.state == TRIGGER_SEQUENCE_SERVICE_PAUSED) {
        if (s_link.phase != LINK_PAUSED) s_resume_phase = s_link.phase;
        if (s_link.config.counter_enabled && s_resume_phase != LINK_WAIT_COUNT) {
            fail(LINK_OWNER); return;
        }
        if (s_link.config.counter_enabled && !observe_counter(&owner)) return;
        if (s_link.config.counter_enabled && s_link.counter_partial >= s_link.config.counter_threshold) {
            s_link.counter_fault_events = owner.counter_events;
            fail(LINK_COUNTER_BUSY); return;
        }
        s_tx_enabled = false;
        s_link.phase = LINK_PAUSED;
        return;
    }
    if (owner.state == TRIGGER_SEQUENCE_SERVICE_STARTING) return;
    if (s_link.config.counter_enabled && s_link.phase == LINK_PAUSED &&
        owner.run_id == s_link.run_id) {
        if (owner.state != TRIGGER_SEQUENCE_SERVICE_READY || owner.accepted != owner.completed) return;
        /* The IO owner rejects boundaries during pause; a resumed boundary is valid. */
        s_link.phase = LINK_WAIT_COUNT;
        if (!observe_counter(&owner)) return;
        s_link.step = owner.completed;
        s_link.generation = owner.generation;
        discard_inbox();
        s_link.phase = LINK_WAIT_COUNT;
    }
    if (owner.run_id != s_link.run_id || s_link.phase == LINK_PAUSED ||
        s_link.phase == LINK_WAIT_START || s_link.phase == LINK_FAULT) {
        if (owner.state != TRIGGER_SEQUENCE_SERVICE_READY || owner.accepted != owner.completed) return;
        if (owner.run_id != s_link.run_id) discard_ready_credits();
        s_link.error = LINK_OK;
        s_link.run_id = owner.run_id;
        s_link.generation = owner.generation;
        s_link.repeat_count = owner.repeat_count;
        s_link.step = owner.completed;
        discard_inbox();
        if (s_link.config.counter_enabled) {
            s_link.counter_events = s_link.counter_consumed = s_link.counter_partial = 0u;
            s_link.counter_fault_events = 0u;
            history_reset();
            s_link.phase = LINK_WAIT_COUNT;
            s_tx_enabled = false;
        } else {
            s_link.phase = LINK_WAIT_APPLIED;
            publish_message(TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
        }
    }
    if (!observe_counter(&owner)) return;
    if (s_link.phase == LINK_WAIT_COUNT &&
        s_link.counter_partial >= s_link.config.counter_threshold) {
        if (s_link.counter_consumed == UINT32_MAX) { fail(LINK_COUNTER_OVERFLOW); return; }
        ++s_link.counter_consumed;
        s_link.counter_partial -= s_link.config.counter_threshold;
        s_position_admitted_ticks = s_now_ticks;
        if (s_link.counter_consumed == 1u && s_link.step == 0u) {
            s_link.phase = LINK_WAIT_APPLIED;
            publish_message(TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
            if (s_link.phase != LINK_FAULT &&
                record_position_step(&owner, s_link.step, s_link.exchange_id))
                record_outcome(TRIGGER_SEQUENCE_LINK_HISTORY_APPLIED, &owner);
        } else {
            (void)submit_next_step(&owner);
        }
    }
    if (s_link.phase == LINK_WAIT_READY && s_link.config.ready_input == 0u &&
        owner.gateway_waiting && !owner.gateway_pulse_busy) {
        const uint32_t credits = __atomic_load_n(&s_ready_credit_count, __ATOMIC_ACQUIRE);
        const bool identity_matches = credits != 0u &&
            __atomic_load_n(&s_ready_credit_run, __ATOMIC_RELAXED) == s_link.run_id &&
            __atomic_load_n(&s_ready_credit_generation, __ATOMIC_RELAXED) == s_link.generation &&
            __atomic_load_n(&s_ready_credit_binding, __ATOMIC_RELAXED) == s_link.binding_epoch;
        if (credits != 0u && !identity_matches) {
            discard_ready_credits();
        } else if (identity_matches) {
            const trigger_sequence_service_result_t result = trigger_sequence_service_gateway_ready(
                s_link.run_id, s_link.generation, s_link.step);
            if (result == TRIGGER_SEQUENCE_SERVICE_OK) {
                (void)__atomic_sub_fetch(&s_ready_credit_count, 1u, __ATOMIC_ACQ_REL);
                s_action_submitted = true;
            } else if (result != TRIGGER_SEQUENCE_SERVICE_BUSY &&
                       result != TRIGGER_SEQUENCE_SERVICE_NOT_READY) {
                fail(LINK_OWNER);
                return;
            }
        }
    }
    if (s_link.phase == LINK_WAIT_READY && owner.gateway_ready_count > s_ready_baseline &&
        !owner.gateway_pulse_busy && !owner.gateway_waiting) {
        s_link.phase = LINK_WAIT_RETURN;
        if (s_link.config.counter_enabled) {
            s_tx_enabled = false;
            record_outcome(TRIGGER_SEQUENCE_LINK_HISTORY_SAMPLE_DONE, &owner);
        } else {
            publish_message(TRIGGER_SEQUENCE_LINK_READY_NEXT);
        }
    } else if (s_link.phase == LINK_WAIT_STEP && owner.state == TRIGGER_SEQUENCE_SERVICE_READY &&
               owner.completed == s_link.step + 1u && owner.accepted == owner.completed) {
        s_link.step = owner.completed;
        record_outcome(TRIGGER_SEQUENCE_LINK_HISTORY_APPLIED, &owner);
        s_link.phase = LINK_WAIT_APPLIED;
        publish_message(TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
    }
    if (s_link.config.counter_enabled) advance_after_sample(&owner);
    const bool manual_ready_wait =
        s_link.phase == LINK_WAIT_READY && s_link.config.ready_input == 0u;
    if (!manual_ready_wait && s_link.phase != LINK_WAIT_COUNT && s_link.phase != LINK_DONE &&
        s_now_ticks - s_phase_at >
            (uint64_t)s_link.config.timeout_ms * (BOARD_SYS_CLOCK_HZ / 1000u))
        fail(LINK_TIMEOUT);
}

/* Core1 only. A busy owner mailbox defers this exact immutable message;
 * owner STOP is serviced before any deferred action on the next short step. */
static void receive_message(trigger_sequence_link_message_t message)
{
    if (!model_valid() || message.run_id != s_link.run_id || message.generation != s_link.generation ||
        message.binding_epoch != s_link.binding_epoch || message.step_ordinal != s_link.step ||
        message.exchange_id != s_link.exchange_id) {
        ++s_link.rejected; return;
    }
    s_link.offer_delay_ms = elapsed_ms(s_message_tx_ticks, s_message_started_ticks);
    s_link.return_delay_ms = elapsed_ms(s_message_rx_ticks, s_message_tx_ticks);
    s_link.inbox_delay_ms = elapsed_ms(s_now_ticks, s_message_rx_ticks);
    s_link.message_total_ms = elapsed_ms(s_now_ticks, s_message_started_ticks);
    trigger_sequence_service_status_t owner;
    trigger_sequence_service_get_status(&owner);
    if (!observe_counter(&owner)) return;
    if (message.kind == TRIGGER_SEQUENCE_LINK_COUNTER_NEXT &&
        s_link.config.counter_enabled && s_link.phase == LINK_WAIT_COUNTER_RETURN &&
        message.source_slot == s_link.config.counter_slot && message.target_slot == s_link.config.dut_slot) {
        if (s_link.counter_consumed == 1u && s_link.step == 0u) {
            if (!record_position_step(&owner, s_link.step, message.exchange_id)) return;
            record_outcome(TRIGGER_SEQUENCE_LINK_HISTORY_APPLIED, &owner);
            s_link.phase = LINK_WAIT_APPLIED;
            publish_message(TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
        } else {
            trigger_sequence_service_result_t result = trigger_sequence_service_cycle_step(
                message.run_id, message.generation, message.step_ordinal);
            if (result == TRIGGER_SEQUENCE_SERVICE_BUSY) { s_retry_valid = true; return; }
            if (result == TRIGGER_SEQUENCE_SERVICE_OK) {
                s_action_submitted = true;
                if (!record_position_step(&owner, s_link.step + 1u,
                        message.exchange_id)) return;
                s_link.phase = LINK_WAIT_STEP;
                s_phase_at = s_now_ticks;
            } else fail(LINK_OWNER);
        }
    } else if (message.kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED && s_link.phase == LINK_WAIT_APPLIED &&
        message.source_slot == s_link.config.dut_slot && message.target_slot == s_link.config.vna_slot) {
        trigger_sequence_service_result_t result = trigger_sequence_service_gateway_fire(
            message.run_id, message.generation, message.step_ordinal);
        if (result == TRIGGER_SEQUENCE_SERVICE_BUSY) { s_retry_valid = true; return; }
        if (result == TRIGGER_SEQUENCE_SERVICE_OK) {
            record_fire_queued();
            s_action_submitted = true;
            s_ready_baseline = owner.gateway_ready_count;
            s_link.phase = LINK_WAIT_READY;
            s_phase_at = s_now_ticks;
        } else fail(LINK_OWNER);
    } else if (message.kind == TRIGGER_SEQUENCE_LINK_READY_NEXT && s_link.phase == LINK_WAIT_RETURN &&
               message.source_slot == s_link.config.vna_slot && message.target_slot == s_link.config.dut_slot) {
        record_outcome(TRIGGER_SEQUENCE_LINK_HISTORY_SAMPLE_DONE, &owner);
        if (s_link.repeat_count != 0u && (uint64_t)s_link.step + 1u >=
            (uint64_t)owner.count * s_link.repeat_count) {
            trigger_sequence_service_result_t result = trigger_sequence_service_cycle_finish(
                message.run_id, message.generation, message.step_ordinal);
            if (result == TRIGGER_SEQUENCE_SERVICE_BUSY) { s_retry_valid = true; return; }
            s_link.phase = LINK_DONE;
            s_tx_enabled = false;
            discard_ready_credits();
            if (result != TRIGGER_SEQUENCE_SERVICE_OK) fail(LINK_OWNER);
            else s_action_submitted = true;
            return;
        }
        if (s_link.config.counter_enabled && owner.count != 0u &&
            ((uint64_t)s_link.step + 1u) % owner.count == 0u) {
            s_rearm_baseline = owner.counter_rearm_count;
            trigger_sequence_service_result_t result = trigger_sequence_service_counter_rearm(
                s_link.run_id, s_link.generation, s_link.step);
            if (result == TRIGGER_SEQUENCE_SERVICE_BUSY) { s_retry_valid = true; return; }
            if (result != TRIGGER_SEQUENCE_SERVICE_OK) { fail(LINK_OWNER); return; }
            s_action_submitted = true;
            s_link.phase = LINK_WAIT_REARM;
            s_phase_at = s_now_ticks;
            s_tx_enabled = false;
            return;
        }
        trigger_sequence_service_result_t result = trigger_sequence_service_cycle_step(
            message.run_id, message.generation, message.step_ordinal);
        if (result == TRIGGER_SEQUENCE_SERVICE_BUSY) { s_retry_valid = true; return; }
        if (result == TRIGGER_SEQUENCE_SERVICE_OK) {
            s_action_submitted = true;
            if (!record_position_step(&owner, s_link.step + 1u,
                    message.exchange_id)) return;
            s_link.phase = LINK_WAIT_STEP;
            s_phase_at = s_now_ticks;
        } else fail(LINK_OWNER);
    } else ++s_link.rejected;
}

void trigger_sequence_link_get_status(trigger_sequence_link_status_t *status)
{
    if (!status) return;
    osal_critical_enter();
    *status = s_published;
    osal_critical_exit();
}
bool trigger_sequence_link_binding_is_current(uint32_t binding_epoch, uint32_t model_epoch)
{
    if (__atomic_load_n(&s_configuring, __ATOMIC_ACQUIRE)) return false;
    osal_critical_enter();
    const bool current = binding_epoch != 0u && s_published.config.enabled &&
        binding_epoch == s_published.binding_epoch && model_epoch == s_published.model_epoch;
    osal_critical_exit();
    return current && model_epoch == refmem_realtime_contract_origin_model_epoch() &&
        !__atomic_load_n(&s_configuring, __ATOMIC_ACQUIRE);
}
bool trigger_sequence_link_get_history(uint32_t ordinal, trigger_sequence_link_history_t *record)
{
    if (!record || ordinal == 0u) return false;
    for (unsigned attempt = 0; attempt < TRIGGER_SEQUENCE_LINK_HISTORY_SNAPSHOT_ATTEMPTS; ++attempt) {
        const uint32_t seq = __atomic_load_n(&s_history_vector.sequence, __ATOMIC_ACQUIRE);
        if (seq & 1u) continue;
        trigger_sequence_link_history_status_t status;
        history_entry_t entry;
        history_load_words(&status, s_history_vector.status, HISTORY_STATUS_WORDS);
        history_load_words(&entry, s_history_vector.records[(ordinal - 1u) %
            TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY], HISTORY_WORDS);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (seq != __atomic_load_n(&s_history_vector.sequence, __ATOMIC_RELAXED)) continue;
        if (ordinal > status.total || status.total - ordinal >= status.retained ||
            entry.ordinal != ordinal) return false;
        trigger_sequence_link_history_t result = {
            .ordinal = entry.ordinal, .run_id = status.run_id, .generation = status.generation,
            .binding_epoch = status.binding_epoch, .exchange_id = entry.exchange_id,
            .position = entry.position, .sequence_index = entry.sequence_index,
            .sequence_state = entry.sequence_state == UINT16_MAX ? UINT32_MAX : entry.sequence_state,
            .output_code = entry.output_code == UINT16_MAX ? UINT32_MAX : entry.output_code,
            .threshold_pulses = entry.position * status.threshold,
            .observed_pulses = entry.observed_pulses,
            .trigger_ordinal = entry.trigger_ordinal, .ready_ordinal = entry.ready_ordinal,
            .position_admitted_tick_ms = entry.position_admitted_tick_ms,
            .sample_done_tick_ms = entry.sample_done_tick_ms,
            .cycle_elapsed_ms = entry.cycle_elapsed_ms,
            .outcome_flags = entry.outcome_flags,
            .timing_flags = entry.timing_flags,
        };
        memcpy(result.timing_ticks, entry.timing_ticks, sizeof(result.timing_ticks));
        *record = result;
        return true;
    }
    return false;
}
bool trigger_sequence_link_get_history_status(trigger_sequence_link_history_status_t *status)
{
    if (!status) return false;
    for (unsigned attempt = 0; attempt < TRIGGER_SEQUENCE_LINK_HISTORY_SNAPSHOT_ATTEMPTS; ++attempt) {
        const uint32_t seq = __atomic_load_n(&s_history_vector.sequence, __ATOMIC_ACQUIRE);
        if (seq & 1u) continue;
        trigger_sequence_link_history_status_t result;
        history_load_words(&result, s_history_vector.status, HISTORY_STATUS_WORDS);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (seq != __atomic_load_n(&s_history_vector.sequence, __ATOMIC_RELAXED)) continue;
        *status = result;
        return true;
    }
    return false;
}
bool trigger_sequence_link_configure(const trigger_sequence_link_config_t *config)
{
    return trigger_sequence_link_configure_checked(config).result == TRIGGER_SEQUENCE_LINK_CONFIG_OK;
}
trigger_sequence_link_config_diagnostic_t trigger_sequence_link_configure_checked(
    const trigger_sequence_link_config_t *config)
{
    trigger_sequence_link_config_diagnostic_t diagnostic = {0};
    if (trigger_sequence_service_is_active()) {
        diagnostic.result = TRIGGER_SEQUENCE_LINK_CONFIG_ACTIVE;
        return diagnostic;
    }
    if (!trigger_sequence_service_configuration_begin()) {
        diagnostic.result = TRIGGER_SEQUENCE_LINK_CONFIG_SERVICE_BUSY;
        return diagnostic;
    }
    if (!take()) {
        trigger_sequence_service_configuration_end();
        diagnostic.result = TRIGGER_SEQUENCE_LINK_CONFIG_WRITER_BUSY;
        return diagnostic;
    }
    __atomic_store_n(&s_configuring, 1u, __ATOMIC_RELEASE);
    diagnostic.result = configure(config, &diagnostic);
    publish();
    /* Clear before releasing the sole-writer lock, so a subsequent
     * configure cannot have its in-progress flag cleared by this call. */
    __atomic_store_n(&s_configuring, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_guard, 0u, __ATOMIC_RELEASE);
    trigger_sequence_service_configuration_end();
    return diagnostic;
}
bool trigger_sequence_link_configure_position_locked(
    const trigger_sequence_link_config_t *config, uint32_t repeat_count)
{
    if (!config || !config->enabled || !config->counter_enabled || !repeat_count || !take())
        return false;
    __atomic_store_n(&s_configuring, 1u, __ATOMIC_RELEASE);
    trigger_sequence_link_config_diagnostic_t diagnostic = {0};
    const bool result = configure(config, &diagnostic) == TRIGGER_SEQUENCE_LINK_CONFIG_OK;
    if (result) trigger_sequence_service_set_repeat_locked(repeat_count);
    publish();
    __atomic_store_n(&s_configuring, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_guard, 0u, __ATOMIC_RELEASE);
    return result;
}
trigger_sequence_service_result_t trigger_sequence_link_next(void)
{
    trigger_sequence_link_status_t view;
    trigger_sequence_link_get_status(&view);
    if (view.phase != LINK_WAIT_READY || !view.config.enabled || view.config.ready_input != 0u)
        return TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    if (__atomic_load_n(&s_ready_credit_count, __ATOMIC_ACQUIRE) != 0u)
        return TRIGGER_SEQUENCE_SERVICE_BUSY;
    return trigger_sequence_link_ready_inject(1u);
}
trigger_sequence_service_result_t trigger_sequence_link_ready_inject(uint32_t count)
{
    /* Core0 only publishes credits. Core1 validates the immutable run identity
     * and submits the owner READY action at the actual boundary. */
    if (count == 0u || count > TRIGGER_SEQUENCE_LINK_READY_INJECT_MAX)
        return TRIGGER_SEQUENCE_SERVICE_INVALID;
    trigger_sequence_link_status_t view;
    trigger_sequence_link_get_status(&view);
    if (!view.config.enabled || view.config.ready_input != 0u)
        return TRIGGER_SEQUENCE_SERVICE_SOURCE_MISMATCH;
    if (view.error != LINK_OK || view.run_id == 0u ||
        view.phase == LINK_OFF || view.phase == LINK_WAIT_START ||
        view.phase == LINK_FAULT || view.phase == LINK_DONE)
        return TRIGGER_SEQUENCE_SERVICE_NOT_READY;

    uint32_t pending = __atomic_load_n(&s_ready_credit_count, __ATOMIC_ACQUIRE);
    for (;;) {
        if (pending == 0u) {
            __atomic_store_n(&s_ready_credit_run, view.run_id, __ATOMIC_RELAXED);
            __atomic_store_n(&s_ready_credit_generation, view.generation, __ATOMIC_RELAXED);
            __atomic_store_n(&s_ready_credit_binding, view.binding_epoch, __ATOMIC_RELAXED);
        } else if (__atomic_load_n(&s_ready_credit_run, __ATOMIC_RELAXED) != view.run_id ||
                   __atomic_load_n(&s_ready_credit_generation, __ATOMIC_RELAXED) != view.generation ||
                   __atomic_load_n(&s_ready_credit_binding, __ATOMIC_RELAXED) != view.binding_epoch) {
            return TRIGGER_SEQUENCE_SERVICE_BUSY;
        }
        if (pending > TRIGGER_SEQUENCE_LINK_READY_INJECT_MAX - count)
            return TRIGGER_SEQUENCE_SERVICE_EXHAUSTED;
        if (__atomic_compare_exchange_n(&s_ready_credit_count, &pending, pending + count,
                false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
            return TRIGGER_SEQUENCE_SERVICE_OK;
    }
}
bool trigger_sequence_link_service(void)
{
    /* OFF has no runtime work. Do not contend with Core0 configuration on
     * every cycle; enabled STOP cleanup still runs through the writer path.
     * An enable after this snapshot is serviced on the next Core1 cycle. */
    osal_critical_enter();
    const bool work = s_published.config.enabled ||
        __atomic_load_n(&s_transport_rejected, __ATOMIC_ACQUIRE) != 0u;
    const bool acquired = work && take();
    osal_critical_exit();
    if (!acquired) return false;
    s_action_submitted = false;
    s_transport_ready = false;
    s_link.tx_fragments = __atomic_load_n(&s_transport_tx_count, __ATOMIC_ACQUIRE) - s_tx_count_baseline;
    s_link.rejected += __atomic_exchange_n(&s_transport_rejected, 0u, __ATOMIC_ACQ_REL);
    service(); /* STOP, owner faults and new run identity always precede RX. */
    if (s_tx_enabled && s_transport_ready && !trigger_sequence_service_stop_pending()) {
        const uint32_t overflow = __atomic_load_n(&s_inbox_overflows, __ATOMIC_ACQUIRE);
        const uint32_t overflow_run = __atomic_load_n(&s_overflow_run, __ATOMIC_RELAXED);
        const uint32_t overflow_generation = __atomic_load_n(&s_overflow_generation, __ATOMIC_RELAXED);
        const uint32_t overflow_binding = __atomic_load_n(&s_overflow_binding, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const bool coherent = !(overflow & 1u) &&
            overflow == __atomic_load_n(&s_inbox_overflows, __ATOMIC_ACQUIRE);
        if (coherent && overflow != s_seen_overflows && overflow_run == s_link.run_id &&
            overflow_generation == s_link.generation && overflow_binding == s_link.binding_epoch) {
            fail(LINK_MAILBOX_FULL);
            discard_inbox();
        } else if (coherent) {
            s_seen_overflows = overflow;
            if (!s_retry_valid) {
                const uint32_t read = __atomic_load_n(&s_inbox_read, __ATOMIC_RELAXED);
                const uint32_t written = __atomic_load_n(&s_inbox_written, __ATOMIC_ACQUIRE);
                if (read != written) {
                    s_retry_message = s_inbox[read % TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY];
                    __atomic_store_n(&s_inbox_read, read + 1u, __ATOMIC_RELEASE);
                    s_retry_valid = true;
                    ++s_link.rx_messages;
                }
            }
            if (s_retry_valid) {
                s_retry_valid = false;
                receive_message(s_retry_message);
            }
        }
    }
    const bool submitted = s_action_submitted;
    release();
    return submitted;
}

/* A published TX offer is permission for this owner identity only. STOP
 * pending, PAUSE or a new START invalidates it before the next LINK service. */
static bool transport_permitted(const trigger_sequence_link_status_t *view)
{
    trigger_sequence_service_status_t owner;
    trigger_sequence_service_get_status(&owner);
    return !trigger_sequence_service_stop_pending() && view->config.enabled &&
        view->run_id == owner.run_id && view->generation == owner.generation &&
        (owner.state == TRIGGER_SEQUENCE_SERVICE_READY ||
         owner.state == TRIGGER_SEQUENCE_SERVICE_RUNNING);
}

bool trigger_sequence_link_tx_fragment(uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE])
{
    trigger_sequence_link_tx_t offer;
    trigger_sequence_link_status_t view;
    bool enabled;
    osal_critical_enter();
    offer = s_tx_offer;
    enabled = s_tx_offer_enabled;
    view = s_published;
    osal_critical_exit();
    if (!fragment || !enabled || !transport_permitted(&view) ||
        transport_current(view.binding_epoch, NULL) != TRANSPORT_CURRENT) return false;
    if (offer.token != s_transport_tx.token ||
        memcmp(offer.wire, s_transport_tx.wire, sizeof(offer.wire)) != 0) {
        uint64_t now;
        if (!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &now) ||
            now < s_message_started_ticks) {
            s_clock_fault = true;
            return false;
        }
        s_transport_tx = offer;
        s_message_tx_ticks = now;
    }
    if (!trigger_sequence_link_tx_next(&s_transport_tx, fragment)) return false;
    (void)__atomic_add_fetch(&s_transport_tx_count, 1u, __ATOMIC_RELEASE);
    return true;
}

void trigger_sequence_link_rx_fragment(uint32_t physical_source,
    const uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE])
{
    trigger_sequence_link_status_t view;
    bool enabled;
    osal_critical_enter();
    view = s_published;
    enabled = s_tx_offer_enabled;
    osal_critical_exit();
    if (!fragment || !enabled || !transport_permitted(&view) ||
        physical_source >= REFMEM_APP_MODEL_NODE_COUNT) return;
    uint32_t local_slot;
    if (transport_current(view.binding_epoch, &local_slot) != TRANSPORT_CURRENT ||
        physical_source != local_slot) return;
    if (s_transport_run != view.run_id || s_transport_generation != view.generation ||
        s_transport_binding != view.binding_epoch) {
        trigger_sequence_link_rx_init(&s_transport_rx);
        s_transport_run = view.run_id;
        s_transport_generation = view.generation;
        s_transport_binding = view.binding_epoch;
    }
    trigger_sequence_link_message_t message;
    trigger_sequence_link_rx_result_t result = trigger_sequence_link_rx_feed(
        &s_transport_rx, fragment, &message);
    if (result == TRIGGER_SEQUENCE_LINK_RX_REJECTED) {
        (void)__atomic_add_fetch(&s_transport_rejected, 1u, __ATOMIC_RELEASE);
        return;
    }
    if (result != TRIGGER_SEQUENCE_LINK_RX_MESSAGE) return;
    /* Cheap stale rejection avoids filling a live inbox with preceding runs.
     * The Core1 consumer still validates full identity, route and phase. */
    if (message.run_id != view.run_id || message.step_ordinal != view.step ||
        (uint8_t)message.exchange_id != (uint8_t)view.exchange_id) {
        (void)__atomic_add_fetch(&s_transport_rejected, 1u, __ATOMIC_RELEASE);
        return;
    }
    message.generation = view.generation;
    message.binding_epoch = view.binding_epoch;
    message.exchange_id = view.exchange_id;
    uint64_t now;
    if (!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &now) ||
        now < s_message_tx_ticks) {
        s_clock_fault = true;
        return;
    }
    s_message_rx_ticks = now;
    if (message.kind == TRIGGER_SEQUENCE_LINK_COUNTER_NEXT) {
        message.source_slot = view.config.counter_slot;
        message.target_slot = view.config.dut_slot;
    } else if (message.kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED) {
        message.source_slot = view.config.dut_slot;
        message.target_slot = view.config.vna_slot;
    } else {
        message.source_slot = view.config.vna_slot;
        message.target_slot = view.config.dut_slot;
    }
    const uint32_t written = __atomic_load_n(&s_inbox_written, __ATOMIC_RELAXED);
    const uint32_t read = __atomic_load_n(&s_inbox_read, __ATOMIC_ACQUIRE);
    if ((uint32_t)(written - read) >= TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY) {
        (void)__atomic_add_fetch(&s_inbox_overflows, 1u, __ATOMIC_ACQ_REL);
        __atomic_store_n(&s_overflow_run, message.run_id, __ATOMIC_RELAXED);
        __atomic_store_n(&s_overflow_generation, message.generation, __ATOMIC_RELAXED);
        __atomic_store_n(&s_overflow_binding, message.binding_epoch, __ATOMIC_RELAXED);
        (void)__atomic_add_fetch(&s_inbox_overflows, 1u, __ATOMIC_RELEASE);
        return;
    }
    s_inbox[written % TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY] = message;
    __atomic_store_n(&s_inbox_written, written + 1u, __ATOMIC_RELEASE);
}
