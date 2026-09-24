#ifndef TRIGGER_ACTION_SERVICE_H
#define TRIGGER_ACTION_SERVICE_H
#include "trigger_action_table.h"
#include "trigger_deployment_service.h"
#include "sync_io_action.h"

enum {
    TRIGGER_ACTION_SERVICE_BUSY=32u, TRIGGER_ACTION_SERVICE_NOT_IDLE,
    TRIGGER_ACTION_SERVICE_UNCONFIGURED, TRIGGER_ACTION_SERVICE_CHANGED,
    TRIGGER_ACTION_SERVICE_GENERATION, TRIGGER_ACTION_SERVICE_DIGEST
};
enum {TRIGGER_ACTION_SERVICE_EMPTY=0u, TRIGGER_ACTION_SERVICE_INSTALLED_CONFIG=1u};
typedef struct {
    trigger_deployment_binding_t binding;
    trigger_action_table_t table;
    uint32_t crc,last_generation,installed,local_row_mask,binding_current;
    uint32_t state,execution_authorized,armed,deadline_valid;
    /* The embedded grid is an unqualified installation definition. Neither
     * equality across boards nor a successful readback authenticates A. */
    uint32_t anchor_authenticated;
} trigger_action_snapshot_t;

/* Core0 SRAM-only STOP metadata transaction. Complete shared table, no
 * IO acquisition, FIFO submission, model mutation, deadline, ARM or READY.
 * Identical same-generation retries require the same full local binding.
 * CLEAR retains the consumed generation high-water mark. Errors preserve
 * installed content and caller outputs. Existing IO leases are untouched. */
uint32_t trigger_action_install(const trigger_action_table_t *table,uint32_t expected_crc);
uint32_t trigger_action_clear(void);
uint32_t trigger_action_snapshot(trigger_action_snapshot_t *out);
/* Only return row arithmetic while its deployment binding remains current.
 * A stale table remains inspectable through snapshot(binding_current=0). */
uint32_t trigger_action_row(uint32_t index,trigger_action_row_t *row,uint64_t *target_vdc_ns);

#define TRIGGER_ACTION_EXECUTION_MIN_LEAD_NS UINT64_C(100000000)
#define TRIGGER_ACTION_EXECUTION_MAX_AHEAD_NS UINT64_C(12000000000)
enum {
    TRIGGER_ACTION_EXECUTION_MODEL=40u, TRIGGER_ACTION_EXECUTION_CLOCK,
    TRIGGER_ACTION_EXECUTION_NOT_LOCAL, TRIGGER_ACTION_EXECUTION_HARDWARE,
    TRIGGER_ACTION_EXECUTION_NOT_LOADED, TRIGGER_ACTION_EXECUTION_EXPIRED,
    TRIGGER_ACTION_EXECUTION_ALREADY_EXECUTED, TRIGGER_ACTION_EXECUTION_LATE
};
typedef struct {
    vdc_dpll_manager_committed_model_t model;
    uint64_t raw_before_tick,raw_after_tick,vdc_before_ns,vdc_after_ns;
    uint32_t tick_hz,diagnostic,anchor_authenticated;
} trigger_action_now_t;
typedef struct {
    trigger_action_snapshot_t installation;
    vdc_dpll_manager_committed_model_t model;
    trigger_time_grid_plan_t plan;
    sync_io_action_snapshot_t io;
    uint64_t prepared_at_tick,expires_at_tick,arm_checked_tick;
    uint32_t loaded,row_index,binding_current,model_current;
    uint32_t diagnostic,anchor_authenticated,actual_event_valid;
    uint32_t consumed_table_generation,consumed_row_mask;
} trigger_action_execution_snapshot_t;

/* STOP diagnostic execution. Exactly one installed local row, with a frozen
 * committed TIMER1/DCO map. Each operation keeps the same owner gate and uses
 * stopped maintenance rather than nesting the installation metadata gate.
 * PREPARE acquires inactive hardware only; ARM submits a finite future local
 * deadline, with no host action at that deadline and no automatic replan.
 * Current common epoch/A is an experiment label, never lock qualification.
 * io.enable_before/after bounds hardware-start origin, NOT an edge latch;
 * actual_event_valid and anchor_authenticated remain zero. Model changes
 * before ARM reject; once submitted, output follows the frozen model. */
uint32_t trigger_action_execution_now(trigger_action_now_t *out);
uint32_t trigger_action_execution_prepare(uint32_t row_index);
uint32_t trigger_action_execution_arm(void);
uint32_t trigger_action_execution_stop(void);
uint32_t trigger_action_execution_release(void);
uint32_t trigger_action_execution_snapshot(trigger_action_execution_snapshot_t *out);
bool trigger_action_execution_is_loaded(void);
#endif
