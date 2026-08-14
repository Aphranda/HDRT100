#ifndef SCPI_MODEL_COMMANDS_H
#define SCPI_MODEL_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_model_turntable_load(scpi_t *context);
scpi_result_t scpi_cmd_model_turntable_load_q(scpi_t *context);
scpi_result_t scpi_cmd_model_turntable_trigger(scpi_t *context);
scpi_result_t scpi_cmd_model_turntable_trigger_q(scpi_t *context);
scpi_result_t scpi_cmd_model_turntable_motion(scpi_t *context);
scpi_result_t scpi_cmd_model_turntable_motion_q(scpi_t *context);
scpi_result_t scpi_cmd_model_turntable_start(scpi_t *context);
scpi_result_t scpi_cmd_model_turntable_stop(scpi_t *context);
scpi_result_t scpi_cmd_model_turntable_state_q(scpi_t *context);

#define SCPI_MODEL_COMMANDS \
    {.pattern = "CONFigure:MODEl:TURNtable:LOAD", .callback = scpi_cmd_model_turntable_load}, \
    {.pattern = "READ:MODEl:TURNtable:LOAD?", .callback = scpi_cmd_model_turntable_load_q}, \
    {.pattern = "CONFigure:MODEl:TURNtable:TRIGger", .callback = scpi_cmd_model_turntable_trigger}, \
    {.pattern = "READ:MODEl:TURNtable:TRIGger?", .callback = scpi_cmd_model_turntable_trigger_q}, \
    {.pattern = "CONFigure:MODEl:TURNtable:MOTion", .callback = scpi_cmd_model_turntable_motion}, \
    {.pattern = "READ:MODEl:TURNtable:MOTion?", .callback = scpi_cmd_model_turntable_motion_q}, \
    {.pattern = "MODEl:TURNtable:STARt", .callback = scpi_cmd_model_turntable_start}, \
    {.pattern = "MODEl:TURNtable:STOP", .callback = scpi_cmd_model_turntable_stop}, \
    {.pattern = "READ:MODEl:TURNtable:STATe?", .callback = scpi_cmd_model_turntable_state_q}

#endif
