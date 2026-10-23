/* ARCHIVED v1 - not built. See ARCHIVE_NOTE.md. */
#include "refmem_cmd_tdma.h"

#include <string.h>

#include "distributed_refmem.h"
#include "board_identity.h"
#include "osal.h"

static uint8_t s_pending[REFMEM_CMD_WIRE_SIZE];
static volatile bool s_pending_valid;

bool refmem_cmd_tdma_submit(const uint8_t wire[REFMEM_CMD_WIRE_SIZE])
{
    if (wire == NULL || s_pending_valid) {
        return false;
    }
    memcpy(s_pending, wire, REFMEM_CMD_WIRE_SIZE);
    s_pending_valid = true;
    return true;
}

bool refmem_cmd_tdma_take(uint8_t wire[REFMEM_CMD_WIRE_SIZE])
{
    if (wire == NULL || !s_pending_valid) {
        return false;
    }
    memcpy(wire, s_pending, REFMEM_CMD_WIRE_SIZE);
    s_pending_valid = false;
    return true;
}

void refmem_cmd_tdma_rx(const uint8_t *payload, size_t payload_size)
{
    if (payload == NULL || payload_size < REFMEM_CMD_WIRE_SIZE) {
        return;
    }
    refmem_command_request_t request;
    if (refmem_cmd_codec_decode(payload, &request) != REFMEM_CMD_CODEC_OK) {
        return;
    }
    const uint32_t board_no = board_identity_get_no();
    const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
    if ((request.target_mask & (1u << local_slot)) == 0u) {
        return;
    }
    if (!distributed_refmem_command_try_post(&request, osal_tick_ms())) {
        return;
    }
    const refmem_command_take_result_t take =
        distributed_refmem_command_try_take(
            local_slot, request.issue_epoch, request.run_id,
            request.payload_crc32, 0u);
    if (take == REFMEM_COMMAND_TAKE_TAKEN) {
        (void)distributed_refmem_command_ack(local_slot, 0u);
    }
}
