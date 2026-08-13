#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "system_manager.h"

bool app_init(void);
bool app_is_ready(void);
void app_run_once(void);
void app_management_run_once(void);
void app_realtime_run_once(void);
void app_comm_service(void);
void app_usb_device_service(void);
void app_scpi_service(void);
void app_refmem_service(void);

typedef struct {
    bool ready;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
} app_loop_engine_status_t;

typedef struct {
    bool ready;
    uint32_t lock_state;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
    uint32_t sync_seq;
} app_vdc_sync_status_t;

typedef struct {
    bool ready;
    uint32_t state;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
    uint32_t update_seq;
} app_dpll_status_t;

typedef struct {
    bool ready;
    uint32_t state;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
    uint32_t command_seq;
    uint32_t link_count;
    uint32_t delay_count;
    uint32_t active_crc32;
    uint32_t last_error;
} app_calibration_status_t;

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
