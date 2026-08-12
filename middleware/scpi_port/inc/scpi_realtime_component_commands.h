#ifndef SCPI_REALTIME_COMPONENT_COMMANDS_H
#define SCPI_REALTIME_COMPONENT_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_realtime_pcnt_commands.h"

scpi_result_t scpi_cmd_trigger_width(scpi_t *context);
scpi_result_t scpi_cmd_trigger_width_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_fire(scpi_t *context);
scpi_result_t scpi_cmd_pulse_width(scpi_t *context);
scpi_result_t scpi_cmd_pulse_width_q(scpi_t *context);
scpi_result_t scpi_cmd_pulse_fire(scpi_t *context);
scpi_result_t scpi_cmd_marker_width(scpi_t *context);
scpi_result_t scpi_cmd_marker_width_q(scpi_t *context);
scpi_result_t scpi_cmd_marker_fire(scpi_t *context);
scpi_result_t scpi_cmd_rj45_trigger_width(scpi_t *context);
scpi_result_t scpi_cmd_rj45_trigger_width_q(scpi_t *context);
scpi_result_t scpi_cmd_rj45_trigger_fire(scpi_t *context);
scpi_result_t scpi_cmd_rj45_trigger_pins_q(scpi_t *context);
scpi_result_t scpi_cmd_sample_rate(scpi_t *context);
scpi_result_t scpi_cmd_sample_rate_q(scpi_t *context);
scpi_result_t scpi_cmd_sample_state(scpi_t *context);
scpi_result_t scpi_cmd_sample_state_q(scpi_t *context);
scpi_result_t scpi_cmd_clock_freq(scpi_t *context);
scpi_result_t scpi_cmd_clock_freq_q(scpi_t *context);
scpi_result_t scpi_cmd_clock_state(scpi_t *context);
scpi_result_t scpi_cmd_clock_state_q(scpi_t *context);
scpi_result_t scpi_cmd_status_q(scpi_t *context);
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
scpi_result_t scpi_cmd_enc_target(scpi_t *context);
scpi_result_t scpi_cmd_enc_target_q(scpi_t *context);
scpi_result_t scpi_cmd_enc_count_q(scpi_t *context);
scpi_result_t scpi_cmd_enc_a_pin(scpi_t *context);
scpi_result_t scpi_cmd_enc_a_pin_q(scpi_t *context);
scpi_result_t scpi_cmd_enc_rev_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_status_q(scpi_t *context);

#define SCPI_REALTIME_COMPONENT_COMMANDS \
    {.pattern = "TRIGger:WIDTh", .callback = scpi_cmd_trigger_width}, \
    {.pattern = "TRIGger:WIDTh?", .callback = scpi_cmd_trigger_width_q}, \
    {.pattern = "TRIGger:IMMediate", .callback = scpi_cmd_trigger_fire}, \
    {.pattern = "PULSe:WIDTh", .callback = scpi_cmd_pulse_width}, \
    {.pattern = "PULSe:WIDTh?", .callback = scpi_cmd_pulse_width_q}, \
    {.pattern = "PULSe:IMMediate", .callback = scpi_cmd_pulse_fire}, \
    {.pattern = "MARKer:WIDTh", .callback = scpi_cmd_marker_width}, \
    {.pattern = "MARKer:WIDTh?", .callback = scpi_cmd_marker_width_q}, \
    {.pattern = "MARKer:IMMediate", .callback = scpi_cmd_marker_fire}, \
    {.pattern = "RJ45:TRIGger:WIDTh", .callback = scpi_cmd_rj45_trigger_width}, \
    {.pattern = "RJ45:TRIGger:WIDTh?", .callback = scpi_cmd_rj45_trigger_width_q}, \
    {.pattern = "RJ45:TRIGger:IMMediate", .callback = scpi_cmd_rj45_trigger_fire}, \
    {.pattern = "RJ45:TRIGger:PINs?", .callback = scpi_cmd_rj45_trigger_pins_q}, \
    {.pattern = "SAMPle:RATE", .callback = scpi_cmd_sample_rate}, \
    {.pattern = "SAMPle:RATE?", .callback = scpi_cmd_sample_rate_q}, \
    {.pattern = "SAMPle:STATe", .callback = scpi_cmd_sample_state}, \
    {.pattern = "SAMPle:STATe?", .callback = scpi_cmd_sample_state_q}, \
    {.pattern = "OUTPut:CLOCk:FREQuency", .callback = scpi_cmd_clock_freq}, \
    {.pattern = "OUTPut:CLOCk:FREQuency?", .callback = scpi_cmd_clock_freq_q}, \
    {.pattern = "OUTPut:CLOCk:STATe", .callback = scpi_cmd_clock_state}, \
    {.pattern = "OUTPut:CLOCk:STATe?", .callback = scpi_cmd_clock_state_q}, \
    {.pattern = "STATus:SYNC?", .callback = scpi_cmd_status_q}, \
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
    {.pattern = "TRIGger:FAULT", .callback = scpi_cmd_trigger_fault}, \
    {.pattern = "TRIGger:ENC:TARGet", .callback = scpi_cmd_enc_target}, \
    {.pattern = "TRIGger:ENC:TARGet?", .callback = scpi_cmd_enc_target_q}, \
    {.pattern = "TRIGger:ENC:COUNt?", .callback = scpi_cmd_enc_count_q}, \
    {.pattern = "TRIGger:ENC:APIN", .callback = scpi_cmd_enc_a_pin}, \
    {.pattern = "TRIGger:ENC:APIN?", .callback = scpi_cmd_enc_a_pin_q}, \
    {.pattern = "TRIGger:ENC:REVolution?", .callback = scpi_cmd_enc_rev_q}, \
    SCPI_REALTIME_PCNT_COMMANDS, \
    {.pattern = "STATus:TRIGger?", .callback = scpi_cmd_trigger_status_q}

#endif
