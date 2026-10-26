#ifndef REFMEM_APPLICATION_MODEL_INTERNAL_H
#define REFMEM_APPLICATION_MODEL_INTERNAL_H

#include "refmem_application_model.h"

#include <stddef.h>
#include <string.h>

#include "ota_crc32.h"
#include "refmem_application_contract.h"
#include "refmem_realtime_contract.h"
#include "refmem_slot_claim.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"

#define REFMEM_APP_TABLE_WIRE_U32_SIZE 4u
#define REFMEM_APP_TABLE_WIRE_HEADER_WORDS 2u
#define REFMEM_APP_TABLE_WIRE_FB_INSTANCE_WORDS 20u
#define REFMEM_APP_TABLE_WIRE_EVENT_LINK_WORDS 12u
#define REFMEM_APP_TABLE_WIRE_DATA_LINK_WORDS 15u
#define REFMEM_APP_TABLE_WIRE_DEPLOYMENT_GATE_WORDS 9u
#define REFMEM_APP_TABLE_WIRE_CONNECTION_QUALITY_WORDS 16u
#define REFMEM_APP_TABLE_WIRE_SIZE(row_count, row_words) \
    ((REFMEM_APP_TABLE_WIRE_HEADER_WORDS + ((row_count) * (row_words))) * \
     REFMEM_APP_TABLE_WIRE_U32_SIZE)

#define REFMEM_APP_INLINE_PACKAGE_DIR_SIZE \
    (REFMEM_TABLE_REGISTRY_COUNT * REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE)
#define REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET \
    (REFMEM_TABLE_PACKAGE_HEADER_SIZE + REFMEM_APP_INLINE_PACKAGE_DIR_SIZE)

#define REFMEM_APP_INSTANCE_B0_REFMEM_SYNC      0u
#define REFMEM_APP_INSTANCE_B0_LOOP_ENGINE      1u
#define REFMEM_APP_INSTANCE_B0_PULSE_COUNTER    2u
#define REFMEM_APP_INSTANCE_B0_TRIGGER_MASTER   3u
#define REFMEM_APP_INSTANCE_B1_TRIGGER          4u
#define REFMEM_APP_INSTANCE_B2_LINK_SWITCH      5u
#define REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY   6u
#define REFMEM_APP_INSTANCE_B3_INSTRUMENT       7u
#define REFMEM_APP_INSTANCE_B3_CALIBRATION      8u
#define REFMEM_APP_INSTANCE_B4_MODEL_VNA        9u
#define REFMEM_APP_INSTANCE_B4_MODEL_TT         10u
#define REFMEM_APP_INSTANCE_TDMA_FOUNDATION     11u

typedef struct {
    refmem_application_map_t application_map;
    refmem_board_capability_table_t board_capability;
    refmem_generic_node_table_t generic_node;
    refmem_node_load_table_t node_load;
    refmem_fb_instance_table_t fb_instance;
    refmem_event_link_table_t event_link;
    refmem_data_link_table_t data_link;
    refmem_deployment_gate_table_t deployment_gate;
    refmem_connection_quality_table_t connection_quality;
    tdma_foundation_profile_t tdma_foundation_profile;
    uint32_t table_crc32[REFMEM_TABLE_REGISTRY_COUNT];
    uint32_t package_crc32;
    uint32_t table_seq;
} refmem_model_parsed_tables_t;


/* N5.0.4h: single definitions live in refmem_application_model.c. */
extern const refmem_application_map_t s_application_map;
extern const refmem_board_capability_table_t s_board_capability_table;
extern const refmem_generic_node_table_t s_generic_node_table;
extern const refmem_node_load_table_t s_node_load_table;
extern const refmem_fb_instance_table_t s_fb_instance_table;
extern const refmem_event_link_table_t s_event_link_table;
extern const refmem_data_link_table_t s_data_link_table;
extern const refmem_deployment_gate_table_t s_deployment_gate;
extern const refmem_connection_quality_table_t s_connection_quality;
extern tdma_foundation_profile_t s_tdma_foundation_profile;
extern tdma_foundation_profile_t s_design_tdma_foundation_profile;
extern refmem_application_model_snapshot_t s_snapshot;
extern refmem_application_model_load_snapshot_t s_load_snapshot;
extern refmem_board_capability_load_snapshot_t s_board_load_snapshot;
extern refmem_node_load_table_t s_staging_node_load_table;
extern refmem_board_capability_table_t s_staging_board_capability_table;
extern refmem_application_map_t s_active_application_map;
extern refmem_board_capability_table_t s_active_board_capability_table;
extern refmem_generic_node_table_t s_active_generic_node_table;
extern refmem_node_load_table_t s_active_node_load_table;
extern refmem_fb_instance_table_t s_active_fb_instance_table;
extern refmem_event_link_table_t s_active_event_link_table;
extern refmem_data_link_table_t s_active_data_link_table;
extern refmem_deployment_gate_table_t s_active_deployment_gate;
extern uint32_t s_origin_model_epoch;
extern refmem_connection_quality_table_t s_active_connection_quality;
extern tdma_foundation_profile_t s_active_tdma_foundation_profile;
extern bool s_staging_node_load_valid;
extern bool s_staging_board_capability_valid;
extern bool s_active_tables_from_image;
extern bool s_initialized;
extern refmem_model_parsed_tables_t s_pending_tables;
extern bool s_pending_tables_valid;

uint32_t refmem_model_rmtp_crc32(const void *data, size_t size);
uint32_t refmem_model_rmtp_crc32_string(const char *text);
uint32_t refmem_model_rmtp_crc32_zero_field(const uint8_t *data, size_t size, size_t zero_offset);
bool refmem_model_write_u32_le(uint8_t *data, size_t size, size_t *cursor, uint32_t value);
bool refmem_model_write_i32_le(uint8_t *data, size_t size, size_t *cursor, int32_t value);
uint32_t refmem_model_crc32_fields(const uint32_t *fields, size_t count);
bool refmem_model_parse_application_map_view(const uint8_t *data, size_t size, void *table);
bool refmem_model_parse_board_capability_view(const uint8_t *data, size_t size, void *table);
bool refmem_model_parse_generic_node_view(const uint8_t *data, size_t size, void *table);
bool refmem_model_parse_node_load_view(const uint8_t *data, size_t size, void *table);
bool refmem_model_parse_fb_instance_view(const uint8_t *data, size_t size, void *table);
bool refmem_model_parse_event_link_view(const uint8_t *data, size_t size, void *table);
bool refmem_model_parse_data_link_view(const uint8_t *data, size_t size, void *table);
bool refmem_model_parse_deployment_gate_view(const uint8_t *data, size_t size, void *table);
bool refmem_model_parse_connection_quality_view(const uint8_t *data, size_t size, void *table);
bool refmem_model_parse_tdma_foundation_profile_view(const uint8_t *data, size_t size, void *table);
uint32_t refmem_model_application_map_crc32(void);
uint32_t refmem_model_board_capability_crc32(void);
uint32_t refmem_model_generic_node_crc32(void);
uint32_t refmem_model_node_load_table_crc32(const refmem_node_load_table_t *table);
uint32_t refmem_model_node_load_crc32(void);
uint32_t refmem_model_fb_instance_crc32(void);
uint32_t refmem_model_event_link_crc32(void);
uint32_t refmem_model_data_link_crc32(void);
uint32_t refmem_model_deployment_gate_crc32(void);
uint32_t refmem_model_connection_quality_crc32(void);
uint32_t refmem_model_tdma_foundation_profile_table_crc32(void);
const refmem_application_map_t *refmem_model_current_application_map(void);
const refmem_board_capability_table_t *refmem_model_current_board_capability_table(void);
const refmem_generic_node_table_t *refmem_model_current_generic_node_table(void);
const refmem_node_load_table_t *refmem_model_current_node_load_table(void);
const refmem_fb_instance_table_t *refmem_model_current_fb_instance_table(void);
const refmem_event_link_table_t *refmem_model_current_event_link_table(void);
const refmem_data_link_table_t *refmem_model_current_data_link_table(void);
const refmem_deployment_gate_table_t *refmem_model_current_deployment_gate_table(void);
const refmem_connection_quality_table_t *refmem_model_current_connection_quality_table(void);
const tdma_foundation_profile_t *refmem_model_current_tdma_foundation_profile(void);
const tdma_foundation_profile_t *refmem_model_design_tdma_foundation_profile(void);
bool refmem_model_instance_exists(uint32_t instance_id);
bool refmem_model_stage_inline_package_image(void);
bool refmem_model_validate_node_capabilities(void);
bool refmem_model_validate_slot_claim_policy(void);
bool refmem_model_validate_application_map(void);
bool refmem_model_validate_board_capability_table(void);
bool refmem_model_validate_generic_node_table(void);
bool refmem_model_validate_node_load_table(void);
bool refmem_model_validate_instances(void);
bool refmem_model_validate_event_links(void);
bool refmem_model_validate_data_links(void);
bool refmem_model_validate_gate_and_quality(void);
void refmem_model_lint_note(bool ok, uint32_t error, uint32_t *error_count, uint32_t *first_error);
bool refmem_model_instance_enabled(const refmem_fb_instance_entry_t *instance);
bool refmem_model_validate_resource_claims(void);
bool refmem_model_validate_tdma_profile_contract( const tdma_foundation_profile_t *profile, const refmem_generic_node_table_t *nodes, const refmem_board_capability_table_t *boards, const refmem_node_load_table_t *loads, const refmem_fb_instance_table_t *instances);
bool refmem_model_validate_tdma_owner(void);
bool refmem_model_validate_io_claims(void);
bool refmem_model_validate_unique_writers(void);
bool refmem_model_validate_required_event_links(void);
void refmem_model_copy_text(char *dst, size_t dst_size, const char *src);
void refmem_model_finish_load_idle(void);

#endif /* REFMEM_APPLICATION_MODEL_INTERNAL_H */

