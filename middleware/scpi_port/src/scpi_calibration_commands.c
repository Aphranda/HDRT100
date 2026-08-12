#include "scpi_calibration_commands.h"

#include "app.h"

scpi_result_t scpi_product_cal_link_q(scpi_t *context)
{
    app_calibration_status_t status;
    app_calibration_get_status(&status);

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

scpi_result_t scpi_product_cal_delay_q(scpi_t *context)
{
    app_calibration_status_t status;
    app_calibration_get_status(&status);

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

scpi_result_t scpi_product_cal_result_q(scpi_t *context)
{
    app_calibration_status_t status;
    app_calibration_get_status(&status);

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

scpi_result_t scpi_product_cal_list_q(scpi_t *context)
{
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultUInt32(context, 0x10000003u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "ALL");
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_cal_active_q(scpi_t *context)
{
    app_calibration_status_t status;
    app_calibration_get_status(&status);

    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultUInt32(context, status.active_crc32);
    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultText(context, "ACK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_cal_meta_q(scpi_t *context)
{
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultText(context, "OP");
    SCPI_ResultText(context, "FIXTURE");
    SCPI_ResultText(context, "CABLE");
    SCPI_ResultInt32(context, 25);
    SCPI_ResultText(context, "FRAMEWORK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_cal_health_q(scpi_t *context)
{
    app_calibration_status_t status;
    app_calibration_get_status(&status);

    SCPI_ResultText(context, status.ready ? "OK" : "INIT");
    SCPI_ResultUInt32(context, status.link_count);
    SCPI_ResultUInt32(context, status.delay_count);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, status.last_error == 0u ? "NONE" : "ERROR");
    return SCPI_RES_OK;
}

scpi_result_t scpi_product_cal_limit_q(scpi_t *context)
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
