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

void app_loop_engine_service(void);
void app_loop_engine_get_status(app_loop_engine_status_t *status);
void app_vdc_sync_service(void);
void app_vdc_sync_get_status(app_vdc_sync_status_t *status);
void app_dpll_service(void);
void app_dpll_get_status(app_dpll_status_t *status);
void app_config_gate_service(void);
void app_config_gate_get_status(app_config_gate_status_t *status);
void app_trigger_service(void);
void app_ota_service(void);
void app_storage_service(void);
void app_ui_service(void);
void app_diag_service(void);

#endif
