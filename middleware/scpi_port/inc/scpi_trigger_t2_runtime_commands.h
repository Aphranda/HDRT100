#ifndef SCPI_TRIGGER_T2_RUNTIME_COMMANDS_H
#define SCPI_TRIGGER_T2_RUNTIME_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_t2_configure(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_start(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_stop(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_announce(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_announce_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_timing(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_timing_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_counts(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_counts_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_period_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_code(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_code_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_repeat(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_repeat_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_ready(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sequence_ready_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_drain(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_drain_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_cursor_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_scan_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_gateway_diag_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_action_diag_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_action_window_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_action_why_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_action_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_seq_pilot(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_seq_pilot_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_seq_mode(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_seq_mode_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_t2_action_arm_branch_q(scpi_t *context);

/* Shared with sibling command modules (was a static helper). */
scpi_result_t scpi_trigger_t2_fail(scpi_t *context);

#endif /* SCPI_TRIGGER_T2_RUNTIME_COMMANDS_H */
