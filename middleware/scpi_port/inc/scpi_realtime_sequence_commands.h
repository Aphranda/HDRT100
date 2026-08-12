#ifndef SCPI_REALTIME_SEQUENCE_COMMANDS_H
#define SCPI_REALTIME_SEQUENCE_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_seq_length(scpi_t *context);
scpi_result_t scpi_cmd_trigger_seq_length_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_seq_width(scpi_t *context);
scpi_result_t scpi_cmd_trigger_seq_width_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_seq_index_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_seq_data(scpi_t *context);
scpi_result_t scpi_cmd_trigger_seq_data_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_arm(scpi_t *context);
scpi_result_t scpi_cmd_trigger_disarm(scpi_t *context);
scpi_result_t scpi_cmd_trigger_fault(scpi_t *context);
scpi_result_t scpi_cmd_trigger_source(scpi_t *context);
scpi_result_t scpi_cmd_trigger_source_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_edge(scpi_t *context);
scpi_result_t scpi_cmd_trigger_edge_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_gate(scpi_t *context);
scpi_result_t scpi_cmd_trigger_gate_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_safe(scpi_t *context);
scpi_result_t scpi_cmd_trigger_safe_q(scpi_t *context);

#define SCPI_REALTIME_SEQUENCE_COMMANDS \
    {.pattern = "REALtime:SOURce", .callback = scpi_cmd_trigger_source}, \
    {.pattern = "REALtime:SOURce?", .callback = scpi_cmd_trigger_source_q}, \
    {.pattern = "REALtime:EDGE", .callback = scpi_cmd_trigger_edge}, \
    {.pattern = "REALtime:EDGE?", .callback = scpi_cmd_trigger_edge_q}, \
    {.pattern = "REALtime:GATE", .callback = scpi_cmd_trigger_gate}, \
    {.pattern = "REALtime:GATE?", .callback = scpi_cmd_trigger_gate_q}, \
    {.pattern = "REALtime:SAFE", .callback = scpi_cmd_trigger_safe}, \
    {.pattern = "REALtime:SAFE?", .callback = scpi_cmd_trigger_safe_q}, \
    {.pattern = "REALtime:SEQ:LENGth", .callback = scpi_cmd_trigger_seq_length}, \
    {.pattern = "REALtime:SEQ:LENGth?", .callback = scpi_cmd_trigger_seq_length_q}, \
    {.pattern = "REALtime:SEQ:WIDTh", .callback = scpi_cmd_trigger_seq_width}, \
    {.pattern = "REALtime:SEQ:WIDTh?", .callback = scpi_cmd_trigger_seq_width_q}, \
    {.pattern = "REALtime:SEQ:INDex?", .callback = scpi_cmd_trigger_seq_index_q}, \
    {.pattern = "REALtime:SEQ:DATA", .callback = scpi_cmd_trigger_seq_data}, \
    {.pattern = "REALtime:SEQ:DATA?", .callback = scpi_cmd_trigger_seq_data_q}, \
    {.pattern = "REALtime:ARM", .callback = scpi_cmd_trigger_arm}, \
    {.pattern = "REALtime:DISarm", .callback = scpi_cmd_trigger_disarm}, \
    {.pattern = "REALtime:DISAble", .callback = scpi_cmd_trigger_disarm}, \
    {.pattern = "REALtime:FAULT", .callback = scpi_cmd_trigger_fault}

#endif
