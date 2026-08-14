#ifndef REFMEM_APPLICATION_MODEL_H
#define REFMEM_APPLICATION_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#include "distributed_refmem.h"

#define REFMEM_APP_MODEL_VERSION                   1u
#define REFMEM_APP_MODEL_NODE_COUNT                DISTRIBUTED_REFMEM_NODE_COUNT
#define REFMEM_APP_MODEL_NODE_LOAD_COUNT           11u
#define REFMEM_APP_MODEL_INSTANCE_COUNT            11u
#define REFMEM_APP_MODEL_EVENT_LINK_COUNT          11u
#define REFMEM_APP_MODEL_DATA_LINK_COUNT           14u
#define REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT    11u
#define REFMEM_APP_MODEL_QUALITY_COUNT             8u
#define REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX       16u
#define REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT    REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX
#define REFMEM_APP_MODEL_SD_MANIFEST_OK            1u

#define REFMEM_APP_ROLE_BOARD                      0x00000001u
#define REFMEM_APP_ROLE_PULSE_DISTRIBUTOR          0x00000002u
#define REFMEM_APP_ROLE_LINK_SWITCHER              0x00000004u
#define REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER      0x00000008u
#define REFMEM_APP_ROLE_GATEWAY                    0x00000010u
#define REFMEM_APP_ROLE_MODEL_VNA                  0x00000020u
#define REFMEM_APP_ROLE_MODEL_TURNTABLE            0x00000040u
#define REFMEM_APP_ROLE_TEST_AGENT                 0x00000080u

#define REFMEM_APP_CAP_BOARD                       0x00000001u
#define REFMEM_APP_CAP_FLASH                       0x00000002u
#define REFMEM_APP_CAP_SD                          0x00000004u
#define REFMEM_APP_CAP_USB                         0x00000008u
#define REFMEM_APP_CAP_PIO                         0x00000010u
#define REFMEM_APP_CAP_DMA                         0x00000020u
#define REFMEM_APP_CAP_LCD                         0x00000040u
#define REFMEM_APP_CAP_RJ45                        0x00000080u
#define REFMEM_APP_CAP_CORE1_RT                    0x00000100u
#define REFMEM_APP_CAP_SMA_IN                      0x00000200u
#define REFMEM_APP_CAP_SMA_OUT                     0x00000400u
#define REFMEM_APP_CAP_LINK_CONTROL                0x00000800u
#define REFMEM_APP_CAP_BISS_C                      0x00001000u
#define REFMEM_APP_CAP_UART_RS485                  0x00002000u
#define REFMEM_APP_CAP_REFMEM                      0x00004000u
#define REFMEM_APP_CAP_VDC                         0x00008000u
#define REFMEM_APP_CAP_BASELINE                    (REFMEM_APP_CAP_BOARD | \
                                                    REFMEM_APP_CAP_REFMEM | \
                                                    REFMEM_APP_CAP_VDC)

#define REFMEM_APP_PERSONA_TRIGGER_MASTER          0x00000001u
#define REFMEM_APP_PERSONA_DISTRIBUTED_TRIGGER     0x00000002u
#define REFMEM_APP_PERSONA_LINK_CONTROL            0x00000004u
#define REFMEM_APP_PERSONA_GATEWAY                 0x00000008u
#define REFMEM_APP_PERSONA_MODEL_INSTRUMENTS       0x00000010u
#define REFMEM_APP_PERSONA_SPARE                   0x00000020u

#define REFMEM_APP_RESOURCE_FLASH                  0x00000001u
#define REFMEM_APP_RESOURCE_SD                     0x00000002u
#define REFMEM_APP_RESOURCE_USB                    0x00000004u
#define REFMEM_APP_RESOURCE_PIO                    0x00000008u
#define REFMEM_APP_RESOURCE_DMA                    0x00000010u
#define REFMEM_APP_RESOURCE_LCD                    0x00000020u
#define REFMEM_APP_RESOURCE_RJ45                   0x00000040u
#define REFMEM_APP_RESOURCE_CORE1_RT               0x00000080u

#define REFMEM_APP_IO_SMA_IN                       0x00000001u
#define REFMEM_APP_IO_SMA_OUT                      0x00000002u
#define REFMEM_APP_IO_RJ45_SYNC                    0x00000004u
#define REFMEM_APP_IO_LINK_CONTROL                 0x00000008u
#define REFMEM_APP_IO_BISS_C                       0x00000010u
#define REFMEM_APP_IO_UART_RS485                   0x00000020u
#define REFMEM_APP_IO_PIO_SPI_SYNC                 0x00000040u
#define REFMEM_APP_IO_MODEL_TURNTABLE_PULSE        0x00000080u
#define REFMEM_APP_IO_MODEL_VNA_READY              0x00000100u
#define REFMEM_APP_IO_MODEL_VNA_TRIGGER            0x00000200u
#define REFMEM_APP_IO_LINK_SWITCH_EVENT            0x00000400u
#define REFMEM_APP_IO_MODEL_SIGNAL_MASK            (REFMEM_APP_IO_MODEL_TURNTABLE_PULSE | \
                                                    REFMEM_APP_IO_MODEL_VNA_READY | \
                                                    REFMEM_APP_IO_MODEL_VNA_TRIGGER | \
                                                    REFMEM_APP_IO_LINK_SWITCH_EVENT)

#define REFMEM_APP_IP_PULSE_CAPTURE                0x00000001u
#define REFMEM_APP_IP_PULSE_FIRE                   0x00000002u
#define REFMEM_APP_IP_LINK_SEQUENCE                0x00000004u
#define REFMEM_APP_IP_BISS_C_CODEC                 0x00000008u
#define REFMEM_APP_IP_RJ45_SYNC_DELTA              0x00000010u
#define REFMEM_APP_IP_VDC_DPLL                     0x00000020u
#define REFMEM_APP_IP_PIO_SPI_SYNC_DELTA           0x00000040u

typedef enum {
    REFMEM_APP_FAIL_STOP = 0u,
    REFMEM_APP_FAIL_HOLDOVER = 1u,
    REFMEM_APP_FAIL_DEGRADE = 2u,
    REFMEM_APP_FAIL_REPORT_ONLY = 3u,
} refmem_app_fail_policy_t;

typedef enum {
    REFMEM_APP_DOMAIN_SYSTEM = 0u,
    REFMEM_APP_DOMAIN_TRIG = 1u,
    REFMEM_APP_DOMAIN_CAL = 2u,
    REFMEM_APP_DOMAIN_SYNC = 3u,
    REFMEM_APP_DOMAIN_MEAS = 4u,
    REFMEM_APP_DOMAIN_REFMEM = 5u,
    REFMEM_APP_DOMAIN_DIAG = 6u,
    REFMEM_APP_DOMAIN_GATEWAY = 7u,
} refmem_app_domain_t;

typedef enum {
    REFMEM_APP_FB_SYSTEM_AO = 0u,
    REFMEM_APP_FB_REFMEM_SYNC = 1u,
    REFMEM_APP_FB_LOOP_ENGINE = 2u,
    REFMEM_APP_FB_TRIGGER_AO = 3u,
    REFMEM_APP_FB_CALIBRATION_AO = 4u,
    REFMEM_APP_FB_VDC_SYNC = 5u,
    REFMEM_APP_FB_DPLL = 6u,
    REFMEM_APP_FB_GATEWAY_AO = 7u,
    REFMEM_APP_FB_MODEL_VNA = 8u,
    REFMEM_APP_FB_MODEL_TURNTABLE = 9u,
    REFMEM_APP_FB_PULSE_COUNTER = 10u,
    REFMEM_APP_FB_INSTRUMENT_CONTROLLER = 11u,
    REFMEM_APP_FB_LINK_SWITCHER = 12u,
} refmem_app_fb_type_t;

typedef enum {
    REFMEM_APP_EVENT_START = 0u,
    REFMEM_APP_EVENT_STOP = 1u,
    REFMEM_APP_EVENT_ARM = 2u,
    REFMEM_APP_EVENT_FIRE_LOAD = 3u,
    REFMEM_APP_EVENT_DONE = 4u,
    REFMEM_APP_EVENT_FAULT = 5u,
    REFMEM_APP_EVENT_ACK = 6u,
    REFMEM_APP_EVENT_NACK = 7u,
    REFMEM_APP_EVENT_CONFIG_STAGE = 8u,
    REFMEM_APP_EVENT_CONFIG_ACTIVATE = 9u,
} refmem_app_event_t;

typedef enum {
    REFMEM_APP_TRANSPORT_LOCAL_QUEUE = 0u,
    REFMEM_APP_TRANSPORT_CORE_IPC = 1u,
    REFMEM_APP_TRANSPORT_COMMAND_SLOT = 2u,
    REFMEM_APP_TRANSPORT_RJ45_SYNC_RING = 3u,
    REFMEM_APP_TRANSPORT_PIO_SPI = 4u,
} refmem_app_transport_t;

typedef enum {
    REFMEM_APP_ACK_NONE = 0u,
    REFMEM_APP_ACK_ANY = 1u,
    REFMEM_APP_ACK_ALL_REQUIRED = 2u,
    REFMEM_APP_ACK_BITMAP = 3u,
} refmem_app_ack_policy_t;

typedef enum {
    REFMEM_APP_DATA_U32 = 0u,
    REFMEM_APP_DATA_I32 = 1u,
    REFMEM_APP_DATA_NS = 2u,
    REFMEM_APP_DATA_TICK = 3u,
    REFMEM_APP_DATA_ENUM = 4u,
    REFMEM_APP_DATA_BITMASK = 5u,
    REFMEM_APP_DATA_CRC = 6u,
} refmem_app_data_type_t;

typedef enum {
    REFMEM_APP_UNIT_NONE = 0u,
    REFMEM_APP_UNIT_NS = 1u,
    REFMEM_APP_UNIT_US = 2u,
    REFMEM_APP_UNIT_TICK = 3u,
    REFMEM_APP_UNIT_HZ = 4u,
    REFMEM_APP_UNIT_COUNT = 5u,
} refmem_app_unit_t;

typedef enum {
    REFMEM_APP_LIFE_STAGING = 0u,
    REFMEM_APP_LIFE_ACTIVE = 1u,
    REFMEM_APP_LIFE_RUN = 2u,
    REFMEM_APP_LIFE_TRANSIENT = 3u,
    REFMEM_APP_LIFE_EVIDENCE = 4u,
} refmem_app_lifecycle_t;

typedef enum {
    REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC = 0u,
    REFMEM_APP_SNAPSHOT_SEQLOCK = 1u,
    REFMEM_APP_SNAPSHOT_DOUBLE_BUFFER = 2u,
    REFMEM_APP_SNAPSHOT_EVIDENCE_REF = 3u,
} refmem_app_snapshot_policy_t;

typedef enum {
    REFMEM_APP_PERMISSION_READ_ONLY = 0u,
    REFMEM_APP_PERMISSION_COMMAND_WRITE = 1u,
    REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE = 2u,
} refmem_app_permission_t;

typedef enum {
    REFMEM_APP_GATE_LAYOUT = 0u,
    REFMEM_APP_GATE_NODE = 1u,
    REFMEM_APP_GATE_INSTANCE = 2u,
    REFMEM_APP_GATE_RESOURCE = 3u,
    REFMEM_APP_GATE_IO = 4u,
    REFMEM_APP_GATE_WRITER = 5u,
    REFMEM_APP_GATE_EVENT = 6u,
    REFMEM_APP_GATE_DATA = 7u,
    REFMEM_APP_GATE_CONFIG = 8u,
    REFMEM_APP_GATE_CAL_SYNC = 9u,
    REFMEM_APP_GATE_QUALITY = 10u,
} refmem_app_gate_check_t;

typedef enum {
    REFMEM_APP_GATE_PASS = 0u,
    REFMEM_APP_GATE_REJECT_RUN = 1u,
    REFMEM_APP_GATE_DEGRADE = 2u,
    REFMEM_APP_GATE_LATCH_FAULT = 3u,
} refmem_app_gate_action_t;

typedef enum {
    REFMEM_APP_QUALITY_NODE = 0u,
    REFMEM_APP_QUALITY_RJ45_LINK = 1u,
    REFMEM_APP_QUALITY_SLOT = 2u,
    REFMEM_APP_QUALITY_EVENT_LINK = 3u,
    REFMEM_APP_QUALITY_DATA_LINK = 4u,
    REFMEM_APP_QUALITY_TRANSPORT_ADAPTER = 5u,
} refmem_app_quality_scope_t;

typedef enum {
    REFMEM_APP_CLAIM_STRICT_UUID = 0u,
    REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT = 1u,
    REFMEM_APP_CLAIM_SPARE_DYNAMIC = 2u,
    REFMEM_APP_CLAIM_DISABLED = 3u,
} refmem_app_claim_policy_t;

typedef enum {
    REFMEM_APP_LINT_OK = 0u,
    REFMEM_APP_LINT_BAD_TABLE_VERSION = 1u,
    REFMEM_APP_LINT_BAD_NODE_RANGE = 2u,
    REFMEM_APP_LINT_BAD_INSTANCE_RANGE = 3u,
    REFMEM_APP_LINT_BAD_INSTANCE_REF = 4u,
    REFMEM_APP_LINT_RESOURCE_CONFLICT = 5u,
    REFMEM_APP_LINT_IO_CONFLICT = 6u,
    REFMEM_APP_LINT_DUPLICATE_WRITER = 7u,
    REFMEM_APP_LINT_BAD_EVENT_LINK = 8u,
    REFMEM_APP_LINT_BAD_DATA_LINK = 9u,
    REFMEM_APP_LINT_BAD_GATE_OR_QUALITY = 10u,
    REFMEM_APP_LINT_BAD_SLOT_CLAIM = 11u,
    REFMEM_APP_LINT_BAD_BOARD_CAPABILITY = 12u,
    REFMEM_APP_LINT_BAD_REALTIME_CONTRACT = 13u,
} refmem_app_lint_error_t;

typedef enum {
    REFMEM_APP_TABLE_APPLICATION_MAP = 0u,
    REFMEM_APP_TABLE_BOARD_CAPABILITY = 1u,
    REFMEM_APP_TABLE_GENERIC_NODE = 2u,
    REFMEM_APP_TABLE_NODE_LOAD = 3u,
    REFMEM_APP_TABLE_FB_INSTANCE = 4u,
    REFMEM_APP_TABLE_EVENT_LINK = 5u,
    REFMEM_APP_TABLE_DATA_LINK = 6u,
    REFMEM_APP_TABLE_DEPLOYMENT_GATE = 7u,
    REFMEM_APP_TABLE_CONNECTION_QUALITY = 8u,
} refmem_app_table_id_t;

#define REFMEM_APP_TABLE_MASK_ALL ((1u << REFMEM_APP_TABLE_APPLICATION_MAP) | \
                                   (1u << REFMEM_APP_TABLE_BOARD_CAPABILITY) | \
                                   (1u << REFMEM_APP_TABLE_GENERIC_NODE) | \
                                   (1u << REFMEM_APP_TABLE_NODE_LOAD) | \
                                   (1u << REFMEM_APP_TABLE_FB_INSTANCE) | \
                                   (1u << REFMEM_APP_TABLE_EVENT_LINK) | \
                                   (1u << REFMEM_APP_TABLE_DATA_LINK) | \
                                   (1u << REFMEM_APP_TABLE_DEPLOYMENT_GATE) | \
                                   (1u << REFMEM_APP_TABLE_CONNECTION_QUALITY))

typedef enum {
    REFMEM_APP_LOAD_SOURCE_DEFAULT = 0u,
    REFMEM_APP_LOAD_SOURCE_SD_SYSTEM_PACK = 1u,
    REFMEM_APP_LOAD_SOURCE_SCPI_INLINE = 2u,
} refmem_app_load_source_t;

typedef enum {
    REFMEM_APP_MODEL_MODE_IDLE = 0u,
    REFMEM_APP_MODEL_MODE_LOAD_TO_STAGING = 1u,
    REFMEM_APP_MODEL_MODE_VALIDATING = 2u,
    REFMEM_APP_MODEL_MODE_ACTIVATING = 3u,
    REFMEM_APP_MODEL_MODE_FAULT = 4u,
} refmem_app_model_mode_t;

typedef enum {
    REFMEM_APP_STAGING_EMPTY = 0u,
    REFMEM_APP_STAGING_STAGED = 1u,
    REFMEM_APP_STAGING_VALIDATED = 2u,
    REFMEM_APP_STAGING_FAILED = 3u,
} refmem_app_staging_state_t;

typedef enum {
    REFMEM_APP_LOAD_OK = 0u,
    REFMEM_APP_LOAD_ERR_BAD_ARGUMENT = 1u,
    REFMEM_APP_LOAD_ERR_MANIFEST_NOT_OK = 2u,
    REFMEM_APP_LOAD_ERR_LINT_FAILED = 3u,
    REFMEM_APP_LOAD_ERR_NODE_RANGE = 4u,
    REFMEM_APP_LOAD_ERR_INSTANCE_RANGE = 5u,
    REFMEM_APP_LOAD_ERR_PACKAGE_READ = 6u,
    REFMEM_APP_LOAD_ERR_PACKAGE_INVALID = 7u,
} refmem_app_load_error_t;

typedef struct {
    uint32_t node_id;
    uint32_t node_uuid_crc32;
    uint32_t capability_mask;
    uint32_t claim_policy;
    uint32_t claim_priority;
    uint32_t default_persona_mask;
    uint32_t hw_profile_crc32;
    uint32_t online_required;
    uint32_t fail_policy;
} refmem_app_node_entry_t;

typedef struct {
    uint32_t board_id;
    uint32_t board_uuid_crc32;
    uint32_t capability_mask;
    uint32_t io_constraint_mask;
    uint32_t ip_core_mask;
    uint32_t default_persona_mask;
    uint32_t hw_profile_crc32;
    uint32_t active_default_slot;
    uint32_t online_required;
} refmem_board_capability_entry_t;

typedef struct {
    uint32_t version;
    uint32_t board_count;
    refmem_board_capability_entry_t board[REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT];
} refmem_board_capability_table_t;

typedef struct {
    uint32_t version;
    uint32_t application_id;
    uint32_t application_version;
    uint32_t profile_id;
    uint32_t layout_version;
    uint32_t target_node_mask;
} refmem_application_map_t;

typedef struct {
    uint32_t version;
    uint32_t node_count;
    refmem_app_node_entry_t node[REFMEM_APP_MODEL_NODE_COUNT];
} refmem_generic_node_table_t;

typedef struct {
    uint32_t load_id;
    uint32_t application_id;
    uint32_t profile_id;
    uint32_t node_id;
    uint32_t instance_id;
    uint32_t role_mask;
    uint32_t persona_mask;
    uint32_t enabled;
    uint32_t required;
    uint32_t fail_policy;
    uint32_t load_order;
} refmem_node_load_entry_t;

typedef struct {
    uint32_t version;
    uint32_t load_count;
    refmem_node_load_entry_t load[REFMEM_APP_MODEL_NODE_LOAD_COUNT];
} refmem_node_load_table_t;

typedef struct {
    uint32_t instance_id;
    uint32_t default_node_id;
    uint32_t domain;
    uint32_t ao_type;
    uint32_t fb_type;
    const char *instance_name;
    uint32_t version;
    uint32_t enable_condition;
    uint32_t resource_claim;
    uint32_t io_claim;
    uint32_t ip_core_claim;
    uint32_t time_budget_us;
    uint32_t state_slot_ref;
    uint32_t health_slot_ref;
    uint32_t event_first;
    uint32_t event_count;
    uint32_t data_first;
    uint32_t data_count;
    uint32_t conflict_class;
    uint32_t restart_policy;
} refmem_fb_instance_entry_t;

typedef struct {
    uint32_t version;
    uint32_t instance_count;
    refmem_fb_instance_entry_t instance[REFMEM_APP_MODEL_INSTANCE_COUNT];
} refmem_fb_instance_table_t;

typedef struct {
    uint32_t event_link_id;
    uint32_t source_instance;
    uint32_t source_event;
    uint32_t target_node_mask;
    uint32_t target_instance;
    uint32_t target_event;
    uint32_t transport;
    uint32_t timeout_us;
    uint32_t ack_policy;
    uint32_t retry_policy;
    uint32_t safety_class;
    uint32_t evidence_ref;
} refmem_event_link_entry_t;

typedef struct {
    uint32_t version;
    uint32_t event_link_count;
    refmem_event_link_entry_t link[REFMEM_APP_MODEL_EVENT_LINK_COUNT];
} refmem_event_link_table_t;

typedef struct {
    uint32_t data_link_id;
    const char *slot_path;
    uint32_t writer_instance;
    uint32_t reader_mask;
    uint32_t type;
    uint32_t unit;
    int32_t scale;
    int32_t min_value;
    int32_t max_value;
    uint32_t lifecycle;
    uint32_t snapshot_policy;
    uint32_t update_period_us;
    uint32_t stale_window_us;
    uint32_t crc_scope;
    uint32_t permission;
} refmem_data_link_entry_t;

typedef struct {
    uint32_t version;
    uint32_t data_link_count;
    refmem_data_link_entry_t link[REFMEM_APP_MODEL_DATA_LINK_COUNT];
} refmem_data_link_table_t;

typedef struct {
    uint32_t check_id;
    uint32_t required;
    uint32_t fail_action;
    uint32_t last_state;
    uint32_t reject_code;
    uint32_t reject_instance;
    uint32_t reject_node;
    uint32_t reject_slot;
    uint32_t reject_evidence_index;
} refmem_deployment_gate_entry_t;

typedef struct {
    uint32_t version;
    uint32_t check_count;
    refmem_deployment_gate_entry_t check[REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT];
} refmem_deployment_gate_table_t;

typedef struct {
    uint32_t quality_id;
    uint32_t scope;
    uint32_t source_node;
    uint32_t target_node;
    uint32_t seq_expected;
    uint32_t seq_last;
    uint32_t crc_error_count;
    uint32_t stale_count;
    uint32_t late_count;
    uint32_t drop_count;
    uint32_t timeout_count;
    uint32_t last_error;
    uint32_t last_error_tick;
    uint32_t p99;
    uint32_t p999;
    uint32_t evidence_index;
} refmem_connection_quality_entry_t;

typedef struct {
    uint32_t version;
    uint32_t quality_count;
    refmem_connection_quality_entry_t quality[REFMEM_APP_MODEL_QUALITY_COUNT];
} refmem_connection_quality_table_t;

typedef struct {
    uint32_t version;
    uint32_t valid;
    uint32_t target_node_mask;
    uint32_t table_mask;
    uint32_t application_map_crc32;
    uint32_t board_capability_crc32;
    uint32_t generic_node_crc32;
    uint32_t node_load_crc32;
    uint32_t fb_instance_crc32;
    uint32_t event_link_crc32;
    uint32_t data_link_crc32;
    uint32_t deployment_gate_crc32;
    uint32_t connection_quality_crc32;
    uint32_t package_crc32;
    uint32_t lint_error_count;
    uint32_t first_lint_error;
} refmem_application_model_snapshot_t;

typedef struct {
    uint32_t version;
    uint32_t load_seq;
    uint32_t source;
    uint32_t mode;
    uint32_t staging_state;
    uint32_t manifest_status;
    uint32_t manifest_schema;
    uint32_t manifest_required_count;
    uint32_t manifest_missing_count;
    uint32_t path_hash;
    uint32_t active_package_crc32;
    uint32_t staging_package_crc32;
    uint32_t staging_lint_error_count;
    uint32_t staging_first_lint_error;
    uint32_t staging_node_id;
    uint32_t staging_instance_id;
    uint32_t staging_role_mask;
    uint32_t staging_persona_mask;
    uint32_t staging_enabled;
    uint32_t staging_required;
    uint32_t staging_load_order;
    uint32_t last_error;
    char manifest_build_id[32];
    char path[96];
} refmem_application_model_load_snapshot_t;

typedef struct {
    uint32_t version;
    uint32_t load_seq;
    uint32_t mode;
    uint32_t staging_state;
    uint32_t active_crc32;
    uint32_t staging_crc32;
    uint32_t staging_lint_error_count;
    uint32_t staging_first_lint_error;
    uint32_t staging_board_id;
    uint32_t staging_board_uuid_crc32;
    uint32_t staging_capability_mask;
    uint32_t staging_io_constraint_mask;
    uint32_t staging_ip_core_mask;
    uint32_t staging_default_persona_mask;
    uint32_t staging_hw_profile_crc32;
    uint32_t staging_active_default_slot;
    uint32_t staging_online_required;
    uint32_t last_error;
} refmem_board_capability_load_snapshot_t;

bool refmem_application_model_init(void);
bool refmem_application_model_validate(void);
bool refmem_application_model_stage_sd_system_pack(const char *path,
                                                   uint32_t path_hash,
                                                   uint32_t manifest_status,
                                                   uint32_t manifest_schema,
                                                   uint32_t manifest_required_count,
                                                   uint32_t manifest_missing_count,
                                                   const char *manifest_build_id,
                                                   uint32_t package_crc32,
                                                   uint32_t package_valid,
                                                   uint32_t package_error);
bool refmem_application_model_stage_scpi_node_config(uint32_t node_id,
                                                     uint32_t instance_id,
                                                     uint32_t role_mask,
                                                     uint32_t persona_mask,
                                                     uint32_t enabled,
                                                     uint32_t required,
                                                     uint32_t load_order);
bool refmem_application_model_stage_scpi_board_capability(uint32_t board_id,
                                                          uint32_t board_uuid_crc32,
                                                          uint32_t capability_mask,
                                                          uint32_t io_constraint_mask,
                                                          uint32_t ip_core_mask,
                                                          uint32_t default_persona_mask,
                                                          uint32_t hw_profile_crc32,
                                                          uint32_t active_default_slot,
                                                          uint32_t online_required);
const refmem_application_map_t *refmem_application_model_get_application_map(void);
const refmem_board_capability_table_t *refmem_application_model_get_board_capability_table(void);
const refmem_generic_node_table_t *refmem_application_model_get_generic_node_table(void);
const refmem_node_load_table_t *refmem_application_model_get_node_load_table(void);
const refmem_fb_instance_table_t *refmem_application_model_get_fb_instance_table(void);
const refmem_event_link_table_t *refmem_application_model_get_event_link_table(void);
const refmem_data_link_table_t *refmem_application_model_get_data_link_table(void);
const refmem_deployment_gate_table_t *refmem_application_model_get_deployment_gate(void);
const refmem_connection_quality_table_t *refmem_application_model_get_connection_quality(void);
const refmem_application_model_snapshot_t *refmem_application_model_get_snapshot(void);
void refmem_application_model_get_load_snapshot(refmem_application_model_load_snapshot_t *snapshot);
void refmem_application_model_get_board_load_snapshot(refmem_board_capability_load_snapshot_t *snapshot);

#endif
