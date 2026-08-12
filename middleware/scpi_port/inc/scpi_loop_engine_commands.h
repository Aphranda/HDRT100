#ifndef SCPI_LOOP_ENGINE_COMMANDS_H
#define SCPI_LOOP_ENGINE_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_loop_status_q(scpi_t *context);

#define SCPI_LOOP_ENGINE_COMMANDS \
    {.pattern = "LOOP:STATus?", .callback = scpi_cmd_loop_status_q}, \
    {.pattern = "LOOP:STAT?", .callback = scpi_cmd_loop_status_q}, \
    {.pattern = "STATus:LOOP?", .callback = scpi_cmd_loop_status_q}

#endif
