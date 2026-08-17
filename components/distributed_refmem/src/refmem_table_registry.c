#include "refmem_table_registry.h"

#include <stddef.h>
#include <string.h>

#include "refmem_application_contract.h"
#include "refmem_vector_table.h"

static refmem_table_registry_entry_t s_registry[REFMEM_TABLE_REGISTRY_COUNT];
static refmem_table_registry_snapshot_t s_snapshot;
static refmem_table_image_descriptor_t s_active_image;
static refmem_table_image_descriptor_t s_staging_image;
static refmem_table_image_descriptor_t s_rollbackable_image;
static uint8_t s_active_image_buffer[REFMEM_TABLE_IMAGE_BUFFER_SIZE];
static uint8_t s_staging_image_buffer[REFMEM_TABLE_IMAGE_BUFFER_SIZE];
static uint8_t s_rollbackable_image_buffer[REFMEM_TABLE_IMAGE_BUFFER_SIZE];
static size_t s_active_image_size;
static size_t s_staging_image_size;
static size_t s_rollbackable_image_size;
static uint32_t s_image_access_count[3];
static uint32_t s_table_seq;

#define REFMEM_TABLE_WIRE_U32_SIZE 4u
#define REFMEM_TABLE_WIRE_HEADER_WORDS 2u
#define REFMEM_TABLE_WIRE_FB_INSTANCE_WORDS 20u
#define REFMEM_TABLE_WIRE_EVENT_LINK_WORDS 12u
#define REFMEM_TABLE_WIRE_DATA_LINK_WORDS 15u
#define REFMEM_TABLE_WIRE_DEPLOYMENT_GATE_WORDS 9u
#define REFMEM_TABLE_WIRE_CONNECTION_QUALITY_WORDS 16u

#define REFMEM_TABLE_WIRE_SIZE(row_count, row_words) \
    ((REFMEM_TABLE_WIRE_HEADER_WORDS + ((row_count) * (row_words))) * REFMEM_TABLE_WIRE_U32_SIZE)

static const uint32_t s_table_image_size[REFMEM_TABLE_REGISTRY_COUNT] = {
    sizeof(refmem_application_map_t),
    sizeof(refmem_board_capability_table_t),
    sizeof(refmem_generic_node_table_t),
    sizeof(refmem_node_load_table_t),
    REFMEM_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_INSTANCE_COUNT,
                           REFMEM_TABLE_WIRE_FB_INSTANCE_WORDS),
    REFMEM_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_EVENT_LINK_COUNT,
                           REFMEM_TABLE_WIRE_EVENT_LINK_WORDS),
    REFMEM_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_DATA_LINK_COUNT,
                           REFMEM_TABLE_WIRE_DATA_LINK_WORDS),
    REFMEM_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT,
                           REFMEM_TABLE_WIRE_DEPLOYMENT_GATE_WORDS),
    REFMEM_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_QUALITY_COUNT,
                           REFMEM_TABLE_WIRE_CONNECTION_QUALITY_WORDS),
};

typedef struct {
    uint32_t instance_id;
    uint32_t default_node_id;
    uint32_t domain;
    uint32_t ao_type;
    uint32_t fb_type;
    uint32_t instance_name_hash;
    uint32_t version;
    uint32_t enable_condition;
    uint32_t resource_claim;
    uint32_t io_claim;
    uint32_t ip_core_claim;
    uint32_t time_budget_us;
    uint32_t state_region_ref;
    uint32_t health_region_ref;
    uint32_t event_first;
    uint32_t event_count;
    uint32_t data_first;
    uint32_t data_count;
    uint32_t conflict_class;
    uint32_t restart_policy;
} refmem_fb_instance_wire_entry_t;

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
    uint32_t evidence_region_ref;
} refmem_event_link_wire_entry_t;

typedef struct {
    uint32_t data_link_id;
    uint32_t region_path_hash;
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
    uint32_t crc_region_ref;
    uint32_t permission;
} refmem_data_link_wire_entry_t;

typedef struct {
    uint32_t check_id;
    uint32_t required;
    uint32_t fail_action;
    uint32_t last_state;
    uint32_t reject_code;
    uint32_t reject_instance;
    uint32_t reject_node;
    uint32_t reject_region_ref;
    uint32_t reject_evidence_index;
} refmem_deployment_gate_wire_entry_t;

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
} refmem_connection_quality_wire_entry_t;

static bool refmem_table_registry_validate_fb_instance(const uint8_t *data, size_t size);
static bool refmem_table_registry_validate_event_link(
    const uint8_t *data,
    size_t size,
    const refmem_application_map_t *application_map);
static bool refmem_table_registry_validate_data_link(const uint8_t *data, size_t size);
static bool refmem_table_registry_validate_deployment_gate(const uint8_t *data, size_t size);
static bool refmem_table_registry_validate_connection_quality(const uint8_t *data, size_t size);

static bool refmem_table_registry_find_package_table(const uint8_t *data,
                                                     size_t size,
                                                     uint32_t table_id,
                                                     uint32_t *table_offset,
                                                     uint32_t *table_size,
                                                     uint32_t *table_crc32);

static uint32_t refmem_table_registry_crc32_update(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0u; i < size; i++) {
        crc ^= bytes[i];
        crc *= 16777619u;
    }
    return crc;
}

static uint32_t refmem_table_read_u32_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int32_t refmem_table_read_i32_le(const uint8_t *data)
{
    return (int32_t)refmem_table_read_u32_le(data);
}

static bool refmem_table_registry_validate_wire_header(const uint8_t *data,
                                                       size_t size,
                                                       uint32_t expected_count,
                                                       uint32_t row_words,
                                                       uint32_t *count)
{
    if (data == NULL ||
        count == NULL ||
        size != REFMEM_TABLE_WIRE_SIZE(expected_count, row_words)) {
        return false;
    }

    const uint32_t version = refmem_table_read_u32_le(&data[0]);
    const uint32_t parsed_count = refmem_table_read_u32_le(&data[4]);
    if (version != REFMEM_APP_MODEL_VERSION ||
        parsed_count != expected_count) {
        return false;
    }

    *count = parsed_count;
    return true;
}

static bool refmem_table_registry_parse_board_capability(
    const uint8_t *data,
    size_t size,
    refmem_board_capability_table_t *table)
{
    if (data == NULL ||
        table == NULL ||
        size != sizeof(refmem_board_capability_table_t)) {
        return false;
    }

    memset(table, 0, sizeof(*table));
    table->version = refmem_table_read_u32_le(&data[0]);
    table->board_count = refmem_table_read_u32_le(&data[4]);
    size_t cursor = 8u;
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT; i++) {
        refmem_board_capability_entry_t *board = &table->board[i];
        board->board_id = refmem_table_read_u32_le(&data[cursor + 0u]);
        board->board_uuid_crc32 = refmem_table_read_u32_le(&data[cursor + 4u]);
        board->capability_mask = refmem_table_read_u32_le(&data[cursor + 8u]);
        board->io_constraint_mask = refmem_table_read_u32_le(&data[cursor + 12u]);
        board->ip_core_mask = refmem_table_read_u32_le(&data[cursor + 16u]);
        board->default_persona_mask = refmem_table_read_u32_le(&data[cursor + 20u]);
        board->hw_profile_crc32 = refmem_table_read_u32_le(&data[cursor + 24u]);
        board->active_default_slot = refmem_table_read_u32_le(&data[cursor + 28u]);
        board->online_required = refmem_table_read_u32_le(&data[cursor + 32u]);
        cursor += 9u * sizeof(uint32_t);
    }
    return true;
}

static bool refmem_table_registry_parse_application_map(
    const uint8_t *data,
    size_t size,
    refmem_application_map_t *table)
{
    if (data == NULL ||
        table == NULL ||
        size != sizeof(refmem_application_map_t)) {
        return false;
    }

    memset(table, 0, sizeof(*table));
    table->version = refmem_table_read_u32_le(&data[0]);
    table->application_id = refmem_table_read_u32_le(&data[4]);
    table->application_version = refmem_table_read_u32_le(&data[8]);
    table->profile_id = refmem_table_read_u32_le(&data[12]);
    table->layout_version = refmem_table_read_u32_le(&data[16]);
    table->target_node_mask = refmem_table_read_u32_le(&data[20]);
    return true;
}

static bool refmem_table_registry_parse_generic_node(
    const uint8_t *data,
    size_t size,
    refmem_generic_node_table_t *table)
{
    if (data == NULL ||
        table == NULL ||
        size != sizeof(refmem_generic_node_table_t)) {
        return false;
    }

    memset(table, 0, sizeof(*table));
    table->version = refmem_table_read_u32_le(&data[0]);
    table->node_count = refmem_table_read_u32_le(&data[4]);
    size_t cursor = 8u;
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        refmem_app_node_entry_t *node = &table->node[i];
        node->node_id = refmem_table_read_u32_le(&data[cursor + 0u]);
        node->node_uuid_crc32 = refmem_table_read_u32_le(&data[cursor + 4u]);
        node->capability_mask = refmem_table_read_u32_le(&data[cursor + 8u]);
        node->claim_policy = refmem_table_read_u32_le(&data[cursor + 12u]);
        node->claim_priority = refmem_table_read_u32_le(&data[cursor + 16u]);
        node->default_persona_mask = refmem_table_read_u32_le(&data[cursor + 20u]);
        node->hw_profile_crc32 = refmem_table_read_u32_le(&data[cursor + 24u]);
        node->online_required = refmem_table_read_u32_le(&data[cursor + 28u]);
        node->fail_policy = refmem_table_read_u32_le(&data[cursor + 32u]);
        cursor += 9u * sizeof(uint32_t);
    }
    return true;
}

static bool refmem_table_registry_parse_node_load(
    const uint8_t *data,
    size_t size,
    refmem_node_load_table_t *table)
{
    if (data == NULL ||
        table == NULL ||
        size != sizeof(refmem_node_load_table_t)) {
        return false;
    }

    memset(table, 0, sizeof(*table));
    table->version = refmem_table_read_u32_le(&data[0]);
    table->load_count = refmem_table_read_u32_le(&data[4]);
    size_t cursor = 8u;
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_NODE_LOAD_COUNT; i++) {
        refmem_node_load_entry_t *load = &table->load[i];
        load->load_id = refmem_table_read_u32_le(&data[cursor + 0u]);
        load->application_id = refmem_table_read_u32_le(&data[cursor + 4u]);
        load->profile_id = refmem_table_read_u32_le(&data[cursor + 8u]);
        load->node_id = refmem_table_read_u32_le(&data[cursor + 12u]);
        load->instance_id = refmem_table_read_u32_le(&data[cursor + 16u]);
        load->role_mask = refmem_table_read_u32_le(&data[cursor + 20u]);
        load->persona_mask = refmem_table_read_u32_le(&data[cursor + 24u]);
        load->enabled = refmem_table_read_u32_le(&data[cursor + 28u]);
        load->required = refmem_table_read_u32_le(&data[cursor + 32u]);
        load->fail_policy = refmem_table_read_u32_le(&data[cursor + 36u]);
        load->load_order = refmem_table_read_u32_le(&data[cursor + 40u]);
        cursor += 11u * sizeof(uint32_t);
    }
    return true;
}

static bool refmem_table_registry_validate_package_owner_contract(
    const uint8_t *data,
    const uint32_t *table_offset,
    const uint32_t *table_size,
    uint32_t table_count,
    uint32_t *first_bad_table)
{
    if (data == NULL ||
        table_offset == NULL ||
        table_size == NULL ||
        table_count != REFMEM_TABLE_REGISTRY_COUNT) {
        if (first_bad_table != NULL) {
            *first_bad_table = 0u;
        }
        return false;
    }

    refmem_application_map_t application_map;
    refmem_board_capability_table_t board_table;
    refmem_generic_node_table_t node_table;
    refmem_node_load_table_t node_load_table;
    const uint32_t app_id = REFMEM_APP_TABLE_APPLICATION_MAP;
    const uint32_t board_id = REFMEM_APP_TABLE_BOARD_CAPABILITY;
    const uint32_t node_id = REFMEM_APP_TABLE_GENERIC_NODE;
    const uint32_t node_load_id = REFMEM_APP_TABLE_NODE_LOAD;
    const uint32_t fb_id = REFMEM_APP_TABLE_FB_INSTANCE;
    const uint32_t event_id = REFMEM_APP_TABLE_EVENT_LINK;
    const uint32_t data_id = REFMEM_APP_TABLE_DATA_LINK;
    const uint32_t gate_id = REFMEM_APP_TABLE_DEPLOYMENT_GATE;
    const uint32_t quality_id = REFMEM_APP_TABLE_CONNECTION_QUALITY;

    if (!refmem_table_registry_parse_application_map(data + table_offset[app_id],
                                                     table_size[app_id],
                                                     &application_map) ||
        !refmem_application_contract_validate_application_map(&application_map)) {
        if (first_bad_table != NULL) {
            *first_bad_table = app_id;
        }
        return false;
    }

    if (!refmem_table_registry_parse_board_capability(data + table_offset[board_id],
                                                      table_size[board_id],
                                                      &board_table)) {
        if (first_bad_table != NULL) {
            *first_bad_table = board_id;
        }
        return false;
    }

    if (!refmem_table_registry_parse_generic_node(data + table_offset[node_id],
                                                  table_size[node_id],
                                                  &node_table)) {
        if (first_bad_table != NULL) {
            *first_bad_table = node_id;
        }
        return false;
    }

    if (!refmem_application_contract_validate_slot_substrate(&node_table, &board_table)) {
        if (first_bad_table != NULL) {
            *first_bad_table = board_id;
        }
        return false;
    }

    if (!refmem_table_registry_parse_node_load(data + table_offset[node_load_id],
                                               table_size[node_load_id],
                                               &node_load_table) ||
        !refmem_application_contract_validate_node_load_table(&node_load_table,
                                                              &application_map)) {
        if (first_bad_table != NULL) {
            *first_bad_table = node_load_id;
        }
        return false;
    }

    if (!refmem_table_registry_validate_fb_instance(data + table_offset[fb_id],
                                                    table_size[fb_id])) {
        if (first_bad_table != NULL) {
            *first_bad_table = fb_id;
        }
        return false;
    }

    if (!refmem_table_registry_validate_event_link(data + table_offset[event_id],
                                                   table_size[event_id],
                                                   &application_map)) {
        if (first_bad_table != NULL) {
            *first_bad_table = event_id;
        }
        return false;
    }

    if (!refmem_table_registry_validate_data_link(data + table_offset[data_id],
                                                  table_size[data_id])) {
        if (first_bad_table != NULL) {
            *first_bad_table = data_id;
        }
        return false;
    }

    if (!refmem_table_registry_validate_deployment_gate(data + table_offset[gate_id],
                                                        table_size[gate_id])) {
        if (first_bad_table != NULL) {
            *first_bad_table = gate_id;
        }
        return false;
    }

    if (!refmem_table_registry_validate_connection_quality(data + table_offset[quality_id],
                                                           table_size[quality_id])) {
        if (first_bad_table != NULL) {
            *first_bad_table = quality_id;
        }
        return false;
    }

    return true;
}

static bool refmem_table_registry_parse_fb_instance_entry(
    const uint8_t *data,
    refmem_fb_instance_wire_entry_t *entry)
{
    if (data == NULL || entry == NULL) {
        return false;
    }

    entry->instance_id = refmem_table_read_u32_le(&data[0]);
    entry->default_node_id = refmem_table_read_u32_le(&data[4]);
    entry->domain = refmem_table_read_u32_le(&data[8]);
    entry->ao_type = refmem_table_read_u32_le(&data[12]);
    entry->fb_type = refmem_table_read_u32_le(&data[16]);
    entry->instance_name_hash = refmem_table_read_u32_le(&data[20]);
    entry->version = refmem_table_read_u32_le(&data[24]);
    entry->enable_condition = refmem_table_read_u32_le(&data[28]);
    entry->resource_claim = refmem_table_read_u32_le(&data[32]);
    entry->io_claim = refmem_table_read_u32_le(&data[36]);
    entry->ip_core_claim = refmem_table_read_u32_le(&data[40]);
    entry->time_budget_us = refmem_table_read_u32_le(&data[44]);
    entry->state_region_ref = refmem_table_read_u32_le(&data[48]);
    entry->health_region_ref = refmem_table_read_u32_le(&data[52]);
    entry->event_first = refmem_table_read_u32_le(&data[56]);
    entry->event_count = refmem_table_read_u32_le(&data[60]);
    entry->data_first = refmem_table_read_u32_le(&data[64]);
    entry->data_count = refmem_table_read_u32_le(&data[68]);
    entry->conflict_class = refmem_table_read_u32_le(&data[72]);
    entry->restart_policy = refmem_table_read_u32_le(&data[76]);
    return true;
}

static bool refmem_table_registry_validate_fb_instance(
    const uint8_t *data,
    size_t size)
{
    uint32_t count = 0u;
    if (!refmem_table_registry_validate_wire_header(data,
                                                    size,
                                                    REFMEM_APP_MODEL_INSTANCE_COUNT,
                                                    REFMEM_TABLE_WIRE_FB_INSTANCE_WORDS,
                                                    &count)) {
        return false;
    }

    size_t cursor = REFMEM_TABLE_WIRE_HEADER_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    for (uint32_t i = 0u; i < count; i++) {
        refmem_fb_instance_wire_entry_t entry;
        if (!refmem_table_registry_parse_fb_instance_entry(&data[cursor], &entry) ||
            entry.instance_id != i ||
            entry.default_node_id >= REFMEM_APP_MODEL_NODE_COUNT ||
            entry.domain > REFMEM_APP_DOMAIN_TDMA ||
            entry.ao_type > REFMEM_APP_FB_TDMA_SCHEDULER ||
            entry.fb_type > REFMEM_APP_FB_TDMA_SCHEDULER ||
            entry.instance_name_hash == 0u ||
            entry.version == 0u ||
            entry.enable_condition > 1u ||
            entry.time_budget_us == 0u ||
            entry.state_region_ref >= REFMEM_VECTOR_REGION_COUNT ||
            entry.health_region_ref >= REFMEM_VECTOR_REGION_COUNT ||
            entry.event_first > REFMEM_APP_MODEL_EVENT_LINK_COUNT ||
            entry.event_count > (REFMEM_APP_MODEL_EVENT_LINK_COUNT - entry.event_first) ||
            entry.data_first > REFMEM_APP_MODEL_DATA_LINK_COUNT ||
            entry.data_count > (REFMEM_APP_MODEL_DATA_LINK_COUNT - entry.data_first)) {
            return false;
        }
        cursor += REFMEM_TABLE_WIRE_FB_INSTANCE_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    }
    return true;
}

static bool refmem_table_registry_validate_event_link(
    const uint8_t *data,
    size_t size,
    const refmem_application_map_t *application_map)
{
    uint32_t count = 0u;
    if (application_map == NULL ||
        !refmem_table_registry_validate_wire_header(data,
                                                    size,
                                                    REFMEM_APP_MODEL_EVENT_LINK_COUNT,
                                                    REFMEM_TABLE_WIRE_EVENT_LINK_WORDS,
                                                    &count)) {
        return false;
    }

    size_t cursor = REFMEM_TABLE_WIRE_HEADER_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    for (uint32_t i = 0u; i < count; i++) {
        refmem_event_link_wire_entry_t entry;
        entry.event_link_id = refmem_table_read_u32_le(&data[cursor + 0u]);
        entry.source_instance = refmem_table_read_u32_le(&data[cursor + 4u]);
        entry.source_event = refmem_table_read_u32_le(&data[cursor + 8u]);
        entry.target_node_mask = refmem_table_read_u32_le(&data[cursor + 12u]);
        entry.target_instance = refmem_table_read_u32_le(&data[cursor + 16u]);
        entry.target_event = refmem_table_read_u32_le(&data[cursor + 20u]);
        entry.transport = refmem_table_read_u32_le(&data[cursor + 24u]);
        entry.timeout_us = refmem_table_read_u32_le(&data[cursor + 28u]);
        entry.ack_policy = refmem_table_read_u32_le(&data[cursor + 32u]);
        entry.retry_policy = refmem_table_read_u32_le(&data[cursor + 36u]);
        entry.safety_class = refmem_table_read_u32_le(&data[cursor + 40u]);
        entry.evidence_region_ref = refmem_table_read_u32_le(&data[cursor + 44u]);

        if (entry.event_link_id != i ||
            entry.source_instance >= REFMEM_APP_MODEL_INSTANCE_COUNT ||
            entry.target_instance >= REFMEM_APP_MODEL_INSTANCE_COUNT ||
            entry.target_node_mask == 0u ||
            (entry.target_node_mask & ~application_map->target_node_mask) != 0u ||
            entry.source_event > REFMEM_APP_EVENT_CONFIG_ACTIVATE ||
            entry.target_event > REFMEM_APP_EVENT_CONFIG_ACTIVATE ||
            entry.transport > REFMEM_APP_TRANSPORT_PIO_SPI ||
            entry.timeout_us == 0u ||
            entry.ack_policy > REFMEM_APP_ACK_BITMAP ||
            entry.evidence_region_ref >= REFMEM_VECTOR_REGION_COUNT) {
            return false;
        }
        cursor += REFMEM_TABLE_WIRE_EVENT_LINK_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    }
    return true;
}

static bool refmem_table_registry_validate_data_link(const uint8_t *data, size_t size)
{
    uint32_t count = 0u;
    if (!refmem_table_registry_validate_wire_header(data,
                                                    size,
                                                    REFMEM_APP_MODEL_DATA_LINK_COUNT,
                                                    REFMEM_TABLE_WIRE_DATA_LINK_WORDS,
                                                    &count)) {
        return false;
    }

    size_t cursor = REFMEM_TABLE_WIRE_HEADER_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    for (uint32_t i = 0u; i < count; i++) {
        refmem_data_link_wire_entry_t entry;
        entry.data_link_id = refmem_table_read_u32_le(&data[cursor + 0u]);
        entry.region_path_hash = refmem_table_read_u32_le(&data[cursor + 4u]);
        entry.writer_instance = refmem_table_read_u32_le(&data[cursor + 8u]);
        entry.reader_mask = refmem_table_read_u32_le(&data[cursor + 12u]);
        entry.type = refmem_table_read_u32_le(&data[cursor + 16u]);
        entry.unit = refmem_table_read_u32_le(&data[cursor + 20u]);
        entry.scale = refmem_table_read_i32_le(&data[cursor + 24u]);
        entry.min_value = refmem_table_read_i32_le(&data[cursor + 28u]);
        entry.max_value = refmem_table_read_i32_le(&data[cursor + 32u]);
        entry.lifecycle = refmem_table_read_u32_le(&data[cursor + 36u]);
        entry.snapshot_policy = refmem_table_read_u32_le(&data[cursor + 40u]);
        entry.update_period_us = refmem_table_read_u32_le(&data[cursor + 44u]);
        entry.stale_window_us = refmem_table_read_u32_le(&data[cursor + 48u]);
        entry.crc_region_ref = refmem_table_read_u32_le(&data[cursor + 52u]);
        entry.permission = refmem_table_read_u32_le(&data[cursor + 56u]);

        if (entry.data_link_id != i ||
            entry.region_path_hash == 0u ||
            entry.writer_instance >= REFMEM_APP_MODEL_INSTANCE_COUNT ||
            entry.reader_mask == 0u ||
            (entry.reader_mask & ~((1u << REFMEM_APP_MODEL_NODE_COUNT) - 1u)) != 0u ||
            entry.type > REFMEM_APP_DATA_CRC ||
            entry.unit > REFMEM_APP_UNIT_COUNT ||
            entry.scale == 0 ||
            entry.min_value > entry.max_value ||
            entry.lifecycle > REFMEM_APP_LIFE_EVIDENCE ||
            entry.snapshot_policy > REFMEM_APP_SNAPSHOT_EVIDENCE_REF ||
            entry.update_period_us == 0u ||
            entry.stale_window_us < entry.update_period_us ||
            entry.crc_region_ref >= REFMEM_VECTOR_REGION_COUNT ||
            entry.permission > REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE) {
            return false;
        }
        cursor += REFMEM_TABLE_WIRE_DATA_LINK_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    }
    return true;
}

static bool refmem_table_registry_validate_deployment_gate(const uint8_t *data, size_t size)
{
    uint32_t count = 0u;
    if (!refmem_table_registry_validate_wire_header(data,
                                                    size,
                                                    REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT,
                                                    REFMEM_TABLE_WIRE_DEPLOYMENT_GATE_WORDS,
                                                    &count)) {
        return false;
    }

    size_t cursor = REFMEM_TABLE_WIRE_HEADER_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    for (uint32_t i = 0u; i < count; i++) {
        refmem_deployment_gate_wire_entry_t entry;
        entry.check_id = refmem_table_read_u32_le(&data[cursor + 0u]);
        entry.required = refmem_table_read_u32_le(&data[cursor + 4u]);
        entry.fail_action = refmem_table_read_u32_le(&data[cursor + 8u]);
        entry.last_state = refmem_table_read_u32_le(&data[cursor + 12u]);
        entry.reject_code = refmem_table_read_u32_le(&data[cursor + 16u]);
        entry.reject_instance = refmem_table_read_u32_le(&data[cursor + 20u]);
        entry.reject_node = refmem_table_read_u32_le(&data[cursor + 24u]);
        entry.reject_region_ref = refmem_table_read_u32_le(&data[cursor + 28u]);
        entry.reject_evidence_index = refmem_table_read_u32_le(&data[cursor + 32u]);

        if (entry.check_id != i ||
            entry.required > 1u ||
            entry.fail_action > REFMEM_APP_GATE_LATCH_FAULT ||
            entry.last_state > REFMEM_APP_GATE_LATCH_FAULT ||
            entry.reject_instance >= REFMEM_APP_MODEL_INSTANCE_COUNT ||
            entry.reject_node >= REFMEM_APP_MODEL_NODE_COUNT ||
            entry.reject_region_ref >= REFMEM_VECTOR_REGION_COUNT) {
            return false;
        }
        cursor += REFMEM_TABLE_WIRE_DEPLOYMENT_GATE_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    }
    return true;
}

static bool refmem_table_registry_validate_connection_quality(const uint8_t *data, size_t size)
{
    uint32_t count = 0u;
    if (!refmem_table_registry_validate_wire_header(data,
                                                    size,
                                                    REFMEM_APP_MODEL_QUALITY_COUNT,
                                                    REFMEM_TABLE_WIRE_CONNECTION_QUALITY_WORDS,
                                                    &count)) {
        return false;
    }

    size_t cursor = REFMEM_TABLE_WIRE_HEADER_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    for (uint32_t i = 0u; i < count; i++) {
        refmem_connection_quality_wire_entry_t entry;
        entry.quality_id = refmem_table_read_u32_le(&data[cursor + 0u]);
        entry.scope = refmem_table_read_u32_le(&data[cursor + 4u]);
        entry.source_node = refmem_table_read_u32_le(&data[cursor + 8u]);
        entry.target_node = refmem_table_read_u32_le(&data[cursor + 12u]);
        entry.seq_expected = refmem_table_read_u32_le(&data[cursor + 16u]);
        entry.seq_last = refmem_table_read_u32_le(&data[cursor + 20u]);
        entry.crc_error_count = refmem_table_read_u32_le(&data[cursor + 24u]);
        entry.stale_count = refmem_table_read_u32_le(&data[cursor + 28u]);
        entry.late_count = refmem_table_read_u32_le(&data[cursor + 32u]);
        entry.drop_count = refmem_table_read_u32_le(&data[cursor + 36u]);
        entry.timeout_count = refmem_table_read_u32_le(&data[cursor + 40u]);
        entry.last_error = refmem_table_read_u32_le(&data[cursor + 44u]);
        entry.last_error_tick = refmem_table_read_u32_le(&data[cursor + 48u]);
        entry.p99 = refmem_table_read_u32_le(&data[cursor + 52u]);
        entry.p999 = refmem_table_read_u32_le(&data[cursor + 56u]);
        entry.evidence_index = refmem_table_read_u32_le(&data[cursor + 60u]);

        if (entry.quality_id != i ||
            entry.scope > REFMEM_APP_QUALITY_TRANSPORT_ADAPTER ||
            entry.source_node >= REFMEM_APP_MODEL_NODE_COUNT ||
            entry.target_node >= REFMEM_APP_MODEL_NODE_COUNT) {
            return false;
        }
        cursor += REFMEM_TABLE_WIRE_CONNECTION_QUALITY_WORDS * REFMEM_TABLE_WIRE_U32_SIZE;
    }
    return true;
}

static uint32_t refmem_table_package_crc32_zero_field(const uint8_t *data,
                                                      size_t size,
                                                      uint32_t zero_offset)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < size; i++) {
        uint8_t byte = data[i];
        if (i >= zero_offset && i < zero_offset + sizeof(uint32_t)) {
            byte = 0u;
        }
        crc ^= byte;
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t refmem_table_package_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < size; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static bool refmem_table_registry_find_package_table(const uint8_t *data,
                                                     size_t size,
                                                     uint32_t table_id,
                                                     uint32_t *table_offset,
                                                     uint32_t *table_size,
                                                     uint32_t *table_crc32)
{
    if (data == NULL ||
        table_offset == NULL ||
        table_size == NULL ||
        table_crc32 == NULL ||
        table_id >= REFMEM_TABLE_REGISTRY_COUNT ||
        size < REFMEM_TABLE_PACKAGE_HEADER_SIZE) {
        return false;
    }

    const uint32_t magic = refmem_table_read_u32_le(&data[0]);
    const uint32_t version = refmem_table_read_u32_le(&data[4]);
    const uint32_t header_size = refmem_table_read_u32_le(&data[8]);
    const uint32_t total_size = refmem_table_read_u32_le(&data[12]);
    const uint32_t table_count = refmem_table_read_u32_le(&data[16]);
    const uint32_t table_dir_size = refmem_table_read_u32_le(&data[20]);
    if (magic != REFMEM_TABLE_PACKAGE_MAGIC ||
        version != REFMEM_TABLE_PACKAGE_VERSION ||
        header_size != REFMEM_TABLE_PACKAGE_HEADER_SIZE ||
        total_size != size ||
        table_count != REFMEM_TABLE_REGISTRY_COUNT ||
        table_dir_size != table_count * REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE ||
        header_size + table_dir_size > size) {
        return false;
    }

    for (uint32_t i = 0u; i < table_count; i++) {
        const size_t cursor = header_size + i * REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE;
        const uint32_t entry_table_id = refmem_table_read_u32_le(&data[cursor + 0u]);
        const uint32_t entry_offset = refmem_table_read_u32_le(&data[cursor + 4u]);
        const uint32_t entry_size = refmem_table_read_u32_le(&data[cursor + 8u]);
        const uint32_t entry_crc32 = refmem_table_read_u32_le(&data[cursor + 12u]);
        if (entry_table_id != table_id) {
            continue;
        }
        if (entry_offset < header_size + table_dir_size ||
            entry_offset > size ||
            entry_size > size - entry_offset ||
            refmem_table_package_crc32(&data[entry_offset], entry_size) != entry_crc32) {
            return false;
        }
        *table_offset = entry_offset;
        *table_size = entry_size;
        *table_crc32 = entry_crc32;
        return true;
    }

    return false;
}

static uint32_t refmem_table_registry_crc32(void)
{
    uint32_t crc = 2166136261u;
    for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
        crc = refmem_table_registry_crc32_update(crc, &s_registry[i], sizeof(s_registry[i]));
    }
    return crc;
}

static uint32_t refmem_table_registry_active_crc(const refmem_application_model_snapshot_t *model,
                                                 uint32_t table_id)
{
    if (model == NULL) {
        return 0u;
    }

    switch ((refmem_app_table_id_t)table_id) {
    case REFMEM_APP_TABLE_APPLICATION_MAP:
        return model->application_map_crc32;
    case REFMEM_APP_TABLE_BOARD_CAPABILITY:
        return model->board_capability_crc32;
    case REFMEM_APP_TABLE_GENERIC_NODE:
        return model->generic_node_crc32;
    case REFMEM_APP_TABLE_NODE_LOAD:
        return model->node_load_crc32;
    case REFMEM_APP_TABLE_FB_INSTANCE:
        return model->fb_instance_crc32;
    case REFMEM_APP_TABLE_EVENT_LINK:
        return model->event_link_crc32;
    case REFMEM_APP_TABLE_DATA_LINK:
        return model->data_link_crc32;
    case REFMEM_APP_TABLE_DEPLOYMENT_GATE:
        return model->deployment_gate_crc32;
    case REFMEM_APP_TABLE_CONNECTION_QUALITY:
        return model->connection_quality_crc32;
    default:
        return 0u;
    }
}

static void refmem_table_registry_refresh_snapshot(void)
{
    uint32_t active_mask = 0u;
    uint32_t staging_mask = 0u;
    for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
        const refmem_table_registry_entry_t *entry = &s_registry[i];
        if ((entry->flags & REFMEM_TABLE_FLAG_ACTIVE_PRESENT) != 0u) {
            active_mask |= (1u << entry->table_id);
        }
        if ((entry->flags & REFMEM_TABLE_FLAG_STAGING_PRESENT) != 0u) {
            staging_mask |= (1u << entry->table_id);
        }
    }

    s_snapshot.version = REFMEM_TABLE_REGISTRY_VERSION;
    s_snapshot.table_count = REFMEM_TABLE_REGISTRY_COUNT;
    s_snapshot.active_table_mask = active_mask;
    s_snapshot.staging_table_mask = staging_mask;
    s_snapshot.registry_crc32 = refmem_table_registry_crc32();
}

static void refmem_table_registry_clear_image(refmem_table_image_descriptor_t *descriptor,
                                              refmem_table_image_role_t role)
{
    (void)memset(descriptor, 0, sizeof(*descriptor));
    descriptor->version = REFMEM_TABLE_REGISTRY_VERSION;
    descriptor->role = (uint32_t)role;
    descriptor->state = REFMEM_TABLE_VALIDATION_EMPTY;
    descriptor->evidence_index = REFMEM_VECTOR_REGION_STATS;
}

static void refmem_table_registry_copy_image(uint8_t *dst,
                                             size_t *dst_size,
                                             refmem_table_image_descriptor_t *dst_descriptor,
                                             const uint8_t *src,
                                             size_t src_size,
                                             const refmem_table_image_descriptor_t *src_descriptor,
                                             refmem_table_image_role_t dst_role)
{
    if (dst == NULL || dst_size == NULL || dst_descriptor == NULL ||
        src == NULL || src_descriptor == NULL ||
        src_size > REFMEM_TABLE_IMAGE_BUFFER_SIZE) {
        return;
    }

    (void)memcpy(dst, src, src_size);
    *dst_size = src_size;
    *dst_descriptor = *src_descriptor;
    dst_descriptor->role = (uint32_t)dst_role;
}

static void refmem_table_registry_clear_staging_buffer(void)
{
    (void)memset(s_staging_image_buffer, 0, sizeof(s_staging_image_buffer));
    s_staging_image_size = 0u;
}

static void refmem_table_registry_clear_staging_payload(void)
{
    refmem_table_registry_clear_staging_buffer();
    refmem_table_registry_clear_image(&s_staging_image, REFMEM_TABLE_IMAGE_STAGING);
}

static uint32_t refmem_table_registry_gate_mask(const refmem_table_activation_gate_t *gate)
{
    if (gate == NULL) {
        return 0u;
    }

    uint32_t mask = 0u;
    mask |= gate->refmem_idle != 0u ? 0x00000001u : 0u;
    mask |= gate->realtime_idle != 0u ? 0x00000002u : 0u;
    mask |= gate->flash_safe != 0u ? 0x00000004u : 0u;
    mask |= gate->crc_ok != 0u ? 0x00000008u : 0u;
    mask |= gate->owner_ok != 0u ? 0x00000010u : 0u;
    mask |= gate->slot_claim_ok != 0u ? 0x00000020u : 0u;
    mask |= gate->deployment_gate_ok != 0u ? 0x00000040u : 0u;
    mask |= gate->command_ack_ok != 0u ? 0x00000080u : 0u;
    return mask;
}

static bool refmem_table_registry_gate_passed(const refmem_table_activation_gate_t *gate)
{
    return refmem_table_registry_gate_mask(gate) == 0x000000FFu;
}

void refmem_table_registry_init(const refmem_application_model_snapshot_t *model)
{
    memset(s_registry, 0, sizeof(s_registry));
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    refmem_table_registry_clear_image(&s_active_image, REFMEM_TABLE_IMAGE_ACTIVE);
    refmem_table_registry_clear_image(&s_staging_image, REFMEM_TABLE_IMAGE_STAGING);
    refmem_table_registry_clear_image(&s_rollbackable_image, REFMEM_TABLE_IMAGE_ROLLBACKABLE);
    (void)memset(s_active_image_buffer, 0, sizeof(s_active_image_buffer));
    (void)memset(s_staging_image_buffer, 0, sizeof(s_staging_image_buffer));
    (void)memset(s_rollbackable_image_buffer, 0, sizeof(s_rollbackable_image_buffer));
    s_active_image_size = 0u;
    s_staging_image_size = 0u;
    s_rollbackable_image_size = 0u;
    (void)memset(s_image_access_count, 0, sizeof(s_image_access_count));
    s_table_seq = 0u;

    for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
        s_registry[i].table_id = i;
        s_registry[i].owner = REFMEM_TABLE_OWNER_REFMEM_AO;
        s_registry[i].layout_version = REFMEM_APP_MODEL_VERSION;
        s_registry[i].image_offset = 0u;
        for (uint32_t j = 0u; j < i; j++) {
            s_registry[i].image_offset += s_table_image_size[j];
        }
        s_registry[i].image_size = s_table_image_size[i];
        s_registry[i].validator_id = i;
        s_registry[i].evidence_index = REFMEM_VECTOR_REGION_STATS;
        s_registry[i].validation_state = REFMEM_TABLE_VALIDATION_EMPTY;
    }

    refmem_table_registry_refresh_active(model);
}

void refmem_table_registry_refresh_active(const refmem_application_model_snapshot_t *model)
{
    if (model == NULL) {
        s_snapshot.last_error = 1u;
        refmem_table_registry_refresh_snapshot();
        return;
    }

    for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
        refmem_table_registry_entry_t *entry = &s_registry[i];
        const uint32_t table_bit = 1u << entry->table_id;
        entry->active_crc32 = refmem_table_registry_active_crc(model, entry->table_id);
        entry->last_result = model->first_lint_error;
        entry->flags = 0u;
        if ((model->table_mask & table_bit) != 0u && entry->active_crc32 != 0u) {
            entry->flags |= REFMEM_TABLE_FLAG_ACTIVE_PRESENT |
                            REFMEM_TABLE_FLAG_CRC_OK;
        }
        entry->validation_state = (model->valid != 0u && entry->active_crc32 != 0u)
                                      ? REFMEM_TABLE_VALIDATION_ACTIVE
                                      : REFMEM_TABLE_VALIDATION_FAILED;
    }

    s_table_seq++;
    s_active_image.version = REFMEM_TABLE_REGISTRY_VERSION;
    s_active_image.role = REFMEM_TABLE_IMAGE_ACTIVE;
    s_active_image.state = model->valid != 0u
                               ? REFMEM_TABLE_VALIDATION_ACTIVE
                               : REFMEM_TABLE_VALIDATION_FAILED;
    s_active_image.table_mask = model->table_mask;
    s_active_image.package_crc32 = model->package_crc32;
    s_active_image.table_seq = s_table_seq;
    s_active_image.path_hash = 0u;
    s_active_image.last_result = model->first_lint_error;
    s_active_image.evidence_index = REFMEM_VECTOR_REGION_STATS;

    s_snapshot.last_error = model->first_lint_error;
    refmem_table_registry_refresh_snapshot();
}

void refmem_table_registry_refresh_staging(const refmem_application_model_load_snapshot_t *load)
{
    refmem_table_registry_clear_staging_payload();

    if (load == NULL) {
        s_snapshot.last_error = 1u;
        refmem_table_registry_refresh_snapshot();
        return;
    }

    const uint32_t staging_crc32 = load->staging_package_crc32;
    uint32_t staging_mask = 0u;
    for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
        refmem_table_registry_entry_t *entry = &s_registry[i];
        entry->staging_crc32 = staging_crc32;
        entry->flags &= ~(REFMEM_TABLE_FLAG_STAGING_PRESENT |
                          REFMEM_TABLE_FLAG_CRC_OK |
                          REFMEM_TABLE_FLAG_OWNER_OK);
        if (entry->active_crc32 != 0u) {
            entry->flags |= REFMEM_TABLE_FLAG_ACTIVE_PRESENT;
        }
        entry->last_result = load->staging_first_lint_error != 0u
                                 ? load->staging_first_lint_error
                                 : load->last_error;

        if (load->staging_state == REFMEM_APP_STAGING_STAGED) {
            entry->validation_state = REFMEM_TABLE_VALIDATION_STAGED;
            if (staging_crc32 != 0u) {
                staging_mask |= (1u << entry->table_id);
                entry->flags |= REFMEM_TABLE_FLAG_STAGING_PRESENT;
            }
        } else if (load->staging_state == REFMEM_APP_STAGING_VALIDATED) {
            entry->validation_state = REFMEM_TABLE_VALIDATION_OWNER_OK;
            if (staging_crc32 != 0u) {
                staging_mask |= (1u << entry->table_id);
                entry->flags |= REFMEM_TABLE_FLAG_STAGING_PRESENT |
                                REFMEM_TABLE_FLAG_CRC_OK |
                                REFMEM_TABLE_FLAG_OWNER_OK;
            }
        } else if (load->staging_state == REFMEM_APP_STAGING_FAILED) {
            entry->validation_state = REFMEM_TABLE_VALIDATION_FAILED;
        } else if (entry->active_crc32 != 0u) {
            entry->validation_state = REFMEM_TABLE_VALIDATION_ACTIVE;
        } else {
            entry->validation_state = REFMEM_TABLE_VALIDATION_EMPTY;
        }
    }

    s_staging_image.version = REFMEM_TABLE_REGISTRY_VERSION;
    s_staging_image.role = REFMEM_TABLE_IMAGE_STAGING;
    s_staging_image.state =
        load->staging_state == REFMEM_APP_STAGING_VALIDATED
            ? REFMEM_TABLE_VALIDATION_OWNER_OK
            : (load->staging_state == REFMEM_APP_STAGING_STAGED
                   ? REFMEM_TABLE_VALIDATION_STAGED
                   : (load->staging_state == REFMEM_APP_STAGING_FAILED
                          ? REFMEM_TABLE_VALIDATION_FAILED
                          : REFMEM_TABLE_VALIDATION_EMPTY));
    s_staging_image.table_mask = staging_mask;
    s_staging_image.package_crc32 = staging_crc32;
    s_staging_image.table_seq = s_table_seq;
    s_staging_image.path_hash = load->path_hash;
    s_staging_image.last_result = load->staging_first_lint_error != 0u
                                      ? load->staging_first_lint_error
                                      : load->last_error;
    s_staging_image.evidence_index = REFMEM_VECTOR_REGION_STATS;

    s_snapshot.last_error = load->last_error;
    refmem_table_registry_refresh_snapshot();
}

bool refmem_table_registry_validate_staging(const refmem_application_model_load_snapshot_t *load)
{
    if (load == NULL ||
        load->staging_state == REFMEM_APP_STAGING_EMPTY ||
        load->staging_package_crc32 == 0u ||
        load->staging_lint_error_count != 0u ||
        load->last_error != REFMEM_APP_LOAD_OK) {
        if (load != NULL) {
            refmem_table_registry_refresh_staging(load);
        }
        return false;
    }

    refmem_table_registry_refresh_staging(load);
    return true;
}

bool refmem_table_registry_stage_package_validation(
    const refmem_application_model_load_snapshot_t *load,
    const refmem_table_package_validation_t *validation)
{
    refmem_table_registry_clear_staging_payload();

    if (load == NULL ||
        validation == NULL ||
        validation->valid == 0u ||
        validation->table_count != REFMEM_TABLE_REGISTRY_COUNT ||
        validation->table_mask != ((1u << REFMEM_TABLE_REGISTRY_COUNT) - 1u) ||
        load->staging_state == REFMEM_APP_STAGING_EMPTY ||
        load->staging_package_crc32 == 0u ||
        load->last_error != REFMEM_APP_LOAD_OK) {
        if (load != NULL) {
            refmem_table_registry_refresh_staging(load);
        }
        return false;
    }

    uint32_t owner_mask = 0u;
    for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
        refmem_table_registry_entry_t *entry = &s_registry[i];
        const uint32_t table_bit = 1u << entry->table_id;
        const bool owner_ok = (validation->owner_validated_table_mask & table_bit) != 0u;

        entry->staging_crc32 = validation->table_crc32[i];
        entry->last_result = validation->error;
        entry->flags &= ~(REFMEM_TABLE_FLAG_STAGING_PRESENT |
                          REFMEM_TABLE_FLAG_CRC_OK |
                          REFMEM_TABLE_FLAG_OWNER_OK);
        if (entry->active_crc32 != 0u) {
            entry->flags |= REFMEM_TABLE_FLAG_ACTIVE_PRESENT;
        }
        entry->flags |= REFMEM_TABLE_FLAG_STAGING_PRESENT | REFMEM_TABLE_FLAG_CRC_OK;
        entry->validation_state = owner_ok
                                      ? REFMEM_TABLE_VALIDATION_OWNER_OK
                                      : REFMEM_TABLE_VALIDATION_CRC_OK;
        if (owner_ok) {
            entry->flags |= REFMEM_TABLE_FLAG_OWNER_OK;
            owner_mask |= table_bit;
        }
    }

    const uint32_t full_mask = (1u << REFMEM_TABLE_REGISTRY_COUNT) - 1u;
    s_staging_image.version = REFMEM_TABLE_REGISTRY_VERSION;
    s_staging_image.role = REFMEM_TABLE_IMAGE_STAGING;
    s_staging_image.state = owner_mask == full_mask
                                ? REFMEM_TABLE_VALIDATION_OWNER_OK
                                : REFMEM_TABLE_VALIDATION_CRC_OK;
    s_staging_image.table_mask = validation->table_mask;
    s_staging_image.package_crc32 = validation->package_crc32;
    s_staging_image.table_seq = s_table_seq;
    s_staging_image.path_hash = load->path_hash;
    s_staging_image.last_result = validation->error;
    s_staging_image.evidence_index = REFMEM_VECTOR_REGION_STATS;

    s_snapshot.last_error = validation->error;
    refmem_table_registry_refresh_snapshot();
    return true;
}

bool refmem_table_registry_stage_package_image(
    const refmem_application_model_load_snapshot_t *load,
    const uint8_t *data,
    size_t size,
    const refmem_table_package_validation_t *validation)
{
    if (size > REFMEM_TABLE_IMAGE_BUFFER_SIZE) {
        refmem_table_registry_clear_staging_payload();
        s_staging_image.last_result = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_TOO_LARGE;
        s_snapshot.last_error = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_TOO_LARGE;
        refmem_table_registry_refresh_snapshot();
        return false;
    }

    refmem_table_package_validation_t local_validation;
    if (validation == NULL) {
        if (!refmem_table_registry_validate_package(data, size, &local_validation)) {
            refmem_table_registry_clear_staging_payload();
            s_snapshot.last_error = local_validation.error;
            refmem_table_registry_refresh_snapshot();
            return false;
        }
        validation = &local_validation;
    }

    if (!refmem_table_registry_stage_package_validation(load, validation)) {
        refmem_table_registry_clear_staging_payload();
        return false;
    }

    if (data == NULL || size == 0u) {
        refmem_table_registry_clear_staging_buffer();
        s_staging_image.last_result = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED;
        s_snapshot.last_error = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED;
        refmem_table_registry_refresh_snapshot();
        return false;
    }

    (void)memcpy(s_staging_image_buffer, data, size);
    s_staging_image_size = size;
    s_staging_image.last_result = validation->error;
    refmem_table_registry_refresh_snapshot();
    return true;
}

bool refmem_table_registry_stage_table(uint32_t table_id,
                                       uint32_t staging_crc32,
                                       uint32_t validation_state,
                                       uint32_t last_result)
{
    refmem_table_registry_clear_staging_payload();

    if (table_id >= REFMEM_TABLE_REGISTRY_COUNT ||
        validation_state > REFMEM_TABLE_VALIDATION_FAILED) {
        s_snapshot.last_error = 1u;
        refmem_table_registry_refresh_snapshot();
        return false;
    }

    refmem_table_registry_entry_t *entry = &s_registry[table_id];
    entry->staging_crc32 = staging_crc32;
    entry->validation_state = validation_state;
    entry->last_result = last_result;
    entry->flags &= ~(REFMEM_TABLE_FLAG_STAGING_PRESENT |
                      REFMEM_TABLE_FLAG_CRC_OK |
                      REFMEM_TABLE_FLAG_OWNER_OK);
    if (entry->active_crc32 != 0u) {
        entry->flags |= REFMEM_TABLE_FLAG_ACTIVE_PRESENT;
    }
    if (staging_crc32 != 0u && validation_state != REFMEM_TABLE_VALIDATION_FAILED) {
        entry->flags |= REFMEM_TABLE_FLAG_STAGING_PRESENT | REFMEM_TABLE_FLAG_CRC_OK;
        if (validation_state == REFMEM_TABLE_VALIDATION_OWNER_OK) {
            entry->flags |= REFMEM_TABLE_FLAG_OWNER_OK;
        }
    }

    s_staging_image.version = REFMEM_TABLE_REGISTRY_VERSION;
    s_staging_image.role = REFMEM_TABLE_IMAGE_STAGING;
    s_staging_image.state = validation_state;
    if (staging_crc32 != 0u && validation_state != REFMEM_TABLE_VALIDATION_FAILED) {
        s_staging_image.table_mask |= (1u << table_id);
    } else {
        s_staging_image.table_mask &= ~(1u << table_id);
    }
    s_staging_image.package_crc32 = staging_crc32;
    s_staging_image.table_seq = s_table_seq;
    s_staging_image.last_result = last_result;
    s_staging_image.evidence_index = REFMEM_VECTOR_REGION_STATS;

    s_snapshot.last_error = last_result;
    refmem_table_registry_refresh_snapshot();
    return validation_state != REFMEM_TABLE_VALIDATION_FAILED;
}

bool refmem_table_registry_activate_staging(const refmem_table_activation_gate_t *gate)
{
    if (gate == NULL) {
        s_snapshot.last_error = REFMEM_TABLE_ACTIVATE_ERR_BAD_ARGUMENT;
        refmem_table_registry_refresh_snapshot();
        return false;
    }

    const bool valid_staging =
        s_staging_image.state == REFMEM_TABLE_VALIDATION_OWNER_OK &&
        s_staging_image.table_mask != 0u &&
        s_staging_image.package_crc32 != 0u;
    if (!valid_staging) {
        s_staging_image.state = REFMEM_TABLE_VALIDATION_FAILED;
        s_staging_image.last_result = REFMEM_TABLE_ACTIVATE_ERR_NO_VALID_STAGING;
        s_snapshot.last_error = REFMEM_TABLE_ACTIVATE_ERR_NO_VALID_STAGING;
        refmem_table_registry_refresh_snapshot();
        return false;
    }

    if (!refmem_table_registry_gate_passed(gate)) {
        s_staging_image.state = REFMEM_TABLE_VALIDATION_OWNER_OK;
        s_staging_image.last_result = REFMEM_TABLE_ACTIVATE_ERR_GATE;
        s_snapshot.last_error = REFMEM_TABLE_ACTIVATE_ERR_GATE;
        refmem_table_registry_refresh_snapshot();
        return false;
    }

    if (s_image_access_count[REFMEM_TABLE_IMAGE_ACTIVE] != 0u ||
        s_image_access_count[REFMEM_TABLE_IMAGE_STAGING] != 0u ||
        s_image_access_count[REFMEM_TABLE_IMAGE_ROLLBACKABLE] != 0u) {
        s_staging_image.state = REFMEM_TABLE_VALIDATION_OWNER_OK;
        s_staging_image.last_result = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_BUSY;
        s_snapshot.last_error = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_BUSY;
        refmem_table_registry_refresh_snapshot();
        return false;
    }

    if (s_staging_image_size == 0u) {
        s_staging_image.state = REFMEM_TABLE_VALIDATION_OWNER_OK;
        s_staging_image.last_result = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED;
        s_snapshot.last_error = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED;
        refmem_table_registry_refresh_snapshot();
        return false;
    }

    if (s_active_image_size != 0u) {
        refmem_table_registry_copy_image(s_rollbackable_image_buffer,
                                         &s_rollbackable_image_size,
                                         &s_rollbackable_image,
                                         s_active_image_buffer,
                                         s_active_image_size,
                                         &s_active_image,
                                         REFMEM_TABLE_IMAGE_ROLLBACKABLE);
        s_rollbackable_image.state = REFMEM_TABLE_VALIDATION_ROLLBACKABLE;
        s_rollbackable_image.last_result = REFMEM_TABLE_ACTIVATE_OK;
    } else {
        s_rollbackable_image_size = 0u;
        refmem_table_registry_clear_image(&s_rollbackable_image,
                                          REFMEM_TABLE_IMAGE_ROLLBACKABLE);
    }

    s_table_seq++;
    refmem_table_registry_copy_image(s_active_image_buffer,
                                     &s_active_image_size,
                                     &s_active_image,
                                     s_staging_image_buffer,
                                     s_staging_image_size,
                                     &s_staging_image,
                                     REFMEM_TABLE_IMAGE_ACTIVE);
    s_active_image.state = REFMEM_TABLE_VALIDATION_ACTIVE;
    s_active_image.table_seq = s_table_seq;
    s_active_image.last_result = REFMEM_TABLE_ACTIVATE_OK;

    for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
        refmem_table_registry_entry_t *entry = &s_registry[i];
        const uint32_t table_bit = 1u << entry->table_id;
        if ((s_active_image.table_mask & table_bit) == 0u) {
            continue;
        }
        entry->active_crc32 = entry->staging_crc32;
        entry->staging_crc32 = 0u;
        entry->validation_state = REFMEM_TABLE_VALIDATION_ACTIVE;
        entry->last_result = REFMEM_TABLE_ACTIVATE_OK;
        entry->flags &= ~(REFMEM_TABLE_FLAG_STAGING_PRESENT |
                          REFMEM_TABLE_FLAG_ROLLBACKABLE);
        entry->flags |= REFMEM_TABLE_FLAG_ACTIVE_PRESENT |
                        REFMEM_TABLE_FLAG_CRC_OK |
                        REFMEM_TABLE_FLAG_OWNER_OK;
        if (s_rollbackable_image_size != 0u) {
            entry->flags |= REFMEM_TABLE_FLAG_ROLLBACKABLE;
        }
    }

    refmem_table_registry_clear_staging_payload();
    s_snapshot.last_error = REFMEM_TABLE_ACTIVATE_OK;
    refmem_table_registry_refresh_snapshot();
    return true;
}

bool refmem_table_registry_note_activation_result(refmem_table_activation_result_t result)
{
    if (result == REFMEM_TABLE_ACTIVATE_OK) {
        return false;
    }

    if (s_staging_image.state != REFMEM_TABLE_VALIDATION_EMPTY) {
        s_staging_image.last_result = (uint32_t)result;
    }
    s_snapshot.last_error = (uint32_t)result;
    refmem_table_registry_refresh_snapshot();
    return true;
}

bool refmem_table_registry_get_image_descriptor(refmem_table_image_role_t role,
                                                refmem_table_image_descriptor_t *descriptor)
{
    if (descriptor == NULL) {
        return false;
    }

    switch (role) {
    case REFMEM_TABLE_IMAGE_ACTIVE:
        *descriptor = s_active_image;
        return true;
    case REFMEM_TABLE_IMAGE_STAGING:
        *descriptor = s_staging_image;
        return true;
    case REFMEM_TABLE_IMAGE_ROLLBACKABLE:
        *descriptor = s_rollbackable_image;
        return true;
    default:
        return false;
    }
}

bool refmem_table_registry_access_table(refmem_table_image_role_t role,
                                        uint32_t table_id,
                                        refmem_table_view_t *view)
{
    if (view == NULL ||
        table_id >= REFMEM_TABLE_REGISTRY_COUNT ||
        role > REFMEM_TABLE_IMAGE_ROLLBACKABLE) {
        return false;
    }

    const uint8_t *image = NULL;
    size_t image_size = 0u;
    const refmem_table_image_descriptor_t *descriptor = NULL;
    switch (role) {
    case REFMEM_TABLE_IMAGE_ACTIVE:
        image = s_active_image_buffer;
        image_size = s_active_image_size;
        descriptor = &s_active_image;
        break;
    case REFMEM_TABLE_IMAGE_STAGING:
        image = s_staging_image_buffer;
        image_size = s_staging_image_size;
        descriptor = &s_staging_image;
        break;
    case REFMEM_TABLE_IMAGE_ROLLBACKABLE:
        image = s_rollbackable_image_buffer;
        image_size = s_rollbackable_image_size;
        descriptor = &s_rollbackable_image;
        break;
    default:
        return false;
    }

    if (descriptor == NULL ||
        image_size == 0u ||
        (descriptor->table_mask & (1u << table_id)) == 0u) {
        return false;
    }

    uint32_t table_offset = 0u;
    uint32_t table_size = 0u;
    uint32_t table_crc32 = 0u;
    if (!refmem_table_registry_find_package_table(image,
                                                  image_size,
                                                  table_id,
                                                  &table_offset,
                                                  &table_size,
                                                  &table_crc32)) {
        return false;
    }

    (void)memset(view, 0, sizeof(*view));
    view->version = REFMEM_TABLE_REGISTRY_VERSION;
    view->role = (uint32_t)role;
    view->table_id = table_id;
    view->table_seq = descriptor->table_seq;
    view->package_crc32 = descriptor->package_crc32;
    view->table_crc32 = table_crc32;
    view->image_offset = table_offset;
    view->image_size = table_size;
    view->data = &image[table_offset];
    view->size = table_size;
    s_image_access_count[role]++;
    return true;
}

bool refmem_table_registry_release_table(const refmem_table_view_t *view)
{
    if (view == NULL ||
        view->version != REFMEM_TABLE_REGISTRY_VERSION ||
        view->role > REFMEM_TABLE_IMAGE_ROLLBACKABLE ||
        s_image_access_count[view->role] == 0u) {
        return false;
    }

    s_image_access_count[view->role]--;
    return true;
}

bool refmem_table_registry_validate_package(const uint8_t *data,
                                            size_t size,
                                            refmem_table_package_validation_t *validation)
{
    refmem_table_package_validation_t result;
    memset(&result, 0, sizeof(result));
    result.error = REFMEM_TABLE_PACKAGE_ERR_TOO_SMALL;

    if (data == NULL || size < REFMEM_TABLE_PACKAGE_HEADER_SIZE) {
        if (validation != NULL) {
            *validation = result;
        }
        return false;
    }

    const uint32_t magic = refmem_table_read_u32_le(&data[0]);
    result.format_version = refmem_table_read_u32_le(&data[4]);
    const uint32_t header_size = refmem_table_read_u32_le(&data[8]);
    result.total_size = refmem_table_read_u32_le(&data[12]);
    result.table_count = refmem_table_read_u32_le(&data[16]);
    const uint32_t table_dir_size = refmem_table_read_u32_le(&data[20]);
    result.payload_crc32 = refmem_table_read_u32_le(&data[24]);
    result.package_crc32 = refmem_table_read_u32_le(&data[28]);

    if (magic != REFMEM_TABLE_PACKAGE_MAGIC) {
        result.error = REFMEM_TABLE_PACKAGE_ERR_MAGIC;
    } else if (result.format_version != REFMEM_TABLE_PACKAGE_VERSION ||
               header_size != REFMEM_TABLE_PACKAGE_HEADER_SIZE) {
        result.error = REFMEM_TABLE_PACKAGE_ERR_VERSION;
    } else if (result.total_size != size ||
               table_dir_size != result.table_count * REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE ||
               result.total_size < header_size + table_dir_size) {
        result.error = REFMEM_TABLE_PACKAGE_ERR_SIZE;
    } else if (result.table_count != REFMEM_TABLE_REGISTRY_COUNT) {
        result.error = REFMEM_TABLE_PACKAGE_ERR_TABLE_COUNT;
    } else {
        const uint8_t *payload = data + header_size + table_dir_size;
        const size_t payload_size = size - header_size - table_dir_size;
        const uint32_t payload_crc = refmem_table_package_crc32(payload, payload_size);
        const uint32_t package_crc =
            refmem_table_package_crc32_zero_field(data, size, 28u);
        if (payload_crc != result.payload_crc32 ||
            package_crc != result.package_crc32) {
            result.error = REFMEM_TABLE_PACKAGE_ERR_CRC;
        } else {
            result.error = REFMEM_TABLE_PACKAGE_OK;
            uint32_t table_offset[REFMEM_TABLE_REGISTRY_COUNT] = {0u};
            uint32_t table_size_bytes[REFMEM_TABLE_REGISTRY_COUNT] = {0u};
            for (uint32_t i = 0u; i < result.table_count; i++) {
                const uint8_t *entry =
                    data + header_size + i * REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE;
                const uint32_t table_id = refmem_table_read_u32_le(&entry[0]);
                const uint32_t offset = refmem_table_read_u32_le(&entry[4]);
                const uint32_t table_size = refmem_table_read_u32_le(&entry[8]);
                const uint32_t table_crc = refmem_table_read_u32_le(&entry[12]);
                if (table_id != i ||
                    offset < header_size + table_dir_size ||
                    table_size == 0u ||
                    offset > result.total_size ||
                    table_size > result.total_size - offset ||
                    refmem_table_package_crc32(data + offset, table_size) != table_crc) {
                    result.error = REFMEM_TABLE_PACKAGE_ERR_TABLE_DIR;
                    result.first_bad_table = i;
                    break;
                }
                table_offset[i] = offset;
                table_size_bytes[i] = table_size;
                result.table_mask |= (1u << table_id);
                result.table_crc32[table_id] = table_crc;
            }
            if (result.error == REFMEM_TABLE_PACKAGE_OK &&
                !refmem_table_registry_validate_package_owner_contract(data,
                                                                       table_offset,
                                                                       table_size_bytes,
                                                                       result.table_count,
                                                                       &result.first_bad_table)) {
                result.error = REFMEM_TABLE_PACKAGE_ERR_OWNER_VALIDATION;
            } else if (result.error == REFMEM_TABLE_PACKAGE_OK) {
                result.owner_validated_table_mask = REFMEM_APP_TABLE_MASK_ALL;
            }
        }
    }

    result.valid = result.error == REFMEM_TABLE_PACKAGE_OK ? 1u : 0u;
    if (validation != NULL) {
        *validation = result;
    }
    return result.valid != 0u;
}

bool refmem_table_registry_get_entry(uint32_t table_id, refmem_table_registry_entry_t *entry)
{
    if (entry == NULL || table_id >= REFMEM_TABLE_REGISTRY_COUNT) {
        return false;
    }

    *entry = s_registry[table_id];
    return true;
}

void refmem_table_registry_get_snapshot(refmem_table_registry_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    refmem_table_registry_refresh_snapshot();
    *snapshot = s_snapshot;
}
