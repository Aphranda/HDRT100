#ifndef SCPI_REALTIME_PCNT_COMMANDS_H
#define SCPI_REALTIME_PCNT_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_pcnt_decode(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_decode_q(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_dir(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_dir_q(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_filter(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_filter_q(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_gate(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_gate_q(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_cmp(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_cmp_q(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_preset(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_preset_q(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_clear(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_total_q(scpi_t *context);
scpi_result_t scpi_cmd_pcnt_freq_q(scpi_t *context);

#define SCPI_REALTIME_PCNT_COMMANDS \
    {.pattern = "REALtime:PCNT:DECode", .callback = scpi_cmd_pcnt_decode}, \
    {.pattern = "REALtime:PCNT:DECode?", .callback = scpi_cmd_pcnt_decode_q}, \
    {.pattern = "REALtime:PCNT:DIRection", .callback = scpi_cmd_pcnt_dir}, \
    {.pattern = "REALtime:PCNT:DIRection?", .callback = scpi_cmd_pcnt_dir_q}, \
    {.pattern = "REALtime:PCNT:FILTer", .callback = scpi_cmd_pcnt_filter}, \
    {.pattern = "REALtime:PCNT:FILTer?", .callback = scpi_cmd_pcnt_filter_q}, \
    {.pattern = "REALtime:PCNT:GATE", .callback = scpi_cmd_pcnt_gate}, \
    {.pattern = "REALtime:PCNT:GATE?", .callback = scpi_cmd_pcnt_gate_q}, \
    {.pattern = "REALtime:PCNT:CMP", .callback = scpi_cmd_pcnt_cmp}, \
    {.pattern = "REALtime:PCNT:CMP?", .callback = scpi_cmd_pcnt_cmp_q}, \
    {.pattern = "REALtime:PCNT:PRESet", .callback = scpi_cmd_pcnt_preset}, \
    {.pattern = "REALtime:PCNT:PRESet?", .callback = scpi_cmd_pcnt_preset_q}, \
    {.pattern = "REALtime:PCNT:CLEar", .callback = scpi_cmd_pcnt_clear}, \
    {.pattern = "REALtime:PCNT:TOTal?", .callback = scpi_cmd_pcnt_total_q}, \
    {.pattern = "REALtime:PCNT:FREQuency?", .callback = scpi_cmd_pcnt_freq_q}, \
    {.pattern = "TRIGger:PCNT:DECode", .callback = scpi_cmd_pcnt_decode}, \
    {.pattern = "TRIGger:PCNT:DECode?", .callback = scpi_cmd_pcnt_decode_q}, \
    {.pattern = "TRIGger:PCNT:DIRection", .callback = scpi_cmd_pcnt_dir}, \
    {.pattern = "TRIGger:PCNT:DIRection?", .callback = scpi_cmd_pcnt_dir_q}, \
    {.pattern = "TRIGger:PCNT:FILTer", .callback = scpi_cmd_pcnt_filter}, \
    {.pattern = "TRIGger:PCNT:FILTer?", .callback = scpi_cmd_pcnt_filter_q}, \
    {.pattern = "TRIGger:PCNT:GATE", .callback = scpi_cmd_pcnt_gate}, \
    {.pattern = "TRIGger:PCNT:GATE?", .callback = scpi_cmd_pcnt_gate_q}, \
    {.pattern = "TRIGger:PCNT:CMP", .callback = scpi_cmd_pcnt_cmp}, \
    {.pattern = "TRIGger:PCNT:CMP?", .callback = scpi_cmd_pcnt_cmp_q}, \
    {.pattern = "TRIGger:PCNT:PRESet", .callback = scpi_cmd_pcnt_preset}, \
    {.pattern = "TRIGger:PCNT:PRESet?", .callback = scpi_cmd_pcnt_preset_q}, \
    {.pattern = "TRIGger:PCNT:CLEar", .callback = scpi_cmd_pcnt_clear}, \
    {.pattern = "TRIGger:PCNT:TOTal?", .callback = scpi_cmd_pcnt_total_q}, \
    {.pattern = "TRIGger:PCNT:FREQuency?", .callback = scpi_cmd_pcnt_freq_q}

#endif
