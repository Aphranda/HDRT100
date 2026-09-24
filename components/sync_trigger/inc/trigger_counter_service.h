#ifndef TRIGGER_COUNTER_SERVICE_H
#define TRIGGER_COUNTER_SERVICE_H

#include "trigger_deployment_service.h"
#include "sync_io_pulse_counter.h"

enum {
    COUNTER_OK=0u, COUNTER_ARGUMENT, COUNTER_BUSY, COUNTER_NOT_IDLE,
    COUNTER_DEPLOYMENT, COUNTER_WRONG_ROLE, COUNTER_CHANGED,
    COUNTER_HARDWARE, COUNTER_NOT_LOADED
};
typedef struct {
    trigger_deployment_binding_t binding;
    sync_io_pulse_counter_snapshot_t io;
    uint32_t loaded;
} trigger_counter_snapshot_t;

/* First COUNTER slice: Core0, stopped TDMA maintenance, input counting only.
 * Deployment identity selects the local slot; board numbers never authorize IO.
 * The sample timestamp describes observing a count, not latching an input edge. */
uint32_t trigger_counter_load(uint32_t deployment_generation, uint32_t crc);
uint32_t trigger_counter_start(void);
uint32_t trigger_counter_stop(void);
uint32_t trigger_counter_clear(void);
uint32_t trigger_counter_unload(void);
uint32_t trigger_counter_snapshot(trigger_counter_snapshot_t *out);
uint32_t trigger_counter_snapshot_core1(trigger_counter_snapshot_t *out);
bool trigger_counter_is_loaded(void);
#endif
