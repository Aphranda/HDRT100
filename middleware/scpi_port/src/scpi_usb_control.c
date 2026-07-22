#include "scpi_usb_control.h"

#include "drv_watchdog.h"
#include "product_config.h"
#include "project_config.h"
#include "scpi_port.h"

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
