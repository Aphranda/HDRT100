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
static uint32_t s_table_seq;

static const uint32_t s_table_image_size[REFMEM_TABLE_REGISTRY_COUNT] = {
    sizeof(refmem_application_map_t),
    sizeof(refmem_board_capability_table_t),
    sizeof(refmem_generic_node_table_t),
    sizeof(refmem_node_load_table_t),
    sizeof(refmem_fb_instance_table_t),
    sizeof(refmem_event_link_table_t),
    sizeof(refmem_data_link_table_t),
    sizeof(refmem_deployment_gate_table_t),
    sizeof(refmem_connection_quality_table_t),
};

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
    descriptor->evidence_index = REFMEM_VECTOR_SLOT_STATS;
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
        s_registry[i].evidence_index = REFMEM_VECTOR_SLOT_STATS;
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
                            REFMEM_TABLE_FLAG_CRC_OK |
                            REFMEM_TABLE_FLAG_OWNER_OK;
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
    s_active_image.evidence_index = REFMEM_VECTOR_SLOT_STATS;

    s_snapshot.last_error = model->first_lint_error;
    refmem_table_registry_refresh_snapshot();
}

void refmem_table_registry_refresh_staging(const refmem_application_model_load_snapshot_t *load)
{
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
    s_staging_image.evidence_index = REFMEM_VECTOR_SLOT_STATS;

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
    s_staging_image.evidence_index = REFMEM_VECTOR_SLOT_STATS;

    s_snapshot.last_error = validation->error;
    refmem_table_registry_refresh_snapshot();
    return true;
}

bool refmem_table_registry_stage_table(uint32_t table_id,
                                       uint32_t staging_crc32,
                                       uint32_t validation_state,
                                       uint32_t last_result)
{
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
    s_staging_image.evidence_index = REFMEM_VECTOR_SLOT_STATS;

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

    s_staging_image.state = REFMEM_TABLE_VALIDATION_OWNER_OK;
    s_staging_image.last_result = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED;
    s_snapshot.last_error = REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED;
    refmem_table_registry_refresh_snapshot();
    return false;
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
                result.owner_validated_table_mask =
                    (1u << REFMEM_APP_TABLE_APPLICATION_MAP) |
                    (1u << REFMEM_APP_TABLE_BOARD_CAPABILITY) |
                    (1u << REFMEM_APP_TABLE_GENERIC_NODE) |
                    (1u << REFMEM_APP_TABLE_NODE_LOAD);
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
