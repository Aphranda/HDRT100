#ifndef SCPI_USB_CONTROL_H
#define SCPI_USB_CONTROL_H

#include "scpi/scpi.h"

scpi_result_t scpi_usb_control_cmd_mode_q(scpi_t *context);
scpi_result_t scpi_usb_control_cmd_mode(scpi_t *context);
scpi_result_t scpi_usb_control_cmd_boot(scpi_t *context);

#define SCPI_USB_CONTROL_COMMANDS \
    {.pattern = "SYSTem:USB:MODE?", .callback = scpi_usb_control_cmd_mode_q}, \
    {.pattern = "SYSTem:USB:MODE", .callback = scpi_usb_control_cmd_mode}, \
    {.pattern = "SYSTem:USB:BOOT", .callback = scpi_usb_control_cmd_boot}

#endif
