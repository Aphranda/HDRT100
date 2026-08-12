#ifndef SCPI_REALTIME_STATUS_COMMANDS_H
#define SCPI_REALTIME_STATUS_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_status_q(scpi_t *context);

#define SCPI_REALTIME_STATUS_COMMANDS \
    {.pattern = "STATus:TRIGger?", .callback = scpi_cmd_trigger_status_q}

#endif
