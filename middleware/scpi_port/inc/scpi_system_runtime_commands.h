#ifndef SCPI_SYSTEM_RUNTIME_COMMANDS_H
#define SCPI_SYSTEM_RUNTIME_COMMANDS_H

#include "project_config.h"
#include "scpi/scpi.h"

scpi_result_t scpi_cmd_core_tst_q(scpi_t *context);
scpi_result_t scpi_cmd_firmware_version_q(scpi_t *context);
scpi_result_t scpi_cmd_firmware_build_q(scpi_t *context);
scpi_result_t scpi_cmd_bootloader_version_q(scpi_t *context);
scpi_result_t scpi_cmd_bootloader_capability_q(scpi_t *context);
scpi_result_t scpi_cmd_log_level(scpi_t *context);
scpi_result_t scpi_cmd_log_level_q(scpi_t *context);
scpi_result_t scpi_cmd_log_status_q(scpi_t *context);
scpi_result_t scpi_cmd_core_status_q(scpi_t *context);
scpi_result_t scpi_cmd_rtos_status_q(scpi_t *context);
scpi_result_t scpi_cmd_ui_keys_q(scpi_t *context);
scpi_result_t scpi_cmd_led_status_q(scpi_t *context);
scpi_result_t scpi_cmd_watchdog_status_q(scpi_t *context);
scpi_result_t scpi_cmd_watchdog_log_q(scpi_t *context);
#if PROJECT_ENABLE_WATCHDOG_TEST
scpi_result_t scpi_cmd_watchdog_test(scpi_t *context);
#endif

#define SCPI_SYSTEM_RUNTIME_COMMANDS \
    {.pattern = "*TST?", .callback = scpi_cmd_core_tst_q}, \
    {.pattern = "SYSTem:FW:VERSion?", .callback = scpi_cmd_firmware_version_q}, \
    {.pattern = "SYSTem:FW:BUILD?", .callback = scpi_cmd_firmware_build_q}, \
    {.pattern = "SYSTem:BOOT:VERSion?", .callback = scpi_cmd_bootloader_version_q}, \
    {.pattern = "SYSTem:BOOT:CAPability?", .callback = scpi_cmd_bootloader_capability_q}, \
    {.pattern = "SYSTem:LOG:LEVel", .callback = scpi_cmd_log_level}, \
    {.pattern = "SYSTem:LOG:LEVel?", .callback = scpi_cmd_log_level_q}, \
    {.pattern = "SYSTem:LOG:STATus?", .callback = scpi_cmd_log_status_q}, \
    {.pattern = "SYSTem:CORE?", .callback = scpi_cmd_core_status_q}, \
    {.pattern = "SYSTem:RTOS:STATus?", .callback = scpi_cmd_rtos_status_q}, \
    {.pattern = "SYSTem:UI:KEYs?", .callback = scpi_cmd_ui_keys_q}, \
    {.pattern = "SYSTem:LED:STATus?", .callback = scpi_cmd_led_status_q}, \
    {.pattern = "SYSTem:WATCHdog:STATus?", .callback = scpi_cmd_watchdog_status_q}, \
    {.pattern = "SYSTem:WATCHdog:LOG?", .callback = scpi_cmd_watchdog_log_q} \
    SCPI_SYSTEM_WATCHDOG_TEST_COMMANDS

#if PROJECT_ENABLE_WATCHDOG_TEST
#define SCPI_SYSTEM_WATCHDOG_TEST_COMMANDS \
    , {.pattern = "SYSTem:WATCHdog:TEST", .callback = scpi_cmd_watchdog_test}
#else
#define SCPI_SYSTEM_WATCHDOG_TEST_COMMANDS
#endif

#endif
