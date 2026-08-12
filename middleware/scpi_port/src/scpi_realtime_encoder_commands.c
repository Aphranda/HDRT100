#include "scpi_realtime_encoder_commands.h"

#include "distributed_config.h"
#include "scpi_port_internal.h"
#include "sync_io_hw_profile.h"
#include "sync_trigger.h"

scpi_result_t scpi_cmd_enc_target(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t target;
    if (!scpi_port_read_u32(context, &target) || target == 0u) {
        return SCPI_RES_ERR;
    }

    const trig_event_t event = {
        .type = TRIG_EVENT_SET_ENC_TARGET,
        .payload.value = target,
    };
    return sync_trigger_post(&event) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_enc_target_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_target);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_enc_count_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_enc_a_pin(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(
            context,
            DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }

    uint32_t pin;
    if (!scpi_port_read_u32(context, &pin)) {
        return SCPI_RES_ERR;
    }

    if (pin != SYNC_IO_HW_ENC_A_PIN) {
        scpi_port_push_exec_error(context, "HW_ENC_PIN_FIXED");
        return SCPI_RES_ERR;
    }

    const trig_event_t ev = {
        .type = TRIG_EVENT_SET_ENC_PINS,
        .payload.value = SYNC_IO_HW_ENC_A_PIN |
                         (SYNC_IO_HW_ENC_B_PIN << 8) |
                         (SYNC_IO_HW_ENC_Z_PIN << 16),
    };
    return sync_trigger_post(&ev) ? scpi_port_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_cmd_enc_a_pin_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_a_pin);
    SCPI_ResultUInt32(context, vector.enc_b_pin);
    SCPI_ResultUInt32(context, vector.enc_z_pin);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_enc_rev_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultUInt32(context, vector.enc_rev_count);
    return SCPI_RES_OK;
}
