#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <stddef.h>

#include "refmem_cmd_frame.h"

/* Host stubs for the OTA CRC used by refmem_sync_frame.c. */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0u; i < length; i++) {
        crc ^= data[i];
        crc *= 16777619u;
    }
    return crc;
}

uint32_t ota_crc32_compute(const uint8_t *data, size_t length)
{
    return ota_crc32_update(2166136261u, data, length);
}



int main(void)
{
    refmem_command_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.state = REFMEM_COMMAND_STATE_POSTED;
    snapshot.command_seq = 42u;
    snapshot.source_node = 0u;
    snapshot.target_mask = 0x0Eu;
    snapshot.required_mask = 0x02u;
    snapshot.command_type = REFMEM_COMMAND_TYPE_START;
    snapshot.command_class = REFMEM_COMMAND_CLASS_RUN;
    snapshot.payload_crc32 = 0x12345678u;

    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS];
    for (uint32_t i = 0u; i < REFMEM_NODE_CONTROL_PARAM_WORDS; i++) {
        params[i] = 0x1000u + i;
    }

    uint8_t frame[REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX];
    size_t frame_size = 0u;
    assert(refmem_cmd_frame_build(0u, 0x0Eu, 1u, 2u, 3u, &snapshot, params,
                                  frame, sizeof(frame), &frame_size));
    assert(frame_size > 0u);

    refmem_command_snapshot_t decoded;
    uint32_t decoded_params[REFMEM_NODE_CONTROL_PARAM_WORDS];
    uint8_t source = 0xFFu;
    uint8_t target = 0u;
    assert(refmem_cmd_frame_apply(frame, frame_size, &decoded, decoded_params,
                                  &source, &target));
    assert(source == 0u && target == 0x0Eu);
    assert(decoded.command_seq == 42u);
    assert(decoded.target_mask == 0x0Eu);
    assert(decoded.command_type == REFMEM_COMMAND_TYPE_START);
    assert(memcmp(decoded_params, params, sizeof(params)) == 0);

    /* Corrupted payload must be rejected by the frame CRC. */
    frame[frame_size - 1u] ^= 0x5Au;
    assert(!refmem_cmd_frame_apply(frame, frame_size, &decoded, decoded_params,
                                   NULL, NULL));

    printf("refmem_cmd_frame: build/apply/crc OK\n");
    return 0;
}
