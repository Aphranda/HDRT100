#include "refmem_table_registry.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static refmem_application_model_snapshot_t make_active_model(void)
{
    refmem_application_model_snapshot_t model;
    (void)memset(&model, 0, sizeof(model));
    model.version = REFMEM_APP_MODEL_VERSION;
    model.valid = 1u;
    model.table_mask = REFMEM_APP_TABLE_MASK_ALL;
    model.application_map_crc32 = 0x10000001u;
    model.board_capability_crc32 = 0x10000002u;
    model.generic_node_crc32 = 0x10000003u;
    model.node_load_crc32 = 0x10000004u;
    model.fb_instance_crc32 = 0x10000005u;
    model.event_link_crc32 = 0x10000006u;
    model.data_link_crc32 = 0x10000007u;
    model.deployment_gate_crc32 = 0x10000008u;
    model.connection_quality_crc32 = 0x10000009u;
    model.package_crc32 = 0xA5A50001u;
    return model;
}

static refmem_application_model_load_snapshot_t make_valid_load(void)
{
    refmem_application_model_load_snapshot_t load;
    (void)memset(&load, 0, sizeof(load));
    load.version = REFMEM_APP_MODEL_VERSION;
    load.source = REFMEM_APP_LOAD_SOURCE_SD_SYSTEM_PACK;
    load.mode = REFMEM_APP_MODEL_MODE_IDLE;
    load.staging_state = REFMEM_APP_STAGING_VALIDATED;
    load.path_hash = 0x12345678u;
    load.active_package_crc32 = 0xA5A50001u;
    load.staging_package_crc32 = 0xA5A50002u;
    load.last_error = REFMEM_APP_LOAD_OK;
    return load;
}

static refmem_table_activation_gate_t make_pass_gate(void)
{
    refmem_table_activation_gate_t gate;
    (void)memset(&gate, 0, sizeof(gate));
    gate.refmem_idle = 1u;
    gate.realtime_idle = 1u;
    gate.flash_safe = 1u;
    gate.crc_ok = 1u;
    gate.owner_ok = 1u;
    gate.slot_claim_ok = 1u;
    gate.deployment_gate_ok = 1u;
    gate.command_ack_ok = 1u;
    return gate;
}

static int test_init_sets_active_descriptor(void)
{
    int failed = 0;
    const refmem_application_model_snapshot_t model = make_active_model();
    refmem_table_image_descriptor_t active;
    refmem_table_registry_snapshot_t snapshot;

    refmem_table_registry_init(&model);
    failed += expect_bool("get active descriptor",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_ACTIVE,
                                                                     &active),
                          true);
    failed += expect_u32("active state", active.state, REFMEM_TABLE_VALIDATION_ACTIVE);
    failed += expect_u32("active mask", active.table_mask, REFMEM_APP_TABLE_MASK_ALL);
    failed += expect_u32("active package", active.package_crc32, model.package_crc32);
    failed += expect_u32("active seq", active.table_seq, 1u);

    refmem_table_registry_get_snapshot(&snapshot);
    failed += expect_u32("snapshot active mask",
                         snapshot.active_table_mask,
                         REFMEM_APP_TABLE_MASK_ALL);
    failed += expect_u32("snapshot staging mask", snapshot.staging_table_mask, 0u);
    return failed;
}

static int test_failed_activation_preserves_active(void)
{
    int failed = 0;
    const refmem_application_model_snapshot_t model = make_active_model();
    refmem_application_model_load_snapshot_t load = make_valid_load();
    refmem_table_activation_gate_t gate = make_pass_gate();
    refmem_table_image_descriptor_t active_before;
    refmem_table_image_descriptor_t active_after;
    refmem_table_image_descriptor_t staging_after;

    refmem_table_registry_init(&model);
    failed += expect_bool("validate staging",
                          refmem_table_registry_validate_staging(&load),
                          true);
    failed += expect_bool("get active before",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_ACTIVE,
                                                                     &active_before),
                          true);

    gate.slot_claim_ok = 0u;
    failed += expect_bool("activation gate rejects",
                          refmem_table_registry_activate_staging(&gate),
                          false);
    failed += expect_bool("get active after",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_ACTIVE,
                                                                     &active_after),
                          true);
    failed += expect_bool("get staging after",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_STAGING,
                                                                     &staging_after),
                          true);
    failed += expect_u32("active unchanged crc",
                         active_after.package_crc32,
                         active_before.package_crc32);
    failed += expect_u32("active unchanged seq", active_after.table_seq, active_before.table_seq);
    failed += expect_u32("staging still owner ok",
                         staging_after.state,
                         REFMEM_TABLE_VALIDATION_OWNER_OK);
    failed += expect_u32("staging gate result",
                         staging_after.last_result,
                         REFMEM_TABLE_ACTIVATE_ERR_GATE);
    return failed;
}

static int test_success_activation_moves_images(void)
{
    int failed = 0;
    const refmem_application_model_snapshot_t model = make_active_model();
    refmem_application_model_load_snapshot_t load = make_valid_load();
    refmem_table_activation_gate_t gate = make_pass_gate();
    refmem_table_image_descriptor_t active;
    refmem_table_image_descriptor_t staging;
    refmem_table_image_descriptor_t rollbackable;
    refmem_table_registry_entry_t entry;
    refmem_table_registry_snapshot_t snapshot;

    refmem_table_registry_init(&model);
    failed += expect_bool("validate staging",
                          refmem_table_registry_validate_staging(&load),
                          true);
    failed += expect_bool("activate staging",
                          refmem_table_registry_activate_staging(&gate),
                          true);

    failed += expect_bool("get active",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_ACTIVE,
                                                                     &active),
                          true);
    failed += expect_bool("get staging",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_STAGING,
                                                                     &staging),
                          true);
    failed += expect_bool("get rollbackable",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_ROLLBACKABLE,
                                                                     &rollbackable),
                          true);
    failed += expect_u32("active package", active.package_crc32, load.staging_package_crc32);
    failed += expect_u32("active state", active.state, REFMEM_TABLE_VALIDATION_ACTIVE);
    failed += expect_u32("active seq advanced", active.table_seq, 2u);
    failed += expect_u32("staging cleared", staging.state, REFMEM_TABLE_VALIDATION_EMPTY);
    failed += expect_u32("rollback state",
                         rollbackable.state,
                         REFMEM_TABLE_VALIDATION_ROLLBACKABLE);
    failed += expect_u32("rollback package", rollbackable.package_crc32, model.package_crc32);

    failed += expect_bool("get table entry",
                          refmem_table_registry_get_entry(REFMEM_APP_TABLE_NODE_LOAD, &entry),
                          true);
    failed += expect_u32("entry active crc", entry.active_crc32, load.staging_package_crc32);
    failed += expect_u32("entry state", entry.validation_state, REFMEM_TABLE_VALIDATION_ACTIVE);
    failed += expect_u32("entry flags rollback",
                         entry.flags & REFMEM_TABLE_FLAG_ROLLBACKABLE,
                         REFMEM_TABLE_FLAG_ROLLBACKABLE);

    refmem_table_registry_get_snapshot(&snapshot);
    failed += expect_u32("snapshot staging cleared", snapshot.staging_table_mask, 0u);
    return failed;
}

static int test_invalid_staging_is_not_activated(void)
{
    int failed = 0;
    const refmem_application_model_snapshot_t model = make_active_model();
    refmem_application_model_load_snapshot_t load = make_valid_load();
    refmem_table_activation_gate_t gate = make_pass_gate();
    refmem_table_image_descriptor_t active;

    refmem_table_registry_init(&model);
    load.staging_package_crc32 = 0u;
    load.last_error = REFMEM_APP_LOAD_ERR_PACKAGE_INVALID;
    failed += expect_bool("invalid staging rejected",
                          refmem_table_registry_validate_staging(&load),
                          false);
    failed += expect_bool("activate invalid staging",
                          refmem_table_registry_activate_staging(&gate),
                          false);
    failed += expect_bool("get active",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_ACTIVE,
                                                                     &active),
                          true);
    failed += expect_u32("active still old package", active.package_crc32, model.package_crc32);
    return failed;
}

int main(void)
{
    int failed = 0;

    failed += test_init_sets_active_descriptor();
    failed += test_failed_activation_preserves_active();
    failed += test_success_activation_moves_images();
    failed += test_invalid_staging_is_not_activated();

    if (failed != 0) {
        (void)printf("refmem_table_registry tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_table_registry tests passed\n");
    return 0;
}
