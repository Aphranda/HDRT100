#include "scpi_realtime_pcnt_commands.h"

#include "distributed_config.h"
#include "scpi_port_internal.h"
#include "sync_trigger.h"

static void scpi_post_pcnt_event(trig_event_type_t type, uint32_t value)
{
    const trig_event_t event = { .type = type, .payload.value = value };
    sync_trigger_post(&event);
}

scpi_result_t scpi_cmd_pcnt_decode(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t v;
    if (!scpi_port_read_u32(context, &v) || v > 3u) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_DECODE, v);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_pcnt_decode_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    const char *s = "UNKNOWN";
    switch (v.enc_decode) {
    case TRIG_PCNT_DECODE_SINGLE:  s = "SINGLE"; break;
    case TRIG_PCNT_DECODE_QUAD_1X: s = "QUAD1X"; break;
    case TRIG_PCNT_DECODE_QUAD_2X: s = "QUAD2X"; break;
    case TRIG_PCNT_DECODE_UP_DOWN: s = "UPDOWN"; break;
    default: break;
    }
    SCPI_ResultText(context, s);
    SCPI_ResultUInt32(context, (uint32_t)v.enc_decode);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_pcnt_dir(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t v;
    if (!scpi_port_read_u32(context, &v) || v > 2u) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_DIR, v);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_pcnt_dir_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    const char *s[] = {"CW","CCW","BOTH"};
    SCPI_ResultText(context, s[v.enc_dir <= 2 ? v.enc_dir : 0]);
    SCPI_ResultUInt32(context, (uint32_t)v.enc_dir);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_pcnt_filter(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t v;
    if (!scpi_port_read_u32(context, &v)) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_FILTER, v);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_pcnt_filter_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_filter_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_pcnt_gate(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    scpi_bool_t en;
    if (SCPI_ParamBool(context, &en, TRUE) != TRUE) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_GATE, en ? 1u : 0u);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_pcnt_gate_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultBool(context, v.enc_gate_enabled ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_pcnt_cmp(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t v;
    if (!scpi_port_read_u32(context, &v)) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_CMP, v);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_pcnt_cmp_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_cmp_pulse_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_pcnt_preset(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t v;
    if (!scpi_port_read_u32(context, &v)) return SCPI_RES_ERR;
    scpi_post_pcnt_event(TRIG_EVENT_SET_PCNT_PRESET, v);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_pcnt_preset_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_preset);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_pcnt_clear(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    scpi_post_pcnt_event(TRIG_EVENT_PCNT_CLEAR, 0u);
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_pcnt_total_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_total);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_pcnt_freq_q(scpi_t *context)
{
    trigger_vector_t v; sync_trigger_get_vector(&v);
    SCPI_ResultUInt32(context, v.enc_frequency_hz);
    return SCPI_RES_OK;
}
