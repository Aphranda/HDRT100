#include "distributed_refmem.h"

#include <stdint.h>
#include <string.h>

#include "osal.h"
#include "refmem_cmd_codec.h"
#include "refmem_cmd_frame.h"
#include "refmem_cmd_load.h"
#include "refmem_command_catalog.h"
#include "refmem_sync_frame.h"

static uint32_t s_control_seq;

static uint32_t params_crc(const uint32_t *params, uint32_t words)
{
    uint32_t crc = 2166136261u;
    for (uint32_t i = 0u; i < words; ++i) {
        for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
            crc ^= (uint8_t)(params[i] >> shift);
            crc *= 16777619u;
        }
    }
    return crc;
}

bool distributed_refmem_control_submit(uint16_t command_id,
                                       uint32_t target_mask,
                                       uint32_t required_mask,
                                       const uint32_t *params,
                                       uint16_t param_words,
                                       uint32_t *command_seq)
{
    const refmem_catalog_entry_t *entry =
        refmem_command_catalog_find_by_id(command_id);
    if (entry == NULL || command_seq == NULL ||
        (param_words != 0u && params == NULL) ||
        param_words != entry->param_words || param_words > 16u ||
        target_mask == 0u || (required_mask & ~target_mask) != 0u) {
        return false;
    }
    const uint32_t zero_param = 0u;
    const uint32_t *payload = params != NULL ? params : &zero_param;

    refmem_command_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (s_control_seq == 0u) {
        s_control_seq = osal_tick_ms();
        if (s_control_seq == 0u) {
            s_control_seq = 1u;
        }
    }
    s_control_seq++;
    snapshot.state = REFMEM_COMMAND_STATE_POSTED;
    snapshot.command_seq = s_control_seq;
    snapshot.source_node = distributed_refmem_local_slot();
    snapshot.target_mask = target_mask;
    snapshot.required_mask = required_mask;
    snapshot.command_type = entry->command_type;
    snapshot.payload_kind = entry->param_kind;
    snapshot.payload_size = (uint32_t)param_words * (uint32_t)sizeof(uint32_t);
    snapshot.payload_crc32 = params_crc(payload, param_words);
    snapshot.issue_epoch = 1u;
    snapshot.run_id = 1u;
    snapshot.timeout_us = entry->timeout_us;

    const uint32_t local_bit = 1u << distributed_refmem_local_slot();
    if ((target_mask & ~local_bit) != 0u) {
        if (!refmem_cmd_load_submit_fragments(&snapshot, payload)) {
            return false;
        }
        *command_seq = s_control_seq;
        return true;
    }

    uint8_t frame[REFMEM_SYNC_FRAME_HEADER_SIZE +
                  REFMEM_SYNC_FRAME_PAYLOAD_MAX];
    size_t frame_size = 0u;
    if (!refmem_cmd_frame_build(DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
                                (uint8_t)target_mask, snapshot.issue_epoch,
                                snapshot.run_id, snapshot.command_seq,
                                &snapshot, payload, frame, sizeof(frame),
                                &frame_size) ||
        !distributed_refmem_cmd_frame_receive(frame, frame_size)) {
        return false;
    }
    *command_seq = s_control_seq;
    return true;
}

