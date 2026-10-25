#include "scpi_trigger_seat_commands.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "scpi_port_internal.h"
#include "scpi_trigger_deployment_commands.h"
#include "scpi_trigger_t2_runtime_commands.h"
#include "scpi_trigger_grid_commands.h"

/* Seat mount table: which seat each load rides. The host supplies an explicit
 * generation and content CRC, so a half-written or stale table is rejected. */
scpi_result_t scpi_cmd_trigger_seat_mount(scpi_t *context)
{
    uint32_t generation = 0u, crc = 0u, count = 0u;
    tdma_seat_mount_table_t table;
    memset(&table, 0, sizeof(table));
    /* Entry marker: proves the write command reached this handler (99) before
     * any later stage overwrites the reason. */
    tdma_seat_mount_store_note_reject(99u);
    if (!scpi_trigger_deployment_u32(context, &generation) ||
        !scpi_trigger_deployment_u32(context, &crc) ||
        !scpi_trigger_deployment_u32(context, &count) ||
        generation == 0u || count == 0u || count > TDMA_SEAT_MOUNT_ENTRY_COUNT) {
        tdma_seat_mount_store_note_reject(10u);
        return scpi_trigger_t2_fail(context);
    }
    table.count = count;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t message_class = 0u, seat_class = 0u;
        if (!scpi_trigger_deployment_u32(context, &message_class) ||
            !scpi_trigger_deployment_u32(context, &seat_class) ||
            message_class == 0u || message_class > 0xFFu || seat_class > 0xFFu ||
            !tdma_seat_mount_seat_valid((uint8_t)seat_class)) {
            tdma_seat_mount_store_note_reject(11u);
            return scpi_trigger_t2_fail(context);
        }
        table.entries[index].message_class = (uint8_t)message_class;
        table.entries[index].seat_class = (uint8_t)seat_class;
    }
    if (!scpi_trigger_grid_end(context)) {
        tdma_seat_mount_store_note_reject(12u);
        return scpi_trigger_t2_fail(context);
    }
    if (tdma_seat_mount_crc(generation, &table) != crc) {
        tdma_seat_mount_store_note_reject(13u);
        return scpi_trigger_t2_fail(context);
    }
    return scpi_trigger_grid_result(context,
        tdma_seat_mount_store_save(&table, generation) ? 0u : 1u);
}

scpi_result_t scpi_cmd_trigger_seat_mount_q(scpi_t *context)
{
    tdma_seat_mount_table_t table;
    const bool loaded = tdma_seat_mount_store_get(&table);
    scpi_trigger_grid_result(context, scpi_trigger_grid_end(context) ? 0u : 1u);
    SCPI_ResultUInt32(context, loaded ? 1u : 0u);
    SCPI_ResultUInt32(context, loaded ? table.count : 0u);
    uint32_t last_reject = 0u, record_generation = 0u;
    tdma_seat_mount_store_status(&last_reject, &record_generation);
    SCPI_ResultUInt32(context, last_reject);
    SCPI_ResultUInt32(context, record_generation);
    for (uint32_t index = 0u; index < TDMA_SEAT_MOUNT_ENTRY_COUNT; ++index) {
        SCPI_ResultUInt32(context, loaded && index < table.count ?
            table.entries[index].message_class : 0u);
        SCPI_ResultUInt32(context, loaded && index < table.count ?
            table.entries[index].seat_class : 0u);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_seat_clear(scpi_t *context)
{
    return scpi_trigger_grid_result(context, scpi_trigger_grid_end(context) ?
        (tdma_seat_mount_store_clear() ? 0u : 1u) : 1u);
}
