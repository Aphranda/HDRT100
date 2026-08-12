#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

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

typedef struct {
    bool ready;
    uint32_t gate_state;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
    uint32_t epoch;
    uint32_t run_id;
    uint32_t config_version;
    uint32_t calibration_version;
    uint32_t loop_plan_version;
    uint32_t action_map_version;
    uint32_t command_seq;
    uint32_t target_mask;
    uint32_t ack_flags;
    uint32_t nack_flags;
    uint32_t busy_flags;
    uint32_t timeout_flags;
    uint32_t build_crc32;
    uint32_t hw_profile_crc32;
    uint32_t role_map_crc32;
    uint32_t loop_plan_crc32;
    uint32_t action_map_crc32;
    uint32_t calibration_crc32;
    uint32_t config_crc32;
} app_config_gate_status_t;

typedef struct {
    uint32_t version;
    uint32_t command_seq;
    uint32_t target_mask;
    uint32_t ack_flags;
    uint32_t nack_flags;
    uint32_t busy_flags;
    uint32_t timeout_flags;
    uint32_t last_nack_reason;
    uint32_t last_nack_node;
    uint32_t reason_count;
    uint32_t reason_table_crc32;
    uint32_t config_crc32;
} app_config_ack_status_t;

typedef struct {
    uint32_t mode_id;
    uint32_t run_allowed;
    uint32_t ota_allowed;
    uint32_t fault_allowed;
    const char *name;
} app_system_mode_entry_t;

typedef struct {
    uint32_t version;
    uint32_t mode_count;
    uint32_t current_mode;
    uint32_t table_crc32;
    app_system_mode_entry_t mode[4];
} app_system_mode_table_t;

typedef struct {
    uint32_t resource_id;
    uint32_t mask;
    uint32_t owner_mode;
    uint32_t active;
    const char *name;
    const char *owner_name;
} app_resource_arbiter_entry_t;

typedef struct {
    uint32_t version;
    uint32_t resource_count;
    uint32_t current_mode;
    uint32_t active_resources;
    uint32_t last_conflict_resources;
    uint32_t table_crc32;
    app_resource_arbiter_entry_t resource[10];
} app_resource_arbiter_table_t;

typedef struct {
    uint32_t fault_id;
    uint32_t domain_id;
    uint32_t severity;
    uint32_t recoverable;
    uint32_t sticky;
    const char *name;
} app_fault_code_entry_t;

typedef struct {
    uint32_t version;
    uint32_t fault_count;
    uint32_t latched;
    uint32_t table_crc32;
    app_fault_code_entry_t fault[20];
} app_fault_code_table_t;

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
