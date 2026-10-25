#ifndef SCPI_TRIGGER_ACTION_EXECUTE_H
#define SCPI_TRIGGER_ACTION_EXECUTE_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_action_prepare(scpi_t *context);
scpi_result_t scpi_cmd_trigger_action_arm(scpi_t *context);
scpi_result_t scpi_cmd_trigger_action_stop(scpi_t *context);
scpi_result_t scpi_cmd_trigger_action_release(scpi_t *context);
scpi_result_t scpi_cmd_trigger_action_now_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_action_execute_q(scpi_t *context);

#endif /* SCPI_TRIGGER_ACTION_EXECUTE_H */
