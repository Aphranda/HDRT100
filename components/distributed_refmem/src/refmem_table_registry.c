#include "refmem_table_registry.h"

#include <stddef.h>
#include <string.h>

#include "refmem_vector_table.h"

static refmem_table_registry_entry_t s_registry[REFMEM_TABLE_REGISTRY_COUNT];
static refmem_table_registry_snapshot_t s_snapshot;

static const uint32_t s_table_image_size[REFMEM_TABLE_REGISTRY_COUNT] = {
    sizeof(refmem_application_map_t),
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

void refmem_table_registry_init(const refmem_application_model_snapshot_t *model)
{
    memset(s_registry, 0, sizeof(s_registry));
    memset(&s_snapshot, 0, sizeof(s_snapshot));

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
        } else if (load->staging_state == REFMEM_APP_STAGING_VALIDATED) {
            entry->validation_state = REFMEM_TABLE_VALIDATION_OWNER_OK;
            if (staging_crc32 != 0u) {
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
