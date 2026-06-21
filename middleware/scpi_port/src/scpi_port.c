#include "scpi_port.h"

#include <stdio.h>
#include <string.h>

#include "diagnostics.h"
#include "pico/error.h"
#include "pico/stdio.h"
#include "project_config.h"
#include "scpi/scpi.h"
#include "sync_io.h"

#define SCPI_PORT_INPUT_BUFFER_LENGTH 256u
#define SCPI_PORT_ERROR_QUEUE_SIZE    16
#define SCPI_PORT_IDN_VENDOR          "RP2350_TRIG"
#define SCPI_PORT_IDN_MODEL           "SYNC_TRIGGER"
#define SCPI_PORT_IDN_SERIAL          NULL
#define SCPI_PORT_POLL_CHARS          32u

static scpi_t s_scpi_context;
static char s_scpi_input_buffer[SCPI_PORT_INPUT_BUFFER_LENGTH];
static scpi_error_t s_scpi_error_queue[SCPI_PORT_ERROR_QUEUE_SIZE];

static scpi_port_config_t s_scpi_config = {
    .trigger_width_us = 10u,
    .pulse_width_us = 10u,
    .marker_width_us = 10u,
    .capture_sample_hz = 1000000u,
    .sync_clock_hz = 1000000u,
    .sync_clock_enabled = false,
};

static size_t scpi_port_write(scpi_t *context, const char *data, size_t len)
{
    (void)context;
    for (size_t i = 0u; i < len; i++) {
        putchar_raw(data[i]);
    }
    return len;
}

static scpi_result_t scpi_port_flush(scpi_t *context)
{
    (void)context;
    stdio_flush();
    return SCPI_RES_OK;
}

static int scpi_port_error(scpi_t *context, int_fast16_t error)
{
    (void)context;
    LOG_WARN("scpi", "error=%d", (int)error);
    return 0;
}

static scpi_result_t scpi_port_control(scpi_t *context, scpi_ctrl_name_t ctrl, scpi_reg_val_t val)
{
    (void)context;
    (void)ctrl;
    (void)val;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_port_reset(scpi_t *context)
{
    (void)context;
    s_scpi_config.trigger_width_us = 10u;
    s_scpi_config.pulse_width_us = 10u;
    s_scpi_config.marker_width_us = 10u;
    s_scpi_config.capture_sample_hz = 1000000u;
    s_scpi_config.sync_clock_hz = 1000000u;
    s_scpi_config.sync_clock_enabled = false;

    sync_io_stop_clock();
    sync_io_stop_capture();
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_core_tst_q(scpi_t *context)
{
    SCPI_ResultInt32(context, 0);
    return SCPI_RES_OK;
}

static bool scpi_port_read_u32(scpi_t *context, uint32_t *value)
{
    return SCPI_ParamUInt32(context, value, TRUE) == TRUE;
}

static scpi_result_t scpi_cmd_trigger_width(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    s_scpi_config.trigger_width_us = value;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_width_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.trigger_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_trigger_fire(scpi_t *context)
{
    (void)context;
    return sync_io_fire_pulse_us(s_scpi_config.trigger_width_us) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_pulse_width(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    s_scpi_config.pulse_width_us = value;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pulse_width_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.pulse_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_pulse_fire(scpi_t *context)
{
    (void)context;
    return sync_io_fire_pulse_out_us(s_scpi_config.pulse_width_us) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_marker_width(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    s_scpi_config.marker_width_us = value;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_marker_width_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.marker_width_us);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_marker_fire(scpi_t *context)
{
    (void)context;
    return sync_io_fire_marker_us(s_scpi_config.marker_width_us) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_sample_rate(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    s_scpi_config.capture_sample_hz = value;
    return sync_io_start_capture(value) ? SCPI_RES_OK : SCPI_RES_ERR;
}

static scpi_result_t scpi_cmd_sample_rate_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.capture_sample_hz);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_sample_state(scpi_t *context)
{
    scpi_bool_t state;
    if (SCPI_ParamBool(context, &state, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    if (state) {
        return sync_io_start_capture(s_scpi_config.capture_sample_hz) ? SCPI_RES_OK : SCPI_RES_ERR;
    }

    sync_io_stop_capture();
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_sample_state_q(scpi_t *context)
{
    sync_io_status_t status;
    sync_io_get_status(&status);
    SCPI_ResultBool(context, status.capture_running ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_freq(scpi_t *context)
{
    uint32_t value;
    if (!scpi_port_read_u32(context, &value) || value == 0u) {
        return SCPI_RES_ERR;
    }

    s_scpi_config.sync_clock_hz = value;
    if (s_scpi_config.sync_clock_enabled) {
        return sync_io_start_clock(value) ? SCPI_RES_OK : SCPI_RES_ERR;
    }

    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_freq_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, s_scpi_config.sync_clock_hz);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_state(scpi_t *context)
{
    scpi_bool_t state;
    if (SCPI_ParamBool(context, &state, TRUE) != TRUE) {
        return SCPI_RES_ERR;
    }

    if (state) {
        if (!sync_io_start_clock(s_scpi_config.sync_clock_hz)) {
            return SCPI_RES_ERR;
        }
        s_scpi_config.sync_clock_enabled = true;
        return SCPI_RES_OK;
    }

    sync_io_stop_clock();
    s_scpi_config.sync_clock_enabled = false;
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_clock_state_q(scpi_t *context)
{
    sync_io_status_t status;
    sync_io_get_status(&status);
    SCPI_ResultBool(context, status.sync_clock_running ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static scpi_result_t scpi_cmd_status_q(scpi_t *context)
{
    sync_io_status_t status;
    sync_io_get_status(&status);
    SCPI_ResultBool(context, status.initialized ? TRUE : FALSE);
    SCPI_ResultBool(context, status.capture_running ? TRUE : FALSE);
    SCPI_ResultBool(context, status.sync_clock_running ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.capture_sample_hz);
    SCPI_ResultUInt32(context, status.sync_clock_hz);
    SCPI_ResultUInt32(context, status.dropped_capture_words);
    return SCPI_RES_OK;
}

static const scpi_command_t s_scpi_commands[] = {
    {.pattern = "*CLS", .callback = SCPI_CoreCls},
    {.pattern = "*ESE", .callback = SCPI_CoreEse},
    {.pattern = "*ESE?", .callback = SCPI_CoreEseQ},
    {.pattern = "*ESR?", .callback = SCPI_CoreEsrQ},
    {.pattern = "*IDN?", .callback = SCPI_CoreIdnQ},
    {.pattern = "*OPC", .callback = SCPI_CoreOpc},
    {.pattern = "*OPC?", .callback = SCPI_CoreOpcQ},
    {.pattern = "*RST", .callback = SCPI_CoreRst},
    {.pattern = "*SRE", .callback = SCPI_CoreSre},
    {.pattern = "*SRE?", .callback = SCPI_CoreSreQ},
    {.pattern = "*STB?", .callback = SCPI_CoreStbQ},
    {.pattern = "*TST?", .callback = scpi_cmd_core_tst_q},
    {.pattern = "*WAI", .callback = SCPI_CoreWai},
    {.pattern = "SYSTem:ERRor[:NEXT]?", .callback = SCPI_SystemErrorNextQ},
    {.pattern = "SYSTem:ERRor:COUNt?", .callback = SCPI_SystemErrorCountQ},
    {.pattern = "SYSTem:VERSion?", .callback = SCPI_SystemVersionQ},
    {.pattern = "TRIGger:WIDTh", .callback = scpi_cmd_trigger_width},
    {.pattern = "TRIGger:WIDTh?", .callback = scpi_cmd_trigger_width_q},
    {.pattern = "TRIGger:IMMediate", .callback = scpi_cmd_trigger_fire},
    {.pattern = "PULSe:WIDTh", .callback = scpi_cmd_pulse_width},
    {.pattern = "PULSe:WIDTh?", .callback = scpi_cmd_pulse_width_q},
    {.pattern = "PULSe:IMMediate", .callback = scpi_cmd_pulse_fire},
    {.pattern = "MARKer:WIDTh", .callback = scpi_cmd_marker_width},
    {.pattern = "MARKer:WIDTh?", .callback = scpi_cmd_marker_width_q},
    {.pattern = "MARKer:IMMediate", .callback = scpi_cmd_marker_fire},
    {.pattern = "SAMPle:RATE", .callback = scpi_cmd_sample_rate},
    {.pattern = "SAMPle:RATE?", .callback = scpi_cmd_sample_rate_q},
    {.pattern = "SAMPle:STATe", .callback = scpi_cmd_sample_state},
    {.pattern = "SAMPle:STATe?", .callback = scpi_cmd_sample_state_q},
    {.pattern = "OUTPut:CLOCk:FREQuency", .callback = scpi_cmd_clock_freq},
    {.pattern = "OUTPut:CLOCk:FREQuency?", .callback = scpi_cmd_clock_freq_q},
    {.pattern = "OUTPut:CLOCk:STATe", .callback = scpi_cmd_clock_state},
    {.pattern = "OUTPut:CLOCk:STATe?", .callback = scpi_cmd_clock_state_q},
    {.pattern = "STATus:SYNC?", .callback = scpi_cmd_status_q},
    SCPI_CMD_LIST_END,
};

static scpi_interface_t s_scpi_interface = {
    .error = scpi_port_error,
    .write = scpi_port_write,
    .control = scpi_port_control,
    .flush = scpi_port_flush,
    .reset = scpi_port_reset,
};

bool scpi_port_init(void)
{
    SCPI_Init(&s_scpi_context,
              s_scpi_commands,
              &s_scpi_interface,
              scpi_units_def,
              SCPI_PORT_IDN_VENDOR,
              SCPI_PORT_IDN_MODEL,
              SCPI_PORT_IDN_SERIAL,
              PROJECT_NAME,
              s_scpi_input_buffer,
              sizeof(s_scpi_input_buffer),
              s_scpi_error_queue,
              SCPI_PORT_ERROR_QUEUE_SIZE);

    LOG_INFO("scpi", "SCPI service initialized");
    return true;
}

void scpi_port_service(void)
{
    char buffer[SCPI_PORT_POLL_CHARS];
    size_t count = 0u;

    while (count < sizeof(buffer)) {
        const int ch = getchar_timeout_us(0u);
        if (ch == PICO_ERROR_TIMEOUT) {
            break;
        }
        if (ch < 0) {
            break;
        }
        buffer[count] = (char)ch;
        count++;
    }

    if (count > 0u) {
        SCPI_Input(&s_scpi_context, buffer, (int)count);
    }
}

void scpi_port_get_config(scpi_port_config_t *config)
{
    if (config == NULL) {
        return;
    }

    *config = s_scpi_config;
}
