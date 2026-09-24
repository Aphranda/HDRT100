#ifndef SYNC_IO_ACTION_H
#define SYNC_IO_ACTION_H
#include <stdbool.h>
#include <stdint.h>
#define SYNC_IO_ACTION_SCHEMA 1u
#define SYNC_IO_ACTION_INSTRUCTION_WORDS 8u
#define SYNC_IO_ACTION_MIN_LEAD_US 100000u
enum {SYNC_IO_ACTION_SWITCH=1u,SYNC_IO_ACTION_GATEWAY=2u};
enum {SYNC_IO_ACTION_IDLE,SYNC_IO_ACTION_LOADED,SYNC_IO_ACTION_ARMED,
    SYNC_IO_ACTION_COMPLETE,SYNC_IO_ACTION_CANCELLED,SYNC_IO_ACTION_FAULT};
typedef struct {
    uint32_t kind,output_mask,code,input_port,pulse_ns;
} sync_io_action_config_t;
typedef struct {
    uint32_t schema,generation,state,loaded;
    sync_io_action_config_t config;
    uint32_t tick_hz,group_count,sm_claimed_mask,sm_enabled_mask;
    uint32_t instruction_words,program_offset,receipts,expected_receipts;
    uint32_t actual_output_mask,ready_level,ready_qualified,fault;
    uint64_t deadline_tick,falling_tick,enable_before_tick,enable_after_tick;
    uint64_t completed_observed_tick;
    uint32_t input_pin,group_base[2],group_count_pins[2];
} sync_io_action_snapshot_t;
/* Core0 STOP-maintenance caller owns business/model validity. LOAD is safe
 * low and inactive. ARM preloads a finite absolute TIMER1 deadline countdown
 * and returns long before its edge. No host-at-deadline or auto-roll-forward.
 * Actual edges retain a common nonnegative enable offset bounded by the
 * enable_before/after interval, plus pad effects. This is not 500ns accuracy
 * qualification. COMPLETE requires real executed-word receipts, not expiry.
 * Gateway READY is raw level only, ready_qualified remains zero (no latch).
 * STOP cancels pending output and forces low without fabricating completion.
 * RELEASE restores selected GPIO CTRL/PAD/SIO/PIO latches. No DMA/IRQ/arena. */
bool sync_io_action_load(const sync_io_action_config_t *config,uint32_t *generation);
bool sync_io_action_arm(uint32_t generation,uint64_t deadline_tick);
/* Optional owner identity recheck immediately before enable. Callback must
 * be bounded/read-only and must not call this backend recursively. A model
 * frozen after this handoff remains that execution's model, not a live servo. */
bool sync_io_action_arm_checked(uint32_t generation,uint64_t deadline_tick,
    bool (*validate)(void *context),void *context);
bool sync_io_action_stop(uint32_t generation);
bool sync_io_action_release(uint32_t generation);
bool sync_io_action_snapshot(sync_io_action_snapshot_t *out);
#endif
