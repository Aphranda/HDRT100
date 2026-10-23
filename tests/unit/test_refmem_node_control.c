#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "refmem_node_control.h"

int main(void)
{
    refmem_command_snapshot_t command;
    memset(&command, 0, sizeof(command));
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

    assert(refmem_node_control_write(&command, &seq, &crc, &snapshot, 7u));
    assert(seq == 7u && crc != 0u);
    assert(!refmem_node_control_write(&command, &seq, &crc, &snapshot, 0u));

    refmem_command_snapshot_t readback;
    assert(refmem_node_control_read(&command, seq, crc, &readback));
    assert(readback.command_seq == 11u && readback.target_mask == 0x0Fu);

    /* A torn/stale copy must be rejected by the CRC guard. */
    command.command_seq ^= 1u;
    assert(!refmem_node_control_read(&command, seq, crc, &readback));
    command.command_seq ^= 1u;

    assert(refmem_node_control_ack(&command, &seq, &crc, 2u, 3u));
    assert(refmem_node_control_read(&command, seq, crc, &readback));
    assert((readback.ack_flags & (1u << 2)) != 0u);
    assert(readback.state == REFMEM_COMMAND_STATE_POSTED);
    assert(readback.evidence_index == 3u);

    assert(refmem_node_control_ack(&command, &seq, &crc, 3u, 4u));
    assert(refmem_node_control_read(&command, seq, crc, &readback));
    assert(readback.state == REFMEM_COMMAND_STATE_ACKED);
    assert(readback.last_completed_seq == 11u);

    assert(refmem_node_control_clear(&command, &seq, &crc));
    assert(seq == 0u && crc == 0u);
    assert(!refmem_node_control_read(&command, seq, crc, &readback));

    printf("refmem_node_control: write/read/ack/clear OK\n");
    return 0;
}
