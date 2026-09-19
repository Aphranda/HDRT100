#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "distributed_config.h"
#include "scpi_config_commands.h"
#include "scpi_sequence_commands.h"
#include "scpi_trigger_commands.h"
#include "sync_io_sequence.h"
#include "sync_trigger.h"
#include "trigger_sequence_link.h"
#include "trigger_sequence_service.h"

static sync_io_sequence_snapshot_t hw;
static sync_io_sequence_config_t hw_config;
static uint32_t hw_values[TRIGGER_SEQUENCE_STATE_MAX];
static bool reserved, legacy_running, arm_ok = true, submit_ok = true;
static unsigned locked, writes, inputs, outputs;
static char output[32768], error_info[128];
static size_t output_size;
static unsigned error_count;

static bool gateway_start_guard(void) { return true; }

void osal_critical_enter(void) { assert(!locked); locked = 1; }
void osal_critical_exit(void) { assert(locked); locked = 0; }
bool sync_trigger_sequence_can_start(void) { assert(!locked); return !legacy_running; }
bool sync_trigger_post(const trig_event_t *event) { (void)event; return true; }
void sync_trigger_get_vector(trigger_vector_t *vector)
{ memset(vector, 0, sizeof(*vector)); vector->state = TRIG_STATE_IDLE; }
void scpi_port_set_trigger_debug_mode(uint32_t mode) { (void)mode; }

bool sync_io_sequence_reserve(void)
{ assert(!locked); if (reserved) return false; reserved = true; return true; }
void sync_io_sequence_release(void) { assert(!locked); reserved = false; }
bool sync_io_sequence_arm_plan_bytes(const sync_io_sequence_config_t *config,
                                    const uint8_t *values, uint32_t count)
{
    assert(!locked && reserved);
    memset(&hw, 0, sizeof(hw));
    hw_config = *config;
    for (uint32_t i = 0u; i < count; ++i) hw_values[i] = values[i];
    hw.plan_count = count;
    hw.current_index = 0u;
    hw.completed_index = UINT32_MAX;
    hw.tick_ns = 100;
    hw.timing_kind = TRIGGER_SEQUENCE_TIMING_PIO0;
    hw.armed = hw.ready = arm_ok;
    hw.rejection_counts_pending = config->input_channel != 0;
    hw.output_ownership_mask = config->sequence_output_mask | config->status_output_mask;
    outputs = (outputs & ~hw.output_ownership_mask) | values[0];
    if (config->status_mode == SYNC_IO_SEQUENCE_STATUS_LEVEL)
        outputs |= config->status_output_mask;
    if (!arm_ok) {
        hw.fault = SYNC_IO_SEQUENCE_FAULT_RESOURCE;
        outputs &= ~hw.output_ownership_mask;
    }
    return arm_ok;
}
static bool physical_step(void)
{
    assert(!locked && hw.armed && !hw.pending && !hw.busy);
    if (!submit_ok) { hw.fault = SYNC_IO_SEQUENCE_FAULT_RESOURCE; return false; }
    hw.current_index = (hw.accepted + 1u) % hw.plan_count;
    ++hw.accepted;
    uint32_t value = hw_values[hw.current_index];
    outputs = (outputs & ~hw.output_ownership_mask) | value;
    ++hw.written;
    ++writes;
    hw.busy = true;
    hw.ready = false;
    return true;
}
bool sync_io_sequence_software_step(void)
{ return !hw_config.input_channel && !hw.paused && hw.ready && physical_step(); }
bool sync_io_sequence_gateway_fire(void) { return false; }
bool sync_io_sequence_software_step_prepared(void) { return sync_io_sequence_software_step(); }
bool sync_io_sequence_counter_rearm_prepared(void) { return false; }
bool sync_io_sequence_gateway_ready(void) { return false; }
bool sync_io_sequence_counter_rearm(void) { return false; }
bool sync_io_sequence_counter_inject(uint32_t input_channel, uint32_t count)
{
    if (input_channel != hw_config.counter_input_channel || !count) return false;
    hw.counter_events += count;
    if (hw.counter_events >= hw_config.counter_threshold) hw.counter_busy = true;
    return true;
}
void sync_io_sequence_stop(void)
{
    assert(!locked);
    outputs &= ~hw.output_ownership_mask;
    hw.cancelled = hw.accepted - hw.completed;
    hw.rejection_counts_pending = false;
    hw.busy = hw.pending = hw.ready = hw.armed = reserved = false;
    hw.output_ownership_mask = 0;
}
void sync_io_sequence_service(void) { assert(!locked); }
bool sync_io_sequence_pause(bool paused)
{
    assert(!locked);
    hw.paused = paused;
    hw.rejection_counts_pending = hw_config.input_channel != 0 &&
        (!paused || hw.busy || hw.pending);
    hw.ready = !paused && hw.armed && !hw.busy && !hw.pending;
    return true;
}
void sync_io_sequence_get_snapshot(sync_io_sequence_snapshot_t *snapshot)
{ assert(!locked); *snapshot = hw; }
bool sync_io_sequence_is_armed(void) { return hw.armed; }
uint32_t sync_io_sequence_read_inputs(void) { return inputs; }
uint32_t sync_io_sequence_read_outputs(void) { return outputs; }
uint32_t sync_io_sequence_owned_mask(void) { return hw.output_ownership_mask; }

void trigger_sequence_link_get_status(trigger_sequence_link_status_t *status)
{ memset(status, 0, sizeof(*status)); }
bool trigger_sequence_link_binding_is_current(uint32_t binding_epoch, uint32_t model_epoch)
{ (void)binding_epoch; (void)model_epoch; return false; }
bool trigger_sequence_link_configure_position_locked(
    const trigger_sequence_link_config_t *config, uint32_t repeat_count)
{ (void)config; (void)repeat_count; return false; }
trigger_sequence_service_result_t trigger_sequence_link_next(void)
{ return TRIGGER_SEQUENCE_SERVICE_NOT_READY; }
trigger_sequence_service_result_t trigger_sequence_link_ready_inject(uint32_t count)
{
    if (!count || count > TRIGGER_SEQUENCE_LINK_READY_INJECT_MAX)
        return TRIGGER_SEQUENCE_SERVICE_INVALID;
    return hw.armed ? TRIGGER_SEQUENCE_SERVICE_OK : TRIGGER_SEQUENCE_SERVICE_NOT_READY;
}

scpi_result_t scpi_port_result_accepted(scpi_t *context)
{ SCPI_ResultUInt32(context, 1); return SCPI_RES_OK; }
void scpi_port_push_exec_error(scpi_t *context, const char *info)
{
    snprintf(error_info, sizeof(error_info), "%s", info);
    SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
}
bool scpi_port_reject_if_run_forbidden(scpi_t *context, uint32_t class_id)
{
    assert(class_id == DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG);
    if (legacy_running) scpi_port_push_exec_error(context, "LEGACY_RUNNING");
    return legacy_running;
}
static size_t capture(scpi_t *context, const char *data, size_t length)
{
    (void)context;
    assert(length < sizeof(output) - output_size);
    memcpy(output + output_size, data, length);
    output_size += length;
    return length;
}
static int error(scpi_t *context, int_fast16_t code)
{ (void)context; if (code) ++error_count; return 0; }
static scpi_result_t flush(scpi_t *context) { (void)context; return SCPI_RES_OK; }

static void edge(unsigned channel, unsigned falling)
{
    if (!hw.armed || !channel || channel != hw_config.input_channel ||
        (falling != 0) != hw_config.falling) return;
    ++hw.input_events;
    if (hw.busy || hw.pending) ++hw.busy_rejected;
    else if (!hw.ready) ++hw.notready_rejected;
    else (void)physical_step();
}

static const scpi_command_t commands[] = {
    SCPI_CONFIG_COMMANDS, SCPI_SEQUENCE_COMMANDS, SCPI_TRIGGER_COMMANDS, SCPI_CMD_LIST_END
};

int main(void)
{
    scpi_t context;
    char input[8192], line[8192];
    scpi_error_t errors[32];
    scpi_interface_t interface = {.write = capture, .flush = flush, .error = error};
    trigger_sequence_service_init();
    SCPI_Init(&context, commands, &interface, scpi_units_def,
              "test", "DHRT100", "test", "test", input, sizeof(input), errors, 32);
    while (fgets(line, sizeof(line), stdin)) {
        if (line[0] == '@') {
            unsigned channel, falling;
            if (strcmp(line, "@service\n") == 0) trigger_sequence_service_service();
            else if (strcmp(line, "@rise\n") == 0) {
                assert(hw.busy);
                outputs |= hw_config.status_output_mask;
            } else if (strcmp(line, "@complete\n") == 0) {
                assert(hw.busy);
                hw.busy = false;
                if (hw_config.status_mode == SYNC_IO_SEQUENCE_STATUS_PULSE)
                    outputs &= ~hw_config.status_output_mask;
                ++hw.completed;
                hw.completed_index = hw.current_index;
                hw.ready = !hw.paused;
                hw.rejection_counts_pending = !hw.paused && hw_config.input_channel != 0;
            } else if (sscanf(line, "@edge %u %u", &channel, &falling) == 2) edge(channel, falling);
            else if (sscanf(line, "@inputs %u", &channel) == 1) inputs = channel & 15u;
            else if (sscanf(line, "@legacy %u", &channel) == 1) legacy_running = channel != 0;
            else if (sscanf(line, "@armok %u", &channel) == 1) arm_ok = channel != 0;
            else if (sscanf(line, "@submitok %u", &channel) == 1) submit_ok = channel != 0;
            else if (strcmp(line, "@position\n") == 0) {
                const trigger_sequence_gateway_config_t gateway = {
                    .enabled = true, .ready_input = 0u, .trigger_output_mask = 8u,
                    .pulse_us = 10u, .counter_input = 1u, .counter_threshold = 1000u};
                assert(trigger_sequence_service_set_outputs(
                    7u, 0u, TRIGGER_SEQUENCE_STATUS_NONE, 10u, 0u) ==
                    TRIGGER_SEQUENCE_SERVICE_OK);
                assert(trigger_sequence_service_set_gateway(&gateway, gateway_start_guard) ==
                    TRIGGER_SEQUENCE_SERVICE_OK);
            }
            else { fprintf(stderr, "unknown control: %s", line); return 2; }
            continue;
        }
        SCPI_ErrorClear(&context);
        output_size = error_count = 0;
        error_info[0] = '\0';
        /* Fragment the transport stream; all commands enter the real parser. */
        for (size_t i = 0; i < strlen(line); ++i) SCPI_Input(&context, line + i, 1);
        while (output_size && (output[output_size - 1] == '\n' || output[output_size - 1] == '\r')) --output_size;
        printf("%u|%s|%u|", error_count, error_info, writes);
        fwrite(output, 1, output_size, stdout);
        putchar('\n');
    }
    return 0;
}
