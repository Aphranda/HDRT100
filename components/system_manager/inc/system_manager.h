#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

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
} system_manager_config_gate_status_t;

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
} system_manager_config_ack_status_t;

typedef struct {
    uint32_t mode_id;
    uint32_t run_allowed;
    uint32_t ota_allowed;
    uint32_t fault_allowed;
    const char *name;
} system_manager_mode_entry_t;

typedef struct {
    uint32_t version;
    uint32_t mode_count;
    uint32_t current_mode;
    uint32_t table_crc32;
    system_manager_mode_entry_t mode[4];
} system_manager_mode_table_t;

typedef struct {
    uint32_t resource_id;
    uint32_t mask;
    uint32_t owner_mode;
    uint32_t active;
    const char *name;
    const char *owner_name;
} system_manager_resource_entry_t;

typedef struct {
    uint32_t version;
    uint32_t resource_count;
    uint32_t current_mode;
    uint32_t active_resources;
    uint32_t last_conflict_resources;
    uint32_t table_crc32;
    system_manager_resource_entry_t resource[10];
} system_manager_resource_table_t;

typedef struct {
    uint32_t fault_id;
    uint32_t domain_id;
    uint32_t severity;
    uint32_t recoverable;
    uint32_t sticky;
    const char *name;
} system_manager_fault_entry_t;

typedef struct {
    uint32_t version;
    uint32_t fault_count;
    uint32_t latched;
    uint32_t table_crc32;
    system_manager_fault_entry_t fault[20];
} system_manager_fault_table_t;

bool system_manager_init(void);
void system_manager_service(void);
void system_manager_get_config_gate_status(system_manager_config_gate_status_t *status);
void system_manager_get_config_ack_status(system_manager_config_ack_status_t *status);
void system_manager_get_mode_table(system_manager_mode_table_t *table);
void system_manager_get_resource_table(system_manager_resource_table_t *table);
void system_manager_get_fault_table(system_manager_fault_table_t *table);

#endif
