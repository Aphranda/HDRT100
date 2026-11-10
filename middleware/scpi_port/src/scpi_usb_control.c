#include "scpi_usb_control.h"

#include "drv_watchdog.h"
#include "product_config.h"
#include "project_config.h"
#include "scpi_port.h"
#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
#include "usbtmc_scpi_port.h"
#endif

static scpi_result_t scpi_usb_control_result_ok(scpi_t *context)
{
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_usb_control_cmd_mode_q(scpi_t *context)
{
    product_config_usb_mode_t mode;
    if (!product_config_get_usb_mode(&mode)) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultText(context, product_config_usb_mode_to_string(mode));
    return SCPI_RES_OK;
}

scpi_result_t scpi_usb_control_cmd_mode(scpi_t *context)
{
    const char *text = NULL;
    size_t length = 0u;
    product_config_usb_mode_t mode;

    if (SCPI_ParamCharacters(context, &text, &length, TRUE) != TRUE ||
        !product_config_usb_mode_from_text(text, (uint32_t)length, &mode)) {
        return SCPI_RES_ERR;
    }

    return product_config_set_usb_mode(mode) ? scpi_usb_control_result_ok(context) : SCPI_RES_ERR;
}

scpi_result_t scpi_usb_control_cmd_boot(scpi_t *context)
{
    scpi_result_t result = scpi_usb_control_result_ok(context);
    scpi_port_flush_now();
    drv_watchdog_reboot(50u);
    return result;
}

#if PROJECT_ENABLE_USBTMC || PROJECT_ENABLE_USB_RUNTIME_SWITCH
/* Reply-transport counters; see usbtmc_scpi_port.h for the field meanings.
 * A setter acknowledgement that costs a full read timeout shows up either as
 * zero_requests (host polls with TransferSize 0) or as last_end_flag == 0. */
scpi_result_t scpi_usb_control_cmd_stats_q(scpi_t *context)
{
    usbtmc_scpi_port_stats_t stats;
    usbtmc_scpi_port_get_stats(&stats);
    SCPI_ResultUInt32(context, stats.bulk_in_requests);
    SCPI_ResultUInt32(context, stats.zero_requests);
    SCPI_ResultUInt32(context, stats.last_request_size);
    SCPI_ResultUInt32(context, stats.last_tx_len);
    SCPI_ResultUInt32(context, stats.last_end_flag);
    SCPI_ResultUInt32(context, stats.last_reply_len);
    return SCPI_RES_OK;
}
#endif
