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
    {.pattern = "TRIGger:SOURce", .callback = scpi_cmd_trigger_source}, \
    {.pattern = "TRIGger:SOURce?", .callback = scpi_cmd_trigger_source_q}, \
    {.pattern = "TRIGger:EDGE", .callback = scpi_cmd_trigger_edge}, \
    {.pattern = "TRIGger:EDGE?", .callback = scpi_cmd_trigger_edge_q}, \
    {.pattern = "TRIGger:GATE", .callback = scpi_cmd_trigger_gate}, \
    {.pattern = "TRIGger:GATE?", .callback = scpi_cmd_trigger_gate_q}, \
    {.pattern = "TRIGger:SAFE", .callback = scpi_cmd_trigger_safe}, \
    {.pattern = "TRIGger:SAFE?", .callback = scpi_cmd_trigger_safe_q}, \
    {.pattern = "TRIGger:SEQ:LENGth", .callback = scpi_cmd_trigger_seq_length}, \
    {.pattern = "TRIGger:SEQ:LENGth?", .callback = scpi_cmd_trigger_seq_length_q}, \
    {.pattern = "TRIGger:SEQ:WIDTh", .callback = scpi_cmd_trigger_seq_width}, \
    {.pattern = "TRIGger:SEQ:WIDTh?", .callback = scpi_cmd_trigger_seq_width_q}, \
    {.pattern = "TRIGger:SEQ:INDex?", .callback = scpi_cmd_trigger_seq_index_q}, \
    {.pattern = "TRIGger:SEQ:DATA", .callback = scpi_cmd_trigger_seq_data}, \
    {.pattern = "TRIGger:SEQ:DATA?", .callback = scpi_cmd_trigger_seq_data_q}, \
    {.pattern = "TRIGger:ARM", .callback = scpi_cmd_trigger_arm}, \
    {.pattern = "TRIGger:DISarm", .callback = scpi_cmd_trigger_disarm}, \
    {.pattern = "TRIGger:DISAble", .callback = scpi_cmd_trigger_disarm}, \
    {.pattern = "TRIGger:FAULT", .callback = scpi_cmd_trigger_fault}

#endif
