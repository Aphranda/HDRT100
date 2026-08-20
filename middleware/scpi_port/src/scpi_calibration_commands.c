#include "scpi_calibration_commands.h"

#include "calibration_manager.h"

scpi_result_t scpi_calibration_loopback_start(scpi_t *context)
{
    uint32_t words = 128u;
    (void)SCPI_ParamUInt32(context, &words, FALSE);
    if (!calibration_manager_start_loopback(words)) {
        scpi_port_push_exec_error(context, "CAL_LOOPBACK_START_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, words);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_loopback_stop(scpi_t *context)
{
    calibration_manager_stop_loopback();
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_calibration_loopback_q(scpi_t *context)
{
    calibration_manager_loopback_snapshot_t snapshot;
    if (!calibration_manager_get_loopback_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, snapshot.raw.armed);
    SCPI_ResultUInt32(context, snapshot.raw.complete);
    SCPI_ResultUInt32(context, snapshot.raw.sample_hz);
    SCPI_ResultUInt32(context, snapshot.raw.sample_period_ns);
    SCPI_ResultUInt32(context, snapshot.raw.produced_words);
    SCPI_ResultUInt32(context, snapshot.raw.edge_mask);
    SCPI_ResultUInt32(context, snapshot.raw.flags);
    SCPI_ResultUInt32(context, snapshot.result.reject_reason);
    SCPI_ResultUInt32(context, snapshot.raw.epoch);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.raw.t1_clk_tx);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.raw.t2_clk_rx);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.raw.t3_data_tx);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.raw.t4_data_rx);
    SCPI_ResultUInt32(context, snapshot.result_valid);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.result.residence_ns);
    SCPI_ResultUInt32(context, (uint32_t)snapshot.result.raw_path_sum_ns);
    SCPI_ResultInt32(context, (int32_t)snapshot.result.delay_estimate_ns);
    SCPI_ResultBool(context, snapshot.result.active_eligible ? TRUE : FALSE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_link_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_manager_get_status(&status);

    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.command_seq);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, status.active_crc32);
    SCPI_ResultBool(context, status.ready ? FALSE : TRUE);
    SCPI_ResultUInt32(context, status.ready ? 1u : 0u);
    SCPI_ResultText(context, "SMA");
    SCPI_ResultText(context, "A0");
    SCPI_ResultText(context, "OUT1");
    SCPI_ResultText(context, "A1");
    SCPI_ResultText(context, "IN1");
    SCPI_ResultText(context, "BIDIR");
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_parameter_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_manager_get_status(&status);

    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.command_seq);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, status.active_crc32);
    SCPI_ResultBool(context, status.ready ? FALSE : TRUE);
    SCPI_ResultUInt32(context, status.ready ? 1u : 0u);
    SCPI_ResultText(context, "SMA");
    SCPI_ResultText(context, "A0");
    SCPI_ResultText(context, "OUT1");
    SCPI_ResultText(context, "A1");
    SCPI_ResultText(context, "IN1");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_result_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_manager_get_status(&status);

    SCPI_ResultText(context, status.ready ? "DONE" : "IDLE");
    SCPI_ResultText(context, "SMA");
    SCPI_ResultText(context, "A0:OUT1");
    SCPI_ResultText(context, "A1:IN1");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, status.last_error);
    SCPI_ResultText(context, status.last_error == 0u ? "NONE" : "ERROR");
    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.state);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.active_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_list_q(scpi_t *context)
{
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultUInt32(context, 0x10000003u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "ALL");
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_active_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_manager_get_status(&status);

    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultUInt32(context, status.active_crc32);
    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultText(context, "ACK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_meta_q(scpi_t *context)
{
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultText(context, "OP");
    SCPI_ResultText(context, "FIXTURE");
    SCPI_ResultText(context, "CABLE");
    SCPI_ResultInt32(context, 25);
    SCPI_ResultText(context, "FRAMEWORK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_health_q(scpi_t *context)
{
    calibration_manager_status_t status;
    calibration_manager_get_status(&status);

    SCPI_ResultText(context, status.ready ? "OK" : "INIT");
    SCPI_ResultUInt32(context, status.link_count);
    SCPI_ResultUInt32(context, status.delay_count);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, status.last_error == 0u ? "NONE" : "ERROR");
    return SCPI_RES_OK;
}

scpi_result_t scpi_calibration_limit_q(scpi_t *context)
{
    SCPI_ResultText(context, "DEFAULT");
    SCPI_ResultText(context, "SMA");
    SCPI_ResultUInt32(context, 1000u);
    SCPI_ResultUInt32(context, 100u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 86400u);
    SCPI_ResultBool(context, FALSE);
    return SCPI_RES_OK;
}
