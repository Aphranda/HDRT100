#ifndef SCPI_PRODUCT_COMMANDS_H
#define SCPI_PRODUCT_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_product_result_accepted(scpi_t *context);
scpi_result_t scpi_product_permission_q(scpi_t *context);
scpi_result_t scpi_product_role_q(scpi_t *context);

#define SCPI_PRODUCT_SYSTEM_PERMISSION_COMMANDS \
    {.pattern = "SYSTem:SCPI:PERMission?", .callback = scpi_product_permission_q}, \
    {.pattern = "SYSTem:SCPI:PERMission", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYSTem:SCPI:ROLE?", .callback = scpi_product_role_q}, \
    {.pattern = "SYSTem:SCPI:ROLE", .callback = scpi_product_result_accepted}

#endif
