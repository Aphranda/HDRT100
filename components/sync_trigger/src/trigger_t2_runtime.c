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

static void publish_begin(void)
{
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_ACQ_REL);
}

static void publish_end(void)
{
    (void)__atomic_add_fetch(&s_guard, 1u, __ATOMIC_RELEASE);
}

static bool local_target_q(uint32_t index, uint64_t count,
                           uint64_t *target_ms_index,
                           uint32_t *target_fine_slice_index)
{
    if (index >= TRIGGER_T2_RUNTIME_POSITION_COUNT ||
        !target_ms_index || !target_fine_slice_index ||
        !s_state.predictor.rate_valid ||
        !s_state.cursor.valid) return false;
    const uint64_t target = s_state.predictor.target_count[index];
    const uint64_t remaining = target > count ? target - count : 0u;
    const uint64_t rate = s_state.predictor.pulses_per_sync_q16;
    const uint64_t subdivisions = s_config.grid.subdivisions;
    if (!rate || subdivisions < 2u ||
        remaining > UINT64_MAX / subdivisions ||
        remaining * subdivisions > (UINT64_MAX >> 16u))
        return false;
    const uint64_t numerator = remaining * subdivisions * 65536u;
    const uint64_t syncs_ahead = (numerator / rate) +
        ((numerator % rate) != 0u ? 1u : 0u);
    if (s_state.cursor.pulse_index > UINT64_MAX / subdivisions ||
        s_state.cursor.pulse_index * subdivisions > UINT64_MAX - syncs_ahead)
        return false;
    const uint64_t target_q = s_state.cursor.pulse_index * subdivisions +
        syncs_ahead;
    *target_ms_index = target_q / subdivisions;
    *target_fine_slice_index = (uint32_t)(target_q % subdivisions);
    return true;
}

static bool build_position(uint32_t index, uint64_t count,
                           trigger_position_descriptor_t *out)
{
    uint64_t target_ms;
    uint32_t target_fine;
    if (!local_target_q(index, count, &target_ms, &target_fine)) return false;
    return trigger_turntable_predictor_build_descriptor(
        &s_state.predictor, s_config.run_id, s_state.cursor.session,
        index + 1u, target_ms, target_fine, s_config.grid.generation,
        s_config.deployment.generation,
        trigger_deployment_crc(&s_config.deployment),
        s_state.cursor.sample_seq, UINT32_MAX, out);
}

static bool stage_one_position(uint64_t count)
{
    if (!s_pending_mask || distributed_refmem_position_descriptor_pending())
        return true;
    for (uint32_t index = 0u; index < TRIGGER_T2_RUNTIME_POSITION_COUNT;
         ++index) {
        if ((s_pending_mask & (1u << index)) == 0u) continue;
        trigger_position_descriptor_t descriptor;
        if (!build_position(index, count, &descriptor)) {
            s_state.descriptor_reject_count++;
            s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
            return false;
        }
        if (!distributed_refmem_stage_position_descriptor(
                &descriptor, s_config.target_mask)) {
            s_state.transport_reject_count++;
            s_state.last_error = TRIGGER_T2_RUNTIME_TRANSPORT;
            return false;
        }
        s_state.descriptors[index] = descriptor;
        s_state.published_mask |= 1u << index;
        s_pending_mask &= ~(1u << index);
        return true;
    }
    return true;
}

static bool accept_remote_position(uint32_t sync_sequence)
{
    trigger_position_descriptor_t descriptor;
    if (!distributed_refmem_get_position_descriptor(
            s_config.source_slot, &descriptor)) return true;
    if (descriptor.run_id != s_config.run_id ||
        descriptor.grid_generation != s_config.grid.generation ||
        descriptor.deployment_generation != s_config.deployment.generation ||
        descriptor.deployment_crc != trigger_deployment_crc(&s_config.deployment) ||
        descriptor.position_id < TRIGGER_TURNTABLE_POSITION_START ||
        descriptor.position_id > TRIGGER_TURNTABLE_POSITION_TERMINAL ||
        sync_sequence < descriptor.valid_from_sync ||
        sync_sequence > descriptor.expiry_sync) {
        s_state.descriptor_reject_count++;
        s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
        return false;
    }
    const uint32_t index = descriptor.position_id - 1u;
    if (s_state.received_mask & (1u << index) &&
        s_state.descriptors[index].prediction_version >=
            descriptor.prediction_version) return true;
    if (!s_actions.templates.count) {
        s_state.descriptors[index] = descriptor;
        s_state.received_mask |= 1u << index;
        return true;
    }
    if (trigger_action_runtime_stage(&s_actions, &descriptor,
                                     s_config.grid.subdivisions) !=
        TRIGGER_ACTION_RUNTIME_OK) {
        s_state.descriptor_reject_count++;
        s_state.last_error = TRIGGER_T2_RUNTIME_ACTION;
        return false;
    }
    s_state.descriptors[index] = descriptor;
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
        !config->target_mask ||
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
        trigger_time_cursor_configure(
            s_config.grid.epoch, s_config.grid.generation,
            s_config.grid.subdivisions) &&
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
    s_state.last_error = TRIGGER_T2_RUNTIME_OK;
    publish_end();
    return true;
}

void trigger_t2_runtime_stop(void)
{
    publish_begin();
    s_state.active = 0u;
    s_pending_mask = 0u;
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
        if (trigger_turntable_predictor_observe_at_sync(
                &s_state.predictor, &s_config.predictor,
                cursor.sample_seq, counter.io.count32, &mask) !=
            TRIGGER_TURNTABLE_OK) {
            s_state.last_error = TRIGGER_T2_RUNTIME_DESCRIPTOR;
            T2_RETURN(s_state.last_error);
        }
        s_pending_mask |= mask;
        if (!stage_one_position(counter.io.count32)) T2_RETURN(s_state.last_error);
    } else {
        if (!accept_remote_position(cursor.sample_seq)) T2_RETURN(s_state.last_error);
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
