#include "scpi_measure_commands.h"

#include <string.h>
#include <stdint.h>

#include "scpi_port_internal.h"
#include "trigger_measure.h"

static bool scpi_get_measure_report(trigger_measure_report_t *report)
{
    if (report == NULL) {
        return false;
    }

    memset(report, 0, sizeof(*report));
    return trigger_measure_get_report(report);
}

scpi_result_t scpi_cmd_meas_freq_q(scpi_t *context)
{
    uint32_t gate_ms = 1000u;
    scpi_port_read_u32(context, &gate_ms);
    if (gate_ms < 10u || gate_ms > 60000u) {
        gate_ms = 1000u;
    }
    const uint32_t freq_hz = trigger_measure_quick_freq_hz(gate_ms);
    SCPI_ResultUInt32(context, freq_hz);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_meas_period_q(scpi_t *context)
{
    trigger_measure_report_t report;
    const bool valid = scpi_get_measure_report(&report);

    SCPI_ResultText(context, valid ? "DONE" : "NO_REPORT");
    SCPI_ResultUInt32(context, report.period_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_meas_jitter_q(scpi_t *context)
{
    trigger_measure_report_t report;
    const bool valid = scpi_get_measure_report(&report);

    SCPI_ResultText(context, valid ? "DONE" : "NO_REPORT");
    SCPI_ResultUInt32(context, report.jitter_est_ppm);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_meas_pulse_width_q(scpi_t *context)
{
    trigger_measure_report_t report;
    const bool valid = scpi_get_measure_report(&report);

    SCPI_ResultText(context, valid ? "DONE" : "NO_REPORT");
    SCPI_ResultUInt32(context, report.seq_width);
    SCPI_ResultText(context, "WIDTH_TICKS");
    return SCPI_RES_OK;
}

static bool scpi_read_text_or_default(scpi_t *context,
                                      char *buffer,
                                      size_t buffer_len,
                                      const char *default_value)
{
    if (buffer == NULL || buffer_len == 0u) {
        return false;
    }

    const char *value = NULL;
    size_t len = 0u;
    if (SCPI_ParamCharacters(context, &value, &len, FALSE) == TRUE) {
        if (value == NULL || len >= buffer_len) {
            return false;
        }
        memcpy(buffer, value, len);
        buffer[len] = '\0';
        return true;
    }

    const size_t default_len = strlen(default_value);
    if (default_len >= buffer_len) {
        return false;
    }
    memcpy(buffer, default_value, default_len + 1u);
    return true;
}

scpi_result_t scpi_cmd_meas_link_delay_q(scpi_t *context)
{
    char src_node[8];
    char src_port[8];
    char dst_node[8];
    char dst_port[8];
    if (!scpi_read_text_or_default(context, src_node, sizeof(src_node), "A0") ||
        !scpi_read_text_or_default(context, src_port, sizeof(src_port), "OUT1") ||
        !scpi_read_text_or_default(context, dst_node, sizeof(dst_node), "A1") ||
        !scpi_read_text_or_default(context, dst_port, sizeof(dst_port), "IN1")) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, src_node);
    SCPI_ResultText(context, src_port);
    SCPI_ResultText(context, dst_node);
    SCPI_ResultText(context, dst_port);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "PENDING_BACKEND");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_meas_t2_q(scpi_t *context)
{
    trigger_measure_report_t report;
    const bool valid = scpi_get_measure_report(&report);

    SCPI_ResultText(context, valid ? "DONE" : "NO_REPORT");
    SCPI_ResultUInt32(context, report.elapsed_us);
    SCPI_ResultUInt32(context, report.trigger_count);
    SCPI_ResultUInt32(context, report.rollover_count);
    SCPI_ResultUInt32(context, report.jitter_est_ppm);
    SCPI_ResultText(context, "MEASURE_REPORT");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_meas_report_q(scpi_t *context)
{
    trigger_measure_report_t report;
    const bool valid = scpi_get_measure_report(&report);

    SCPI_ResultText(context, valid ? "DONE" : "NO_REPORT");
    SCPI_ResultUInt32(context, report.freq_hz);
    SCPI_ResultUInt32(context, report.period_ns);
    SCPI_ResultUInt32(context, report.trigger_count);
    SCPI_ResultUInt32(context, report.rollover_count);
    SCPI_ResultUInt32(context, report.elapsed_us);
    SCPI_ResultUInt32(context, report.jitter_est_ppm);
    SCPI_ResultUInt32(context, report.gate_ms);
    SCPI_ResultUInt32(context, report.seq_len);
    SCPI_ResultUInt32(context, report.seq_width);
    return SCPI_RES_OK;
}
