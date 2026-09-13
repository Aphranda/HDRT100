#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include "diagnostics_tdma_record.h"
#include "project_build_info.h"
#include "storage_manager.h"
#include "tdma_runtime_owner.h"
#include "pico/time.h"
#include "pico/unique_id.h"

static uint32_t s_record_transaction;

bool app_tdma_record_copy(uint32_t offset, uint8_t *data, uint32_t size)
{
    diagnostics_tdma_record_status_t status;
    return diagnostics_tdma_record_status(&status) &&
        status.state == TDMA_RECORD_FROZEN &&
        storage_manager_copy_evidence_write(s_record_transaction, offset, data, size);
}

static bool app_record_begin(uint32_t epoch, uint32_t *capacity)
{
    char path[64];
    (void)snprintf(path, sizeof(path), "/logs/tdma_%lu.bin", (unsigned long)epoch);
    *capacity = STORAGE_MANAGER_FILE_WRITE_MAX_BYTES;
    return storage_manager_begin_evidence_write(path, *capacity, 0u,
                                                &s_record_transaction);
}

static bool app_record_append(uint32_t offset, const uint8_t *data, size_t size)
{
    return storage_manager_write_file_chunk(s_record_transaction, offset, data, size);
}

static bool app_record_save(uint32_t crc, uint32_t *job)
{
    /* This finite recorder intentionally defers SD IO until the owner has
     * acknowledged STOP. Core1 never waits for the buffer or the filesystem. */
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring) || ring.enabled != 0u ||
        ring.adapter_started != 0u || ring.config_seq != ring.applied_config_seq) {
        return false;
    }
    return storage_manager_finish_evidence_write(s_record_transaction, crc, job);
}

static int app_record_save_result(uint32_t job)
{
    storage_manager_job_result_t result;
    storage_manager_get_job_result(&result);
    if (result.id != job || result.state == STORAGE_MANAGER_JOB_STATE_FAILED) {
        return -1;
    }
    return result.state == STORAGE_MANAGER_JOB_STATE_DONE ? 1 : 0;
}

static uint32_t app_record_snapshot(uint32_t *words)
{
    tdma_ring_runtime_snapshot_t ring = {0};
    tdma_flight_engine_snapshot_t engine = {0};
    tdma_flight_fifo_snapshot_t fifo = {0};
    tdma_pio_spi_ring_adapter_snapshot_t adapter = {0};
    tdma_pio_spi_phys_snapshot_t phys = {0};
    app_realtime_schedule_snapshot_t schedule = {0};
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    tdma_pio_spi_ring_adapter_t *ring_adapter = tdma_runtime_owner_get_ring_adapter();
    uint32_t valid = 0u;
    if (tdma_runtime_owner_get_ring_snapshot(&ring)) valid |= 1u;
    if (owner != NULL && tdma_service_get_flight_engine_snapshot(owner, &engine)) valid |= 2u;
    if (owner != NULL && tdma_service_get_flight_fifo_snapshot(owner, &fifo)) valid |= 4u;
    if (tdma_runtime_owner_get_phys_snapshot(&phys)) valid |= 8u;
    if (ring_adapter != NULL && tdma_pio_spi_ring_adapter_get_snapshot(ring_adapter, &adapter)) valid |= 16u;
    if (app_realtime_get_schedule_snapshot(&schedule)) valid |= 32u;
    uint32_t cursor = 0u;
#define RECORD_U32(group, name, expression) words[cursor++] = (uint32_t)(expression);
#define RECORD_I32(group, name, expression) RECORD_U32(group, name, expression)
#define RECORD_U64(group, name, expression) do { \
        const uint64_t value = (uint64_t)(expression); \
        words[cursor++] = (uint32_t)value; words[cursor++] = (uint32_t)(value >> 32u); \
    } while (0);
#include "diagnostics_tdma_record_fields.def"
#undef RECORD_U32
#undef RECORD_I32
#undef RECORD_U64
    _Static_assert(APP_REALTIME_PHASE_COUNT == 10u, "update diagnostic file phase schema");
    return valid;
}

static void app_record_identity(uint64_t *build, uint64_t *board)
{
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    *board = 0u;
    for (size_t i = 0u; i < sizeof(id.id); ++i) {
        *board = (*board << 8u) | id.id[i];
    }
    *build = strtoull(g_project_build_id, NULL, 10);
}

void app_tdma_record_service(void)
{
    static const diagnostics_tdma_record_port_t port = {
        .begin = app_record_begin, .append = app_record_append,
        .save = app_record_save, .save_result = app_record_save_result,
        .snapshot = app_record_snapshot, .now_us = time_us_64,
        .identity = app_record_identity
    };
    diagnostics_tdma_record_service(&port);
}
