#include "trigger_sequence_service.h"

#include <string.h>

#include "osal.h"
#include "sync_io_sequence.h"
#include "sync_trigger.h"

typedef enum { COMMAND_NONE, COMMAND_START, COMMAND_STOP, COMMAND_PAUSE,
               COMMAND_CONTINUE, COMMAND_STEP, COMMAND_EXHAUSTED,
               COMMAND_GATEWAY_FIRE, COMMAND_FINISH } command_t;

typedef struct {
    trigger_sequence_service_io_t io;
    uint32_t count;
    uint32_t generation;
    trigger_sequence_gateway_config_t gateway;
    uint32_t repeat_count;
    uint8_t ids[TRIGGER_SEQUENCE_STATE_MAX];
    uint32_t values[TRIGGER_SEQUENCE_STATE_MAX];
} run_config_t;
_Static_assert(TRIGGER_SEQUENCE_STATE_MAX <= 256u, "compact state identifiers");

static trigger_sequence_store_t s_store;
static trigger_sequence_service_io_t s_io;
static uint8_t s_codes[TRIGGER_SEQUENCE_STATE_MAX];
static bool s_code_valid[TRIGGER_SEQUENCE_STATE_MAX];
/* Core0 fills this only while the published owner is IDLE. The START mailbox
 * publishes it to Core1; it remains immutable until STOP publishes IDLE after
 * backend cleanup. No second launch/runtime copy is needed. */
static run_config_t s_run;
static trigger_sequence_service_status_t s_runtime;
static trigger_sequence_service_status_t s_published;
static command_t s_command;
static uint32_t s_command_serial;
static bool s_processing;
static uint32_t s_queued_cancellations;
static uint32_t s_busy_rejected;
static uint32_t s_notready_rejected;
static uint32_t s_written_receipts;
static bool s_pause_requested;
static bool s_run_armed;
static trigger_sequence_gateway_config_t s_gateway;
static bool (*s_gateway_guard)(void);
static uint32_t s_repeat_count = 1u;

static uint32_t saturated_add(uint32_t value, uint32_t add)
{
    return add > UINT32_MAX - value ? UINT32_MAX : value + add;
}

static void increment(uint32_t *value)
{
    *value = saturated_add(*value, 1u);
}

static void reset_cursors(trigger_sequence_service_status_t *status)
{
    status->current_index = TRIGGER_SEQUENCE_NO_INDEX;
    status->current_state = TRIGGER_SEQUENCE_NO_INDEX;
    status->executed_index = TRIGGER_SEQUENCE_NO_INDEX;
    status->executed_state = TRIGGER_SEQUENCE_NO_INDEX;
    status->completed_index = TRIGGER_SEQUENCE_NO_INDEX;
    status->completed_state = TRIGGER_SEQUENCE_NO_INDEX;
}

void trigger_sequence_service_init(void)
{
    trigger_sequence_init(&s_store);
    memset(&s_io, 0, sizeof(s_io));
    memset(s_code_valid, 0, sizeof(s_code_valid));
    memset(&s_runtime, 0, sizeof(s_runtime));
    reset_cursors(&s_runtime);
    s_published = s_runtime;
    s_command = COMMAND_NONE;
    s_command_serial = 0;
    s_processing = false;
    s_queued_cancellations = 0;
    s_busy_rejected = 0;
    s_notready_rejected = 0;
    s_written_receipts = 0;
    s_pause_requested = false;
    s_run_armed = false;
    memset(&s_gateway, 0, sizeof(s_gateway));
    s_gateway_guard = NULL;
    s_repeat_count = 1u;
}

trigger_sequence_store_t *trigger_sequence_service_config(void)
{
    osal_critical_enter();
    bool idle = s_published.state == TRIGGER_SEQUENCE_SERVICE_IDLE &&
                s_command == COMMAND_NONE && !s_processing;
    osal_critical_exit();
    if (idle) (void)trigger_sequence_set_frozen(&s_store, false);
    return &s_store;
}

static void synchronize_generation(void)
{
    if (s_io.generation != s_store.generation) {
        memset(s_code_valid, 0, sizeof(s_code_valid));
        s_io.generation = s_store.generation;
        s_io.valid = false;
    }
}

static bool configuration_available(void)
{
    (void)trigger_sequence_service_config();
    synchronize_generation();
    return !s_store.frozen;
}

trigger_sequence_service_result_t trigger_sequence_service_set_source(
    uint32_t source, bool falling)
{
    if (!configuration_available()) return TRIGGER_SEQUENCE_SERVICE_FROZEN;
    if (source > 4u) return TRIGGER_SEQUENCE_SERVICE_INVALID;
    s_io.source = source;
    s_io.falling = falling;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}

trigger_sequence_service_result_t trigger_sequence_service_set_io(
    uint32_t output_mask, uint32_t completion_channel,
    uint32_t settle_us, uint32_t pulse_us)
{
    if (!configuration_available()) return TRIGGER_SEQUENCE_SERVICE_FROZEN;
    if (!s_store.configured) return TRIGGER_SEQUENCE_SERVICE_CONFIG;
    if (completion_channel < 1u || completion_channel > 4u)
        return TRIGGER_SEQUENCE_SERVICE_INVALID;
    return trigger_sequence_service_set_outputs(
        output_mask, 1u << (completion_channel - 1u),
        TRIGGER_SEQUENCE_STATUS_PULSE, settle_us, pulse_us);
}

trigger_sequence_service_result_t trigger_sequence_service_set_outputs(
    uint32_t sequence_output_mask, uint32_t status_output_mask,
    trigger_sequence_status_mode_t status_mode,
    uint32_t settle_us, uint32_t pulse_us)
{
    if (!configuration_available()) return TRIGGER_SEQUENCE_SERVICE_FROZEN;
    if (!s_store.configured) return TRIGGER_SEQUENCE_SERVICE_CONFIG;
    if (!sequence_output_mask ||
        ((status_mode == TRIGGER_SEQUENCE_STATUS_NONE) != (status_output_mask == 0u)) ||
        (sequence_output_mask | status_output_mask) > 15u ||
        (sequence_output_mask & status_output_mask) != 0u ||
        (status_mode != TRIGGER_SEQUENCE_STATUS_PULSE &&
         status_mode != TRIGGER_SEQUENCE_STATUS_LEVEL &&
         status_mode != TRIGGER_SEQUENCE_STATUS_NONE) ||
        settle_us > SYNC_IO_SEQUENCE_TIME_MAX_US ||
        (status_mode == TRIGGER_SEQUENCE_STATUS_PULSE &&
         (pulse_us == 0u || pulse_us > SYNC_IO_SEQUENCE_TIME_MAX_US)) ||
        (status_mode != TRIGGER_SEQUENCE_STATUS_PULSE && pulse_us != 0u))
        return TRIGGER_SEQUENCE_SERVICE_INVALID;
    /* Existing mappings survive timing changes, but not output-lane changes. */
    if (s_io.sequence_output_mask != sequence_output_mask)
        memset(s_code_valid, 0, sizeof(s_code_valid));
    s_io.sequence_output_mask = sequence_output_mask;
    s_io.status_output_mask = status_output_mask;
    s_io.status_mode = status_mode;
    s_io.settle_us = settle_us;
    s_io.pulse_us = pulse_us;
    s_io.valid = true;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}

trigger_sequence_service_result_t trigger_sequence_service_set_code(
    uint32_t state_id, uint32_t value)
{
    if (!configuration_available()) return TRIGGER_SEQUENCE_SERVICE_FROZEN;
    if (!s_io.valid) return TRIGGER_SEQUENCE_SERVICE_IO_CONFIG;
    if (state_id >= s_store.state_count || state_id >= TRIGGER_SEQUENCE_STATE_MAX ||
        (value & ~s_io.sequence_output_mask)) return TRIGGER_SEQUENCE_SERVICE_INVALID;
    s_codes[state_id] = (uint8_t)value;
    s_code_valid[state_id] = true;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}

void trigger_sequence_service_get_io(trigger_sequence_service_io_t *io)
{
    synchronize_generation();
    if (io) *io = s_io;
}

bool trigger_sequence_service_get_code(uint32_t state_id, uint32_t *value)
{
    synchronize_generation();
    if (!value || state_id >= TRIGGER_SEQUENCE_STATE_MAX || !s_code_valid[state_id])
        return false;
    *value = s_codes[state_id];
    return true;
}

trigger_sequence_service_result_t trigger_sequence_service_start(const char *plan_id)
{
    if (!configuration_available()) return TRIGGER_SEQUENCE_SERVICE_FROZEN;
    if (s_gateway.enabled &&
        (s_io.source != 0u || s_io.status_mode != TRIGGER_SEQUENCE_STATUS_NONE ||
         (s_io.sequence_output_mask & s_gateway.trigger_output_mask) != 0u ||
         s_gateway_guard == NULL || !s_gateway_guard()))
        return TRIGGER_SEQUENCE_SERVICE_CONFIG;
    trigger_sequence_check_t check;
    if (trigger_sequence_check(&s_store, plan_id, &check) != TRIGGER_SEQUENCE_OK)
        return TRIGGER_SEQUENCE_SERVICE_CONFIG;
    if (!s_io.valid) return TRIGGER_SEQUENCE_SERVICE_IO_CONFIG;
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(&s_store, plan_id);
    if (!plan || !plan->count) return TRIGGER_SEQUENCE_SERVICE_CONFIG;
    if (s_repeat_count != 0u && (uint64_t)plan->count * s_repeat_count > UINT32_MAX)
        return TRIGGER_SEQUENCE_SERVICE_INVALID;
    for (uint32_t i = 0; i < plan->count; ++i) {
        if (!s_code_valid[plan->state_ids[i]]) return TRIGGER_SEQUENCE_SERVICE_CODE_MISSING;
    }
    osal_critical_enter();
    bool exhausted = s_published.run_id == UINT32_MAX;
    osal_critical_exit();
    if (exhausted) return TRIGGER_SEQUENCE_SERVICE_EXHAUSTED;
    if (!sync_trigger_sequence_can_start() || !sync_io_sequence_reserve())
        return TRIGGER_SEQUENCE_SERVICE_RESOURCE;
    if (plan_id && trigger_sequence_activate(&s_store, plan_id) != TRIGGER_SEQUENCE_OK) {
        sync_io_sequence_release();
        return TRIGGER_SEQUENCE_SERVICE_CONFIG;
    }
    s_run.io = s_io;
    s_run.gateway = s_gateway;
    s_run.repeat_count = s_repeat_count;
    s_run.count = plan->count;
    s_run.generation = s_store.generation;
    for (uint32_t i = 0; i < plan->count; ++i) {
        s_run.ids[i] = plan->state_ids[i];
        s_run.values[i] = s_codes[plan->state_ids[i]];
    }
    (void)trigger_sequence_set_frozen(&s_store, true);
    osal_critical_enter();
    s_busy_rejected = 0;
    s_notready_rejected = 0;
    s_command = COMMAND_START;
    ++s_command_serial;
    osal_critical_exit();
    return TRIGGER_SEQUENCE_SERVICE_OK;
}

static trigger_sequence_service_result_t request(command_t command)
{
    trigger_sequence_service_result_t result = TRIGGER_SEQUENCE_SERVICE_OK;
    osal_critical_enter();
    trigger_sequence_service_state_t state = s_published.state;
    if (command == COMMAND_STOP) {
        if (s_command != COMMAND_STOP &&
            (s_command != COMMAND_NONE || state != TRIGGER_SEQUENCE_SERVICE_IDLE)) {
            if (s_command == COMMAND_STEP && !s_processing) increment(&s_queued_cancellations);
            s_command = COMMAND_STOP;
            ++s_command_serial;
        }
    } else if (s_command != COMMAND_NONE) {
        result = TRIGGER_SEQUENCE_SERVICE_BUSY;
    } else if (command == COMMAND_STEP && (s_io.source != 0u || s_gateway.enabled)) {
        result = TRIGGER_SEQUENCE_SERVICE_SOURCE_MISMATCH;
    } else if (command == COMMAND_STEP && state != TRIGGER_SEQUENCE_SERVICE_READY) {
        result = (state == TRIGGER_SEQUENCE_SERVICE_RUNNING ||
                  state == TRIGGER_SEQUENCE_SERVICE_PAUSING) ?
                 TRIGGER_SEQUENCE_SERVICE_BUSY : TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    } else if (command == COMMAND_STEP && s_published.accepted == UINT32_MAX) {
        result = TRIGGER_SEQUENCE_SERVICE_EXHAUSTED;
        s_command = COMMAND_EXHAUSTED;
        ++s_command_serial;
    } else if (command == COMMAND_PAUSE && state != TRIGGER_SEQUENCE_SERVICE_READY &&
               state != TRIGGER_SEQUENCE_SERVICE_RUNNING) {
        result = TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    } else if (command == COMMAND_CONTINUE && state != TRIGGER_SEQUENCE_SERVICE_PAUSED) {
        result = TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    } else if (command == COMMAND_STOP && state == TRIGGER_SEQUENCE_SERVICE_IDLE) {
        /* Idempotent STOP requires no owner work. */
    } else {
        s_command = command;
        ++s_command_serial;
    }
    if (command == COMMAND_STEP && result != TRIGGER_SEQUENCE_SERVICE_OK) {
        if (result == TRIGGER_SEQUENCE_SERVICE_BUSY) increment(&s_busy_rejected);
        else increment(&s_notready_rejected);
    }
    osal_critical_exit();
    return result;
}

trigger_sequence_service_result_t trigger_sequence_service_stop(void)
{ return request(COMMAND_STOP); }
trigger_sequence_service_result_t trigger_sequence_service_pause(void)
{ return request(COMMAND_PAUSE); }
trigger_sequence_service_result_t trigger_sequence_service_continue(void)
{ return request(COMMAND_CONTINUE); }
trigger_sequence_service_result_t trigger_sequence_service_step(void)
{ return request(COMMAND_STEP); }

trigger_sequence_service_result_t trigger_sequence_service_set_repeat(uint32_t count)
{
    if (!configuration_available()) return TRIGGER_SEQUENCE_SERVICE_FROZEN;
    s_repeat_count = count;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}
uint32_t trigger_sequence_service_get_repeat(void) { return s_repeat_count; }

trigger_sequence_service_result_t trigger_sequence_service_set_gateway(
    const trigger_sequence_gateway_config_t *config, bool (*start_guard)(void))
{
    if (!configuration_available()) return TRIGGER_SEQUENCE_SERVICE_FROZEN;
    if (config == NULL || (config->enabled &&
        (config->ready_input < 1u || config->ready_input > 4u ||
         config->trigger_output_mask == 0u || config->trigger_output_mask > 15u ||
         (config->trigger_output_mask & (config->trigger_output_mask - 1u)) != 0u ||
         config->pulse_us == 0u || config->pulse_us > SYNC_IO_SEQUENCE_TIME_MAX_US ||
         start_guard == NULL))) return TRIGGER_SEQUENCE_SERVICE_INVALID;
    s_gateway = *config;
    s_gateway_guard = start_guard;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}

static trigger_sequence_service_result_t cycle_action(
    command_t command, uint32_t run, uint32_t generation, uint32_t step)
{
    trigger_sequence_service_result_t result = TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    osal_critical_enter();
    if (s_gateway.enabled && s_published.run_id == run &&
        s_published.generation == generation && s_published.completed == step &&
        s_published.accepted == step && s_published.state == TRIGGER_SEQUENCE_SERVICE_READY &&
        !s_published.gateway_pulse_busy && !s_published.gateway_waiting) {
        if (s_command != COMMAND_NONE || s_processing) result = TRIGGER_SEQUENCE_SERVICE_BUSY;
        else {
            s_command = command;
            ++s_command_serial;
            result = TRIGGER_SEQUENCE_SERVICE_OK;
        }
    }
    osal_critical_exit();
    return result;
}

trigger_sequence_service_result_t trigger_sequence_service_gateway_fire(
    uint32_t run, uint32_t generation, uint32_t step)
{ return cycle_action(COMMAND_GATEWAY_FIRE, run, generation, step); }
trigger_sequence_service_result_t trigger_sequence_service_cycle_step(
    uint32_t run, uint32_t generation, uint32_t step)
{ return cycle_action(COMMAND_STEP, run, generation, step); }
trigger_sequence_service_result_t trigger_sequence_service_cycle_finish(
    uint32_t run, uint32_t generation, uint32_t step)
{ return cycle_action(COMMAND_FINISH, run, generation, step); }

static bool record_receipts(const sync_io_sequence_snapshot_t *io)
{
    if (!s_run.count || io->plan_count != s_run.count ||
        io->completed > io->written || io->written > io->accepted ||
        io->accepted - io->completed > 1u ||
        io->accepted < s_runtime.accepted || io->completed < s_runtime.completed ||
        io->written < s_written_receipts ||
        io->current_index != io->accepted % s_run.count ||
        io->completed_index != (io->completed ? io->completed % s_run.count : UINT32_MAX))
        return false;
    s_written_receipts = io->written;
    s_runtime.accepted = io->accepted;
    s_runtime.completed = io->completed;
    s_runtime.cancelled = io->cancelled;
    s_runtime.busy_rejected = io->busy_rejected;
    s_runtime.notready_rejected = io->notready_rejected;
    s_runtime.rejection_counts_pending = io->rejection_counts_pending;
    s_runtime.backend_fault = io->fault;
    s_runtime.tick_ns = io->tick_ns;
    s_runtime.timing_kind = io->timing_kind;
    s_runtime.gateway_waiting = io->gateway_waiting;
    s_runtime.gateway_pulse_busy = io->gateway_pulse_busy;
    s_runtime.gateway_trigger_count = io->gateway_trigger_count;
    s_runtime.gateway_ready_count = io->gateway_ready_count;
    s_runtime.gateway_cancelled = io->gateway_cancelled;
    /* PIO receipts prove ordering/counts, not an absolute physical timestamp. */
    s_runtime.written_at_us = 0;
    s_runtime.completion_rise_at_us = 0;
    s_runtime.completed_at_us = 0;
    s_runtime.alarm_late_us = 0;
    s_runtime.current_index = io->accepted % s_run.count;
    s_runtime.current_state = s_run.ids[s_runtime.current_index];
    s_runtime.cycles = io->accepted / s_run.count;
    if (io->written) {
        s_runtime.executed_index = io->written % s_run.count;
        s_runtime.executed_state = s_run.ids[s_runtime.executed_index];
    }
    if (io->completed) {
        s_runtime.completed_index = io->completed % s_run.count;
        s_runtime.completed_state = s_run.ids[s_runtime.completed_index];
    }
    s_runtime.next_index = (io->completed % s_run.count + 1u) % s_run.count;
    bool busy = io->busy || io->pending || io->accepted != io->completed;
    s_runtime.state = (s_pause_requested || io->paused) ?
        (busy ? TRIGGER_SEQUENCE_SERVICE_PAUSING : TRIGGER_SEQUENCE_SERVICE_PAUSED) :
        (busy || !io->ready ?
            (io->accepted ? TRIGGER_SEQUENCE_SERVICE_RUNNING : TRIGGER_SEQUENCE_SERVICE_STARTING) :
            TRIGGER_SEQUENCE_SERVICE_READY);
    return true;
}

static void fail(trigger_sequence_service_result_t error)
{
    sync_io_sequence_stop();
    sync_io_sequence_snapshot_t io;
    sync_io_sequence_get_snapshot(&io);
    if (s_run_armed) (void)record_receipts(&io);
    s_run_armed = false;
    s_runtime.backend_fault = io.fault;
    s_runtime.error = error;
    s_runtime.state = TRIGGER_SEQUENCE_SERVICE_FAULT;
    increment(&s_runtime.faults);
}

static void abort_run(void)
{
    sync_io_sequence_stop();
    sync_io_sequence_snapshot_t io;
    sync_io_sequence_get_snapshot(&io);
    if (s_run_armed && (!record_receipts(&io) || io.fault) &&
        s_runtime.error == TRIGGER_SEQUENCE_SERVICE_OK) {
        s_runtime.error = TRIGGER_SEQUENCE_SERVICE_BACKEND;
        s_runtime.backend_fault = io.fault;
        increment(&s_runtime.faults);
    }
    s_run_armed = false;
    osal_critical_enter();
    uint32_t queued = s_queued_cancellations;
    s_queued_cancellations = 0;
    osal_critical_exit();
    if (queued) {
        s_runtime.accepted = saturated_add(s_runtime.accepted, queued);
        s_runtime.current_index = s_runtime.next_index;
        s_runtime.current_state = s_run.ids[s_runtime.next_index];
    }
    s_runtime.cancelled = saturated_add(s_runtime.cancelled, queued);
    s_pause_requested = false;
    s_runtime.state = TRIGGER_SEQUENCE_SERVICE_IDLE;
}

static void publish(command_t command, uint32_t serial)
{
    /* A STOP posted while ARM or submit ran takes precedence over publication. */
    osal_critical_enter();
    bool stop = s_command == COMMAND_STOP && command != COMMAND_STOP;
    uint32_t stop_serial = s_command_serial;
    osal_critical_exit();
    if (stop) {
        abort_run();
        command = COMMAND_STOP;
        serial = stop_serial;
    }
    osal_critical_enter();
    s_published = s_runtime;
    if (command != COMMAND_NONE && s_command_serial == serial) s_command = COMMAND_NONE;
    s_processing = false;
    osal_critical_exit();
}

void trigger_sequence_service_service(void)
{
    osal_critical_enter();
    command_t command = s_command;
    uint32_t serial = s_command_serial;
    s_processing = command != COMMAND_NONE;
    osal_critical_exit();

    if (command == COMMAND_START) {
        uint32_t next_run = s_runtime.run_id + 1u;
        memset(&s_runtime, 0, sizeof(s_runtime));
        reset_cursors(&s_runtime);
        s_runtime.run_id = next_run;
        s_runtime.generation = s_run.generation;
        s_runtime.count = s_run.count;
        s_runtime.repeat_count = s_run.repeat_count;
        s_written_receipts = 0;
        s_pause_requested = false;
        s_run_armed = false;
        sync_io_sequence_config_t config = {
            .input_channel = s_run.io.source, .falling = s_run.io.falling,
            .sequence_output_mask = s_run.io.sequence_output_mask,
            .status_output_mask = s_run.io.status_output_mask,
            .status_mode = (sync_io_sequence_status_mode_t)s_run.io.status_mode,
            .settle_us = s_run.io.settle_us, .pulse_us = s_run.io.pulse_us,
            .gateway_input_channel = s_run.gateway.enabled ? s_run.gateway.ready_input : 0u,
            .gateway_output_mask = s_run.gateway.enabled ? s_run.gateway.trigger_output_mask : 0u,
            .gateway_pulse_us = s_run.gateway.enabled ? s_run.gateway.pulse_us : 0u,
            .gateway_falling = s_run.gateway.falling
            , .step_limit_enabled = !s_run.gateway.enabled && s_run.repeat_count != 0u,
            .max_steps = s_run.repeat_count ? s_run.count * s_run.repeat_count - 1u : 0u
        };
        if (!sync_io_sequence_arm_plan(&config, s_run.values, s_run.count)) {
            fail(TRIGGER_SEQUENCE_SERVICE_RESOURCE);
        } else {
            s_run_armed = true;
            sync_io_sequence_snapshot_t io;
            sync_io_sequence_service();
            sync_io_sequence_get_snapshot(&io);
            if (!record_receipts(&io) || io.fault) fail(TRIGGER_SEQUENCE_SERVICE_BACKEND);
        }
        publish(command, serial);
        return;
    }

    if (command == COMMAND_STOP || command == COMMAND_FINISH) {
        abort_run();
        if (command == COMMAND_FINISH) s_runtime.finished = true;
        publish(command, serial);
        return;
    }
    if (s_runtime.state == TRIGGER_SEQUENCE_SERVICE_IDLE ||
        s_runtime.state == TRIGGER_SEQUENCE_SERVICE_FAULT) {
        publish(command, serial);
        return;
    }
    sync_io_sequence_service();
    sync_io_sequence_snapshot_t io;
    sync_io_sequence_get_snapshot(&io);
    if (!record_receipts(&io) || io.fault) {
        fail(TRIGGER_SEQUENCE_SERVICE_BACKEND);
    } else if (command == COMMAND_EXHAUSTED) {
        fail(TRIGGER_SEQUENCE_SERVICE_EXHAUSTED);
    } else if (command == COMMAND_STEP) {
        if (s_runtime.accepted == UINT32_MAX) fail(TRIGGER_SEQUENCE_SERVICE_EXHAUSTED);
        else if (!sync_io_sequence_software_step()) fail(TRIGGER_SEQUENCE_SERVICE_BACKEND);
    } else if (command == COMMAND_GATEWAY_FIRE) {
        if (!sync_io_sequence_gateway_fire()) fail(TRIGGER_SEQUENCE_SERVICE_BACKEND);
    } else if (command == COMMAND_PAUSE || command == COMMAND_CONTINUE) {
        s_pause_requested = command == COMMAND_PAUSE;
        if (!sync_io_sequence_pause(s_pause_requested)) fail(TRIGGER_SEQUENCE_SERVICE_BACKEND);
    }
    if (s_runtime.state != TRIGGER_SEQUENCE_SERVICE_FAULT) {
        sync_io_sequence_service();
        sync_io_sequence_get_snapshot(&io);
        if (!record_receipts(&io) || io.fault) fail(TRIGGER_SEQUENCE_SERVICE_BACKEND);
        else if (io.finished && !s_run.gateway.enabled) {
            abort_run();
            s_runtime.finished = true;
        }
    }
    publish(command, serial);
}

void trigger_sequence_service_get_status(trigger_sequence_service_status_t *status)
{
    if (!status) return;
    osal_critical_enter();
    *status = s_published;
    status->busy_rejected = saturated_add(status->busy_rejected, s_busy_rejected);
    status->notready_rejected = saturated_add(status->notready_rejected, s_notready_rejected);
    if (s_command == COMMAND_START) status->state = TRIGGER_SEQUENCE_SERVICE_STARTING;
    if (s_command == COMMAND_STOP) status->state = TRIGGER_SEQUENCE_SERVICE_STOPPING;
    if (s_command == COMMAND_FINISH) status->state = TRIGGER_SEQUENCE_SERVICE_STOPPING;
    if (s_command == COMMAND_PAUSE) status->state = TRIGGER_SEQUENCE_SERVICE_PAUSING;
    if (s_command == COMMAND_STEP) status->state = TRIGGER_SEQUENCE_SERVICE_RUNNING;
    osal_critical_exit();
}

bool trigger_sequence_service_is_active(void)
{
    trigger_sequence_service_status_t status;
    trigger_sequence_service_get_status(&status);
    return status.state != TRIGGER_SEQUENCE_SERVICE_IDLE;
}

const char *trigger_sequence_service_result_name(trigger_sequence_service_result_t result)
{
    static const char *const names[] = { "NONE", "INVALID_ARGUMENT", "FROZEN",
        "NOT_READY", "BUSY", "SOURCE_MISMATCH", "CONFIG_INVALID", "IO_CONFIG_INVALID",
        "CODE_MISSING", "RESOURCE_BUSY", "BACKEND_FAULT", "RUN_EXHAUSTED" };
    return (uint32_t)result < sizeof(names) / sizeof(names[0]) ? names[result] : "UNKNOWN";
}

const char *trigger_sequence_service_state_name(trigger_sequence_service_state_t state)
{
    static const char *const names[] = { "IDLE", "STARTING", "READY", "BUSY", "PAUSING",
        "PAUSED", "STOPPING", "FAULT" };
    return (uint32_t)state < sizeof(names) / sizeof(names[0]) ? names[state] : "UNKNOWN";
}
