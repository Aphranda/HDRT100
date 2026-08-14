#include "refmem_quality.h"

#include <string.h>

static void refmem_quality_clear_entry(refmem_connection_quality_entry_t *entry)
{
    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
    }
}

bool refmem_quality_map_local_adapter(
    const refmem_sync_context_t *sync,
    const refmem_pio_spi_adapter_snapshot_t *adapter,
    refmem_connection_quality_entry_t *entry)
{
    if (sync == NULL || adapter == NULL || entry == NULL ||
        sync->local_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }

    refmem_sync_quality_counters_t sync_quality;
    refmem_sync_get_quality(sync, &sync_quality);

    refmem_quality_clear_entry(entry);
    entry->quality_id = 0u;
    entry->scope = REFMEM_APP_QUALITY_TRANSPORT_ADAPTER;
    entry->source_node = sync->local_slot;
    entry->target_node = sync->local_slot;
    entry->seq_expected = adapter->tx_count + 1u;
    entry->seq_last = adapter->tx_count;
    entry->crc_error_count = sync_quality.crc_error_count + adapter->bad_frame_count;
    entry->stale_count = sync_quality.stale_count;
    entry->late_count = 0u;
    entry->drop_count = sync_quality.drop_count + adapter->drop_count;
    entry->timeout_count = adapter->timeout_count;
    entry->last_error = adapter->last_error;
    entry->last_error_tick = adapter->last_rx_timestamp;
    entry->p99 = adapter->latency_class_us;
    entry->p999 = adapter->latency_class_us;
    entry->evidence_index = adapter->rx_pending;
    return true;
}

bool refmem_quality_map_remote_sync(
    const refmem_sync_remote_quality_snapshot_t *remote,
    refmem_connection_quality_entry_t *entry)
{
    if (remote == NULL || entry == NULL || remote->seen == 0u ||
        remote->source_slot >= REFMEM_SYNC_NODE_COUNT ||
        remote->target_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }

    refmem_quality_clear_entry(entry);
    entry->quality_id = remote->quality_id;
    entry->scope = remote->scope;
    entry->source_node = remote->source_slot;
    entry->target_node = remote->target_slot;
    entry->seq_expected = remote->seq_expected;
    entry->seq_last = remote->seq_last;
    entry->crc_error_count = remote->crc_error_count;
    entry->stale_count = remote->stale_count;
    entry->late_count = remote->late_count;
    entry->drop_count = remote->drop_count;
    entry->timeout_count = remote->timeout_count;
    entry->last_error = remote->last_error;
    entry->last_error_tick = remote->last_frame_seq32;
    entry->p99 = remote->p99_us;
    entry->p999 = remote->p999_us;
    entry->evidence_index = remote->evidence_index;
    return true;
}

bool refmem_quality_build_runtime_table(
    uint32_t active_table_crc32,
    const refmem_sync_context_t *sync,
    const refmem_pio_spi_adapter_snapshot_t *adapter,
    refmem_quality_runtime_table_t *table)
{
    if (sync == NULL || adapter == NULL || table == NULL ||
        sync->local_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }

    memset(table, 0, sizeof(*table));
    table->version = REFMEM_APP_MODEL_VERSION;
    table->active_table_crc32 = active_table_crc32;
    table->local_slot = sync->local_slot;
    table->epoch_id = sync->active_epoch_id;
    table->run_id = sync->active_run_id;

    if (!refmem_quality_map_local_adapter(sync, adapter, &table->entry[0])) {
        return false;
    }
    table->entry_count = 1u;

    for (uint32_t source = 0u; source < REFMEM_SYNC_NODE_COUNT; source++) {
        const refmem_sync_remote_quality_snapshot_t *remote = &sync->remote_quality[source];
        if (remote->seen == 0u) {
            continue;
        }
        if (table->entry_count >= REFMEM_QUALITY_RUNTIME_ENTRY_COUNT) {
            table->overflow_count++;
            continue;
        }
        if (refmem_quality_map_remote_sync(remote, &table->entry[table->entry_count])) {
            table->entry_count++;
        }
    }
    return true;
}

const refmem_connection_quality_entry_t *refmem_quality_get_entry(
    const refmem_quality_runtime_table_t *table,
    uint32_t index)
{
    if (table == NULL || index >= table->entry_count ||
        index >= REFMEM_QUALITY_RUNTIME_ENTRY_COUNT) {
        return NULL;
    }
    return &table->entry[index];
}
