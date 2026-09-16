#ifndef SCPI_SEQUENCE_COMMANDS_H
#define SCPI_SEQUENCE_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_sequence_next(scpi_t *context);
scpi_result_t scpi_sequence_source(scpi_t *context);
scpi_result_t scpi_sequence_source_q(scpi_t *context);
scpi_result_t scpi_sequence_io(scpi_t *context);
scpi_result_t scpi_sequence_io_q(scpi_t *context);
scpi_result_t scpi_sequence_output_config(scpi_t *context);
scpi_result_t scpi_sequence_output_config_q(scpi_t *context);
scpi_result_t scpi_sequence_code(scpi_t *context);
scpi_result_t scpi_sequence_code_q(scpi_t *context);
scpi_result_t scpi_sequence_status_q(scpi_t *context);
scpi_result_t scpi_sequence_timing_q(scpi_t *context);
scpi_result_t scpi_sequence_rejections_q(scpi_t *context);
scpi_result_t scpi_sequence_input_q(scpi_t *context);
scpi_result_t scpi_sequence_output_q(scpi_t *context);
scpi_result_t scpi_sequence_io_state_q(scpi_t *context);
scpi_result_t scpi_sequence_repeat(scpi_t *context);
scpi_result_t scpi_sequence_repeat_q(scpi_t *context);

#define SCPI_SEQUENCE_COMMANDS \
    {.pattern = "CONFigure:SEQuence:REPeat", .callback = scpi_sequence_repeat}, \
    {.pattern = "READ:SEQuence:REPeat?", .callback = scpi_sequence_repeat_q}, \
    {.pattern = "TRIGger:SEQuence:NEXT", .callback = scpi_sequence_next}, \
    {.pattern = "TRIGger:SEQuence:NEXT?", .callback = scpi_sequence_status_q}, \
    {.pattern = "CONFigure:SEQuence:SOURce", .callback = scpi_sequence_source}, \
    {.pattern = "READ:SEQuence:SOURce?", .callback = scpi_sequence_source_q}, \
    {.pattern = "CONFigure:SEQuence:IO", .callback = scpi_sequence_io}, \
    {.pattern = "READ:SEQuence:IO?", .callback = scpi_sequence_io_q}, \
    {.pattern = "CONFigure:SEQuence:OUTPut", .callback = scpi_sequence_output_config}, \
    {.pattern = "READ:SEQuence:OUTPut?", .callback = scpi_sequence_output_config_q}, \
    {.pattern = "CONFigure:SEQuence:CODE", .callback = scpi_sequence_code}, \
    {.pattern = "READ:SEQuence:CODE?", .callback = scpi_sequence_code_q}, \
    {.pattern = "READ:SEQuence:STATe?", .callback = scpi_sequence_status_q}, \
    {.pattern = "READ:SEQuence:TIMing?", .callback = scpi_sequence_timing_q}, \
    {.pattern = "READ:SEQuence:REJections?", .callback = scpi_sequence_rejections_q}, \
    {.pattern = "READ:IO:INPut?", .callback = scpi_sequence_input_q}, \
    {.pattern = "READ:IO:OUTPut?", .callback = scpi_sequence_output_q}, \
    {.pattern = "READ:IO:STATe?", .callback = scpi_sequence_io_state_q}

#endif
