#ifndef TRIGGER_SEQUENCE_LINK_H
#define TRIGGER_SEQUENCE_LINK_H
#include <stdbool.h>
#include <stdint.h>
#include "trigger_sequence_service.h"
#include "trigger_sequence_link_protocol.h"

#define TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY 256u
#define TRIGGER_SEQUENCE_LINK_HISTORY_SNAPSHOT_ATTEMPTS 4u
#define TRIGGER_SEQUENCE_LINK_HISTORY_VECTOR_VERSION 1u
#define TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY 4u
#define TRIGGER_SEQUENCE_LINK_READY_INJECT_MAX 256u
#define TRIGGER_SEQUENCE_LINK_HISTORY_REQUESTED 1u
#define TRIGGER_SEQUENCE_LINK_HISTORY_APPLIED 2u
#define TRIGGER_SEQUENCE_LINK_HISTORY_SAMPLE_DONE 4u
/* TIMER1 offsets from the position admission, retained without rounding.
 * These are Core1 observation/submission boundaries, not physical IO edges. */
#define TRIGGER_SEQUENCE_LINK_TIMING_VERSION 1u
typedef enum {
    TRIGGER_SEQUENCE_LINK_TIME_REQUEST,
    TRIGGER_SEQUENCE_LINK_TIME_APPLIED,
    TRIGGER_SEQUENCE_LINK_TIME_OFFERED,
    TRIGGER_SEQUENCE_LINK_TIME_RETURNED,
    TRIGGER_SEQUENCE_LINK_TIME_FIRE_QUEUED,
    TRIGGER_SEQUENCE_LINK_TIME_DONE,
    TRIGGER_SEQUENCE_LINK_TIME_COUNT
} trigger_sequence_link_time_t;
typedef struct {
    uint32_t ordinal, run_id, generation, binding_epoch, exchange_id;
    uint32_t position, sequence_index, sequence_state, output_code;
    /* position is one-based; sequence_index is zero-based. observed_pulses
     * is the Core1 counter snapshot at request acceptance, not an edge
     * timestamp. The tick fields are software admission/completion times. */
    uint32_t threshold_pulses, observed_pulses;
    uint32_t trigger_ordinal, ready_ordinal;
    uint32_t position_admitted_tick_ms, sample_done_tick_ms, cycle_elapsed_ms;
    uint32_t outcome_flags;
    uint32_t timing_flags;
    uint64_t timing_ticks[TRIGGER_SEQUENCE_LINK_TIME_COUNT];
} trigger_sequence_link_history_t;

/* Trigger diagnostic vector. Core1 LINK is the sole writer. Core0 readers
 * copy atomic words with a bounded seqlock retry; readers never hold a lock.
 * Identity is frozen at run admission. STOP/configure preserve that run's
 * partial trace; the next Core1 run admission invalidates the old window. */
typedef struct {
    uint32_t version, clock_hz, capacity, run_id, generation, binding_epoch;
    uint32_t threshold, total, retained, overwritten;
} trigger_sequence_link_history_status_t;

typedef struct {
    bool enabled;
    uint32_t dut_slot, vna_slot;
    uint32_t ready_input; /* 0=MANUAL; 1..4=logical IN. */
    uint32_t trigger_output_mask, pulse_us, timeout_ms;
    bool falling;
    /* Each threshold starts a full plan round. READY advances only within
     * that round; the last READY waits for the next turntable position. */
    bool counter_enabled;
    uint32_t counter_slot, counter_input, counter_threshold;
} trigger_sequence_link_config_t;
typedef enum {
    TRIGGER_SEQUENCE_LINK_CONFIG_OK = 0,
    TRIGGER_SEQUENCE_LINK_CONFIG_INVALID,
    TRIGGER_SEQUENCE_LINK_CONFIG_ACTIVE,
    TRIGGER_SEQUENCE_LINK_CONFIG_EPOCH_EXHAUSTED,
    TRIGGER_SEQUENCE_LINK_CONFIG_ROLE_INVALID,
    TRIGGER_SEQUENCE_LINK_CONFIG_COUNTER_INVALID,
    TRIGGER_SEQUENCE_LINK_CONFIG_GATEWAY_INVALID,
    TRIGGER_SEQUENCE_LINK_CONFIG_SERVICE_BUSY,
    TRIGGER_SEQUENCE_LINK_CONFIG_WRITER_BUSY,
    TRIGGER_SEQUENCE_LINK_CONFIG_TDMA_REJECTED,
    TRIGGER_SEQUENCE_LINK_CONFIG_GATEWAY_REJECTED,
} trigger_sequence_link_config_result_t;
/* Per-call Core0 diagnostics; detail values use the owning domain's enums. */
typedef struct {
    trigger_sequence_link_config_result_t result;
    uint32_t tdma_result, gateway_result, rollback_result;
} trigger_sequence_link_config_diagnostic_t;
typedef struct {
    trigger_sequence_link_config_t config;
    uint32_t phase, error, binding_epoch, model_epoch;
    uint32_t run_id, generation, step, tx_fragments, rx_messages, rejected;
    uint32_t triggers, ready, completed;
    uint32_t repeat_count;
    uint32_t exchange_id;
    uint32_t counter_events, counter_consumed, counter_partial, counter_fault_events;
    uint32_t history_total, history_retained;
    /* TIMER1 deltas on Core1, floor-rounded to milliseconds for SCPI. */
    uint32_t offer_delay_ms, return_delay_ms, inbox_delay_ms, message_total_ms;
} trigger_sequence_link_status_t;
/* Core0 configuration/commands; called by RefMem owner and SCPI.
 * This implementation binds two or three local roles; physical topology addresses
 * remain transport-owned and are not confused with RefMem logical role slots. */
bool trigger_sequence_link_configure(const trigger_sequence_link_config_t *config);
trigger_sequence_link_config_diagnostic_t trigger_sequence_link_configure_checked(
    const trigger_sequence_link_config_t *config);
/* Core0 only, configuration_begin held. Commits POSITION binding and finite
 * repeat together; on failure neither binding nor repeat is changed. */
bool trigger_sequence_link_configure_position_locked(
    const trigger_sequence_link_config_t *config, uint32_t repeat_count);
trigger_sequence_service_result_t trigger_sequence_link_next(void);
trigger_sequence_service_result_t trigger_sequence_link_ready_inject(uint32_t count);
void trigger_sequence_link_get_status(trigger_sequence_link_status_t *status);
/* Validates the caller's snapshot against the current binding and RefMem
 * model generation, including model changes before Core1 observes them. */
bool trigger_sequence_link_binding_is_current(uint32_t binding_epoch, uint32_t model_epoch);
bool trigger_sequence_link_get_history(uint32_t ordinal, trigger_sequence_link_history_t *record);
/* False means no coherent snapshot; output remains untouched. */
bool trigger_sequence_link_get_history_status(trigger_sequence_link_history_status_t *status);
/* Core1 only: bounded RUN FSM step, after the sequence IO owner service.
 * True means an IO intent/STOP was submitted; service the owner once more. */
bool trigger_sequence_link_service(void);
/* Core1 RefMem flight phase only. RX copies complete messages to a bounded
 * inbox; neither transport API executes runtime IO actions. */
bool trigger_sequence_link_tx_fragment(uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE]);
void trigger_sequence_link_rx_fragment(uint32_t physical_source,
    const uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE]);
#endif
