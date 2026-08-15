#ifndef REFMEM_QUALITY_H
#define REFMEM_QUALITY_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_application_model.h"
#include "refmem_pio_spi_adapter.h"
#include "refmem_realtime_tdma.h"
#include "refmem_sync.h"

#define REFMEM_QUALITY_RUNTIME_ENTRY_COUNT (REFMEM_SYNC_NODE_COUNT + 2u)
#define REFMEM_QUALITY_RUNTIME_LOCAL_ADAPTER_ID 0u
#define REFMEM_QUALITY_RUNTIME_TDMA_SERVICE_ID 0x54444D41u

typedef enum {
    REFMEM_QUALITY_GATE_OK = 0u,
    REFMEM_QUALITY_GATE_BAD_ARGUMENT = 1u,
    REFMEM_QUALITY_GATE_NO_RUNTIME_ENTRY = 2u,
    REFMEM_QUALITY_GATE_CRC_ERROR = 3u,
    REFMEM_QUALITY_GATE_STALE = 4u,
    REFMEM_QUALITY_GATE_LATE = 5u,
    REFMEM_QUALITY_GATE_DROP = 6u,
    REFMEM_QUALITY_GATE_TIMEOUT = 7u,
    REFMEM_QUALITY_GATE_LAST_ERROR = 8u,
} refmem_quality_gate_reason_t;

typedef struct {
    uint32_t max_crc_error_count;
    uint32_t max_stale_count;
    uint32_t max_late_count;
    uint32_t max_drop_count;
    uint32_t max_timeout_count;
    uint32_t require_no_last_error;
} refmem_quality_gate_threshold_t;

typedef struct {
    uint32_t version;
    uint32_t entry_count;
    uint32_t active_table_crc32;
    uint32_t local_slot;
    uint32_t epoch_id;
    uint32_t run_id;
    uint32_t overflow_count;
    refmem_connection_quality_entry_t entry[REFMEM_QUALITY_RUNTIME_ENTRY_COUNT];
} refmem_quality_runtime_table_t;

bool refmem_quality_map_local_adapter(
    const refmem_sync_context_t *sync,
    const refmem_pio_spi_adapter_snapshot_t *adapter,
    refmem_connection_quality_entry_t *entry);
bool refmem_quality_map_realtime_tdma(
    const refmem_sync_context_t *sync,
    const refmem_realtime_tdma_snapshot_t *tdma,
    refmem_connection_quality_entry_t *entry);
bool refmem_quality_map_remote_sync(
    const refmem_sync_remote_quality_snapshot_t *remote,
    refmem_connection_quality_entry_t *entry);
bool refmem_quality_build_runtime_table(
    uint32_t active_table_crc32,
    const refmem_sync_context_t *sync,
    const refmem_pio_spi_adapter_snapshot_t *adapter,
    const refmem_realtime_tdma_snapshot_t *tdma,
    refmem_quality_runtime_table_t *table);
const refmem_connection_quality_entry_t *refmem_quality_get_entry(
    const refmem_quality_runtime_table_t *table,
    uint32_t index);
bool refmem_quality_evaluate_deployment_gate(
    const refmem_quality_runtime_table_t *table,
    const refmem_quality_gate_threshold_t *threshold,
    refmem_deployment_gate_entry_t *gate);

#endif
