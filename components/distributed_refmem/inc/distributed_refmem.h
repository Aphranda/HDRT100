#ifndef DISTRIBUTED_REFMEM_H
#define DISTRIBUTED_REFMEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DISTRIBUTED_REFMEM_TABLE_SIZE       65536u
#define DISTRIBUTED_REFMEM_LAYOUT_VERSION   1u
#define DISTRIBUTED_REFMEM_NODE_COUNT       8u
#define DISTRIBUTED_REFMEM_LOCAL_NODE_ID    0u

#define DISTRIBUTED_REFMEM_NODE_FLAG_VIRTUAL 0x00000001u

#define DISTRIBUTED_REFMEM_OWNER_CORE0       0u
#define DISTRIBUTED_REFMEM_OWNER_CORE1       1u
#define DISTRIBUTED_REFMEM_OWNER_SHARED      2u

#define DISTRIBUTED_REFMEM_IRQ_USB_MASK      0x00000001u
#define DISTRIBUTED_REFMEM_IRQ_STORAGE_MASK  0x00000002u
#define DISTRIBUTED_REFMEM_IRQ_OTA_MASK      0x00000004u
#define DISTRIBUTED_REFMEM_IRQ_UI_MASK       0x00000008u
#define DISTRIBUTED_REFMEM_IRQ_PIO_MASK      0x00000100u
#define DISTRIBUTED_REFMEM_IRQ_DMA_MASK      0x00000200u
#define DISTRIBUTED_REFMEM_IRQ_CAPTURE_MASK  0x00000400u
#define DISTRIBUTED_REFMEM_IRQ_TIMER_MASK    0x00000800u

#define DISTRIBUTED_REFMEM_PROT_RAM_RESIDENT_REQUIRED 0x00000001u
#define DISTRIBUTED_REFMEM_PROT_FLASH_LOCKOUT_READY   0x00000002u
#define DISTRIBUTED_REFMEM_PROT_CORE1_PARKED          0x00000004u
#define DISTRIBUTED_REFMEM_PROT_ENTRY_OWNER_VALID     0x00000008u

#define DISTRIBUTED_REFMEM_FLAG_DIRECTORY_VALID       0x00000001u
#define DISTRIBUTED_REFMEM_FLAG_DIRECTORY_CRC_VALID   0x00000002u
#define DISTRIBUTED_REFMEM_FLAG_APP_MODEL_VALID       0x00000004u

#define DISTRIBUTED_REFMEM_HEADER_SIZE      1024u
#define DISTRIBUTED_REFMEM_SYSTEM_SIZE      1024u
#define DISTRIBUTED_REFMEM_ROLE_SIZE        2048u
#define DISTRIBUTED_REFMEM_VDC_SIZE         2048u
#define DISTRIBUTED_REFMEM_LOOP_SIZE        4096u
#define DISTRIBUTED_REFMEM_DPLL_SIZE        2048u
#define DISTRIBUTED_REFMEM_NODE_SLOT_SIZE   512u
#define DISTRIBUTED_REFMEM_TRIGGER_SIZE     8192u
#define DISTRIBUTED_REFMEM_IO_SIZE          8192u
#define DISTRIBUTED_REFMEM_CAL_SIZE         8192u
#define DISTRIBUTED_REFMEM_STATS_SIZE       8192u
#define DISTRIBUTED_REFMEM_ACK_CMD_SIZE     4096u
#define DISTRIBUTED_REFMEM_FAULT_SIZE       6144u
#define DISTRIBUTED_REFMEM_GATEWAY_SIZE     2048u
#define DISTRIBUTED_REFMEM_SERVICE_SIZE     2048u
#define DISTRIBUTED_REFMEM_TLV_SIZE         2048u

#include "refmem_command.h"
#include "refmem_realtime_tdma.h"

typedef bool (*distributed_refmem_node_load_owner_t)(uint32_t instance_id,
                                                     uint32_t slot_id,
                                                     uint32_t payload_ref,
                                                     void *context);

typedef enum {
    DISTRIBUTED_REFMEM_NODE_MISSING = 0,
    DISTRIBUTED_REFMEM_NODE_OK = 1,
    DISTRIBUTED_REFMEM_NODE_STALE = 2,
    DISTRIBUTED_REFMEM_NODE_INVALID = 3,
    DISTRIBUTED_REFMEM_NODE_FAULT = 4,
} distributed_refmem_node_state_t;

typedef enum {
    DISTRIBUTED_REFMEM_NODE_TYPE_BOARD = 0,
    DISTRIBUTED_REFMEM_NODE_TYPE_MODEL_VNA = 1,
    DISTRIBUTED_REFMEM_NODE_TYPE_MODEL_TURNTABLE = 2,
    DISTRIBUTED_REFMEM_NODE_TYPE_MODEL_DUT = 3,
    DISTRIBUTED_REFMEM_NODE_TYPE_TEST_AGENT = 4,
} distributed_refmem_node_type_t;

typedef struct {
    uint32_t table_size;
    uint32_t layout_version;
    uint32_t table_seq;
    uint32_t local_node_id;
    uint32_t node_count;
    uint32_t local_heartbeat;
    uint32_t service_count;
    uint32_t flags;
} distributed_refmem_status_t;

typedef struct {
    uint32_t node_id;
    uint32_t state;
    uint32_t heartbeat;
    uint32_t slot_version;
    uint32_t last_update_ms;
    uint32_t stale_count;
    uint32_t fault_code;
    uint32_t flags;
    uint32_t node_type;
} distributed_refmem_node_snapshot_t;

typedef struct {
    uint32_t table_seq;
    uint32_t owner;
    uint32_t crc32;
    uint32_t stale;
    uint32_t flags;
} distributed_refmem_slot_guard_t;

typedef struct {
    uint32_t version;
    uint32_t table_seq;
    uint32_t core_count;
    uint32_t core0_vtor_owner;
    uint32_t core1_vtor_owner;
    uint32_t core0_irq_owner_mask;
    uint32_t core1_irq_owner_mask;
    uint32_t entry_table_owner;
    uint32_t flags;
    distributed_refmem_slot_guard_t guard;
} distributed_refmem_core_vector_snapshot_t;

typedef struct {
    uint32_t version;
    uint32_t table_seq;
    uint32_t ram_resident_required;
    uint32_t flash_lockout_supported;
    uint32_t flash_lockout_online;
    uint32_t flash_lockout_requested;
    uint32_t flash_lockout_acknowledged;
    uint32_t park_state;
    uint32_t last_result;
    uint32_t last_elapsed_us;
    uint32_t request_seq;
    uint32_t ack_seq;
    uint32_t release_seq;
    uint32_t timeout_count;
    uint32_t release_timeout_count;
    uint32_t entry_table_owner;
    uint32_t flags;
    distributed_refmem_slot_guard_t guard;
} distributed_refmem_runtime_protection_snapshot_t;

bool distributed_refmem_init(void);
void distributed_refmem_service(void);
void distributed_refmem_realtime_run_once(void);
void distributed_refmem_get_status(distributed_refmem_status_t *status);
bool distributed_refmem_get_node(uint32_t node_id, distributed_refmem_node_snapshot_t *snapshot);
void distributed_refmem_get_core_vector(distributed_refmem_core_vector_snapshot_t *snapshot);
void distributed_refmem_get_runtime_protection(distributed_refmem_runtime_protection_snapshot_t *snapshot);
bool distributed_refmem_get_realtime_tdma(refmem_realtime_tdma_snapshot_t *snapshot);
bool distributed_refmem_get_realtime_tdma_frame(uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size);
bool distributed_refmem_submit_realtime_tdma_tx(
    const refmem_realtime_tdma_intent_config_t *config);
bool distributed_refmem_submit_realtime_tdma_rx(
    const refmem_realtime_tdma_intent_config_t *config);
void distributed_refmem_abort_realtime_tdma(void);
bool distributed_refmem_quality_gate_ready(void);
bool distributed_refmem_command_set_reason_table_crc32(uint32_t reason_table_crc32);
bool distributed_refmem_command_try_post(const refmem_command_request_t *request,
                                         uint32_t issue_tick32);
bool distributed_refmem_command_ack(uint32_t target_node,
                                    uint32_t evidence_index);
bool distributed_refmem_command_nack(uint32_t target_node,
                                     refmem_command_reason_t reason,
                                     uint32_t evidence_index);
bool distributed_refmem_command_mark_timeout(uint32_t now_tick32,
                                             uint32_t evidence_index);
bool distributed_refmem_command_clear(uint32_t clear_seq);
bool distributed_refmem_get_command_snapshot(refmem_command_snapshot_t *snapshot);
bool distributed_refmem_register_node_load_owner(
    uint32_t instance_id,
    distributed_refmem_node_load_owner_t owner,
    void *context);
bool distributed_refmem_stage_node_load(uint32_t node_id,
                                        uint32_t instance_id,
                                        uint32_t role_mask,
                                        uint32_t persona_mask,
                                        uint32_t enabled,
                                        uint32_t required,
                                        uint32_t load_order);
bool distributed_refmem_stage_sd_system_pack(const char *path,
                                             uint32_t path_hash,
                                             uint32_t manifest_status,
                                             uint32_t manifest_schema,
                                             uint32_t manifest_required_count,
                                             uint32_t manifest_missing_count,
                                             const char *manifest_build_id,
                                             uint32_t package_crc32,
                                             uint32_t package_valid,
                                             uint32_t package_error,
                                             const uint8_t *package_data,
                                             size_t package_size,
                                             const uint32_t *table_crc32,
                                             uint32_t table_crc32_count,
                                             uint32_t owner_validated_table_mask,
                                             uint32_t first_bad_table);
bool distributed_refmem_stage_board_capability(uint32_t board_id,
                                               uint32_t board_uuid_crc32,
                                               uint32_t capability_mask,
                                               uint32_t io_constraint_mask,
                                               uint32_t ip_core_mask,
                                               uint32_t default_persona_mask,
                                               uint32_t hw_profile_crc32,
                                               uint32_t active_default_slot,
                                               uint32_t online_required);
bool distributed_refmem_stage_model_turntable_load(uint32_t slot_id,
                                                   uint32_t output_index);

#endif
