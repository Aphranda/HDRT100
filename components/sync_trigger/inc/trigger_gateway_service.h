#ifndef TRIGGER_GATEWAY_SERVICE_H
#define TRIGGER_GATEWAY_SERVICE_H

#include "trigger_deployment_service.h"
#include "sync_io_gateway.h"

enum {
    GATEWAY_OK=0u, GATEWAY_ARGUMENT, GATEWAY_BUSY, GATEWAY_NOT_IDLE,
    GATEWAY_DEPLOYMENT, GATEWAY_WRONG_ROLE, GATEWAY_CHANGED,
    GATEWAY_HARDWARE, GATEWAY_NOT_LOADED
};
typedef struct {
    trigger_deployment_binding_t binding;
    sync_io_gateway_snapshot_t io;
    uint32_t loaded;
} trigger_gateway_snapshot_t;

/* Core0 STOP maintenance only. The active GATEWAY deployment selects this
 * board by UID/slot and selects its one logical input and one logical output
 * independently from the deployment masks (public ports 1..4). Loading prepares disabled
 * hardware and a safe-low output; it grants no FIRE or reservation execution.
 * ready_level is a pad observation, not a new READY edge or completion.
 * STOP retains the lease; UNLOAD preserves last binding/backend readback. */
uint32_t trigger_gateway_load(uint32_t deployment_generation,uint32_t crc,
    uint32_t pulse_ns);
uint32_t trigger_gateway_stop(void);
uint32_t trigger_gateway_unload(void);
uint32_t trigger_gateway_snapshot(trigger_gateway_snapshot_t *out);
bool trigger_gateway_is_loaded(void);

#endif
