#include "refmem_quality.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

static int test_local_adapter_mapping(void)
{
    int failed = 0;
    refmem_sync_context_t sync;
    refmem_pio_spi_adapter_snapshot_t adapter;
    refmem_connection_quality_entry_t entry;

    (void)refmem_sync_init(&sync, 2u, 7u, 8u);
    sync.quality.crc_error_count = 3u;
    sync.quality.stale_count = 4u;
    sync.quality.drop_count = 5u;

    (void)memset(&adapter, 0, sizeof(adapter));
    adapter.tx_count = 10u;
    adapter.bad_frame_count = 6u;
    adapter.drop_count = 7u;
    adapter.timeout_count = 8u;
    adapter.last_error = 9u;
    adapter.last_rx_timestamp = 1234u;
    adapter.latency_class_us = 50u;
    adapter.rx_pending = 1u;

    failed += expect_bool("local adapter map",
                          refmem_quality_map_local_adapter(&sync, &adapter, &entry),
                          true);
    failed += expect_u32("local quality id", entry.quality_id, 0u);
    failed += expect_u32("local scope", entry.scope, REFMEM_APP_QUALITY_TRANSPORT_ADAPTER);
    failed += expect_u32("local source", entry.source_node, 2u);
    failed += expect_u32("local target", entry.target_node, 2u);
    failed += expect_u32("local expected", entry.seq_expected, 11u);
    failed += expect_u32("local last", entry.seq_last, 10u);
    failed += expect_u32("local crc", entry.crc_error_count, 9u);
    failed += expect_u32("local stale", entry.stale_count, 4u);
    failed += expect_u32("local drop", entry.drop_count, 12u);
    failed += expect_u32("local timeout", entry.timeout_count, 8u);
    failed += expect_u32("local last error", entry.last_error, 9u);
    failed += expect_u32("local tick", entry.last_error_tick, 1234u);
    failed += expect_u32("local p99", entry.p99, 50u);
    failed += expect_u32("local evidence", entry.evidence_index, 1u);
    return failed;
}

static int test_remote_quality_mapping(void)
{
    int failed = 0;
    refmem_sync_remote_quality_snapshot_t remote;
    refmem_connection_quality_entry_t entry;

    (void)memset(&remote, 0, sizeof(remote));
    remote.seen = 1u;
    remote.source_slot = 1u;
    remote.quality_id = 21u;
    remote.scope = REFMEM_APP_QUALITY_DATA_LINK;
    remote.target_slot = 3u;
    remote.seq_expected = 9u;
    remote.seq_last = 8u;
    remote.crc_error_count = 2u;
    remote.stale_count = 3u;
    remote.drop_count = 4u;
    remote.late_count = 5u;
    remote.timeout_count = 6u;
    remote.last_error = 7u;
    remote.p99_us = 99u;
    remote.p999_us = 999u;
    remote.evidence_index = 12u;
    remote.last_frame_seq32 = 30u;

    failed += expect_bool("remote map",
                          refmem_quality_map_remote_sync(&remote, &entry),
                          true);
    failed += expect_u32("remote quality id", entry.quality_id, 21u);
    failed += expect_u32("remote scope", entry.scope, REFMEM_APP_QUALITY_DATA_LINK);
    failed += expect_u32("remote source", entry.source_node, 1u);
    failed += expect_u32("remote target", entry.target_node, 3u);
    failed += expect_u32("remote expected", entry.seq_expected, 9u);
    failed += expect_u32("remote last", entry.seq_last, 8u);
    failed += expect_u32("remote crc", entry.crc_error_count, 2u);
    failed += expect_u32("remote stale", entry.stale_count, 3u);
    failed += expect_u32("remote drop", entry.drop_count, 4u);
    failed += expect_u32("remote late", entry.late_count, 5u);
    failed += expect_u32("remote timeout", entry.timeout_count, 6u);
    failed += expect_u32("remote error", entry.last_error, 7u);
    failed += expect_u32("remote tick", entry.last_error_tick, 30u);
    failed += expect_u32("remote p999", entry.p999, 999u);
    failed += expect_u32("remote evidence", entry.evidence_index, 12u);
    return failed;
}

static int test_runtime_table(void)
{
    int failed = 0;
    refmem_sync_context_t sync;
    refmem_pio_spi_adapter_snapshot_t adapter;
    refmem_quality_runtime_table_t table;

    (void)refmem_sync_init(&sync, 0u, 7u, 8u);
    (void)memset(&adapter, 0, sizeof(adapter));
    adapter.tx_count = 1u;
    adapter.latency_class_us = 50u;

    sync.remote_quality[1].seen = 1u;
    sync.remote_quality[1].source_slot = 1u;
    sync.remote_quality[1].quality_id = 2u;
    sync.remote_quality[1].scope = REFMEM_APP_QUALITY_NODE;
    sync.remote_quality[1].target_slot = 0u;
    sync.remote_quality[1].seq_expected = 4u;
    sync.remote_quality[1].seq_last = 3u;

    failed += expect_bool("build runtime table",
                          refmem_quality_build_runtime_table(0x12345678u,
                                                             &sync,
                                                             &adapter,
                                                             &table),
                          true);
    failed += expect_u32("table version", table.version, REFMEM_APP_MODEL_VERSION);
    failed += expect_u32("table count", table.entry_count, 2u);
    failed += expect_u32("table crc", table.active_table_crc32, 0x12345678u);
    failed += expect_u32("table local", table.local_slot, 0u);
    failed += expect_u32("table epoch", table.epoch_id, 7u);
    failed += expect_u32("table run", table.run_id, 8u);

    const refmem_connection_quality_entry_t *local = refmem_quality_get_entry(&table, 0u);
    const refmem_connection_quality_entry_t *remote = refmem_quality_get_entry(&table, 1u);
    const refmem_connection_quality_entry_t *missing = refmem_quality_get_entry(&table, 2u);
    failed += expect_bool("local entry present", local != NULL, true);
    failed += expect_bool("remote entry present", remote != NULL, true);
    failed += expect_bool("missing entry absent", missing != NULL, false);
    if (local != NULL) {
        failed += expect_u32("local entry scope", local->scope, REFMEM_APP_QUALITY_TRANSPORT_ADAPTER);
    }
    if (remote != NULL) {
        failed += expect_u32("runtime remote id", remote->quality_id, 2u);
        failed += expect_u32("runtime remote source", remote->source_node, 1u);
        failed += expect_u32("runtime remote last", remote->seq_last, 3u);
    }
    return failed;
}

static int test_rejects_invalid_inputs(void)
{
    int failed = 0;
    refmem_sync_remote_quality_snapshot_t remote;
    refmem_connection_quality_entry_t entry;

    (void)memset(&remote, 0, sizeof(remote));
    failed += expect_bool("unseen remote rejected",
                          refmem_quality_map_remote_sync(&remote, &entry),
                          false);
    remote.seen = 1u;
    remote.source_slot = REFMEM_SYNC_NODE_COUNT;
    remote.target_slot = 0u;
    failed += expect_bool("bad source rejected",
                          refmem_quality_map_remote_sync(&remote, &entry),
                          false);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_local_adapter_mapping();
    failed += test_remote_quality_mapping();
    failed += test_runtime_table();
    failed += test_rejects_invalid_inputs();

    if (failed != 0) {
        (void)printf("refmem_quality tests failed: %d\n", failed);
        return 1;
    }

    (void)printf("refmem_quality tests passed\n");
    return 0;
}
