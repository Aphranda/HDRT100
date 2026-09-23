#ifndef TRIGGER_SWITCH_SERVICE_H
#define TRIGGER_SWITCH_SERVICE_H
#include "trigger_deployment_service.h"
#include "sync_io_switch.h"

enum {
    SWITCH_OK=0u, SWITCH_ARGUMENT, SWITCH_BUSY, SWITCH_NOT_IDLE,
    SWITCH_DEPLOYMENT, SWITCH_WRONG_ROLE, SWITCH_CHANGED,
    SWITCH_HARDWARE, SWITCH_NOT_LOADED
};
typedef struct {
    trigger_deployment_binding_t binding;
    sync_io_switch_snapshot_t io;
    uint32_t loaded;
} trigger_switch_snapshot_t;

/* Core0 STOP maintenance only. SWITCH_A/SWITCH_B are deployment roles,
 * not fixed board numbers. Their output mask selects any local OUT subset.
 * STAGE stores a code without output. APPLY is an explicit maintenance
 * operation, not a VDC reservation, ARM, READY or RUN authority. No clock,
 * fence or physical completion qualification is implied by a command ACK. */
uint32_t trigger_switch_load(uint32_t deployment_generation,uint32_t crc);
uint32_t trigger_switch_stage(uint32_t code);
uint32_t trigger_switch_apply(void);
uint32_t trigger_switch_stop(void);
uint32_t trigger_switch_unload(void);
uint32_t trigger_switch_snapshot(trigger_switch_snapshot_t *out);
bool trigger_switch_is_loaded(void);
#endif
