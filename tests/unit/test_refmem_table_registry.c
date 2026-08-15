#include "refmem_table_registry.h"
#include "refmem_vector_table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TEST_REFMEM_TABLE_PACKAGE_CAPACITY 8192u

uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0u; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

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

static uint32_t test_crc32(const uint8_t *data, size_t size)
{
    return ota_crc32_update(0xFFFFFFFFu, data, size) ^ 0xFFFFFFFFu;
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8) & 0xFFu);
    data[2] = (uint8_t)((value >> 16) & 0xFFu);
    data[3] = (uint8_t)((value >> 24) & 0xFFu);
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

static refmem_application_map_t make_valid_application_map(void)
{
    refmem_application_map_t map;
    (void)memset(&map, 0, sizeof(map));
    map.version = REFMEM_APP_MODEL_VERSION;
    map.application_id = 1u;
    map.application_version = 1u;
    map.profile_id = 1u;
    map.layout_version = DISTRIBUTED_REFMEM_LAYOUT_VERSION;
    map.target_node_mask = (1u << REFMEM_APP_MODEL_NODE_COUNT) - 1u;
    return map;
}

static void make_valid_board_table(refmem_board_capability_table_t *table)
{
    (void)memset(table, 0, sizeof(*table));
    table->version = REFMEM_APP_MODEL_VERSION;
    table->board_count = REFMEM_APP_MODEL_NODE_COUNT;
    for (uint32_t i = 0u; i < table->board_count; i++) {
        table->board[i].board_id = i;
        table->board[i].board_uuid_crc32 = 0xB0000000u + i;
        table->board[i].capability_mask = REFMEM_APP_CAP_BASELINE |
                                          REFMEM_APP_CAP_PIO |
                                          REFMEM_APP_CAP_DMA |
                                          REFMEM_APP_CAP_CORE1_RT;
        table->board[i].io_constraint_mask = REFMEM_APP_IO_PIO_SPI_SYNC;
        table->board[i].ip_core_mask = REFMEM_APP_IP_PIO_SPI_SYNC_DELTA;
        table->board[i].default_persona_mask = REFMEM_APP_PERSONA_SPARE;
        table->board[i].active_default_slot = i;
        table->board[i].online_required = 1u;
    }
}

static void make_valid_generic_node_table(refmem_generic_node_table_t *table)
{
    (void)memset(table, 0, sizeof(*table));
    table->version = REFMEM_APP_MODEL_VERSION;
    table->node_count = REFMEM_APP_MODEL_NODE_COUNT;
    for (uint32_t i = 0u; i < table->node_count; i++) {
        table->node[i].node_id = i;
        table->node[i].node_uuid_crc32 = 0xA0000000u + i;
        table->node[i].capability_mask = REFMEM_APP_CAP_BASELINE |
                                         REFMEM_APP_CAP_PIO |
                                         REFMEM_APP_CAP_DMA |
                                         REFMEM_APP_CAP_CORE1_RT;
        table->node[i].claim_policy = REFMEM_APP_CLAIM_STRICT_UUID;
        table->node[i].claim_priority = 100u - i;
        table->node[i].default_persona_mask = REFMEM_APP_PERSONA_SPARE;
        table->node[i].online_required = 1u;
        table->node[i].fail_policy = REFMEM_APP_FAIL_STOP;
    }
}

static void make_valid_node_load_table(refmem_node_load_table_t *table)
{
    static const refmem_node_load_entry_t rows[REFMEM_APP_MODEL_NODE_LOAD_COUNT] = {
        {0u, 1u, 1u, 0u, 0u, REFMEM_APP_ROLE_BOARD,
         REFMEM_APP_PERSONA_TRIGGER_MASTER, 1u, 1u, REFMEM_APP_FAIL_STOP, 0u},
        {1u, 1u, 1u, 0u, 1u, REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_TRIGGER_MASTER, 1u, 1u, REFMEM_APP_FAIL_STOP, 1u},
        {2u, 1u, 1u, 0u, 2u, REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_TRIGGER_MASTER, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 2u},
        {3u, 1u, 1u, 0u, 3u, REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_TRIGGER_MASTER, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 3u},
        {4u, 1u, 1u, 1u, 4u, REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_DISTRIBUTED_TRIGGER, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 0u},
        {5u, 1u, 1u, 2u, 5u, REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_LINK_SWITCHER,
         REFMEM_APP_PERSONA_LINK_CONTROL, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 0u},
        {6u, 1u, 1u, 3u, 6u, REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_GATEWAY,
         REFMEM_APP_PERSONA_GATEWAY, 1u, 1u, REFMEM_APP_FAIL_STOP, 0u},
        {7u, 1u, 1u, 3u, 7u, REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER | REFMEM_APP_ROLE_GATEWAY,
         REFMEM_APP_PERSONA_GATEWAY, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 1u},
        {8u, 1u, 1u, 3u, 8u, REFMEM_APP_ROLE_GATEWAY,
         REFMEM_APP_PERSONA_GATEWAY, 1u, 1u, REFMEM_APP_FAIL_STOP, 2u},
        {9u, 1u, 1u, 4u, 9u, REFMEM_APP_ROLE_MODEL_VNA | REFMEM_APP_ROLE_TEST_AGENT,
         REFMEM_APP_PERSONA_MODEL_INSTRUMENTS, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 0u},
        {10u, 1u, 1u, 4u, 10u, REFMEM_APP_ROLE_MODEL_TURNTABLE | REFMEM_APP_ROLE_TEST_AGENT,
         REFMEM_APP_PERSONA_MODEL_INSTRUMENTS, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 1u},
    };

    (void)memset(table, 0, sizeof(*table));
    table->version = REFMEM_APP_MODEL_VERSION;
    table->load_count = REFMEM_APP_MODEL_NODE_LOAD_COUNT;
    (void)memcpy(table->load, rows, sizeof(rows));
}

static void append_repeated_u32(uint8_t *payload, size_t *payload_size, const uint32_t *fields, size_t count)
{
    for (size_t i = 0u; i < count; i++) {
        write_u32_le(&payload[*payload_size], fields[i]);
        *payload_size += sizeof(uint32_t);
    }
}

static void append_valid_fb_instance_table(uint8_t *payload, size_t *payload_size)
{
    const uint32_t header[] = {REFMEM_APP_MODEL_VERSION, REFMEM_APP_MODEL_INSTANCE_COUNT};
    append_repeated_u32(payload, payload_size, header, 2u);
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_INSTANCE_COUNT; i++) {
        const uint32_t row[] = {
            i, i % REFMEM_APP_MODEL_NODE_COUNT, REFMEM_APP_DOMAIN_SYSTEM,
            REFMEM_APP_FB_SYSTEM_AO, REFMEM_APP_FB_SYSTEM_AO, 0x90000000u + i,
            1u, i < 2u ? 1u : 0u, 0u, 0u, 0u, 1000u,
            REFMEM_VECTOR_SLOT_SYSTEM, REFMEM_VECTOR_SLOT_STATS,
            0u, 0u, 0u, 0u, 0u, 1u,
        };
        append_repeated_u32(payload, payload_size, row, 20u);
    }
}

static void append_valid_event_link_table(uint8_t *payload, size_t *payload_size)
{
    const uint32_t header[] = {REFMEM_APP_MODEL_VERSION, REFMEM_APP_MODEL_EVENT_LINK_COUNT};
    append_repeated_u32(payload, payload_size, header, 2u);
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_EVENT_LINK_COUNT; i++) {
        const uint32_t row[] = {
            i, 0u, REFMEM_APP_EVENT_START, 1u, 0u, REFMEM_APP_EVENT_START,
            REFMEM_APP_TRANSPORT_COMMAND_SLOT, 1000u, REFMEM_APP_ACK_NONE,
            0u, 0u, REFMEM_VECTOR_SLOT_ACK_CMD,
        };
        append_repeated_u32(payload, payload_size, row, 12u);
    }
}

static void append_valid_data_link_table(uint8_t *payload, size_t *payload_size)
{
    const uint32_t header[] = {REFMEM_APP_MODEL_VERSION, REFMEM_APP_MODEL_DATA_LINK_COUNT};
    append_repeated_u32(payload, payload_size, header, 2u);
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_DATA_LINK_COUNT; i++) {
        const uint32_t row[] = {
            i, 0x91000000u + i, 0u, 1u, REFMEM_APP_DATA_U32, REFMEM_APP_UNIT_COUNT,
            1u, 0u, 1000u, REFMEM_APP_LIFE_ACTIVE, REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC,
            1000u, 10000u, REFMEM_VECTOR_SLOT_SYSTEM, REFMEM_APP_PERMISSION_READ_ONLY,
        };
        append_repeated_u32(payload, payload_size, row, 15u);
    }
}

static void append_valid_deployment_gate_table(uint8_t *payload, size_t *payload_size)
{
    const uint32_t header[] = {REFMEM_APP_MODEL_VERSION, REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT};
    append_repeated_u32(payload, payload_size, header, 2u);
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT; i++) {
        const uint32_t row[] = {
            i, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS,
            0u, 0u, 0u, REFMEM_VECTOR_SLOT_SYSTEM, 0u,
        };
        append_repeated_u32(payload, payload_size, row, 9u);
    }
}

static void append_valid_connection_quality_table(uint8_t *payload, size_t *payload_size)
{
    const uint32_t header[] = {REFMEM_APP_MODEL_VERSION, REFMEM_APP_MODEL_QUALITY_COUNT};
    append_repeated_u32(payload, payload_size, header, 2u);
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_QUALITY_COUNT; i++) {
        const uint32_t row[] = {
            i, REFMEM_APP_QUALITY_NODE, i % REFMEM_APP_MODEL_NODE_COUNT,
            i % REFMEM_APP_MODEL_NODE_COUNT, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
            0u, 0u, 0u, 0u, i,
        };
        append_repeated_u32(payload, payload_size, row, 16u);
    }
}

static size_t build_test_package(uint8_t *package,
                                 size_t package_capacity,
                                 bool fixed_contract_tables)
{
    const size_t dir_size = REFMEM_TABLE_REGISTRY_COUNT *
                            REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE;
    uint32_t table_offset[REFMEM_TABLE_REGISTRY_COUNT];
    uint32_t table_size[REFMEM_TABLE_REGISTRY_COUNT];
    uint8_t payload[TEST_REFMEM_TABLE_PACKAGE_CAPACITY];
    size_t payload_size = 0u;

    for (uint32_t table_id = 0u; table_id < REFMEM_TABLE_REGISTRY_COUNT; table_id++) {
        table_offset[table_id] = (uint32_t)(REFMEM_TABLE_PACKAGE_HEADER_SIZE +
                                            dir_size +
                                            payload_size);
        if (fixed_contract_tables && table_id == REFMEM_APP_TABLE_APPLICATION_MAP) {
            const refmem_application_map_t table = make_valid_application_map();
            table_size[table_id] = sizeof(table);
            (void)memcpy(&payload[payload_size], &table, sizeof(table));
            payload_size += sizeof(table);
        } else if (fixed_contract_tables && table_id == REFMEM_APP_TABLE_BOARD_CAPABILITY) {
            refmem_board_capability_table_t table;
            make_valid_board_table(&table);
            table_size[table_id] = sizeof(table);
            (void)memcpy(&payload[payload_size], &table, sizeof(table));
            payload_size += sizeof(table);
        } else if (fixed_contract_tables && table_id == REFMEM_APP_TABLE_GENERIC_NODE) {
            refmem_generic_node_table_t table;
            make_valid_generic_node_table(&table);
            table_size[table_id] = sizeof(table);
            (void)memcpy(&payload[payload_size], &table, sizeof(table));
            payload_size += sizeof(table);
        } else if (fixed_contract_tables && table_id == REFMEM_APP_TABLE_NODE_LOAD) {
            refmem_node_load_table_t table;
            make_valid_node_load_table(&table);
            table_size[table_id] = sizeof(table);
            (void)memcpy(&payload[payload_size], &table, sizeof(table));
            payload_size += sizeof(table);
        } else if (fixed_contract_tables && table_id == REFMEM_APP_TABLE_FB_INSTANCE) {
            const size_t before = payload_size;
            append_valid_fb_instance_table(payload, &payload_size);
            table_size[table_id] = (uint32_t)(payload_size - before);
        } else if (fixed_contract_tables && table_id == REFMEM_APP_TABLE_EVENT_LINK) {
            const size_t before = payload_size;
            append_valid_event_link_table(payload, &payload_size);
            table_size[table_id] = (uint32_t)(payload_size - before);
        } else if (fixed_contract_tables && table_id == REFMEM_APP_TABLE_DATA_LINK) {
            const size_t before = payload_size;
            append_valid_data_link_table(payload, &payload_size);
            table_size[table_id] = (uint32_t)(payload_size - before);
        } else if (fixed_contract_tables && table_id == REFMEM_APP_TABLE_DEPLOYMENT_GATE) {
            const size_t before = payload_size;
            append_valid_deployment_gate_table(payload, &payload_size);
            table_size[table_id] = (uint32_t)(payload_size - before);
        } else if (fixed_contract_tables && table_id == REFMEM_APP_TABLE_CONNECTION_QUALITY) {
            const size_t before = payload_size;
            append_valid_connection_quality_table(payload, &payload_size);
            table_size[table_id] = (uint32_t)(payload_size - before);
        } else {
            table_size[table_id] = 64u;
            (void)memset(&payload[payload_size], 0, 64u);
            payload[payload_size] = (uint8_t)('A' + table_id);
            payload_size += 64u;
        }
    }

    const size_t total_size = REFMEM_TABLE_PACKAGE_HEADER_SIZE + dir_size + payload_size;
    if (package == NULL || package_capacity < total_size) {
        return 0u;
    }

    (void)memset(package, 0, total_size);
    write_u32_le(&package[0], REFMEM_TABLE_PACKAGE_MAGIC);
    write_u32_le(&package[4], REFMEM_TABLE_PACKAGE_VERSION);
    write_u32_le(&package[8], REFMEM_TABLE_PACKAGE_HEADER_SIZE);
    write_u32_le(&package[12], (uint32_t)total_size);
    write_u32_le(&package[16], REFMEM_TABLE_REGISTRY_COUNT);
    write_u32_le(&package[20], (uint32_t)dir_size);
    write_u32_le(&package[24], test_crc32(payload, payload_size));

    for (uint32_t table_id = 0u; table_id < REFMEM_TABLE_REGISTRY_COUNT; table_id++) {
        const size_t entry = REFMEM_TABLE_PACKAGE_HEADER_SIZE +
                             table_id * REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE;
        write_u32_le(&package[entry + 0u], table_id);
        write_u32_le(&package[entry + 4u], table_offset[table_id]);
        write_u32_le(&package[entry + 8u], table_size[table_id]);
        write_u32_le(&package[entry + 12u],
                     test_crc32(&payload[table_offset[table_id] -
                                         REFMEM_TABLE_PACKAGE_HEADER_SIZE -
                                         dir_size],
                                table_size[table_id]));
    }

    (void)memcpy(&package[REFMEM_TABLE_PACKAGE_HEADER_SIZE + dir_size],
                 payload,
                 payload_size);
    write_u32_le(&package[28], test_crc32(package, total_size));
    return total_size;
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

static int test_activation_requires_real_table_image(void)
{
    int failed = 0;
    const refmem_application_model_snapshot_t model = make_active_model();
    refmem_application_model_load_snapshot_t load = make_valid_load();
    refmem_table_activation_gate_t gate = make_pass_gate();
    refmem_table_image_descriptor_t active_before;
    refmem_table_image_descriptor_t active;
    refmem_table_image_descriptor_t staging;
    refmem_table_image_descriptor_t rollbackable;
    refmem_table_registry_entry_t entry;
    refmem_table_registry_entry_t entry_before;
    refmem_table_registry_snapshot_t snapshot;

    refmem_table_registry_init(&model);
    failed += expect_bool("validate staging",
                          refmem_table_registry_validate_staging(&load),
                          true);
    failed += expect_bool("get active before",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_ACTIVE,
                                                                     &active_before),
                          true);
    failed += expect_bool("get table entry before",
                          refmem_table_registry_get_entry(REFMEM_APP_TABLE_NODE_LOAD,
                                                          &entry_before),
                          true);
    failed += expect_bool("activate staging",
                          refmem_table_registry_activate_staging(&gate),
                          false);

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
    failed += expect_u32("active package unchanged",
                         active.package_crc32,
                         active_before.package_crc32);
    failed += expect_u32("active state", active.state, REFMEM_TABLE_VALIDATION_ACTIVE);
    failed += expect_u32("active seq unchanged", active.table_seq, active_before.table_seq);
    failed += expect_u32("staging retained", staging.state, REFMEM_TABLE_VALIDATION_OWNER_OK);
    failed += expect_u32("staging image missing result",
                         staging.last_result,
                         REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED);
    failed += expect_u32("rollback state",
                         rollbackable.state,
                         REFMEM_TABLE_VALIDATION_EMPTY);
    failed += expect_u32("rollback package", rollbackable.package_crc32, 0u);

    failed += expect_bool("get table entry",
                          refmem_table_registry_get_entry(REFMEM_APP_TABLE_NODE_LOAD, &entry),
                          true);
    failed += expect_u32("entry active crc unchanged",
                         entry.active_crc32,
                         entry_before.active_crc32);
    failed += expect_u32("entry state still owner ok",
                         entry.validation_state,
                         REFMEM_TABLE_VALIDATION_OWNER_OK);
    failed += expect_u32("entry flags no rollback",
                         entry.flags & REFMEM_TABLE_FLAG_ROLLBACKABLE,
                         0u);

    refmem_table_registry_get_snapshot(&snapshot);
    failed += expect_u32("snapshot staging retained",
                         snapshot.staging_table_mask,
                         REFMEM_APP_TABLE_MASK_ALL);
    failed += expect_u32("snapshot last error",
                         snapshot.last_error,
                         REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED);
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

static int test_package_owner_validation_rejects_placeholder_tables(void)
{
    int failed = 0;
    uint8_t package[TEST_REFMEM_TABLE_PACKAGE_CAPACITY];
    refmem_table_package_validation_t validation;
    const size_t package_size = build_test_package(package, sizeof(package), false);

    failed += expect_bool("placeholder package validates",
                          refmem_table_registry_validate_package(package,
                                                                 package_size,
                                                                 &validation),
                          false);
    failed += expect_u32("placeholder owner error",
                         validation.error,
                         REFMEM_TABLE_PACKAGE_ERR_OWNER_VALIDATION);
    failed += expect_u32("placeholder first bad table",
                         validation.first_bad_table,
                         REFMEM_APP_TABLE_APPLICATION_MAP);
    return failed;
}

static int test_package_owner_validation_accepts_contract_tables(void)
{
    int failed = 0;
    uint8_t package[TEST_REFMEM_TABLE_PACKAGE_CAPACITY];
    refmem_table_package_validation_t validation;
    const size_t package_size = build_test_package(package, sizeof(package), true);

    failed += expect_bool("contract package validates",
                          refmem_table_registry_validate_package(package,
                                                                 package_size,
                                                                 &validation),
                          true);
    failed += expect_u32("contract package error",
                         validation.error,
                         REFMEM_TABLE_PACKAGE_OK);
    failed += expect_u32("contract package table mask",
                         validation.table_mask,
                         REFMEM_APP_TABLE_MASK_ALL);
    failed += expect_u32("contract package owner mask",
                         validation.owner_validated_table_mask,
                         REFMEM_APP_TABLE_MASK_ALL);
    failed += expect_u32("application table crc present",
                         validation.table_crc32[REFMEM_APP_TABLE_APPLICATION_MAP] != 0u ? 1u : 0u,
                         1u);
    failed += expect_u32("board table crc present",
                         validation.table_crc32[REFMEM_APP_TABLE_BOARD_CAPABILITY] != 0u ? 1u : 0u,
                         1u);
    return failed;
}

static int test_package_stage_uses_table_crc_and_partial_owner_state(void)
{
    int failed = 0;
    uint8_t package[TEST_REFMEM_TABLE_PACKAGE_CAPACITY];
    refmem_table_package_validation_t validation;
    const size_t package_size = build_test_package(package, sizeof(package), true);
    const refmem_application_model_snapshot_t model = make_active_model();
    refmem_application_model_load_snapshot_t load = make_valid_load();
    refmem_table_activation_gate_t gate = make_pass_gate();
    refmem_table_registry_entry_t board_entry;
    refmem_table_registry_entry_t node_load_entry;
    refmem_table_registry_entry_t fb_entry;
    refmem_table_image_descriptor_t staging;

    failed += expect_bool("contract package validates for stage",
                          refmem_table_registry_validate_package(package,
                                                                 package_size,
                                                                 &validation),
                          true);
    load.staging_package_crc32 = validation.package_crc32;

    refmem_table_registry_init(&model);
    failed += expect_bool("stage package validation",
                          refmem_table_registry_stage_package_validation(&load, &validation),
                          true);
    failed += expect_bool("get board table",
                          refmem_table_registry_get_entry(REFMEM_APP_TABLE_BOARD_CAPABILITY,
                                                          &board_entry),
                          true);
    failed += expect_bool("get nodeload table",
                          refmem_table_registry_get_entry(REFMEM_APP_TABLE_NODE_LOAD,
                                                          &node_load_entry),
                          true);
    failed += expect_bool("get fb table",
                          refmem_table_registry_get_entry(REFMEM_APP_TABLE_FB_INSTANCE,
                                                          &fb_entry),
                          true);
    failed += expect_bool("get staging descriptor",
                          refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_STAGING,
                                                                     &staging),
                          true);

    failed += expect_u32("board staging table crc",
                         board_entry.staging_crc32,
                         validation.table_crc32[REFMEM_APP_TABLE_BOARD_CAPABILITY]);
    failed += expect_u32("board owner ok state",
                         board_entry.validation_state,
                         REFMEM_TABLE_VALIDATION_OWNER_OK);
    failed += expect_u32("nodeload staging table crc",
                         node_load_entry.staging_crc32,
                         validation.table_crc32[REFMEM_APP_TABLE_NODE_LOAD]);
    failed += expect_u32("nodeload owner ok state",
                         node_load_entry.validation_state,
                         REFMEM_TABLE_VALIDATION_OWNER_OK);
    failed += expect_u32("fb staging table crc",
                         fb_entry.staging_crc32,
                         validation.table_crc32[REFMEM_APP_TABLE_FB_INSTANCE]);
    failed += expect_u32("fb owner ok state",
                         fb_entry.validation_state,
                         REFMEM_TABLE_VALIDATION_OWNER_OK);
    failed += expect_u32("staging descriptor owner ok",
                         staging.state,
                         REFMEM_TABLE_VALIDATION_OWNER_OK);
    failed += expect_u32("staging package crc",
                         staging.package_crc32,
                         validation.package_crc32);
    failed += expect_bool("partial owner package cannot activate",
                          refmem_table_registry_activate_staging(&gate),
                          false);
    return failed;
}

int main(void)
{
    int failed = 0;

    failed += test_init_sets_active_descriptor();
    failed += test_failed_activation_preserves_active();
    failed += test_activation_requires_real_table_image();
    failed += test_invalid_staging_is_not_activated();
    failed += test_package_owner_validation_rejects_placeholder_tables();
    failed += test_package_owner_validation_accepts_contract_tables();
    failed += test_package_stage_uses_table_crc_and_partial_owner_state();

    if (failed != 0) {
        (void)printf("refmem_table_registry tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_table_registry tests passed\n");
    return 0;
}
