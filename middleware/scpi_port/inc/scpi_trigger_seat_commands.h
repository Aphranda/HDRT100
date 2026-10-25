#ifndef SCPI_TRIGGER_SEAT_COMMANDS_H
#define SCPI_TRIGGER_SEAT_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_seat_mount(scpi_t *context);
scpi_result_t scpi_cmd_trigger_seat_mount_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_seat_clear(scpi_t *context);

#endif /* SCPI_TRIGGER_SEAT_COMMANDS_H */
