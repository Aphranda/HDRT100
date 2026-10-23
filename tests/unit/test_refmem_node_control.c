#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "refmem_node_control.h"

int main(void)
{
    refmem_command_snapshot_t command;
    memset(&command, 0, sizeof(command));
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS];
    memset(params, 0, sizeof(params));
    uint32_t seq = 0u;
    uint32_t crc = 0u;

    refmem_command_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.state = REFMEM_COMMAND_STATE_POSTED;
    snapshot.command_seq = 11u;
    snapshot.source_node = 0u;
    snapshot.target_mask = 0x0Fu;
    snapshot.required_mask = 0x0Cu;
    snapshot.command_type = REFMEM_COMMAND_TYPE_START;
    snapshot.command_class = REFMEM_COMMAND_CLASS_RUN;
    snapshot.payload_crc32 = 0x12345678u;

    uint32_t new_params[REFMEM_NODE_CONTROL_PARAM_WORDS] = {
        0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u,
        0x55555555u, 0x66666666u, 0x77777777u, 0x88888888u,
    };

    assert(refmem_node_control_write(&command, params, &seq, &crc, &snapshot,
                                     new_params, 7u));
    assert(seq == 7u && crc != 0u);
    assert(!refmem_node_control_write(&command, params, &seq, &crc, &snapshot,
                                      new_params, 0u));

    refmem_command_snapshot_t readback;
    uint32_t read_params[REFMEM_NODE_CONTROL_PARAM_WORDS];
    memset(read_params, 0, sizeof(read_params));
    assert(refmem_node_control_read(&command, params, seq, crc, &readback,
                                    read_params));
    assert(readback.command_seq == 11u && readback.target_mask == 0x0Fu);
    assert(memcmp(read_params, new_params, sizeof(new_params)) == 0);

    /* A torn/stale copy must be rejected by the CRC guard. */
    command.command_seq ^= 1u;
    assert(!refmem_node_control_read(&command, params, seq, crc, &readback,
                                     NULL));
    command.command_seq ^= 1u;
    params[3] ^= 1u;
    assert(!refmem_node_control_read(&command, params, seq, crc, &readback,
                                     NULL));
    params[3] ^= 1u;

    assert(refmem_node_control_ack(&command, params, &seq, &crc, 2u, 3u));
    assert(refmem_node_control_read(&command, params, seq, crc, &readback,
                                    read_params));
    assert((readback.ack_flags & (1u << 2)) != 0u);
    assert(readback.state == REFMEM_COMMAND_STATE_POSTED);
    assert(readback.evidence_index == 3u);
    assert(memcmp(read_params, new_params, sizeof(new_params)) == 0);

    assert(refmem_node_control_ack(&command, params, &seq, &crc, 3u, 4u));
    assert(refmem_node_control_read(&command, params, seq, crc, &readback,
                                    read_params));
    assert(readback.state == REFMEM_COMMAND_STATE_ACKED);
    assert(readback.last_completed_seq == 11u);

    assert(refmem_node_control_clear(&command, params, &seq, &crc));
    assert(seq == 0u && crc == 0u);
    assert(!refmem_node_control_read(&command, params, seq, crc, &readback,
                                     NULL));

    printf("refmem_node_control: write/read/params/ack/clear OK\n");
    return 0;
}
