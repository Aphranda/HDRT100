#ifndef DISTRIBUTED_ACTION_PROBE_H
#define DISTRIBUTED_ACTION_PROBE_H
#include <stdbool.h>
#include <stdint.h>
#define DISTRIBUTED_ACTION_PROBE_TIMEOUT_MS 120000u
enum {
    ACTION_PROBE_CANCEL_STOP=1u, ACTION_PROBE_CANCEL_INACTIVE=2u,
    ACTION_PROBE_CANCEL_BINDING=3u, ACTION_PROBE_CANCEL_TIMEOUT=4u,
    ACTION_PROBE_CANCEL_CLEAR=5u
};
/* Explicit diagnostic action: SET_VALUE changes only a local RAM test value.
 * No GPIO, READY, VDC timestamp, fence or physical completion is produced. */
typedef struct {
    uint32_t configured, session, deployment_generation, deployment_crc;
    uint32_t local_slot, role, sent, received, applied, duplicates, rejected;
    uint32_t queued, completed, last_request, value, ring_config, cancelled;
    uint32_t last_completed_request, completed_mask;
    uint32_t cancel_reason, elapsed_ms, completed_elapsed_ms, last_result;
} distributed_action_probe_status_t;
bool distributed_action_probe_configure(uint32_t session);
bool distributed_action_probe_send(uint32_t role,uint32_t request,uint32_t value);
bool distributed_action_probe_get(distributed_action_probe_status_t *out);
bool distributed_action_probe_clear(void);
#endif
