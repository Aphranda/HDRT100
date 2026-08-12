#ifndef SCPI_SYSTEM_ACCESS_COMMANDS_H
#define SCPI_SYSTEM_ACCESS_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_port_internal.h"

scpi_result_t scpi_system_access_permission_q(scpi_t *context);
scpi_result_t scpi_system_access_role_q(scpi_t *context);

#define SCPI_SYSTEM_ACCESS_COMMANDS \
    {.pattern = "SYSTem:SCPI:PERMission?", .callback = scpi_system_access_permission_q}, \
    {.pattern = "SYSTem:SCPI:PERMission", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYSTem:SCPI:ROLE?", .callback = scpi_system_access_role_q}, \
    {.pattern = "SYSTem:SCPI:ROLE", .callback = scpi_port_result_accepted}

#endif
