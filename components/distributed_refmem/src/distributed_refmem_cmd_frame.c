#include "distributed_refmem.h"

#include "refmem_cmd_frame.h"
#include "refmem_node_control.h"

#include <stddef.h>

static uint32_t s_cmd_frame_calls;
static uint32_t s_cmd_frame_apply_fail;
static uint32_t s_cmd_frame_target_miss;
static uint32_t s_cmd_frame_request_fail;
static uint32_t s_cmd_frame_ok;

void distributed_refmem_cmd_frame_counters(uint32_t *calls,
                                           uint32_t *apply_fail,
                                           uint32_t *target_miss,
                                           uint32_t *request_fail,
                                           uint32_t *ok)
{
    if (calls != NULL) {
        *calls = s_cmd_frame_calls;
    }
    if (apply_fail != NULL) {
        *apply_fail = s_cmd_frame_apply_fail;
    }
    if (target_miss != NULL) {
        *target_miss = s_cmd_frame_target_miss;
    }
    if (request_fail != NULL) {
        *request_fail = s_cmd_frame_request_fail;
    }
    if (ok != NULL) {
        *ok = s_cmd_frame_ok;
    }
}

bool distributed_refmem_cmd_frame_receive(const uint8_t *frame, size_t size)
{
    s_cmd_frame_calls++;
    refmem_command_snapshot_t snapshot;
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS];
    uint8_t source_slot = 0u;
    uint8_t target_mask = 0u;
    if (!refmem_cmd_frame_apply(frame, size, &snapshot, params, &source_slot,
                                &target_mask)) {
        s_cmd_frame_apply_fail++;
        return false;
    }
    (void)source_slot;
    const uint32_t local = distributed_refmem_local_slot();
    if ((snapshot.target_mask & (1u << local)) == 0u) {
        s_cmd_frame_target_miss++;
        return false;
    }
    if (!distributed_refmem_node_control_apply_request(
            local, &snapshot, params,
            snapshot.command_seq != 0u ? snapshot.command_seq : 1u)) {
        s_cmd_frame_request_fail++;
        return false;
    }
    s_cmd_frame_ok++;
    return true;
}
