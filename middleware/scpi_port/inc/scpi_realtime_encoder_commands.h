#ifndef SCPI_REALTIME_ENCODER_COMMANDS_H
#define SCPI_REALTIME_ENCODER_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_enc_target(scpi_t *context);
scpi_result_t scpi_cmd_enc_target_q(scpi_t *context);
scpi_result_t scpi_cmd_enc_count_q(scpi_t *context);
scpi_result_t scpi_cmd_enc_a_pin(scpi_t *context);
scpi_result_t scpi_cmd_enc_a_pin_q(scpi_t *context);
scpi_result_t scpi_cmd_enc_rev_q(scpi_t *context);

#define SCPI_REALTIME_ENCODER_COMMANDS \
    {.pattern = "TRIGger:ENC:TARGet", .callback = scpi_cmd_enc_target}, \
    {.pattern = "TRIGger:ENC:TARGet?", .callback = scpi_cmd_enc_target_q}, \
    {.pattern = "TRIGger:ENC:COUNt?", .callback = scpi_cmd_enc_count_q}, \
    {.pattern = "TRIGger:ENC:APIN", .callback = scpi_cmd_enc_a_pin}, \
    {.pattern = "TRIGger:ENC:APIN?", .callback = scpi_cmd_enc_a_pin_q}, \
    {.pattern = "TRIGger:ENC:REVolution?", .callback = scpi_cmd_enc_rev_q}

#endif
