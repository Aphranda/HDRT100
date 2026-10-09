#include "trigger_t2_runtime.h"

#include <string.h>

#include "distributed_refmem.h"
#include "board_config.h"
#include "vdc_dpll_manager.h"
#include "vdc_timer1_coordinate.h"
#include "sync_io_gateway.h"
#include "sync_io_switch.h"
#include "trigger_counter_service.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/platform.h"
#include "tdma_seat_mount_store.h"
#define T2_HOT(name) __not_in_flash_func(name)
#else
#define T2_HOT(name) name
#endif
#define T2_BOUNDED __attribute__((noinline, optimize("Os")))

/* STOP validates the full shared tables; RUN retains only immutable fields
 * it actually consumes. The one local template is stored separately. */
static struct {
    trigger_time_grid_t grid;
    trigger_turntable_predictor_config_t predictor;
    uint32_t run_id, source_slot, target_mask, descriptor_window_sync, origin;
    uint32_t deployment_generation;
} s_config;
static trigger_t2_runtime_snapshot_t s_state;
/* Static templates never change in RUN. The first hardware executor has one
 * local template per position and one pending hardware transaction per board. */
static trigger_action_template_t s_local_template;
static uint32_t s_local_template_count, s_deployment_crc;
typedef struct {
    uint64_t action_q, deadline_tick;
    uint32_t session, ring_config_seq, prediction_version, expiry_sync;
    uint32_t model_token;
} t2_deadline_plan_t;
_Static_assert(sizeof(t2_deadline_plan_t) % sizeof(uint32_t) == 0u,
    "deadline handoff uses atomic words");
typedef struct {
    uint32_t words[sizeof(t2_deadline_plan_t) / sizeof(uint32_t)];
    uint32_t guard;
} t2_deadline_mailbox_t;
/* Core0 is the only RUN writer of mailboxes; Core1 owns plans and states. */
static t2_deadline_mailbox_t s_deadline_mailbox[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static t2_deadline_plan_t s_deadline[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static uint32_t s_deadline_seen[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static uint32_t s_frozen_public;
/* Core1 publishes the threshold before attempting the FREEZE handoff; Core0
 * abandons in-progress predictions for that position. Transport additionally
 * serializes PREVIEW/FREEZE acceptance to close the final check/stage race. */
static uint32_t s_threshold_public;
static uint32_t s_version_exhausted;
static uint32_t s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
static uint32_t s_pending_mask;
static uint32_t s_core0_prepare_calls, s_core0_preview_saved, s_core0_prepare_last_reason;
static volatile uint32_t s_guard;

#define T2_WINDOW_HISTORY_CAPACITY 17u
typedef struct { uint32_t count, coarse; } t2_count_sample_t;
static t2_count_sample_t s_count_history[T2_WINDOW_HISTORY_CAPACITY];
static uint32_t s_count_head, s_count_size, s_count_last_seq;
static uint32_t s_last_calculated_coarse[TRIGGER_T2_RUNTIME_POSITION_COUNT];
_Static_assert(sizeof(trigger_position_descriptor_t) % sizeof(uint32_t) == 0u,
    "preview handoff uses whole atomic words");
static uint32_t s_preview_history[TRIGGER_T2_RUNTIME_POSITION_COUNT][3]
    [sizeof(trigger_position_descriptor_t) / sizeof(uint32_t)];
static volatile uint32_t s_preview_guard;
static volatile uint32_t s_session;

static bool preview_copy(uint32_t index, uint32_t age,
                         trigger_position_descriptor_t *out)
{
    if (index >= TRIGGER_T2_RUNTIME_POSITION_COUNT || age >= 3u || !out)
        return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t before = __atomic_load_n(&s_preview_guard, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        for (size_t i = 0u; i < sizeof(*out) / sizeof(uint32_t); ++i) {
            const uint32_t word = __atomic_load_n(&s_preview_history[index][age][i],
                                                 __ATOMIC_RELAXED);
            memcpy((uint8_t *)out + i * sizeof(word), &word, sizeof(word));
        }
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        if (before == __atomic_load_n(&s_preview_guard, __ATOMIC_ACQUIRE))
            return out->prediction_version != 0u;
    }
    return false;
}

static void preview_save(uint32_t index,
                         const trigger_position_descriptor_t *descriptor)
{
    (void)__atomic_add_fetch(&s_preview_guard, 1u, __ATOMIC_ACQ_REL);
    for (size_t i = 0u; i < sizeof(*descriptor) / sizeof(uint32_t); ++i) {
        const uint32_t old = __atomic_load_n(&s_preview_history[index][1][i], __ATOMIC_RELAXED);
        const uint32_t latest = __atomic_load_n(&s_preview_history[index][0][i], __ATOMIC_RELAXED);
        uint32_t word;
        memcpy(&word, (const uint8_t *)descriptor + i * sizeof(word), sizeof(word));
        __atomic_store_n(&s_preview_history[index][2][i], old, __ATOMIC_RELAXED);
        __atomic_store_n(&s_preview_history[index][1][i], latest, __ATOMIC_RELAXED);
        __atomic_store_n(&s_preview_history[index][0][i], word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&s_preview_guard, 1u, __ATOMIC_RELEASE);
}

static void publish_begin(void)
{
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_ACQ_REL);
}

static void publish_end(void)
{
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_RELEASE);
}

static bool stage_one_freeze(void)
{
    /* The legacy descriptor mailbox remains single-depth until compact-q
     * transport admission is installed. Do not overtake an in-flight PREVIEW
     * and reorder it behind FREEZE. */
    if (!s_pending_mask || distributed_refmem_position_descriptor_pending())
        return true;
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT;
         ++index) {
        if ((s_pending_mask & (1u << index)) == 0u) continue;
        trigger_position_descriptor_t latest;
        if (!preview_copy(index, 0u, &latest)) return true;
        const trigger_position_descriptor_t freeze = {
            .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
            .stage = TRIGGER_POSITION_STAGE_FREEZE,
            .position_id = index + 1u,
            .prediction_version = latest.prediction_version,
        };
        if (!distributed_refmem_stage_position_descriptor(&freeze,
                                                         s_config.target_mask)) {
            s_state.transport_reject_count++;
            s_state.last_error = TRIGGER_T2_RUNTIME_TRANSPORT;
            return false;
        }
        s_state.descriptors[index] = latest;
        s_state.freeze_version[index] = latest.prediction_version;
        s_state.frozen_mask |= 1u << index;
        s_state.published_mask |= 1u << index;
        s_pending_mask &= ~(1u << index);
        return true;
    }
    return true;
}

static void deadline_publish(uint32_t index, const t2_deadline_plan_t *plan)
{
    t2_deadline_mailbox_t *mail = &s_deadline_mailbox[index];
    (void)__atomic_add_fetch(&mail->guard, 1u, __ATOMIC_ACQ_REL);
    for (size_t i = 0u; i < sizeof(*plan) / sizeof(uint32_t); ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)plan + i * sizeof(word), sizeof(word));
        __atomic_store_n(&mail->words[i], word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&mail->guard, 1u, __ATOMIC_RELEASE);
}

static T2_BOUNDED void deadline_receive(void)
{
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index) {
        if (s_state.frozen_mask & (1u << index)) continue;
        t2_deadline_mailbox_t *mail = &s_deadline_mailbox[index];
        const uint32_t before = __atomic_load_n(&mail->guard, __ATOMIC_ACQUIRE);
        if ((before & 1u) || before == s_deadline_seen[index]) continue;
        t2_deadline_plan_t plan;
        for (size_t i = 0u; i < sizeof(plan) / sizeof(uint32_t); ++i) {
            const uint32_t word = __atomic_load_n(&mail->words[i], __ATOMIC_RELAXED);
            memcpy((uint8_t *)&plan + i * sizeof(word), &word, sizeof(word));
        }
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        if (before != __atomic_load_n(&mail->guard, __ATOMIC_ACQUIRE)) continue;
        s_deadline_seen[index] = before;
        if (plan.prediction_version && plan.session == s_state.cursor.session &&
            plan.ring_config_seq == s_state.cursor.ring_config_seq)
            s_deadline[index] = plan;
    }
}

static void action_fail(uint32_t index)
{
    s_state.action_failed_mask |= 1u << index;
    s_state.last_error = TRIGGER_T2_RUNTIME_ACTION;
}

static T2_BOUNDED bool accept_remote_position(uint64_t current_ms)
{
    trigger_position_descriptor_t descriptor;
    if (!distributed_refmem_get_position_descriptor(
            s_config.source_slot, &descriptor)) return true;
    if (descriptor.position_id < 1u ||
        descriptor.position_id > TRIGGER_T2_RUNTIME_POSITION_COUNT ||
        descriptor.prediction_version > TRIGGER_T2_PREDICTION_VERSION_MAX)
        goto reject;
    const uint32_t index = descriptor.position_id - 1u;
    const uint32_t bit = 1u << index;
    if (s_state.frozen_mask & bit) return true;
    if (descriptor.stage == TRIGGER_POSITION_STAGE_FREEZE) {
        if (!trigger_position_descriptor_valid(&descriptor, s_config.grid.subdivisions))
            goto reject;
        s_state.frozen_mask |= bit;
        __atomic_store_n(&s_frozen_public, s_state.frozen_mask, __ATOMIC_RELEASE);
        s_state.freeze_version[index] = descriptor.prediction_version;
        const t2_deadline_plan_t *plan = &s_deadline[index];
        s_state.freeze_local_age[index] = 0xFFu;
        s_state.freeze_local_version[index] = plan->prediction_version;
        if (!plan->prediction_version) {
            s_state.freeze_missing_local_count++;
            s_state.freeze_mismatch_count++;
            action_fail(index);
            return false;
        }
        /* FREEZE uses the newest completed local solve, even when RX has a
         * newer PREVIEW which Core0 has not solved yet. Never solve at FREEZE. */
        for (uint32_t age = 0u; age < 3u; ++age) {
            trigger_position_descriptor_t previous;
            if (preview_copy(index, age, &previous) &&
                previous.prediction_version == plan->prediction_version) {
                s_state.freeze_local_age[index] = (uint8_t)age;
                s_state.descriptors[index] = previous;
                break;
            }
        }
        if (plan->prediction_version != descriptor.prediction_version) {
            s_state.freeze_mismatch_count++;
            s_state.freeze_fallback_count++;
        }
        if (current_ms > plan->expiry_sync ||
            plan->session != s_state.cursor.session ||
            plan->ring_config_seq != s_state.cursor.ring_config_seq ||
            plan->action_q <= s_state.cursor.action_cursor) {
            action_fail(index);
            return false;
        }
        for (uint32_t other = 0u; other < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++other) {
            if (other == index || !(s_state.frozen_mask & (1u << other)) ||
                (s_state.action_failed_mask & (1u << other))) continue;
            const uint64_t a = plan->action_q, b = s_deadline[other].action_q;
            const uint64_t distance = a > b ? a - b : b - a;
            if (distance < s_local_template.span_slices ||
                (s_armed_index == other && a < b)) {
                action_fail(index);
                return false;
            }
        }
        return true;
    }
    /* Transport owns wire CRC. Validate each new version here once, not the
     * same deployment/table CRC on every realtime polling call. */
    if (s_state.received_mask & bit &&
        s_state.descriptors[index].prediction_version == descriptor.prediction_version &&
        s_state.descriptors[index].descriptor_crc32 == descriptor.descriptor_crc32)
        return true;
    if (!trigger_position_descriptor_valid(&descriptor, s_config.grid.subdivisions) ||
        descriptor.run_id != s_config.run_id ||
        descriptor.sync_epoch != s_state.cursor.grid_epoch ||
        descriptor.count_epoch != s_state.predictor.count_epoch ||
        descriptor.grid_generation != s_config.grid.generation ||
        descriptor.deployment_generation != s_config.deployment_generation ||
        descriptor.deployment_crc != s_deployment_crc ||
        current_ms < descriptor.valid_from_sync || current_ms > descriptor.expiry_sync)
        goto reject;
    if (s_state.received_mask & bit &&
        s_state.descriptors[index].prediction_version >= descriptor.prediction_version)
        goto reject;
    s_state.descriptors[index] = descriptor;
    preview_save(index, &descriptor);
    s_state.received_mask |= bit;
    return true;
reject:
    s_state.descriptor_reject_count++;
    s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
    return false;
}

static void complete_action(uint32_t index)
{
    const uint32_t bit = 1u << index;
    s_state.emitted_position_mask |= bit;
    s_state.action_completed_mask |= bit;
    if (s_state.event_count >= TRIGGER_T2_RUNTIME_EVENT_CAPACITY) {
        s_state.event_dropped_count++;
        return;
    }
    const uint32_t event_index = s_state.event_count++;
    s_state.events[event_index] = (trigger_action_runtime_event_t){
        .position_id = index + 1u, .template_id = s_local_template.template_id,
        .role = s_local_template.role, .kind = s_local_template.kind,
        .argument = s_local_template.argument, .action_q = s_deadline[index].action_q};
    s_state.event_dispatched[event_index] = 1u;
}

static void note_backend(uint32_t index, uint32_t state, uint64_t before, uint64_t after)
{
    if (index >= TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    s_state.action_backend_state[index] = state;
    s_state.action_origin_width_ticks[index] = !after || after < before ||
        after - before > UINT32_MAX ? UINT32_MAX : (uint32_t)(after - before);
}

static bool cancel_backend(void)
{
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_deadline_status_t status;
        (void)sync_io_switch_realtime_cancel();
        if (!sync_io_switch_realtime_status(&status)) return true;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        return status.state == SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
    }
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_GATEWAY) {
        sync_io_gateway_deadline_status_t status;
        (void)sync_io_gateway_realtime_cancel();
        if (!sync_io_gateway_realtime_status(&status)) return true;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        return status.state == SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN;
    }
    return false;
}

static void stop_backend_core0(void)
{
    if (s_armed_index >= TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    bool stopped = false;
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_snapshot_t status;
        stopped = sync_io_switch_snapshot(&status) && status.loaded &&
            sync_io_switch_stop(status.generation);
    } else if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_GATEWAY) {
        sync_io_gateway_snapshot_t status;
        stopped = sync_io_gateway_snapshot(&status) && status.loaded &&
            sync_io_gateway_stop(status.generation);
    }
    /* STOP is never a completion. Backend retains CANCELLED/UNCERTAIN detail;
     * inability to stop is additionally visible in the T2 diagnostic. */
    if (!stopped) s_state.action_uncertain_mask |= 1u << s_armed_index;
}

static T2_BOUNDED void service_deadlines(bool cursor_valid)
{
    if (!s_local_template_count) return;
    bool complete = false, terminal = false, uncertain = false;
    uint32_t sequence = 0u;
    uint64_t deadline = 0u;
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_realtime_service_core1();
        sync_io_switch_deadline_status_t status;
        if (!sync_io_switch_realtime_status(&status)) return;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        sequence = status.sequence; deadline = status.deadline_tick;
        complete = status.state == SYNC_IO_SWITCH_DEADLINE_COMPLETE;
        uncertain = status.state == SYNC_IO_SWITCH_DEADLINE_UNCERTAIN;
        terminal = uncertain || status.state == SYNC_IO_SWITCH_DEADLINE_CANCELLED;
    } else {
        sync_io_gateway_realtime_service_core1();
        sync_io_gateway_deadline_status_t status;
        if (!sync_io_gateway_realtime_status(&status)) return;
        note_backend(s_armed_index, status.state, status.origin_before_tick, status.origin_after_tick);
        sequence = status.sequence; deadline = status.deadline_tick;
        complete = status.state == SYNC_IO_GATEWAY_DEADLINE_COMPLETE &&
            status.completed_sequence == status.sequence;
        uncertain = status.state == SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN;
        terminal = uncertain || status.state == SYNC_IO_GATEWAY_DEADLINE_CANCELLED;
    }
    if (s_armed_index < TRIGGER_T2_RUNTIME_POSITION_COUNT) {
        const uint32_t index = s_armed_index, bit = 1u << index;
        if (sequence != s_state.action_sequence[index] ||
            deadline != s_deadline[index].deadline_tick) {
            cancel_backend();
            s_state.action_uncertain_mask |= bit;
            action_fail(index);
            s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
        } else if (complete) {
            complete_action(index);
            s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
        } else if (terminal || !cursor_valid ||
                   s_deadline[index].session != s_state.cursor.session ||
                   s_deadline[index].ring_config_seq != s_state.cursor.ring_config_seq) {
            if (!terminal) uncertain = cancel_backend();
            if (uncertain) s_state.action_uncertain_mask |= bit;
            action_fail(index);
            s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
        }
    }
    if (!cursor_valid) {
        s_state.action_failed_mask |= s_state.frozen_mask & ~s_state.action_completed_mask;
        return;
    }
    if (s_armed_index < TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    uint32_t selected = TRIGGER_T2_RUNTIME_POSITION_COUNT;
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index) {
        const uint32_t bit = 1u << index;
        if (!(s_state.frozen_mask & bit) ||
            ((s_state.action_failed_mask | s_state.action_completed_mask) & bit)) continue;
        const t2_deadline_plan_t *plan = &s_deadline[index];
        if (!plan->prediction_version || plan->session != s_state.cursor.session ||
            plan->ring_config_seq != s_state.cursor.ring_config_seq ||
            s_state.cursor.action_cursor / s_config.grid.subdivisions > plan->expiry_sync ||
            plan->action_q <= s_state.cursor.action_cursor) {
            action_fail(index);
            continue;
        }
        if (selected == TRIGGER_T2_RUNTIME_POSITION_COUNT ||
            plan->action_q < s_deadline[selected].action_q) selected = index;
    }
    if (selected == TRIGGER_T2_RUNTIME_POSITION_COUNT) return;
    const t2_deadline_plan_t *plan = &s_deadline[selected];
    const bool armed = s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH ?
        sync_io_switch_realtime_schedule_at(s_local_template.argument, plan->deadline_tick) :
        sync_io_gateway_realtime_schedule_at(s_local_template.argument, plan->deadline_tick);
    if (!armed) {
        /* A rejected absolute deadline is terminal. No fire-now fallback.
         * Origin validation can fail after the edge: preserve UNCERTAIN. */
        if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
            sync_io_switch_deadline_status_t status;
            if (sync_io_switch_realtime_status(&status)) {
                note_backend(selected, status.state, status.origin_before_tick, status.origin_after_tick);
                if (status.state == SYNC_IO_SWITCH_DEADLINE_UNCERTAIN)
                    s_state.action_uncertain_mask |= 1u << selected;
            }
        } else {
            sync_io_gateway_deadline_status_t status;
            if (sync_io_gateway_realtime_status(&status)) {
                note_backend(selected, status.state, status.origin_before_tick, status.origin_after_tick);
                if (status.state == SYNC_IO_GATEWAY_DEADLINE_UNCERTAIN)
                    s_state.action_uncertain_mask |= 1u << selected;
            }
        }
        action_fail(selected);
        return;
    }
    s_armed_index = selected;
    s_state.action_armed_mask |= 1u << selected;
    s_state.action_deadline_tick[selected] = plan->deadline_tick;
    /* Read the owner's accepted sequence; a later mismatch cannot complete us. */
    if (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_SWITCH) {
        sync_io_switch_deadline_status_t status;
        if (sync_io_switch_realtime_status(&status)) {
            s_state.action_sequence[selected] = status.sequence;
            note_backend(selected, status.state, status.origin_before_tick, status.origin_after_tick);
        }
    } else {
        sync_io_gateway_deadline_status_t status;
        if (sync_io_gateway_realtime_status(&status)) {
            s_state.action_sequence[selected] = status.sequence;
            note_backend(selected, status.state, status.origin_before_tick, status.origin_after_tick);
        }
    }
}

bool trigger_t2_runtime_configure(const trigger_t2_runtime_config_t *config)
{
    /* Split so a bench readout names the rejected condition (temporary). */
    if (!config) { tdma_seat_mount_store_note_reject(140u); return false; }
    if (!trigger_time_grid_valid(&config->grid)) {
        tdma_seat_mount_store_note_reject(141u); return false;
    }
    if (!trigger_turntable_predictor_config_valid(&config->predictor)) {
        tdma_seat_mount_store_note_reject(142u); return false;
    }
    if (trigger_deployment_validate(&config->deployment, UINT32_MAX) !=
        TRIGGER_DEPLOYMENT_OK) {
        tdma_seat_mount_store_note_reject(143u); return false;
    }
    if (!config->run_id) { tdma_seat_mount_store_note_reject(144u); return false; }
    if (config->source_slot >= TRIGGER_POSITION_DESCRIPTOR_NODE_COUNT) {
        tdma_seat_mount_store_note_reject(145u); return false;
    }
    if (!config->target_mask) { tdma_seat_mount_store_note_reject(146u); return false; }
    if (!config->descriptor_window_sync) {
        tdma_seat_mount_store_note_reject(147u); return false;
    }
    if (trigger_action_template_table_validate(
            &config->templates, &config->deployment, config->template_crc) !=
        TRIGGER_ACTION_TEMPLATE_TABLE_OK) {
        tdma_seat_mount_store_note_reject(148u); return false;
    }
    if (s_state.active) return false;
    uint32_t local_count = 0u;
    trigger_action_template_t local = {0};
    if (config->local_role >= TRIGGER_DEPLOYMENT_ROLE_COUNT) return false;
    for (uint32_t i = 0u; i < config->templates.count; ++i)
        if (config->templates.row[i].role == config->local_role) {
            local = config->templates.row[i];
            if (++local_count > TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY) return false;
        }
    publish_begin();
    s_config.grid = config->grid;
    s_config.predictor = config->predictor;
    s_config.run_id = config->run_id;
    s_config.source_slot = config->source_slot;
    s_config.target_mask = config->target_mask;
    s_config.descriptor_window_sync = config->descriptor_window_sync;
    s_config.origin = config->origin;
    s_config.deployment_generation = config->deployment.generation;
    memset(&s_state, 0, sizeof(s_state));
    s_local_template = local;
    s_local_template_count = local_count;
    s_deployment_crc = trigger_deployment_crc(&config->deployment);
    s_state.configured = 1u;
    s_state.origin = config->origin ? 1u : 0u;
    s_state.source_slot = config->source_slot;
    s_state.target_mask = config->target_mask;
    s_state.run_id = config->run_id;
    s_state.last_error = TRIGGER_T2_RUNTIME_OK;
    publish_end();
    return true;
}

bool trigger_t2_runtime_start(uint32_t count_epoch, uint64_t origin_count)
{
    if (!s_state.configured || s_state.active || !count_epoch) return false;
    /* START runs inside STOP maintenance, with the RX producer parked. The
     * transport mailbox is single-depth; consume a previous run's untagged
     * FREEZE before establishing this run's local mirrors. */
    trigger_position_descriptor_t discarded;
    (void)distributed_refmem_get_position_descriptor(s_config.source_slot, &discarded);
    publish_begin();
    memset(&s_state, 0, sizeof(s_state));
    s_state.configured = 1u;
    s_state.origin = s_config.origin ? 1u : 0u;
    s_state.source_slot = s_config.source_slot;
    s_state.target_mask = s_config.target_mask;
    s_state.run_id = s_config.run_id;
    for (uint32_t i = 0u; i < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++i)
        s_state.action_origin_width_ticks[i] = UINT32_MAX;
    const bool started =
        trigger_time_cursor_configure(&s_config.grid) &&
        trigger_turntable_predictor_configure(
            &s_state.predictor, &s_config.predictor) &&
        trigger_turntable_predictor_begin(
            &s_state.predictor, &s_config.predictor, count_epoch,
            origin_count);
    if (!started) {
        publish_end();
        return false;
    }
    s_state.active = 1u;
    s_pending_mask = 0u;
    memset(s_deadline, 0, sizeof(s_deadline));
    memset(s_deadline_mailbox, 0, sizeof(s_deadline_mailbox));
    memset(s_deadline_seen, 0, sizeof(s_deadline_seen));
    __atomic_store_n(&s_frozen_public, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_threshold_public, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_version_exhausted, 0u, __ATOMIC_RELEASE);
    s_core0_prepare_calls = s_core0_preview_saved = s_core0_prepare_last_reason = 0u;
    s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
    s_count_head = s_count_size = s_count_last_seq = 0u;
    memset(s_count_history, 0, sizeof(s_count_history));
    memset(s_last_calculated_coarse, 0, sizeof(s_last_calculated_coarse));
    (void)__atomic_add_fetch(&s_preview_guard, 1u, __ATOMIC_ACQ_REL);
    memset(s_preview_history, 0, sizeof(s_preview_history));
    (void)__atomic_add_fetch(&s_preview_guard, 1u, __ATOMIC_RELEASE);
    (void)__atomic_add_fetch(&s_session, 1u, __ATOMIC_RELEASE);
    s_state.last_error = TRIGGER_T2_RUNTIME_OK;
    publish_end();
    return true;
}

void trigger_t2_runtime_stop(void)
{
    (void)__atomic_add_fetch(&s_session, 1u, __ATOMIC_ACQ_REL);
    publish_begin();
    s_state.active = 0u;
    s_pending_mask = 0u;
    /* STOP owner is serialized after the realtime plane has parked. Drop all
     * unsubmitted intentions and cancel the preloaded backend countdown. */
    stop_backend_core0();
    s_state.action_failed_mask |= s_state.frozen_mask & ~s_state.action_completed_mask;
    s_armed_index = TRIGGER_T2_RUNTIME_POSITION_COUNT;
    distributed_refmem_request_position_cancel();
    trigger_time_cursor_clear();
    publish_end();
}

static inline bool deadlines_need_service(void)
{
    return s_local_template_count &&
        (s_armed_index < TRIGGER_T2_RUNTIME_POSITION_COUNT ||
         (s_state.frozen_mask & ~(s_state.action_failed_mask | s_state.action_completed_mask)) ||
         /* Keep polling a prior gateway transaction for a late READY even
          * after its independent output receipt has completed. */
         (s_local_template.kind == TRIGGER_ACTION_TEMPLATE_GATEWAY && s_state.action_armed_mask));
}

static T2_BOUNDED bool service_origin(void)
{
    uint32_t count;
    if (trigger_counter_read_core1(&count) != COUNTER_OK) {
        s_state.last_error = TRIGGER_T2_RUNTIME_COUNTER;
        return false;
    }
    s_state.last_counter_count = count;
    uint32_t mask = 0u;
    if (trigger_turntable_predictor_observe(&s_state.predictor,
            &s_config.predictor, count, &mask) != TRIGGER_TURNTABLE_OK) {
        s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
        return false;
    }
    s_pending_mask |= mask;
    __atomic_store_n(&s_threshold_public, s_state.predictor.announced_mask,
                     __ATOMIC_RELEASE);
    return stage_one_freeze();
}

T2_BOUNDED trigger_t2_runtime_result_t T2_HOT(trigger_t2_runtime_service)(void)
{
    if (!s_state.configured) return TRIGGER_T2_RUNTIME_NOT_CONFIGURED;
    if (!s_state.active) return TRIGGER_T2_RUNTIME_NOT_ACTIVE;
    publish_begin();
#define T2_RETURN(value) do { publish_end(); return (value); } while (0)
    trigger_time_cursor_service();
    /* Core1 is the sole RUN writer and publication is guarded: read directly
     * into its snapshot instead of copying the complete cursor twice. */
    trigger_time_cursor_snapshot_t *const cursor = &s_state.cursor;
    if (!trigger_time_cursor_snapshot(cursor)) {
        s_state.cursor.valid = 0u;
        if (deadlines_need_service()) service_deadlines(false);
        s_state.last_error = TRIGGER_T2_RUNTIME_CURSOR;
        T2_RETURN(s_state.last_error);
    }
    if (!cursor->valid || !cursor->pulse_index) {
        cursor->valid = 0u;
        if (deadlines_need_service()) service_deadlines(false);
        s_state.last_error = TRIGGER_T2_RUNTIME_CURSOR;
        T2_RETURN(s_state.last_error);
    }
    s_state.last_error = TRIGGER_T2_RUNTIME_OK;
    const bool new_coarse = cursor->sample_seq != s_state.last_sync_sequence;
    s_state.last_sync_sequence = cursor->sample_seq;

    if (s_state.origin && new_coarse) {
        if (!service_origin()) T2_RETURN(s_state.last_error);
    } else if (!s_state.origin) {
        deadline_receive();
        /* The validated cursor already supplies coarse ordinal + 1. */
        (void)accept_remote_position(cursor->pulse_index - 1u);
    }
    if (deadlines_need_service()) service_deadlines(true);
    const trigger_t2_runtime_result_t completed=s_state.last_error;
    if (completed != TRIGGER_T2_RUNTIME_OK) T2_RETURN(completed);
    T2_RETURN(TRIGGER_T2_RUNTIME_OK);
#undef T2_RETURN
}

bool trigger_t2_runtime_take_event(trigger_action_runtime_event_t *out)
{
    if (!out || !s_state.event_count) return false;
    publish_begin();
    *out = s_state.events[0];
    if (s_state.event_count > 1u) {
        memmove(&s_state.events[0], &s_state.events[1],
                (s_state.event_count - 1u) * sizeof(s_state.events[0]));
        memmove(&s_state.event_dispatched[0], &s_state.event_dispatched[1],
                s_state.event_count - 1u);
    }
    s_state.event_dispatched[s_state.event_count-1u]=0u;
    s_state.event_count--;
    publish_end();
    return true;
}

bool trigger_t2_runtime_snapshot(trigger_t2_runtime_snapshot_t *out)
{
    if (!out) return false;
    for (uint32_t attempt = 0u; attempt < 4u; ++attempt) {
        const uint32_t before = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        *out = s_state;
        const uint32_t after = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
        if (before == after) {
            out->core0_prepare_calls=s_core0_prepare_calls;
            out->core0_preview_saved=s_core0_preview_saved;
            out->core0_prepare_last_reason=s_core0_prepare_last_reason;
            out->prediction_version_exhausted_mask =
                __atomic_load_n(&s_version_exhausted, __ATOMIC_ACQUIRE);
            return true;
        }
    }
    return false;
}

bool trigger_t2_runtime_preview_history(uint32_t position_id, uint32_t age,
                                        trigger_position_descriptor_t *out)
{
    return position_id >= 1u &&
        preview_copy(position_id - 1u, age, out);
}

static void prepare_deadlines_core0(uint32_t session,
    const trigger_t2_runtime_snapshot_t *snapshot)
{
    if (!s_local_template_count) return;
    vdc_dpll_manager_committed_model_t model;
    if (!vdc_dpll_manager_get_committed_model(&model) ||
        model.session != snapshot->cursor.session || !model.token || !model.dco.valid ||
        model.dco.nominal_period_ns != s_config.grid.period_ns) return;
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT; ++index) {
        if (__atomic_load_n(&s_frozen_public, __ATOMIC_ACQUIRE) & (1u << index)) continue;
        trigger_position_descriptor_t descriptor;
        if (!preview_copy(index, 0u, &descriptor)) continue;
        /* Mailbox has one RUN writer: Core0 can inspect its own old words. */
        t2_deadline_plan_t previous;
        memcpy(&previous, s_deadline_mailbox[index].words, sizeof(previous));
        if (previous.prediction_version == descriptor.prediction_version &&
            previous.model_token == model.token) continue;
        trigger_action_template_instance_t instance;
        if (!trigger_action_template_expand(&s_local_template, &descriptor,
                s_config.grid.subdivisions, &instance)) continue;
        const uint64_t q = instance.position.action_q;
        const uint64_t coarse = q / s_config.grid.subdivisions;
        const uint32_t fine = (uint32_t)(q % s_config.grid.subdivisions);
        if (q <= snapshot->cursor.action_cursor ||
            coarse > (UINT64_MAX - s_config.grid.anchor_vdc_ns) / s_config.grid.period_ns)
            continue;
        uint64_t output_ns = s_config.grid.anchor_vdc_ns + coarse * s_config.grid.period_ns;
        /* Round toward the first representable ns/tick at or after the slice. */
        const uint64_t fraction = (uint64_t)fine * s_config.grid.period_ns;
        const uint64_t fine_ns = fraction / s_config.grid.subdivisions +
            (fraction % s_config.grid.subdivisions != 0u);
        if (output_ns > UINT64_MAX - fine_ns) continue;
        output_ns += fine_ns;
        uint64_t local_ns, raw_tick;
        if (!vdc_domain_dco_output_to_local_ns(&model.dco, output_ns, &local_ns) ||
            !vdc_timer1_ns_to_ticks(BOARD_SYS_CLOCK_HZ, local_ns, true, &raw_tick) ||
            raw_tick < model.valid_from_raw) continue;
        const t2_deadline_plan_t plan = {
            .action_q = q, .deadline_tick = raw_tick,
            .session = model.session, .ring_config_seq = snapshot->cursor.ring_config_seq,
            .prediction_version = descriptor.prediction_version,
            .expiry_sync = descriptor.expiry_sync, .model_token = model.token};
        if (session != __atomic_load_n(&s_session, __ATOMIC_ACQUIRE)) return;
        if (!(__atomic_load_n(&s_frozen_public, __ATOMIC_ACQUIRE) & (1u << index)))
            deadline_publish(index, &plan);
    }
}

void trigger_t2_runtime_prepare_core0(void)
{
    if (s_core0_prepare_calls != UINT32_MAX) ++s_core0_prepare_calls;
    const uint32_t session = __atomic_load_n(&s_session, __ATOMIC_ACQUIRE);
    trigger_t2_runtime_snapshot_t snapshot;
    if (!trigger_t2_runtime_snapshot(&snapshot) || !snapshot.active ||
        !snapshot.cursor.valid) { s_core0_prepare_last_reason=1u; return; }
    if (!snapshot.origin) {
        prepare_deadlines_core0(session, &snapshot);
        return;
    }
    if (snapshot.last_sync_sequence == s_count_last_seq ||
        !snapshot.last_sync_sequence) { s_core0_prepare_last_reason=2u; return; }

    const uint64_t coarse =
        snapshot.cursor.action_cursor / s_config.grid.subdivisions;
    if (coarse > UINT32_MAX || snapshot.last_counter_count > UINT32_MAX) { s_core0_prepare_last_reason=3u; return; }
    s_count_last_seq = snapshot.last_sync_sequence;
    s_count_history[s_count_head] = (t2_count_sample_t){
        .count = (uint32_t)snapshot.last_counter_count, .coarse = (uint32_t)coarse};
    s_count_head = (s_count_head + 1u) % T2_WINDOW_HISTORY_CAPACITY;
    if (s_count_size < T2_WINDOW_HISTORY_CAPACITY) s_count_size++;
    if (distributed_refmem_position_descriptor_pending()) { s_core0_prepare_last_reason=4u; return; }

    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT;
         ++index) {
        const uint32_t bit = 1u << index;
        if (__atomic_load_n(&s_threshold_public, __ATOMIC_ACQUIRE) & bit ||
            snapshot.predictor.announced_mask & bit ||
            snapshot.last_counter_count >= snapshot.predictor.threshold_count[index] ||
            snapshot.last_counter_count >= snapshot.predictor.target_count[index])
            continue;

        const uint64_t to_threshold =
            snapshot.predictor.threshold_count[index] - snapshot.last_counter_count;
        const uint64_t span = snapshot.predictor.target_count[index] -
                              snapshot.predictor.origin_count;
        const uint32_t window = to_threshold <= span / 20u ? 4u :
                                to_threshold <= span / 5u ? 8u : 16u;
        const uint32_t interval = window == 4u ? 1u :
                                  window == 8u ? 2u : 8u;
        if (s_last_calculated_coarse[index] &&
            coarse - s_last_calculated_coarse[index] < interval) continue;
        s_last_calculated_coarse[index] = (uint32_t)coarse;

        t2_count_sample_t old = {0};
        bool found = false;
        for (uint32_t age = 1u; age < s_count_size; ++age) {
            const uint32_t slot = (s_count_head + T2_WINDOW_HISTORY_CAPACITY -
                                   1u - age) % T2_WINDOW_HISTORY_CAPACITY;
            old = s_count_history[slot];
            if (old.coarse < coarse && coarse - old.coarse >= window) {
                found = true;
                break;
            }
        }
        if (!found || old.count >= snapshot.last_counter_count ||
            snapshot.last_counter_count - old.count > UINT32_MAX ||
            coarse - old.coarse > UINT32_MAX) { s_core0_prepare_last_reason=5u; continue; }

        trigger_position_descriptor_t previous = {0};
        (void)preview_copy(index, 0u, &previous);
        trigger_position_descriptor_t next = {
            .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
            .run_id = s_config.run_id,
            .sync_epoch = snapshot.cursor.grid_epoch,
            .position_id = index + 1u,
            .count_epoch = snapshot.predictor.count_epoch,
            .target_count = snapshot.predictor.target_count[index],
            .prediction_version = previous.prediction_version + 1u,
            .grid_generation = s_config.grid.generation,
            .deployment_generation = s_config.deployment_generation,
            .deployment_crc = s_deployment_crc,
            .valid_from_sync = (uint32_t)coarse,
            .expiry_sync = UINT32_MAX,
            .stage = TRIGGER_POSITION_STAGE_PREVIEW,
            .observed_count = snapshot.last_counter_count,
            .window_pulses = (uint32_t)(snapshot.last_counter_count - old.count),
            .window_syncs = (uint32_t)(coarse - old.coarse),
        };
        uint64_t target_q;
        if (!trigger_position_descriptor_solve_q(
                &next, s_config.grid.subdivisions, &target_q)) { s_core0_prepare_last_reason=6u; continue; }
        next.target_ms_index = target_q / s_config.grid.subdivisions;
        next.target_fine_slice_index =
            (uint32_t)(target_q % s_config.grid.subdivisions);
        if (next.target_ms_index < coarse ||
            next.target_ms_index > UINT32_MAX - s_config.descriptor_window_sync) {
            s_core0_prepare_last_reason=7u; continue;
        }
        next.expiry_sync = (uint32_t)next.target_ms_index +
                           s_config.descriptor_window_sync;
        if (previous.prediction_version &&
            previous.target_ms_index == next.target_ms_index &&
            previous.target_fine_slice_index == next.target_fine_slice_index)
            continue;
        if (previous.prediction_version >= TRIGGER_T2_PREDICTION_VERSION_MAX) {
            /* Preserve the last valid local version for FREEZE fallback.
             * A new run is needed before the compact revision can restart. */
            __atomic_fetch_or(&s_version_exhausted, bit, __ATOMIC_RELEASE);
            s_core0_prepare_last_reason = 10u;
            continue;
        }
        next.descriptor_crc32 = trigger_position_descriptor_crc(&next);
        if (session != __atomic_load_n(&s_session, __ATOMIC_ACQUIRE)) return;
        if (__atomic_load_n(&s_threshold_public, __ATOMIC_ACQUIRE) & bit)
            continue;
        if (distributed_refmem_stage_position_descriptor(
                &next, s_config.target_mask)) {
            preview_save(index, &next); ++s_core0_preview_saved; s_core0_prepare_last_reason=8u;
        } else s_core0_prepare_last_reason=9u;
        return;
    }
}
