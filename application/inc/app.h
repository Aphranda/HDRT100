#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "calibration_manager.h"
#include "loop_engine.h"
#include "system_manager.h"
#include "vdc_dpll_manager.h"

bool app_init(void);
bool app_is_ready(void);
void app_run_once(void);
void app_management_run_once(void);
void app_realtime_run_once(void);
void app_comm_service(void);
void app_usb_device_service(void);
void app_scpi_service(void);
void app_refmem_service(void);

typedef loop_engine_status_t app_loop_engine_status_t;

typedef vdc_dpll_manager_vdc_status_t app_vdc_sync_status_t;
typedef vdc_dpll_manager_dpll_status_t app_dpll_status_t;

typedef calibration_manager_status_t app_calibration_status_t;

typedef system_manager_config_gate_status_t app_config_gate_status_t;
typedef system_manager_config_ack_status_t app_config_ack_status_t;
typedef system_manager_mode_entry_t app_system_mode_entry_t;
typedef system_manager_mode_table_t app_system_mode_table_t;
typedef system_manager_resource_entry_t app_resource_arbiter_entry_t;
typedef system_manager_resource_table_t app_resource_arbiter_table_t;
typedef system_manager_fault_entry_t app_fault_code_entry_t;
typedef system_manager_fault_table_t app_fault_code_table_t;

void app_loop_engine_service(void);
void app_loop_engine_get_status(app_loop_engine_status_t *status);
void app_vdc_sync_service(void);
void app_vdc_sync_get_status(app_vdc_sync_status_t *status);
void app_dpll_service(void);
void app_dpll_get_status(app_dpll_status_t *status);
void app_calibration_service(void);
void app_calibration_get_status(app_calibration_status_t *status);
void app_config_gate_service(void);
void app_config_gate_get_status(app_config_gate_status_t *status);
void app_config_gate_get_ack_status(app_config_ack_status_t *status);
void app_system_mode_get_table(app_system_mode_table_t *table);
void app_resource_arbiter_get_table(app_resource_arbiter_table_t *table);
void app_fault_code_get_table(app_fault_code_table_t *table);
void app_trigger_service(void);
void app_ota_service(void);
void app_storage_service(void);
void app_ui_service(void);
void app_diag_service(void);

#endif
