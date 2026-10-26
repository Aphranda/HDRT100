#include "refmem_application_model_internal.h"

static bool refmem_model_validate_required_data_links(void)
{
    uint32_t region_mask = 0u;
    const uint32_t required_region_mask = (1u << REFMEM_VECTOR_REGION_SYSTEM) |
                                          (1u << REFMEM_VECTOR_REGION_ROLE) |
                                          (1u << REFMEM_VECTOR_REGION_VDC) |
                                          (1u << REFMEM_VECTOR_REGION_LOOP) |
                                          (1u << REFMEM_VECTOR_REGION_DPLL) |
                                          (1u << REFMEM_VECTOR_REGION_NODE) |
                                          (1u << REFMEM_VECTOR_REGION_TRIGGER) |
                                          (1u << REFMEM_VECTOR_REGION_IO) |
                                          (1u << REFMEM_VECTOR_REGION_CAL) |
                                          (1u << REFMEM_VECTOR_REGION_ACK_CMD) |
                                          (1u << REFMEM_VECTOR_REGION_GATEWAY);

    for (uint32_t i = 0u; i < s_data_link_table.data_link_count; i++) {
        const refmem_data_link_entry_t *link = &s_data_link_table.link[i];
        region_mask |= (1u << link->crc_region_ref);
    }

    return (region_mask & required_region_mask) == required_region_mask;
}

static void refmem_model_lint(uint32_t *error_count, uint32_t *first_error)
{
    *error_count = 0u;
    *first_error = REFMEM_APP_LINT_OK;

    refmem_model_lint_note(refmem_model_validate_application_map(),
                           REFMEM_APP_LINT_BAD_TABLE_VERSION,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_board_capability_table(),
                           REFMEM_APP_LINT_BAD_BOARD_CAPABILITY,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_generic_node_table(),
                           REFMEM_APP_LINT_BAD_NODE_RANGE,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_node_load_table(),
                           REFMEM_APP_LINT_BAD_NODE_RANGE,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_node_capabilities(),
                           REFMEM_APP_LINT_BAD_REALTIME_CONTRACT,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_slot_claim_policy(),
                           REFMEM_APP_LINT_BAD_SLOT_CLAIM,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_instances(),
                           REFMEM_APP_LINT_BAD_INSTANCE_RANGE,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_resource_claims(),
                           REFMEM_APP_LINT_RESOURCE_CONFLICT,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_tdma_owner(),
                           REFMEM_APP_LINT_BAD_TDMA_PROFILE,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_io_claims(),
                           REFMEM_APP_LINT_IO_CONFLICT,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_event_links(),
                           REFMEM_APP_LINT_BAD_EVENT_LINK,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_required_event_links(),
                           REFMEM_APP_LINT_BAD_EVENT_LINK,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_data_links(),
                           REFMEM_APP_LINT_BAD_DATA_LINK,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_unique_writers(),
                           REFMEM_APP_LINT_DUPLICATE_WRITER,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_required_data_links(),
                           REFMEM_APP_LINT_BAD_DATA_LINK,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_gate_and_quality(),
                           REFMEM_APP_LINT_BAD_GATE_OR_QUALITY,
                           error_count,
                           first_error);
}

void refmem_model_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0u) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t i = 0u;
    while (i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void refmem_model_finish_load_idle(void)
{
    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    refmem_table_registry_refresh_staging(&s_load_snapshot);
}

typedef bool (*refmem_model_parse_table_view_fn)(const uint8_t *data, size_t size, void *table);

static bool refmem_model_parse_image_view(refmem_table_image_role_t role,
                                          uint32_t table_id,
                                          refmem_model_parse_table_view_fn parser,
                                          void *table,
                                          uint32_t *table_crc32,
                                          uint32_t *package_crc32,
                                          uint32_t *table_seq,
                                          bool *context_set)
{
    if (parser == NULL ||
        table == NULL ||
        table_crc32 == NULL ||
        package_crc32 == NULL ||
        table_seq == NULL ||
        context_set == NULL) {
        return false;
    }

    refmem_table_view_t view;
    if (!refmem_table_registry_access_table(role, table_id, &view)) {
        return false;
    }

    bool ok = true;
    if (*context_set) {
        ok = view.package_crc32 == *package_crc32 && view.table_seq == *table_seq;
    } else {
        *package_crc32 = view.package_crc32;
        *table_seq = view.table_seq;
        *context_set = true;
    }

    if (ok) {
        ok = parser(view.data, view.size, table);
    }
    if (ok) {
        *table_crc32 = view.table_crc32;
    }

    const bool released = refmem_table_registry_release_table(&view);
    return ok && released;
}

static bool refmem_model_parse_image_views(refmem_table_image_role_t role,
                                           refmem_model_parsed_tables_t *parsed)
{
    if (parsed == NULL) {
        return false;
    }

    memset(parsed, 0, sizeof(*parsed));
    bool context_set = false;

    if (!refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_APPLICATION_MAP,
                                       refmem_model_parse_application_map_view,
                                       &parsed->application_map,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_APPLICATION_MAP],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_BOARD_CAPABILITY,
                                       refmem_model_parse_board_capability_view,
                                       &parsed->board_capability,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_BOARD_CAPABILITY],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_GENERIC_NODE,
                                       refmem_model_parse_generic_node_view,
                                       &parsed->generic_node,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_GENERIC_NODE],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_NODE_LOAD,
                                       refmem_model_parse_node_load_view,
                                       &parsed->node_load,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_NODE_LOAD],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_FB_INSTANCE,
                                       refmem_model_parse_fb_instance_view,
                                       &parsed->fb_instance,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_FB_INSTANCE],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_EVENT_LINK,
                                       refmem_model_parse_event_link_view,
                                       &parsed->event_link,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_EVENT_LINK],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_DATA_LINK,
                                       refmem_model_parse_data_link_view,
                                       &parsed->data_link,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_DATA_LINK],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_DEPLOYMENT_GATE,
                                       refmem_model_parse_deployment_gate_view,
                                       &parsed->deployment_gate,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_DEPLOYMENT_GATE],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_CONNECTION_QUALITY,
                                       refmem_model_parse_connection_quality_view,
                                       &parsed->connection_quality,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_CONNECTION_QUALITY],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(
            role,
            REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE,
            refmem_model_parse_tdma_foundation_profile_view,
            &parsed->tdma_foundation_profile,
            &parsed->table_crc32[REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE],
            &parsed->package_crc32,
            &parsed->table_seq,
                                       &context_set)) {
        return false;
    }

    return refmem_application_contract_validate_application_map(&parsed->application_map) &&
           refmem_application_contract_validate_slot_substrate(&parsed->generic_node,
                                                               &parsed->board_capability) &&
           refmem_application_contract_validate_node_load_table(&parsed->node_load,
                                                                 &parsed->application_map) &&
           refmem_model_validate_tdma_profile_contract(
               &parsed->tdma_foundation_profile,
               &parsed->generic_node,
               &parsed->board_capability,
               &parsed->node_load,
               &parsed->fb_instance);
}

static void refmem_model_apply_parsed_tables(const refmem_model_parsed_tables_t *parsed)
{
    if (parsed == NULL) {
        return;
    }

    (void)__atomic_add_fetch(&s_origin_model_epoch, 1u, __ATOMIC_ACQ_REL);
    s_active_application_map = parsed->application_map;
    s_active_board_capability_table = parsed->board_capability;
    s_active_generic_node_table = parsed->generic_node;
    s_active_node_load_table = parsed->node_load;
    s_active_fb_instance_table = parsed->fb_instance;
    s_active_event_link_table = parsed->event_link;
    s_active_data_link_table = parsed->data_link;
    s_active_deployment_gate = parsed->deployment_gate;
    s_active_connection_quality = parsed->connection_quality;
    s_active_tdma_foundation_profile = parsed->tdma_foundation_profile;
    s_design_tdma_foundation_profile = parsed->tdma_foundation_profile;
    s_active_tables_from_image = true;

    s_snapshot.version = REFMEM_APP_MODEL_VERSION;
    s_snapshot.valid = 1u;
    s_snapshot.target_node_mask = s_active_application_map.target_node_mask;
    s_snapshot.table_mask = REFMEM_APP_TABLE_MASK_ALL;
    s_snapshot.application_map_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_APPLICATION_MAP];
    s_snapshot.board_capability_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_BOARD_CAPABILITY];
    s_snapshot.generic_node_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_GENERIC_NODE];
    s_snapshot.node_load_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_NODE_LOAD];
    s_snapshot.fb_instance_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_FB_INSTANCE];
    s_snapshot.event_link_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_EVENT_LINK];
    s_snapshot.data_link_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_DATA_LINK];
    s_snapshot.deployment_gate_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_DEPLOYMENT_GATE];
    s_snapshot.connection_quality_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_CONNECTION_QUALITY];
    s_snapshot.tdma_foundation_profile_table_crc32 =
        parsed->table_crc32[REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE];
    s_snapshot.package_crc32 = parsed->package_crc32;
    s_snapshot.lint_error_count = 0u;
    s_snapshot.first_lint_error = REFMEM_APP_LINT_OK;
    s_load_snapshot.active_package_crc32 = parsed->package_crc32;
    s_board_load_snapshot.active_crc32 = s_snapshot.board_capability_crc32;
    (void)__atomic_add_fetch(&s_origin_model_epoch, 1u, __ATOMIC_RELEASE);
}

bool refmem_application_model_apply_active_table_views(void)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    refmem_model_parsed_tables_t parsed;
    if (!refmem_model_parse_image_views(REFMEM_TABLE_IMAGE_ACTIVE, &parsed)) {
        return false;
    }
    refmem_model_apply_parsed_tables(&parsed);
    return true;
}

bool refmem_application_model_prepare_staging_table_views(void)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    s_pending_tables_valid = false;
    if (!refmem_model_parse_image_views(REFMEM_TABLE_IMAGE_STAGING, &s_pending_tables)) {
        memset(&s_pending_tables, 0, sizeof(s_pending_tables));
        return false;
    }

    s_pending_tables_valid = true;
    return true;
}

bool refmem_application_model_commit_prepared_table_views(void)
{
    if (!s_pending_tables_valid) {
        return false;
    }

    refmem_model_apply_parsed_tables(&s_pending_tables);
    memset(&s_pending_tables, 0, sizeof(s_pending_tables));
    s_pending_tables_valid = false;
    return true;
}

void refmem_application_model_discard_prepared_table_views(void)
{
    memset(&s_pending_tables, 0, sizeof(s_pending_tables));
    s_pending_tables_valid = false;
}

bool refmem_application_model_get_prepared_tdma_foundation_profile(
    tdma_foundation_profile_t *profile)
{
    if (!s_pending_tables_valid || profile == NULL) {
        return false;
    }

    *profile = s_pending_tables.tdma_foundation_profile;
    return true;
}

static bool refmem_model_make_staging_node_load_table(
    uint32_t node_id,
    uint32_t instance_id,
    uint32_t role_mask,
    uint32_t persona_mask,
    uint32_t enabled,
    uint32_t required,
    uint32_t load_order,
    refmem_node_load_table_t *candidate,
    uint32_t *candidate_crc32)
{
    if (candidate == NULL || candidate_crc32 == NULL) {
        return false;
    }

    if (s_staging_node_load_valid) {
        *candidate = s_staging_node_load_table;
    } else {
        *candidate = *refmem_application_model_get_node_load_table();
    }

    refmem_node_load_entry_t *target = NULL;
    for (uint32_t i = 0u; i < candidate->load_count; i++) {
        refmem_node_load_entry_t *entry = &candidate->load[i];
        if (entry->instance_id == instance_id) {
            target = entry;
            break;
        }
    }

    if (target == NULL) {
        return false;
    }

    const refmem_application_map_t *application_map = refmem_model_current_application_map();
    target->application_id = application_map->application_id;
    target->profile_id = application_map->profile_id;
    target->node_id = node_id;
    target->role_mask = role_mask;
    target->persona_mask = persona_mask;
    target->enabled = enabled;
    target->required = required;
    target->load_order = load_order;

    if (!refmem_application_contract_validate_node_load_table(candidate, application_map)) {
        return false;
    }

    *candidate_crc32 = refmem_model_node_load_table_crc32(candidate);
    return *candidate_crc32 != 0u;
}

static bool refmem_model_make_staging_board_capability_table(
    const refmem_board_capability_entry_t *candidate_entry,
    refmem_board_capability_table_t *candidate,
    uint32_t *candidate_crc32)
{
    if (candidate_entry == NULL || candidate == NULL || candidate_crc32 == NULL ||
        candidate_entry->board_id >= REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT) {
        return false;
    }

    if (s_staging_board_capability_valid) {
        *candidate = s_staging_board_capability_table;
    } else {
        *candidate = *refmem_application_model_get_board_capability_table();
    }

    if (candidate_entry->board_id >= candidate->board_count) {
        return false;
    }

    candidate->board[candidate_entry->board_id] = *candidate_entry;
    if (!refmem_application_contract_validate_board_capability_table(
            candidate,
            refmem_application_model_get_generic_node_table()->node_count)) {
        return false;
    }

    *candidate_crc32 = refmem_model_rmtp_crc32(candidate, sizeof(*candidate));
    return *candidate_crc32 != 0u;
}

bool refmem_application_model_validate(void)
{
    uint32_t error_count;
    uint32_t first_error;
    refmem_model_lint(&error_count, &first_error);
    return error_count == 0u;
}

bool refmem_application_model_init(void)
{
    (void)__atomic_add_fetch(&s_origin_model_epoch, 1u, __ATOMIC_ACQ_REL);
    s_active_tables_from_image = false;
    refmem_application_model_discard_prepared_table_views();

    if (!tdma_foundation_profile_default(&s_tdma_foundation_profile,
                                         REFMEM_APP_INSTANCE_TDMA_FOUNDATION,
                                         0u,
                                         0u,
                                         TDMA_ADAPTER_PIO_SPI)) {
        s_snapshot.valid = 0u;
        (void)__atomic_add_fetch(&s_origin_model_epoch, 1u, __ATOMIC_RELEASE);
        return false;
    }
    s_tdma_foundation_profile.resource.io_claim_mask =
        REFMEM_APP_IO_PIO_SPI_SYNC;
    s_tdma_foundation_profile.resource.ip_core_claim_mask =
        REFMEM_APP_IP_PIO_SPI_SYNC_DELTA | REFMEM_APP_IP_TDMA_SCHEDULER;
    s_tdma_foundation_profile.profile_crc32 =
        tdma_foundation_profile_crc32(&s_tdma_foundation_profile);
    s_design_tdma_foundation_profile = s_tdma_foundation_profile;

    s_snapshot.version = REFMEM_APP_MODEL_VERSION;
    s_snapshot.target_node_mask = s_application_map.target_node_mask;
    s_snapshot.table_mask = REFMEM_APP_TABLE_MASK_ALL;
    s_snapshot.application_map_crc32 = refmem_model_application_map_crc32();
    s_snapshot.board_capability_crc32 = refmem_model_board_capability_crc32();
    s_snapshot.generic_node_crc32 = refmem_model_generic_node_crc32();
    s_snapshot.node_load_crc32 = refmem_model_node_load_crc32();
    s_snapshot.fb_instance_crc32 = refmem_model_fb_instance_crc32();
    s_snapshot.event_link_crc32 = refmem_model_event_link_crc32();
    s_snapshot.data_link_crc32 = refmem_model_data_link_crc32();
    s_snapshot.deployment_gate_crc32 = refmem_model_deployment_gate_crc32();
    s_snapshot.connection_quality_crc32 = refmem_model_connection_quality_crc32();
    s_snapshot.tdma_foundation_profile_table_crc32 =
        refmem_model_tdma_foundation_profile_table_crc32();

    const uint32_t package_fields[] = {
        s_snapshot.version,
        s_snapshot.target_node_mask,
        s_snapshot.table_mask,
        s_snapshot.application_map_crc32,
        s_snapshot.board_capability_crc32,
        s_snapshot.generic_node_crc32,
        s_snapshot.node_load_crc32,
        s_snapshot.fb_instance_crc32,
        s_snapshot.event_link_crc32,
        s_snapshot.data_link_crc32,
        s_snapshot.deployment_gate_crc32,
        s_snapshot.connection_quality_crc32,
        s_snapshot.tdma_foundation_profile_table_crc32,
    };
    s_snapshot.package_crc32 =
        refmem_model_crc32_fields(package_fields,
                                  sizeof(package_fields) / sizeof(package_fields[0]));
    refmem_model_lint(&s_snapshot.lint_error_count, &s_snapshot.first_lint_error);
    s_snapshot.valid = s_snapshot.lint_error_count == 0u ? 1u : 0u;

    memset(&s_load_snapshot, 0, sizeof(s_load_snapshot));
    s_load_snapshot.version = REFMEM_APP_MODEL_VERSION;
    s_load_snapshot.source = REFMEM_APP_LOAD_SOURCE_DEFAULT;
    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    s_load_snapshot.staging_state = REFMEM_APP_STAGING_EMPTY;
    s_load_snapshot.active_package_crc32 = s_snapshot.package_crc32;
    s_load_snapshot.last_error = REFMEM_APP_LOAD_OK;

    memset(&s_board_load_snapshot, 0, sizeof(s_board_load_snapshot));
    s_board_load_snapshot.version = REFMEM_APP_MODEL_VERSION;
    s_board_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    s_board_load_snapshot.staging_state = REFMEM_APP_STAGING_EMPTY;
    s_board_load_snapshot.active_crc32 = s_snapshot.board_capability_crc32;
    s_board_load_snapshot.last_error = REFMEM_APP_LOAD_OK;

    memset(&s_staging_node_load_table, 0, sizeof(s_staging_node_load_table));
    memset(&s_staging_board_capability_table, 0, sizeof(s_staging_board_capability_table));
    s_staging_node_load_valid = false;
    s_staging_board_capability_valid = false;
    s_initialized = true;
    refmem_table_registry_init(&s_snapshot);
    (void)__atomic_add_fetch(&s_origin_model_epoch, 1u, __ATOMIC_RELEASE);
    return s_snapshot.valid != 0u;
}

bool refmem_application_model_stage_sd_system_pack(const char *path,
                                                   uint32_t path_hash,
                                                   uint32_t manifest_status,
                                                   uint32_t manifest_schema,
                                                   uint32_t manifest_required_count,
                                                   uint32_t manifest_missing_count,
                                                   const char *manifest_build_id,
                                                   uint32_t package_crc32,
                                                   uint32_t package_valid,
                                                   uint32_t package_error)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    if (s_load_snapshot.mode != REFMEM_APP_MODEL_MODE_IDLE) {
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_BAD_ARGUMENT;
        return false;
    }

    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_LOAD_TO_STAGING;
    s_load_snapshot.load_seq++;
    s_load_snapshot.source = REFMEM_APP_LOAD_SOURCE_SD_SYSTEM_PACK;
    s_load_snapshot.manifest_status = manifest_status;
    s_load_snapshot.manifest_schema = manifest_schema;
    s_load_snapshot.manifest_required_count = manifest_required_count;
    s_load_snapshot.manifest_missing_count = manifest_missing_count;
    s_load_snapshot.path_hash = path_hash;
    s_load_snapshot.active_package_crc32 = s_snapshot.package_crc32;
    refmem_model_copy_text(s_load_snapshot.path, sizeof(s_load_snapshot.path), path);
    refmem_model_copy_text(s_load_snapshot.manifest_build_id,
                           sizeof(s_load_snapshot.manifest_build_id),
                           manifest_build_id);

    if (manifest_status != REFMEM_APP_MODEL_SD_MANIFEST_OK || manifest_missing_count != 0u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.staging_package_crc32 = 0u;
        s_load_snapshot.staging_lint_error_count = 1u;
        s_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_BAD_TABLE_VERSION;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_MANIFEST_NOT_OK;
        refmem_model_finish_load_idle();
        return false;
    }

    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_VALIDATING;
    s_load_snapshot.staging_package_crc32 = package_crc32;
    s_load_snapshot.staging_lint_error_count = s_snapshot.lint_error_count;
    s_load_snapshot.staging_first_lint_error = package_error;
    s_load_snapshot.staging_node_id = 0u;
    s_load_snapshot.staging_instance_id = 0u;
    s_load_snapshot.staging_role_mask = 0u;
    s_load_snapshot.staging_persona_mask = 0u;
    s_load_snapshot.staging_enabled = 0u;
    s_load_snapshot.staging_required = 0u;
    s_load_snapshot.staging_load_order = 0u;
    if (package_valid == 0u || package_crc32 == 0u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_PACKAGE_INVALID;
        refmem_model_finish_load_idle();
        return false;
    }

    if (s_snapshot.valid == 0u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.staging_first_lint_error = s_snapshot.first_lint_error;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_LINT_FAILED;
        refmem_model_finish_load_idle();
        return false;
    }

    s_load_snapshot.staging_state = REFMEM_APP_STAGING_VALIDATED;
    s_load_snapshot.last_error = REFMEM_APP_LOAD_OK;
    refmem_model_finish_load_idle();
    (void)refmem_table_registry_validate_staging(&s_load_snapshot);
    return true;
}

bool refmem_application_model_stage_scpi_node_config(uint32_t node_id,
                                                     uint32_t instance_id,
                                                     uint32_t role_mask,
                                                     uint32_t persona_mask,
                                                     uint32_t enabled,
                                                     uint32_t required,
                                                     uint32_t load_order)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    if (s_load_snapshot.mode != REFMEM_APP_MODEL_MODE_IDLE) {
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_BAD_ARGUMENT;
        return false;
    }

    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_LOAD_TO_STAGING;
    s_load_snapshot.load_seq++;
    s_load_snapshot.source = REFMEM_APP_LOAD_SOURCE_SCPI_INLINE;
    s_load_snapshot.manifest_status = 0u;
    s_load_snapshot.manifest_schema = 0u;
    s_load_snapshot.manifest_required_count = 0u;
    s_load_snapshot.manifest_missing_count = 0u;
    s_load_snapshot.path_hash = 0u;
    s_load_snapshot.active_package_crc32 = s_snapshot.package_crc32;
    s_load_snapshot.staging_node_id = node_id;
    s_load_snapshot.staging_instance_id = instance_id;
    s_load_snapshot.staging_role_mask = role_mask;
    s_load_snapshot.staging_persona_mask = persona_mask;
    s_load_snapshot.staging_enabled = enabled;
    s_load_snapshot.staging_required = required;
    s_load_snapshot.staging_load_order = load_order;
    s_load_snapshot.manifest_build_id[0] = '\0';
    s_load_snapshot.path[0] = '\0';

    if (node_id >= REFMEM_APP_MODEL_NODE_COUNT) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.staging_package_crc32 = 0u;
        s_load_snapshot.staging_lint_error_count = 1u;
        s_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_BAD_NODE_RANGE;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_NODE_RANGE;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_NODE_LOAD,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                s_load_snapshot.staging_first_lint_error);
        return false;
    }

    if (!refmem_model_instance_exists(instance_id) ||
        enabled > 1u ||
        required > 1u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.staging_package_crc32 = 0u;
        s_load_snapshot.staging_lint_error_count = 1u;
        s_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_BAD_INSTANCE_RANGE;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_INSTANCE_RANGE;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_NODE_LOAD,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                s_load_snapshot.staging_first_lint_error);
        return false;
    }

    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_VALIDATING;
    refmem_node_load_table_t candidate;
    uint32_t candidate_crc32 = 0u;
    const bool candidate_valid =
        refmem_model_make_staging_node_load_table(node_id,
                                                  instance_id,
                                                  role_mask,
                                                  persona_mask,
                                                  enabled,
                                                  required,
                                                  load_order,
                                                  &candidate,
                                                  &candidate_crc32);
    s_load_snapshot.staging_lint_error_count =
        (s_snapshot.lint_error_count == 0u && candidate_valid) ? 0u : 1u;
    s_load_snapshot.staging_first_lint_error =
        candidate_valid ? s_snapshot.first_lint_error : REFMEM_APP_LINT_BAD_INSTANCE_RANGE;
    s_load_snapshot.staging_package_crc32 = 0u;
    if (s_snapshot.valid == 0u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_LINT_FAILED;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_NODE_LOAD,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                s_load_snapshot.staging_first_lint_error);
        return false;
    }
    if (!candidate_valid) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_LINT_FAILED;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_NODE_LOAD,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                s_load_snapshot.staging_first_lint_error);
        return false;
    }

    s_staging_node_load_table = candidate;
    s_staging_node_load_valid = true;
    return refmem_model_stage_inline_package_image();
}

bool refmem_application_model_stage_scpi_board_capability(uint32_t board_id,
                                                          uint32_t board_uuid_crc32,
                                                          uint32_t capability_mask,
                                                          uint32_t io_constraint_mask,
                                                          uint32_t ip_core_mask,
                                                          uint32_t default_persona_mask,
                                                          uint32_t hw_profile_crc32,
                                                          uint32_t active_default_slot,
                                                          uint32_t online_required)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    if (s_load_snapshot.mode != REFMEM_APP_MODEL_MODE_IDLE ||
        s_board_load_snapshot.mode != REFMEM_APP_MODEL_MODE_IDLE) {
        s_board_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_BAD_ARGUMENT;
        return false;
    }

    refmem_board_capability_entry_t candidate = {
        board_id,
        board_uuid_crc32,
        capability_mask,
        io_constraint_mask,
        ip_core_mask,
        default_persona_mask,
        hw_profile_crc32,
        active_default_slot,
        online_required,
    };

    s_board_load_snapshot.mode = REFMEM_APP_MODEL_MODE_LOAD_TO_STAGING;
    s_board_load_snapshot.load_seq++;
    s_board_load_snapshot.active_crc32 = s_snapshot.board_capability_crc32;
    s_board_load_snapshot.staging_crc32 = 0u;
    s_board_load_snapshot.staging_board_id = board_id;
    s_board_load_snapshot.staging_board_uuid_crc32 = board_uuid_crc32;
    s_board_load_snapshot.staging_capability_mask = capability_mask;
    s_board_load_snapshot.staging_io_constraint_mask = io_constraint_mask;
    s_board_load_snapshot.staging_ip_core_mask = ip_core_mask;
    s_board_load_snapshot.staging_default_persona_mask = default_persona_mask;
    s_board_load_snapshot.staging_hw_profile_crc32 = hw_profile_crc32;
    s_board_load_snapshot.staging_active_default_slot = active_default_slot;
    s_board_load_snapshot.staging_online_required = online_required;

    bool valid = true;
    uint32_t first_error = REFMEM_APP_LINT_OK;
    if (board_id >= REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT ||
        board_uuid_crc32 == 0u ||
        active_default_slot >= REFMEM_APP_MODEL_NODE_COUNT ||
        online_required > 1u) {
        valid = false;
        first_error = REFMEM_APP_LINT_BAD_BOARD_CAPABILITY;
    }

    if (valid &&
        (capability_mask & REFMEM_APP_CAP_BASELINE) != REFMEM_APP_CAP_BASELINE) {
        valid = false;
        first_error = REFMEM_APP_LINT_BAD_BOARD_CAPABILITY;
    }

    const uint32_t io_capability =
        refmem_realtime_contract_io_capability_mask(io_constraint_mask);
    const uint32_t ip_capability =
        refmem_realtime_contract_ip_capability_mask(ip_core_mask);
    if (valid && (((io_capability | ip_capability) & ~capability_mask) != 0u)) {
        valid = false;
        first_error = REFMEM_APP_LINT_BAD_BOARD_CAPABILITY;
    }

    s_board_load_snapshot.mode = REFMEM_APP_MODEL_MODE_VALIDATING;
    refmem_board_capability_table_t candidate_table;
    uint32_t candidate_crc32 = 0u;
    if (valid &&
        !refmem_model_make_staging_board_capability_table(&candidate,
                                                          &candidate_table,
                                                          &candidate_crc32)) {
        valid = false;
        first_error = REFMEM_APP_LINT_BAD_BOARD_CAPABILITY;
    }

    s_board_load_snapshot.staging_lint_error_count = valid ? 0u : 1u;
    s_board_load_snapshot.staging_first_lint_error = first_error;
    s_board_load_snapshot.staging_crc32 = valid ? candidate_crc32 : 0u;
    s_board_load_snapshot.last_error = valid ? REFMEM_APP_LOAD_OK : REFMEM_APP_LOAD_ERR_LINT_FAILED;
    s_board_load_snapshot.staging_state =
        valid ? REFMEM_APP_STAGING_VALIDATED : REFMEM_APP_STAGING_FAILED;
    if (valid) {
        s_staging_board_capability_table = candidate_table;
        s_staging_board_capability_valid = true;
    }

    if (valid) {
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_VALIDATING;
        s_load_snapshot.load_seq++;
        s_load_snapshot.source = REFMEM_APP_LOAD_SOURCE_SCPI_INLINE;
        s_load_snapshot.manifest_status = 0u;
        s_load_snapshot.manifest_schema = 0u;
        s_load_snapshot.manifest_required_count = 0u;
        s_load_snapshot.manifest_missing_count = 0u;
        s_load_snapshot.path_hash = 0u;
        s_load_snapshot.active_package_crc32 = s_snapshot.package_crc32;
        s_load_snapshot.staging_node_id = 0u;
        s_load_snapshot.staging_instance_id = 0u;
        s_load_snapshot.staging_role_mask = 0u;
        s_load_snapshot.staging_persona_mask = 0u;
        s_load_snapshot.staging_enabled = 0u;
        s_load_snapshot.staging_required = 0u;
        s_load_snapshot.staging_load_order = 0u;
        s_load_snapshot.manifest_build_id[0] = '\0';
        s_load_snapshot.path[0] = '\0';
        valid = refmem_model_stage_inline_package_image();
        s_board_load_snapshot.last_error =
            valid ? REFMEM_APP_LOAD_OK : REFMEM_APP_LOAD_ERR_PACKAGE_INVALID;
        s_board_load_snapshot.staging_state =
            valid ? REFMEM_APP_STAGING_VALIDATED : REFMEM_APP_STAGING_FAILED;
        if (!valid) {
            s_board_load_snapshot.staging_lint_error_count = 1u;
            s_board_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_BAD_TABLE_VERSION;
        }
    } else {
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_BOARD_CAPABILITY,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                first_error);
    }
    s_board_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    return valid;
}

const refmem_application_map_t *refmem_application_model_get_application_map(void)
{
    return s_active_tables_from_image ? &s_active_application_map : &s_application_map;
}

const refmem_board_capability_table_t *refmem_application_model_get_board_capability_table(void)
{
    return s_active_tables_from_image ? &s_active_board_capability_table : &s_board_capability_table;
}

const refmem_generic_node_table_t *refmem_application_model_get_generic_node_table(void)
{
    return s_active_tables_from_image ? &s_active_generic_node_table : &s_generic_node_table;
}

const refmem_node_load_table_t *refmem_application_model_get_node_load_table(void)
{
    return s_active_tables_from_image ? &s_active_node_load_table : &s_node_load_table;
}

const refmem_fb_instance_table_t *refmem_application_model_get_fb_instance_table(void)
{
    return s_active_tables_from_image ? &s_active_fb_instance_table : &s_fb_instance_table;
}

const refmem_event_link_table_t *refmem_application_model_get_event_link_table(void)
{
    return s_active_tables_from_image ? &s_active_event_link_table : &s_event_link_table;
}

const refmem_data_link_table_t *refmem_application_model_get_data_link_table(void)
{
    return s_active_tables_from_image ? &s_active_data_link_table : &s_data_link_table;
}

const refmem_deployment_gate_table_t *refmem_application_model_get_deployment_gate(void)
{
    return s_active_tables_from_image ? &s_active_deployment_gate : &s_deployment_gate;
}

const refmem_connection_quality_table_t *refmem_application_model_get_connection_quality(void)
{
    return s_active_tables_from_image ? &s_active_connection_quality : &s_connection_quality;
}

const tdma_foundation_profile_t *refmem_application_model_get_tdma_foundation_profile(void)
{
    return refmem_model_current_tdma_foundation_profile();
}

/* N5.2b: derive the control owner from the active SlotClaimMap.  The fallback is
 * the lowest claimed slot, which every node computes identically from its local
 * snapshot; no second authority state is kept. */
bool refmem_application_model_control_owner_slot(uint32_t *owner_slot)
{
    if (owner_slot == NULL) {
        return false;
    }
    refmem_slot_claim_map_t map;
    if (!refmem_slot_claim_derive_map(refmem_model_current_generic_node_table(),
                                      refmem_model_current_board_capability_table(),
                                      refmem_model_current_node_load_table(),
                                      refmem_model_current_fb_instance_table(),
                                      &map)) {
        return false;
    }
    return refmem_slot_claim_lowest_claimed(&map, owner_slot);
}

bool refmem_application_model_set_tdma_ring_local_slot(uint32_t local_slot_id)
{
    const tdma_foundation_profile_t *profile =
        refmem_model_current_tdma_foundation_profile();
    return refmem_application_model_set_tdma_ring_topology(
        local_slot_id, profile->ring.reference_index, profile->ring.node_count);
}

bool refmem_application_model_set_tdma_ring_topology(uint32_t local_slot_id,
                                                     uint32_t reference_slot_id,
                                                     uint32_t node_count)
{
    tdma_foundation_profile_t *profile =
        s_active_tables_from_image ? &s_active_tdma_foundation_profile
                                   : &s_tdma_foundation_profile;
    if (node_count < 2u || node_count > TDMA_RING_NODE_MAX ||
        local_slot_id >= node_count || reference_slot_id >= node_count) {
        return false;
    }
    if (local_slot_id == profile->ring.local_index &&
        reference_slot_id == profile->ring.reference_index &&
        node_count == profile->ring.node_count) {
        return true;
    }
    tdma_foundation_profile_t updated = *profile;
    if (!tdma_ring_profile_default(&updated.ring,
                                   local_slot_id,
                                   reference_slot_id,
                                   node_count)) {
        return false;
    }
    updated.profile_crc32 = tdma_foundation_profile_crc32(&updated);
    (void)__atomic_add_fetch(&s_origin_model_epoch, 1u, __ATOMIC_ACQ_REL);
    *profile = updated;
    (void)__atomic_add_fetch(&s_origin_model_epoch, 1u, __ATOMIC_RELEASE);
    return true;
}

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
__attribute__((section(".time_critical.refmem_origin_model_epoch")))
#endif
uint32_t refmem_realtime_contract_origin_model_epoch(void)
{
    return __atomic_load_n(&s_origin_model_epoch, __ATOMIC_ACQUIRE);
}

bool refmem_realtime_contract_admit_origin_trial(
    const refmem_realtime_origin_capability_t *capability,
    uint32_t foundation_crc32, refmem_realtime_origin_admission_t *admission)
{
    if (admission == NULL) return false;
    memset(admission, 0, sizeof(*admission));
    if (capability == NULL || !s_initialized || !s_snapshot.valid ||
        (capability->features & REFMEM_RT_ORIGIN_REQUIRED_FEATURES) !=
            REFMEM_RT_ORIGIN_REQUIRED_FEATURES ||
        capability->persona == 0u || capability->clk_sys_hz == 0u ||
        capability->physical_bytes == 0u || capability->resource_mask == 0u ||
        capability->dma_mask == 0u || (capability->dma_mask & (1u << 7u)) != 0u ||
        capability->tx_pio == 0u || capability->rx_pio == 0u ||
        capability->tx_pio == capability->rx_pio) return false;
    const uint32_t epoch = refmem_realtime_contract_origin_model_epoch();
    if ((epoch & 1u) != 0u || epoch == 0u) return false;
    const tdma_foundation_profile_t *profile = refmem_model_current_tdma_foundation_profile();
    const refmem_generic_node_table_t *nodes = refmem_model_current_generic_node_table();
    const refmem_board_capability_table_t *boards = refmem_model_current_board_capability_table();
    const refmem_node_load_table_t *loads = refmem_model_current_node_load_table();
    const refmem_fb_instance_table_t *instances = refmem_model_current_fb_instance_table();
    const refmem_deployment_gate_table_t *gate = refmem_model_current_deployment_gate_table();
    if (profile->profile_crc32 != foundation_crc32 ||
        profile->ring.local_index != profile->ring.reference_index ||
        !refmem_model_validate_tdma_profile_contract(profile, nodes, boards, loads, instances) ||
        gate->version != REFMEM_APP_MODEL_VERSION ||
        gate->check_count != REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT) return false;
    uint32_t gate_mask = 0u;
    for (uint32_t i = 0u; i < gate->check_count; ++i) {
        const refmem_deployment_gate_entry_t *check = &gate->check[i];
        if (check->check_id >= REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT ||
            (gate_mask & (1u << check->check_id)) != 0u || check->required == 0u ||
            check->last_state != REFMEM_APP_GATE_PASS || check->reject_code != 0u)
            return false;
        gate_mask |= 1u << check->check_id;
    }
    refmem_slot_claim_map_t map;
    if (!refmem_slot_claim_derive_map(nodes, boards, loads, instances, &map)) return false;
    const refmem_slot_claim_assignment_t *assignment =
        refmem_slot_claim_find_assignment(&map, profile->ring.local_index);
    if (assignment == NULL || assignment->claim_state != REFMEM_SLOT_CLAIM_CLAIMED)
        return false;
    /* N5.2b GATE_CONFIG: a RUN must have a derivable control owner inside the
     * ring.  Without it no system-level command may be issued, so admission
     * fails closed instead of running a ring with no authority. */
    uint32_t control_owner = 0u;
    if (!refmem_slot_claim_lowest_claimed(&map, &control_owner) ||
        control_owner >= profile->ring.node_count)
        return false;
    if (epoch != refmem_realtime_contract_origin_model_epoch()) return false;
    *admission = (refmem_realtime_origin_admission_t){
        .model_epoch = epoch, .foundation_crc32 = foundation_crc32,
        .deployment_crc32 = s_snapshot.deployment_gate_crc32,
        .owner_instance = profile->owner_instance_id, .node_id = profile->ring.local_index,
        .board_id = assignment->board_id, .gate_mask = gate_mask,
        .diagnostic_valid = 1u, .product_valid = 0u,
        /* No product timing record is implemented by this experimental API. */
        .product_reject_mask = capability->product_reject_mask | REFMEM_RT_ORIGIN_PRODUCT_TIMING,
        .capability = *capability};
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (epoch != refmem_realtime_contract_origin_model_epoch()) {
        memset(admission, 0, sizeof(*admission));
        return false;
    }
    return true;
}

const refmem_application_model_snapshot_t *refmem_application_model_get_snapshot(void)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }
    return &s_snapshot;
}

bool refmem_application_model_get_staging_node_load_entry(
    uint32_t instance_id,
    refmem_node_load_entry_t *entry)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }
    if (entry == NULL || !s_staging_node_load_valid) {
        return false;
    }

    for (uint32_t i = 0u; i < s_staging_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *candidate = &s_staging_node_load_table.load[i];
        if (candidate->instance_id == instance_id) {
            *entry = *candidate;
            return true;
        }
    }
    return false;
}

void refmem_application_model_get_load_snapshot(refmem_application_model_load_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }
    *snapshot = s_load_snapshot;
}

void refmem_application_model_get_board_load_snapshot(refmem_board_capability_load_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }
    *snapshot = s_board_load_snapshot;
}
