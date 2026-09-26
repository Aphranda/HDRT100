#include "trigger_t2_runtime.h"

#include <string.h>

#include "distributed_refmem.h"
#include "sync_io_gateway.h"
#include "sync_io_switch.h"
#include "trigger_counter_service.h"

static trigger_t2_runtime_config_t s_config;
static trigger_t2_runtime_snapshot_t s_state;
static trigger_action_runtime_t s_actions;
static uint32_t s_pending_mask;
static volatile uint32_t s_guard;

#define T2_WINDOW_HISTORY_CAPACITY 17u
typedef struct { uint64_t count, coarse; } t2_count_sample_t;
static t2_count_sample_t s_count_history[T2_WINDOW_HISTORY_CAPACITY];
static uint32_t s_count_head, s_count_size, s_count_last_seq;
static uint64_t s_last_calculated_coarse[TRIGGER_T2_RUNTIME_POSITION_COUNT];
static trigger_position_descriptor_t s_preview_history[TRIGGER_T2_RUNTIME_POSITION_COUNT][3];
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
        *out = s_preview_history[index][age];
        if (before == __atomic_load_n(&s_preview_guard, __ATOMIC_ACQUIRE))
            return out->prediction_version != 0u;
    }
    return false;
}

static void preview_save(uint32_t index,
                         const trigger_position_descriptor_t *descriptor)
{
    (void)__atomic_add_fetch(&s_preview_guard, 1u, __ATOMIC_ACQ_REL);
    s_preview_history[index][2] = s_preview_history[index][1];
    s_preview_history[index][1] = s_preview_history[index][0];
    s_preview_history[index][0] = *descriptor;
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

static bool accept_remote_position(uint64_t current_ms)
{
    trigger_position_descriptor_t descriptor;
    if (!distributed_refmem_get_position_descriptor(
            s_config.source_slot, &descriptor)) return true;
    if (descriptor.stage == TRIGGER_POSITION_STAGE_FREEZE) {
        if (descriptor.position_id < 1u ||
            descriptor.position_id > TRIGGER_T2_RUNTIME_POSITION_COUNT)
            return false;
        const uint32_t index = descriptor.position_id - 1u;
        if (s_state.frozen_mask & (1u << index)) return true;
        s_state.frozen_mask |= 1u << index;
        s_state.freeze_version[index] = descriptor.prediction_version;
        trigger_position_descriptor_t latest;
        const bool have_latest = preview_copy(index, 0u, &latest);
        s_state.freeze_local_age[index] = 0xFFu;
        s_state.freeze_local_version[index] = 0u;
        if (have_latest) {
            s_state.freeze_local_version[index] = latest.prediction_version;
            for (uint32_t age = 0u; age < 3u; ++age) {
                trigger_position_descriptor_t candidate;
                if (preview_copy(index, age, &candidate) &&
                    candidate.prediction_version == latest.prediction_version) {
                    s_state.freeze_local_age[index] = (uint8_t)age;
                    break;
                }
            }
        }
        if (have_latest && latest.run_id == s_config.run_id &&
            latest.sync_epoch == s_state.cursor.grid_epoch &&
            latest.count_epoch == s_state.predictor.count_epoch &&
            latest.grid_generation == s_config.grid.generation &&
            latest.deployment_generation == s_config.deployment.generation &&
            latest.deployment_crc == trigger_deployment_crc(&s_config.deployment) &&
            latest.valid_from_sync <= current_ms &&
            current_ms <= latest.expiry_sync &&
            !(s_state.received_mask & (1u << index)) && s_actions.templates.count) {
            trigger_action_runtime_t staged = s_actions;
            if (trigger_action_runtime_stage(&staged, &latest,
                                             s_config.grid.subdivisions) ==
                    TRIGGER_ACTION_RUNTIME_OK) {
                s_actions = staged;
                s_state.descriptors[index] = latest;
                s_state.received_mask |= 1u << index;
                if (latest.prediction_version != descriptor.prediction_version)
                    s_state.freeze_fallback_count++;
            } else {
                s_state.freeze_mismatch_count++;
            }
        } else if (!have_latest ||
                   latest.prediction_version != descriptor.prediction_version) {
            s_state.freeze_mismatch_count++;
            if (have_latest) s_state.freeze_fallback_count++;
            else s_state.freeze_missing_local_count++;
        }
        return true;
    }
    if (descriptor.run_id != s_config.run_id ||
        descriptor.sync_epoch != s_state.cursor.grid_epoch ||
        descriptor.count_epoch != s_state.predictor.count_epoch ||
        descriptor.grid_generation != s_config.grid.generation ||
        descriptor.deployment_generation != s_config.deployment.generation ||
        descriptor.deployment_crc != trigger_deployment_crc(&s_config.deployment) ||
        descriptor.position_id < TRIGGER_TURNTABLE_POSITION_START ||
        descriptor.position_id > TRIGGER_TURNTABLE_POSITION_TERMINAL ||
        current_ms < descriptor.valid_from_sync ||
        current_ms > descriptor.expiry_sync) {
        s_state.descriptor_reject_count++;
        s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
        return false;
    }
    const uint32_t index = descriptor.position_id - 1u;
    if (s_state.frozen_mask & (1u << index)) return true;
    if (s_state.emitted_position_mask & (1u << index)) {
        if (s_state.descriptors[index].descriptor_crc32 ==
            descriptor.descriptor_crc32) return true;
        s_state.descriptor_reject_count++;
        s_state.last_error = TRIGGER_T2_RUNTIME_ACTION;
        return false;
    }
    if (s_state.received_mask & (1u << index) &&
        s_state.descriptors[index].prediction_version >=
            descriptor.prediction_version) return true;
    if (!s_actions.templates.count) {
        s_state.descriptors[index] = descriptor;
        preview_save(index, &descriptor);
        s_state.received_mask |= 1u << index;
        return true;
    }
    trigger_action_runtime_t staged = s_actions;
    if (trigger_action_runtime_stage(&staged, &descriptor,
                                     s_config.grid.subdivisions) !=
        TRIGGER_ACTION_RUNTIME_OK) {
        s_state.descriptor_reject_count++;
        s_state.last_error = TRIGGER_T2_RUNTIME_ACTION;
        return false;
    }
    for (uint32_t i = 0u; i < staged.instance_count; ++i) {
        if (staged.instance[i].position.action_q < s_state.cursor.action_cursor ||
            staged.instance[i].position.action_q % s_config.grid.subdivisions != 0u) {
            s_state.descriptor_reject_count++;
            s_state.last_error = TRIGGER_T2_RUNTIME_ACTION;
            return false;
        }
    }
    s_actions = staged;
    s_state.descriptors[index] = descriptor;
    preview_save(index, &descriptor);
    s_state.received_mask |= 1u << index;
    return true;
}

static void collect_events(void)
{
    trigger_action_runtime_event_t next[TRIGGER_T2_RUNTIME_EVENT_CAPACITY];
    uint32_t count = 0u;
    if (trigger_action_runtime_advance(
            &s_actions, s_state.cursor.action_cursor, next,
            TRIGGER_T2_RUNTIME_EVENT_CAPACITY, &count) !=
        TRIGGER_ACTION_RUNTIME_OK) {
        s_state.last_error = TRIGGER_T2_RUNTIME_BACKWARD;
        return;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        const uint32_t position_index = next[i].position_id - 1u;
        if (position_index < TRIGGER_T2_RUNTIME_POSITION_COUNT)
            s_state.emitted_position_mask |= 1u << position_index;
        if (s_state.event_count >= TRIGGER_T2_RUNTIME_EVENT_CAPACITY) {
            s_state.event_dropped_count++;
            continue;
        }
        s_state.events[s_state.event_count++] = next[i];
    }
}

static bool dispatch_event(uint32_t index)
{
    const trigger_action_runtime_event_t *event=&s_state.events[index];
    if (event->role != s_config.local_role) return true;
    if (event->kind == TRIGGER_ACTION_TEMPLATE_SWITCH)
        return sync_io_switch_realtime_fire(event->argument);
    if (event->kind == TRIGGER_ACTION_TEMPLATE_GATEWAY)
        return sync_io_gateway_realtime_fire(event->argument);
    return false;
}

static void dispatch_pending_events(void)
{
    bool failed=false;
    sync_io_switch_realtime_service_core1();
    sync_io_gateway_realtime_service_core1();
    for (uint32_t i=0u;i<s_state.event_count;++i) {
        if (s_state.event_dispatched[i]) continue;
        if (dispatch_event(i)) s_state.event_dispatched[i]=1u;
        else failed=true;
    }
    if (failed) s_state.last_error=TRIGGER_T2_RUNTIME_ACTION;
}

bool trigger_t2_runtime_configure(const trigger_t2_runtime_config_t *config)
{
    if (!config || !trigger_time_grid_valid(&config->grid) ||
        !trigger_turntable_predictor_config_valid(&config->predictor) ||
        trigger_deployment_validate(&config->deployment, UINT32_MAX) !=
            TRIGGER_DEPLOYMENT_OK || !config->run_id ||
        config->source_slot >= TRIGGER_POSITION_DESCRIPTOR_NODE_COUNT ||
        !config->target_mask || !config->descriptor_window_sync ||
        trigger_action_template_table_validate(
            &config->templates, &config->deployment, config->template_crc) !=
            TRIGGER_ACTION_TEMPLATE_TABLE_OK) return false;
    if (s_state.active) return false;
    publish_begin();
    s_config = *config;
    memset(&s_state, 0, sizeof(s_state));
    memset(&s_actions, 0, sizeof(s_actions));
    if (trigger_action_runtime_configure_for_role(
            &s_actions, &config->templates, &config->deployment,
            config->template_crc, config->local_role) !=
        TRIGGER_ACTION_RUNTIME_OK) {
        memset(&s_config, 0, sizeof(s_config));
        publish_end();
        return false;
    }
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
    publish_begin();
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
    distributed_refmem_request_position_cancel();
    trigger_time_cursor_clear();
    publish_end();
}

trigger_t2_runtime_result_t trigger_t2_runtime_service(void)
{
    if (!s_state.configured) return TRIGGER_T2_RUNTIME_NOT_CONFIGURED;
    if (!s_state.active) return TRIGGER_T2_RUNTIME_NOT_ACTIVE;
    publish_begin();
#define T2_RETURN(value) do { publish_end(); return (value); } while (0)
    trigger_time_cursor_service();
    trigger_time_cursor_snapshot_t cursor;
    if (!trigger_time_cursor_snapshot(&cursor)) {
        s_state.last_error = TRIGGER_T2_RUNTIME_CURSOR;
        T2_RETURN(s_state.last_error);
    }
    if (!cursor.valid) {
        s_state.last_error = TRIGGER_T2_RUNTIME_CURSOR;
        T2_RETURN(s_state.last_error);
    }
    s_state.cursor = cursor;
    s_state.last_error = TRIGGER_T2_RUNTIME_OK;
    if (cursor.sample_seq == s_state.last_sync_sequence) {
        dispatch_pending_events();
        T2_RETURN(TRIGGER_T2_RUNTIME_OK);
    }
    s_state.last_sync_sequence = cursor.sample_seq;

    if (s_state.origin) {
        trigger_counter_snapshot_t counter;
        if (trigger_counter_snapshot_core1(&counter) != COUNTER_OK ||
            !counter.loaded || !counter.io.valid) {
            s_state.last_error = TRIGGER_T2_RUNTIME_COUNTER;
            T2_RETURN(s_state.last_error);
        }
        s_state.last_counter_count = counter.io.count32;
        uint32_t mask = 0u;
        if (trigger_turntable_predictor_observe(
                &s_state.predictor, &s_config.predictor,
                counter.io.count32, &mask) !=
            TRIGGER_TURNTABLE_OK) {
            s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
            T2_RETURN(s_state.last_error);
        }
        s_pending_mask |= mask;
        if (!stage_one_freeze()) T2_RETURN(s_state.last_error);
    } else {
        if (!accept_remote_position(
                cursor.action_cursor / s_config.grid.subdivisions))
            T2_RETURN(s_state.last_error);
        collect_events();
    }
    dispatch_pending_events();
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
        out->predictor = s_state.predictor;
        const uint32_t after = __atomic_load_n(&s_guard, __ATOMIC_ACQUIRE);
        if (before == after) return true;
    }
    return false;
}

bool trigger_t2_runtime_preview_history(uint32_t position_id, uint32_t age,
                                        trigger_position_descriptor_t *out)
{
    return position_id >= 1u &&
        preview_copy(position_id - 1u, age, out);
}

void trigger_t2_runtime_prepare_core0(void)
{
    const uint32_t session = __atomic_load_n(&s_session, __ATOMIC_ACQUIRE);
    trigger_t2_runtime_snapshot_t snapshot;
    if (!trigger_t2_runtime_snapshot(&snapshot) || !snapshot.active ||
        !snapshot.origin || !snapshot.cursor.valid ||
        snapshot.last_sync_sequence == s_count_last_seq ||
        !snapshot.last_sync_sequence) return;

    const uint64_t coarse =
        snapshot.cursor.action_cursor / s_config.grid.subdivisions;
    if (coarse > UINT32_MAX) return;
    s_count_last_seq = snapshot.last_sync_sequence;
    s_count_history[s_count_head] = (t2_count_sample_t){
        .count = snapshot.last_counter_count, .coarse = coarse};
    s_count_head = (s_count_head + 1u) % T2_WINDOW_HISTORY_CAPACITY;
    if (s_count_size < T2_WINDOW_HISTORY_CAPACITY) s_count_size++;
    if (distributed_refmem_position_descriptor_pending()) return;

    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT;
         ++index) {
        const uint32_t bit = 1u << index;
        if (snapshot.predictor.announced_mask & bit ||
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
        s_last_calculated_coarse[index] = coarse;

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
            coarse - old.coarse > UINT32_MAX) continue;

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
            .deployment_generation = s_config.deployment.generation,
            .deployment_crc = trigger_deployment_crc(&s_config.deployment),
            .valid_from_sync = (uint32_t)coarse,
            .expiry_sync = UINT32_MAX,
            .stage = TRIGGER_POSITION_STAGE_PREVIEW,
            .observed_count = snapshot.last_counter_count,
            .window_pulses = (uint32_t)(snapshot.last_counter_count - old.count),
            .window_syncs = (uint32_t)(coarse - old.coarse),
        };
        if (!next.prediction_version) next.prediction_version = 1u;
        uint64_t target_q;
        if (!trigger_position_descriptor_solve_q(
                &next, s_config.grid.subdivisions, &target_q)) continue;
        next.target_ms_index = target_q / s_config.grid.subdivisions;
        next.target_fine_slice_index =
            (uint32_t)(target_q % s_config.grid.subdivisions);
        if (next.target_ms_index < coarse ||
            next.target_ms_index > UINT32_MAX - s_config.descriptor_window_sync)
            continue;
        next.expiry_sync = (uint32_t)next.target_ms_index +
                           s_config.descriptor_window_sync;
        if (previous.prediction_version &&
            previous.target_ms_index == next.target_ms_index &&
            previous.target_fine_slice_index == next.target_fine_slice_index)
            continue;
        next.descriptor_crc32 = trigger_position_descriptor_crc(&next);
        if (session != __atomic_load_n(&s_session, __ATOMIC_ACQUIRE)) return;
        if (distributed_refmem_stage_position_descriptor(
                &next, s_config.target_mask)) preview_save(index, &next);
        return;
    }
}
