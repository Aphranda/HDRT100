#include "refmem_application_model_internal.h"

const refmem_event_link_table_t *refmem_model_current_event_link_table(void)
{
    return s_active_tables_from_image ? &s_active_event_link_table : &s_event_link_table;
}

const refmem_data_link_table_t *refmem_model_current_data_link_table(void)
{
    return s_active_tables_from_image ? &s_active_data_link_table : &s_data_link_table;
}

const refmem_deployment_gate_table_t *refmem_model_current_deployment_gate_table(void)
{
    return s_active_tables_from_image ? &s_active_deployment_gate : &s_deployment_gate;
}

const refmem_connection_quality_table_t *refmem_model_current_connection_quality_table(void)
{
    return s_active_tables_from_image ? &s_active_connection_quality : &s_connection_quality;
}

const tdma_foundation_profile_t *refmem_model_current_tdma_foundation_profile(void)
{
    return s_active_tables_from_image ? &s_active_tdma_foundation_profile
                                      : &s_tdma_foundation_profile;
}

const tdma_foundation_profile_t *refmem_model_design_tdma_foundation_profile(void)
{
    return &s_design_tdma_foundation_profile;
}

bool refmem_model_instance_exists(uint32_t instance_id)
{
    const refmem_fb_instance_table_t *table = refmem_model_current_fb_instance_table();
    return instance_id < table->instance_count &&
           table->instance[instance_id].instance_id == instance_id;
}

static const refmem_fb_instance_entry_t *refmem_model_instance_by_id(uint32_t instance_id)
{
    if (!refmem_model_instance_exists(instance_id)) {
        return NULL;
    }
    return &refmem_model_current_fb_instance_table()->instance[instance_id];
}

static size_t refmem_model_table_payload_size(uint32_t table_id)
{
    switch (table_id) {
    case REFMEM_APP_TABLE_APPLICATION_MAP:
        return sizeof(refmem_application_map_t);
    case REFMEM_APP_TABLE_BOARD_CAPABILITY:
        return sizeof(refmem_board_capability_table_t);
    case REFMEM_APP_TABLE_GENERIC_NODE:
        return sizeof(refmem_generic_node_table_t);
    case REFMEM_APP_TABLE_NODE_LOAD:
        return sizeof(refmem_node_load_table_t);
    case REFMEM_APP_TABLE_FB_INSTANCE:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_INSTANCE_COUNT,
                                          REFMEM_APP_TABLE_WIRE_FB_INSTANCE_WORDS);
    case REFMEM_APP_TABLE_EVENT_LINK:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_EVENT_LINK_COUNT,
                                          REFMEM_APP_TABLE_WIRE_EVENT_LINK_WORDS);
    case REFMEM_APP_TABLE_DATA_LINK:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_DATA_LINK_COUNT,
                                          REFMEM_APP_TABLE_WIRE_DATA_LINK_WORDS);
    case REFMEM_APP_TABLE_DEPLOYMENT_GATE:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT,
                                          REFMEM_APP_TABLE_WIRE_DEPLOYMENT_GATE_WORDS);
    case REFMEM_APP_TABLE_CONNECTION_QUALITY:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_QUALITY_COUNT,
                                          REFMEM_APP_TABLE_WIRE_CONNECTION_QUALITY_WORDS);
    case REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE:
        return TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE;
    default:
        return 0u;
    }
}

static bool refmem_model_serialize_wire_header(uint8_t *data,
                                               size_t size,
                                               size_t *cursor,
                                               uint32_t count)
{
    return refmem_model_write_u32_le(data, size, cursor, REFMEM_APP_MODEL_VERSION) &&
           refmem_model_write_u32_le(data, size, cursor, count);
}

static bool refmem_model_serialize_application_map(uint8_t *data,
                                                   size_t size,
                                                   size_t *cursor,
                                                   const refmem_application_map_t *table)
{
    return table != NULL &&
           refmem_model_write_u32_le(data, size, cursor, table->version) &&
           refmem_model_write_u32_le(data, size, cursor, table->application_id) &&
           refmem_model_write_u32_le(data, size, cursor, table->application_version) &&
           refmem_model_write_u32_le(data, size, cursor, table->profile_id) &&
           refmem_model_write_u32_le(data, size, cursor, table->layout_version) &&
           refmem_model_write_u32_le(data, size, cursor, table->target_node_mask);
}

static bool refmem_model_serialize_board_capability(
    uint8_t *data,
    size_t size,
    size_t *cursor,
    const refmem_board_capability_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->board_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT; i++) {
        const refmem_board_capability_entry_t *entry = &table->board[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->board_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->board_uuid_crc32) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->capability_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->io_constraint_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->ip_core_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->default_persona_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->hw_profile_crc32) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->active_default_slot) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->online_required)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_generic_node(uint8_t *data,
                                                size_t size,
                                                size_t *cursor,
                                                const refmem_generic_node_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->node_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        const refmem_app_node_entry_t *entry = &table->node[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->node_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->node_uuid_crc32) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->capability_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->claim_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->claim_priority) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->default_persona_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->hw_profile_crc32) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->online_required) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->fail_policy)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_node_load(uint8_t *data,
                                             size_t size,
                                             size_t *cursor,
                                             const refmem_node_load_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->load_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_NODE_LOAD_COUNT; i++) {
        const refmem_node_load_entry_t *entry = &table->load[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->load_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->application_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->profile_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->node_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->instance_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->role_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->persona_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->enabled) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->required) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->fail_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->load_order)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_fb_instance(uint8_t *data,
                                               size_t size,
                                               size_t *cursor,
                                               const refmem_fb_instance_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->instance_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_INSTANCE_COUNT; i++) {
        const refmem_fb_instance_entry_t *entry = &table->instance[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->instance_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->default_node_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->domain) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->ao_type) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->fb_type) ||
            !refmem_model_write_u32_le(data, size, cursor,
                                       refmem_model_rmtp_crc32_string(entry->instance_name)) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->version) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->enable_condition) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->resource_claim) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->io_claim) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->ip_core_claim) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->time_budget_us) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->state_region_ref) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->health_region_ref) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->event_first) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->event_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->data_first) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->data_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->conflict_class) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->restart_policy)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_event_link(uint8_t *data,
                                              size_t size,
                                              size_t *cursor,
                                              const refmem_event_link_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->event_link_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_EVENT_LINK_COUNT; i++) {
        const refmem_event_link_entry_t *entry = &table->link[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->event_link_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->source_instance) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->source_event) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->target_node_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->target_instance) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->target_event) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->transport) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->timeout_us) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->ack_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->retry_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->safety_class) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->evidence_region_ref)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_data_link(uint8_t *data,
                                             size_t size,
                                             size_t *cursor,
                                             const refmem_data_link_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->data_link_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_DATA_LINK_COUNT; i++) {
        const refmem_data_link_entry_t *entry = &table->link[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->data_link_id) ||
            !refmem_model_write_u32_le(data, size, cursor,
                                       refmem_model_rmtp_crc32_string(entry->region_path)) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->writer_instance) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reader_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->type) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->unit) ||
            !refmem_model_write_i32_le(data, size, cursor, entry->scale) ||
            !refmem_model_write_i32_le(data, size, cursor, entry->min_value) ||
            !refmem_model_write_i32_le(data, size, cursor, entry->max_value) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->lifecycle) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->snapshot_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->update_period_us) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->stale_window_us) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->crc_region_ref) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->permission)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_deployment_gate(
    uint8_t *data,
    size_t size,
    size_t *cursor,
    const refmem_deployment_gate_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->check_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT; i++) {
        const refmem_deployment_gate_entry_t *entry = &table->check[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->check_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->required) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->fail_action) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->last_state) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_code) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_instance) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_node) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_region_ref) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_evidence_index)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_connection_quality(
    uint8_t *data,
    size_t size,
    size_t *cursor,
    const refmem_connection_quality_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->quality_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_QUALITY_COUNT; i++) {
        const refmem_connection_quality_entry_t *entry = &table->quality[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->quality_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->scope) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->source_node) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->target_node) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->seq_expected) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->seq_last) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->crc_error_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->stale_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->late_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->drop_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->timeout_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->last_error) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->last_error_tick) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->p99) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->p999) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->evidence_index)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_tdma_foundation_profile(
    uint8_t *data,
    size_t size,
    size_t *cursor,
    const tdma_foundation_profile_t *profile)
{
    if (data == NULL || cursor == NULL || profile == NULL ||
        *cursor > size || size - *cursor < TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE ||
        !tdma_foundation_profile_encode_table(profile,
                                              &data[*cursor],
                                              TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE)) {
        return false;
    }
    *cursor += TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE;
    return true;
}

static bool refmem_model_serialize_candidate_table(uint32_t table_id,
                                                   uint8_t *data,
                                                   size_t size,
                                                   size_t *cursor)
{
    switch (table_id) {
    case REFMEM_APP_TABLE_APPLICATION_MAP:
        return refmem_model_serialize_application_map(data,
                                                      size,
                                                      cursor,
                                                      refmem_model_current_application_map());
    case REFMEM_APP_TABLE_BOARD_CAPABILITY:
        return refmem_model_serialize_board_capability(
            data,
            size,
            cursor,
            s_staging_board_capability_valid ? &s_staging_board_capability_table
                                             : refmem_model_current_board_capability_table());
    case REFMEM_APP_TABLE_GENERIC_NODE:
        return refmem_model_serialize_generic_node(data,
                                                   size,
                                                   cursor,
                                                   refmem_model_current_generic_node_table());
    case REFMEM_APP_TABLE_NODE_LOAD:
        return refmem_model_serialize_node_load(
            data,
            size,
            cursor,
            s_staging_node_load_valid ? &s_staging_node_load_table
                                      : refmem_model_current_node_load_table());
    case REFMEM_APP_TABLE_FB_INSTANCE:
        return refmem_model_serialize_fb_instance(data,
                                                  size,
                                                  cursor,
                                                  refmem_model_current_fb_instance_table());
    case REFMEM_APP_TABLE_EVENT_LINK:
        return refmem_model_serialize_event_link(data,
                                                 size,
                                                 cursor,
                                                 refmem_model_current_event_link_table());
    case REFMEM_APP_TABLE_DATA_LINK:
        return refmem_model_serialize_data_link(data,
                                                size,
                                                cursor,
                                                refmem_model_current_data_link_table());
    case REFMEM_APP_TABLE_DEPLOYMENT_GATE:
        return refmem_model_serialize_deployment_gate(
            data,
            size,
            cursor,
            refmem_model_current_deployment_gate_table());
    case REFMEM_APP_TABLE_CONNECTION_QUALITY:
        return refmem_model_serialize_connection_quality(
            data,
            size,
            cursor,
            refmem_model_current_connection_quality_table());
    case REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE:
        return refmem_model_serialize_tdma_foundation_profile(
            data,
            size,
            cursor,
            refmem_model_design_tdma_foundation_profile());
    default:
        return false;
    }
}

static bool refmem_model_build_inline_package_image(uint8_t *data,
                                                    size_t capacity,
                                                    size_t *package_size,
                                                    refmem_table_package_validation_t *validation)
{
    if (data == NULL || package_size == NULL || validation == NULL ||
        capacity < REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET) {
        return false;
    }

    const size_t table_dir_size = REFMEM_APP_INLINE_PACKAGE_DIR_SIZE;
    size_t total_size = REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET;
    for (uint32_t table_id = 0u; table_id < REFMEM_TABLE_REGISTRY_COUNT; table_id++) {
        const size_t table_size = refmem_model_table_payload_size(table_id);
        if (table_size == 0u ||
            total_size > capacity ||
            table_size > capacity - total_size) {
            return false;
        }
        total_size += table_size;
    }

    memset(data, 0, total_size);
    size_t cursor = 0u;
    if (!refmem_model_write_u32_le(data, total_size, &cursor, REFMEM_TABLE_PACKAGE_MAGIC) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, REFMEM_TABLE_PACKAGE_VERSION) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, REFMEM_TABLE_PACKAGE_HEADER_SIZE) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, (uint32_t)total_size) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, REFMEM_TABLE_REGISTRY_COUNT) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, (uint32_t)table_dir_size) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, 0u) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, 0u)) {
        return false;
    }

    size_t payload_cursor = REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET;
    for (uint32_t table_id = 0u; table_id < REFMEM_TABLE_REGISTRY_COUNT; table_id++) {
        const size_t table_offset = payload_cursor;
        const size_t table_size = refmem_model_table_payload_size(table_id);
        if (!refmem_model_serialize_candidate_table(table_id,
                                                    data,
                                                    total_size,
                                                    &payload_cursor) ||
            payload_cursor - table_offset != table_size) {
            return false;
        }

        size_t dir_cursor =
            REFMEM_TABLE_PACKAGE_HEADER_SIZE +
            table_id * REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE;
        const uint32_t table_crc = refmem_model_rmtp_crc32(&data[table_offset], table_size);
        if (!refmem_model_write_u32_le(data, total_size, &dir_cursor, table_id) ||
            !refmem_model_write_u32_le(data, total_size, &dir_cursor, (uint32_t)table_offset) ||
            !refmem_model_write_u32_le(data, total_size, &dir_cursor, (uint32_t)table_size) ||
            !refmem_model_write_u32_le(data, total_size, &dir_cursor, table_crc)) {
            return false;
        }
    }

    if (payload_cursor != total_size) {
        return false;
    }

    const uint32_t payload_crc =
        refmem_model_rmtp_crc32(&data[REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET],
                                total_size - REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET);
    cursor = 24u;
    if (!refmem_model_write_u32_le(data, total_size, &cursor, payload_crc)) {
        return false;
    }
    const uint32_t package_crc = refmem_model_rmtp_crc32_zero_field(data, total_size, 28u);
    cursor = 28u;
    if (!refmem_model_write_u32_le(data, total_size, &cursor, package_crc)) {
        return false;
    }

    if (!refmem_table_registry_validate_package(data, total_size, validation)) {
        return false;
    }

    *package_size = total_size;
    return true;
}

bool refmem_model_stage_inline_package_image(void)
{
    uint8_t *package_buffer = NULL;
    size_t package_capacity = 0u;
    if (!refmem_table_registry_begin_staging_write(
            REFMEM_TABLE_OWNER_REFMEM_AO, &package_buffer, &package_capacity)) {
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_BUSY;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        return false;
    }

    refmem_table_package_validation_t validation;
    memset(&validation, 0, sizeof(validation));
    size_t package_size = 0u;
    if (!refmem_model_build_inline_package_image(package_buffer,
                                                 package_capacity,
                                                 &package_size,
                                                 &validation)) {
        s_load_snapshot.staging_package_crc32 = 0u;
        s_load_snapshot.staging_lint_error_count = 1u;
        /* N5.2a.1 diagnostic: low byte = package validation error, next byte = first bad table. */
        s_load_snapshot.staging_first_lint_error =
            (uint32_t)validation.error |
            ((uint32_t)validation.first_bad_table << 8u);
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_PACKAGE_INVALID;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        if (validation.error != REFMEM_TABLE_PACKAGE_OK &&
            validation.first_bad_table < REFMEM_TABLE_REGISTRY_COUNT) {
            (void)refmem_table_registry_stage_table(validation.first_bad_table,
                                                    0u,
                                                    REFMEM_TABLE_VALIDATION_FAILED,
                                                    validation.error);
        } else {
            refmem_table_registry_refresh_staging(&s_load_snapshot);
        }
        (void)refmem_table_registry_end_staging_write(
            REFMEM_TABLE_OWNER_REFMEM_AO, false);
        return false;
    }

    s_load_snapshot.staging_package_crc32 = validation.package_crc32;
    s_load_snapshot.staging_lint_error_count = 0u;
    s_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_OK;
    s_load_snapshot.staging_state = REFMEM_APP_STAGING_VALIDATED;
    s_load_snapshot.last_error = REFMEM_APP_LOAD_OK;
    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    const bool staged = refmem_table_registry_stage_package_image(&s_load_snapshot,
                                                                   package_buffer,
                                                                   package_size,
                                                                   &validation);
    (void)refmem_table_registry_end_staging_write(
        REFMEM_TABLE_OWNER_REFMEM_AO, staged);
    return staged;
}

static bool refmem_model_node_load_enabled(const refmem_node_load_entry_t *load)
{
    return load != NULL && load->enabled != 0u;
}

static const refmem_app_node_entry_t *refmem_model_generic_node_by_id(uint32_t node_id)
{
    if (node_id >= s_generic_node_table.node_count) {
        return NULL;
    }
    return &s_generic_node_table.node[node_id];
}

static bool refmem_model_instance_is_loaded(uint32_t instance_id)
{
    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *load = &s_node_load_table.load[i];
        if (refmem_model_node_load_enabled(load) && load->instance_id == instance_id) {
            return true;
        }
    }
    return false;
}

bool refmem_model_validate_node_capabilities(void)
{
    refmem_slot_claim_map_t claim_map;
    if (!refmem_slot_claim_derive_map(&s_generic_node_table,
                                      &s_board_capability_table,
                                      &s_node_load_table,
                                      &s_fb_instance_table,
                                      &claim_map)) {
        return false;
    }

    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *load = &s_node_load_table.load[i];
        const refmem_fb_instance_entry_t *instance =
            refmem_model_instance_by_id(load->instance_id);
        const refmem_app_node_entry_t *node = refmem_model_generic_node_by_id(load->node_id);
        if (!refmem_model_node_load_enabled(load) ||
            !refmem_model_instance_enabled(instance) ||
            node == NULL) {
            continue;
        }

        refmem_realtime_contract_t contract;
        if (!refmem_realtime_contract_derive_from_claim_map(load,
                                                            instance,
                                                            node,
                                                            &claim_map,
                                                            &contract)) {
            return false;
        }
    }
    return true;
}

bool refmem_model_validate_slot_claim_policy(void)
{
    for (uint32_t i = 0u; i < s_generic_node_table.node_count; i++) {
        const refmem_app_node_entry_t *node = &s_generic_node_table.node[i];
        if (node->claim_policy > REFMEM_APP_CLAIM_DISABLED) {
            return false;
        }

        if (node->online_required != 0u &&
            node->claim_policy != REFMEM_APP_CLAIM_STRICT_UUID &&
            node->claim_policy != REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT) {
            return false;
        }

        if (node->claim_policy == REFMEM_APP_CLAIM_SPARE_DYNAMIC &&
            (node->online_required != 0u || node->fail_policy != REFMEM_APP_FAIL_REPORT_ONLY)) {
            return false;
        }

        if (node->claim_policy == REFMEM_APP_CLAIM_DISABLED &&
            (node->online_required != 0u || node->claim_priority != 0u)) {
            return false;
        }
    }

    return true;
}

bool refmem_model_validate_application_map(void)
{
    return refmem_application_contract_validate_application_map(&s_application_map);
}

bool refmem_model_validate_board_capability_table(void)
{
    return refmem_application_contract_validate_board_capability_table(
        &s_board_capability_table,
        REFMEM_APP_MODEL_NODE_COUNT);
}

bool refmem_model_validate_generic_node_table(void)
{
    return refmem_application_contract_validate_slot_substrate(
        &s_generic_node_table,
        &s_board_capability_table);
}

bool refmem_model_validate_node_load_table(void)
{
    if (!refmem_application_contract_validate_node_load_table(&s_node_load_table,
                                                              &s_application_map)) {
        return false;
    }

    uint32_t loaded_instance_mask = 0u;
    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *load = &s_node_load_table.load[i];
        if (!refmem_model_instance_exists(load->instance_id)) {
            return false;
        }

        if (!refmem_model_node_load_enabled(load)) {
            continue;
        }

        const uint32_t instance_bit = 1u << load->instance_id;
        if ((loaded_instance_mask & instance_bit) != 0u) {
            return false;
        }
        loaded_instance_mask |= instance_bit;
    }

    for (uint32_t i = 0u; i < s_fb_instance_table.instance_count; i++) {
        const refmem_fb_instance_entry_t *instance = &s_fb_instance_table.instance[i];
        if (refmem_model_instance_enabled(instance) &&
            !refmem_model_instance_is_loaded(instance->instance_id)) {
            return false;
        }
    }

    return true;
}

bool refmem_model_validate_instances(void)
{
    if (s_fb_instance_table.version != REFMEM_APP_MODEL_VERSION ||
        s_fb_instance_table.instance_count != REFMEM_APP_MODEL_INSTANCE_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_fb_instance_table.instance_count; i++) {
        const refmem_fb_instance_entry_t *instance = &s_fb_instance_table.instance[i];
        if (instance->instance_id != i ||
            instance->default_node_id >= REFMEM_APP_MODEL_NODE_COUNT ||
            instance->instance_name == NULL ||
            instance->domain > REFMEM_APP_DOMAIN_TDMA ||
            instance->state_region_ref >= REFMEM_VECTOR_REGION_COUNT ||
            instance->health_region_ref >= REFMEM_VECTOR_REGION_COUNT ||
            instance->event_first > s_event_link_table.event_link_count ||
            instance->event_count > (s_event_link_table.event_link_count - instance->event_first) ||
            instance->data_first > s_data_link_table.data_link_count ||
            instance->data_count > (s_data_link_table.data_link_count - instance->data_first)) {
            return false;
        }
    }
    return true;
}

bool refmem_model_validate_event_links(void)
{
    if (s_event_link_table.version != REFMEM_APP_MODEL_VERSION ||
        s_event_link_table.event_link_count != REFMEM_APP_MODEL_EVENT_LINK_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_event_link_table.event_link_count; i++) {
        const refmem_event_link_entry_t *link = &s_event_link_table.link[i];
        if (link->event_link_id != i ||
            !refmem_model_instance_exists(link->source_instance) ||
            !refmem_model_instance_exists(link->target_instance) ||
            (link->target_node_mask & ~s_application_map.target_node_mask) != 0u ||
            link->transport > REFMEM_APP_TRANSPORT_PIO_SPI ||
            link->ack_policy > REFMEM_APP_ACK_BITMAP ||
            link->evidence_region_ref >= REFMEM_VECTOR_REGION_COUNT) {
            return false;
        }
    }
    return true;
}

bool refmem_model_validate_data_links(void)
{
    if (s_data_link_table.version != REFMEM_APP_MODEL_VERSION ||
        s_data_link_table.data_link_count != REFMEM_APP_MODEL_DATA_LINK_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_data_link_table.data_link_count; i++) {
        const refmem_data_link_entry_t *link = &s_data_link_table.link[i];
        if (link->data_link_id != i ||
            link->region_path == NULL ||
            !refmem_model_instance_exists(link->writer_instance) ||
            (link->reader_mask & ~s_application_map.target_node_mask) != 0u ||
            link->type > REFMEM_APP_DATA_CRC ||
            link->unit > REFMEM_APP_UNIT_COUNT ||
            link->lifecycle > REFMEM_APP_LIFE_EVIDENCE ||
            link->snapshot_policy > REFMEM_APP_SNAPSHOT_EVIDENCE_REF ||
            link->crc_region_ref >= REFMEM_VECTOR_REGION_COUNT ||
            link->permission > REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE ||
            link->min_value > link->max_value) {
            return false;
        }
    }
    return true;
}

bool refmem_model_validate_gate_and_quality(void)
{
    if (s_deployment_gate.version != REFMEM_APP_MODEL_VERSION ||
        s_deployment_gate.check_count != REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT ||
        s_connection_quality.version != REFMEM_APP_MODEL_VERSION ||
        s_connection_quality.quality_count != REFMEM_APP_MODEL_QUALITY_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_deployment_gate.check_count; i++) {
        const refmem_deployment_gate_entry_t *check = &s_deployment_gate.check[i];
        if (check->check_id != i ||
            check->fail_action > REFMEM_APP_GATE_LATCH_FAULT ||
            check->last_state > REFMEM_APP_GATE_LATCH_FAULT ||
            check->reject_region_ref >= REFMEM_VECTOR_REGION_COUNT) {
            return false;
        }
    }

    for (uint32_t i = 0u; i < s_connection_quality.quality_count; i++) {
        const refmem_connection_quality_entry_t *quality = &s_connection_quality.quality[i];
        if (quality->quality_id != i ||
            quality->scope > REFMEM_APP_QUALITY_TRANSPORT_ADAPTER ||
            quality->source_node >= REFMEM_APP_MODEL_NODE_COUNT ||
            quality->target_node >= REFMEM_APP_MODEL_NODE_COUNT) {
            return false;
        }
    }

    refmem_slot_claim_map_t claim_map;
    refmem_slot_claim_gate_status_t claim_gate;
    if (!refmem_slot_claim_derive_map(&s_generic_node_table,
                                      &s_board_capability_table,
                                      &s_node_load_table,
                                      &s_fb_instance_table,
                                      &claim_map)) {
        return false;
    }
    if (!refmem_slot_claim_gate_evaluate(&claim_map, &claim_gate) ||
        claim_gate.ready == 0u) {
        return false;
    }
    return true;
}

void refmem_model_lint_note(bool ok,
                                   uint32_t error,
                                   uint32_t *error_count,
                                   uint32_t *first_error)
{
    if (ok) {
        return;
    }

    if (*error_count == 0u) {
        *first_error = error;
    }
    (*error_count)++;
}

bool refmem_model_instance_enabled(const refmem_fb_instance_entry_t *instance)
{
    return instance != NULL && instance->enable_condition != 0u;
}

bool refmem_model_validate_resource_claims(void)
{
    const uint32_t exclusive_resource_mask = REFMEM_APP_RESOURCE_FLASH |
                                            REFMEM_APP_RESOURCE_SD |
                                            REFMEM_APP_RESOURCE_USB |
                                            REFMEM_APP_RESOURCE_LCD |
                                            REFMEM_APP_RESOURCE_TDMA_SCHEDULER;

    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *left_load = &s_node_load_table.load[i];
        const refmem_fb_instance_entry_t *left =
            refmem_model_instance_by_id(left_load->instance_id);
        if (!refmem_model_node_load_enabled(left_load) ||
            !refmem_model_instance_enabled(left)) {
            continue;
        }

        for (uint32_t j = i + 1u; j < s_node_load_table.load_count; j++) {
            const refmem_node_load_entry_t *right_load = &s_node_load_table.load[j];
            const refmem_fb_instance_entry_t *right =
                refmem_model_instance_by_id(right_load->instance_id);
            if (!refmem_model_node_load_enabled(right_load) ||
                !refmem_model_instance_enabled(right) ||
                left_load->node_id != right_load->node_id) {
                continue;
            }

            if (left->conflict_class != 0u &&
                left->conflict_class == right->conflict_class) {
                return false;
            }

            if (((left->resource_claim & right->resource_claim) & exclusive_resource_mask) != 0u) {
                return false;
            }
        }
    }

    return true;
}

bool refmem_model_validate_tdma_profile_contract(
    const tdma_foundation_profile_t *profile,
    const refmem_generic_node_table_t *nodes,
    const refmem_board_capability_table_t *boards,
    const refmem_node_load_table_t *loads,
    const refmem_fb_instance_table_t *instances)
{
    tdma_profile_result_t profile_result = TDMA_PROFILE_BAD_ARGUMENT;
    if (!tdma_foundation_profile_validate(profile, &profile_result) ||
        nodes == NULL || boards == NULL || loads == NULL || instances == NULL ||
        profile->owner_instance_id >= instances->instance_count ||
        profile->ring.node_count > nodes->node_count ||
        profile->ring.local_index >= nodes->node_count) {
        return false;
    }

    const refmem_fb_instance_entry_t *owner =
        &instances->instance[profile->owner_instance_id];
    const uint32_t required_resource = REFMEM_APP_RESOURCE_DMA |
                                       REFMEM_APP_RESOURCE_CORE1_RT |
                                       REFMEM_APP_RESOURCE_TDMA_SCHEDULER |
        ((profile->resource.adapter_type == TDMA_ADAPTER_PIO_SPI ||
          profile->resource.adapter_type == TDMA_ADAPTER_BISS_C)
             ? REFMEM_APP_RESOURCE_PIO
             : 0u);
    if (owner->instance_id != profile->owner_instance_id ||
        owner->enable_condition == 0u ||
        owner->domain != REFMEM_APP_DOMAIN_TDMA ||
        owner->fb_type != REFMEM_APP_FB_TDMA_SCHEDULER ||
        (owner->resource_claim & required_resource) != required_resource ||
        profile->resource.io_claim_mask == 0u ||
        (profile->resource.ip_core_claim_mask & REFMEM_APP_IP_TDMA_SCHEDULER) == 0u ||
        (profile->resource.io_claim_mask & ~owner->io_claim) != 0u ||
        (profile->resource.ip_core_claim_mask & ~owner->ip_core_claim) != 0u) {
        return false;
    }

    uint32_t owner_count = 0u;
    const refmem_node_load_entry_t *owner_load = NULL;
    for (uint32_t i = 0u; i < loads->load_count; i++) {
        const refmem_node_load_entry_t *load = &loads->load[i];
        if (load->instance_id >= instances->instance_count || load->enabled == 0u) {
            continue;
        }
        const refmem_fb_instance_entry_t *instance = &instances->instance[load->instance_id];
        if (instance->domain == REFMEM_APP_DOMAIN_TDMA &&
            instance->fb_type == REFMEM_APP_FB_TDMA_SCHEDULER &&
            instance->enable_condition != 0u) {
            owner_count++;
            owner_load = load;
        }
        if (load->node_id == profile->ring.local_index &&
            load->instance_id != profile->owner_instance_id &&
            instance->enable_condition != 0u &&
            (instance->io_claim & profile->resource.io_claim_mask) != 0u) {
            return false;
        }
    }
    if (owner_count != 1u || owner_load == NULL ||
        owner_load->instance_id != profile->owner_instance_id ||
        owner_load->node_id != profile->ring.local_index) {
        return false;
    }

    refmem_slot_claim_map_t claim_map;
    refmem_realtime_contract_t realtime;
    return refmem_slot_claim_derive_map(nodes, boards, loads, instances, &claim_map) &&
           refmem_realtime_contract_derive_from_claim_map(
               owner_load,
               owner,
               &nodes->node[owner_load->node_id],
               &claim_map,
               &realtime) &&
           realtime.valid != 0u &&
           (profile->resource.io_claim_mask & ~realtime.target_io_constraint_mask) == 0u &&
           (profile->resource.ip_core_claim_mask & ~realtime.target_ip_core_mask) == 0u;
}

bool refmem_model_validate_tdma_owner(void)
{
    return refmem_model_validate_tdma_profile_contract(
        &s_tdma_foundation_profile,
        &s_generic_node_table,
        &s_board_capability_table,
        &s_node_load_table,
        &s_fb_instance_table);
}

bool refmem_model_validate_io_claims(void)
{
    const uint32_t exclusive_io_mask = REFMEM_APP_IO_LINK_CONTROL |
                                      REFMEM_APP_IO_BISS_C |
                                      REFMEM_APP_IO_UART_RS485 |
                                      REFMEM_APP_IO_PIO_SPI_SYNC |
                                      REFMEM_APP_IO_MODEL_SIGNAL_MASK;

    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *left_load = &s_node_load_table.load[i];
        const refmem_fb_instance_entry_t *left =
            refmem_model_instance_by_id(left_load->instance_id);
        if (!refmem_model_node_load_enabled(left_load) ||
            !refmem_model_instance_enabled(left)) {
            continue;
        }

        for (uint32_t j = i + 1u; j < s_node_load_table.load_count; j++) {
            const refmem_node_load_entry_t *right_load = &s_node_load_table.load[j];
            const refmem_fb_instance_entry_t *right =
                refmem_model_instance_by_id(right_load->instance_id);
            if (!refmem_model_node_load_enabled(right_load) ||
                !refmem_model_instance_enabled(right) ||
                left_load->node_id != right_load->node_id) {
                continue;
            }

            if (((left->io_claim & right->io_claim) & exclusive_io_mask) != 0u) {
                return false;
            }
        }
    }

    return true;
}

bool refmem_model_validate_unique_writers(void)
{
    for (uint32_t i = 0u; i < s_data_link_table.data_link_count; i++) {
        const refmem_data_link_entry_t *left = &s_data_link_table.link[i];
        for (uint32_t j = i + 1u; j < s_data_link_table.data_link_count; j++) {
            const refmem_data_link_entry_t *right = &s_data_link_table.link[j];
            if (strcmp(left->region_path, right->region_path) == 0 &&
                left->writer_instance != right->writer_instance) {
                return false;
            }
        }
    }

    return true;
}

bool refmem_model_validate_required_event_links(void)
{
    bool has_start = false;
    bool has_stop = false;
    bool has_fire_load = false;
    bool has_done = false;
    bool has_fault = false;

    for (uint32_t i = 0u; i < s_event_link_table.event_link_count; i++) {
        const refmem_event_link_entry_t *link = &s_event_link_table.link[i];
        has_start = has_start ||
                    (link->source_event == REFMEM_APP_EVENT_START &&
                     link->target_event == REFMEM_APP_EVENT_START);
        has_stop = has_stop ||
                   (link->source_event == REFMEM_APP_EVENT_STOP &&
                    link->target_event == REFMEM_APP_EVENT_STOP);
        has_fire_load = has_fire_load ||
                        (link->source_event == REFMEM_APP_EVENT_FIRE_LOAD &&
                         link->target_event == REFMEM_APP_EVENT_FIRE_LOAD);
        has_done = has_done ||
                   (link->source_event == REFMEM_APP_EVENT_DONE &&
                    link->target_event == REFMEM_APP_EVENT_DONE);
        has_fault = has_fault ||
                    (link->source_event == REFMEM_APP_EVENT_FAULT &&
                     link->target_event == REFMEM_APP_EVENT_FAULT);
    }

    return has_start && has_stop && has_fire_load && has_done && has_fault;
}
