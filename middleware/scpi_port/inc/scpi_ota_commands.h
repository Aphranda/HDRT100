#ifndef SCPI_OTA_COMMANDS_H
#define SCPI_OTA_COMMANDS_H

#include "project_config.h"
#include "scpi/scpi.h"

scpi_result_t scpi_cmd_ota_status_q(scpi_t *context);
scpi_result_t scpi_cmd_ota_progress_q(scpi_t *context);
scpi_result_t scpi_cmd_ota_begin(scpi_t *context);
scpi_result_t scpi_cmd_ota_package_begin(scpi_t *context);
scpi_result_t scpi_cmd_ota_data(scpi_t *context);
scpi_result_t scpi_cmd_ota_end(scpi_t *context);
scpi_result_t scpi_cmd_ota_abort(scpi_t *context);
scpi_result_t scpi_cmd_ota_boot(scpi_t *context);
scpi_result_t scpi_cmd_ota_commit(scpi_t *context);
scpi_result_t scpi_cmd_ota_slot_q(scpi_t *context);
scpi_result_t scpi_cmd_ota_result_q(scpi_t *context);
scpi_result_t scpi_cmd_ota_transaction_q(scpi_t *context);
scpi_result_t scpi_cmd_ota_mode_q(scpi_t *context);
scpi_result_t scpi_cmd_ota_target_q(scpi_t *context);
scpi_result_t scpi_cmd_ota_capability_q(scpi_t *context);

#if PROJECT_ENABLE_OTA_FAULT_INJECTION
scpi_result_t scpi_cmd_ota_mode(scpi_t *context);
scpi_result_t scpi_cmd_ota_inject_copy(scpi_t *context);
scpi_result_t scpi_cmd_ota_inject_clear(scpi_t *context);
scpi_result_t scpi_cmd_ota_inject_copy_q(scpi_t *context);
scpi_result_t scpi_cmd_ota_inject_lockout(scpi_t *context);
scpi_result_t scpi_cmd_ota_inject_lockout_q(scpi_t *context);
scpi_result_t scpi_cmd_ota_inject_metadata_corrupt(scpi_t *context);
scpi_result_t scpi_cmd_ota_inject_metadata_repair(scpi_t *context);
#define SCPI_OTA_FAULT_INJECTION_COMMANDS \
    , {.pattern = "SYSTem:OTA:MODE", .callback = scpi_cmd_ota_mode}, \
    {.pattern = "SYSTem:OTA:INJect:COPY", .callback = scpi_cmd_ota_inject_copy}, \
    {.pattern = "SYSTem:OTA:INJect:CLEar", .callback = scpi_cmd_ota_inject_clear}, \
    {.pattern = "SYSTem:OTA:INJect:COPY?", .callback = scpi_cmd_ota_inject_copy_q}, \
    {.pattern = "SYSTem:OTA:INJect:LOCKout", .callback = scpi_cmd_ota_inject_lockout}, \
    {.pattern = "SYSTem:OTA:INJect:LOCKout?", .callback = scpi_cmd_ota_inject_lockout_q}, \
    {.pattern = "SYSTem:OTA:INJect:MCORrupt", .callback = scpi_cmd_ota_inject_metadata_corrupt}, \
    {.pattern = "SYSTem:OTA:INJect:MREPair", .callback = scpi_cmd_ota_inject_metadata_repair}
#else
#define SCPI_OTA_FAULT_INJECTION_COMMANDS
#endif

#define SCPI_OTA_COMMANDS \
    {.pattern = "SYSTem:OTA:STATus?", .callback = scpi_cmd_ota_status_q}, \
    {.pattern = "SYSTem:OTA:PROGress?", .callback = scpi_cmd_ota_progress_q}, \
    {.pattern = "SYSTem:OTA:BEGIN", .callback = scpi_cmd_ota_begin}, \
    {.pattern = "SYSTem:OTA:PBEGIN", .callback = scpi_cmd_ota_package_begin}, \
    {.pattern = "SYSTem:OTA:DATA", .callback = scpi_cmd_ota_data}, \
    {.pattern = "SYSTem:OTA:END", .callback = scpi_cmd_ota_end}, \
    {.pattern = "SYSTem:OTA:ABORt", .callback = scpi_cmd_ota_abort}, \
    {.pattern = "SYSTem:OTA:BOOT", .callback = scpi_cmd_ota_boot}, \
    {.pattern = "SYSTem:OTA:COMMit", .callback = scpi_cmd_ota_commit}, \
    {.pattern = "SYSTem:OTA:SLOT?", .callback = scpi_cmd_ota_slot_q}, \
    {.pattern = "SYSTem:OTA:RESult?", .callback = scpi_cmd_ota_result_q}, \
    {.pattern = "SYSTem:OTA:TXN?", .callback = scpi_cmd_ota_transaction_q}, \
    {.pattern = "SYSTem:OTA:TRANsaction?", .callback = scpi_cmd_ota_transaction_q}, \
    {.pattern = "SYSTem:OTA:MODE?", .callback = scpi_cmd_ota_mode_q}, \
    {.pattern = "SYSTem:OTA:TARGet?", .callback = scpi_cmd_ota_target_q}, \
    {.pattern = "SYSTem:OTA:CAPability?", .callback = scpi_cmd_ota_capability_q} \
    SCPI_OTA_FAULT_INJECTION_COMMANDS

#endif
