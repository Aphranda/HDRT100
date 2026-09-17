#ifndef TRIGGER_SEQUENCE_LINK_H
#define TRIGGER_SEQUENCE_LINK_H
#include <stdbool.h>
#include <stdint.h>
#include "trigger_sequence_service.h"
#include "trigger_sequence_link_protocol.h"

#define TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY 32u
#define TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY 4u
#define TRIGGER_SEQUENCE_LINK_HISTORY_REQUESTED 1u
#define TRIGGER_SEQUENCE_LINK_HISTORY_APPLIED 2u
#define TRIGGER_SEQUENCE_LINK_HISTORY_SAMPLE_DONE 4u
typedef struct {
    uint32_t ordinal, run_id, generation, position, sequence_index;
    /* position is one-based; sequence_index is zero-based. observed_pulses
     * is the Core1 snapshot at request acceptance, not an edge timestamp. */
    uint32_t threshold_pulses, observed_pulses;
    uint32_t outcome_flags;
} trigger_sequence_link_history_t;

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
typedef struct {
    trigger_sequence_link_config_t config;
    uint32_t phase, error, binding_epoch, model_epoch;
    uint32_t run_id, generation, step, tx_fragments, rx_messages, rejected;
    uint32_t triggers, ready, completed;
    uint32_t repeat_count;
    uint32_t exchange_id;
    uint32_t counter_events, counter_consumed, counter_partial, counter_fault_events;
    uint32_t history_total, history_retained;
} trigger_sequence_link_status_t;
/* Core0 configuration/commands; called by RefMem owner and SCPI.
 * This implementation binds two or three local roles; physical topology addresses
 * remain transport-owned and are not confused with RefMem logical role slots. */
bool trigger_sequence_link_configure(const trigger_sequence_link_config_t *config);
trigger_sequence_service_result_t trigger_sequence_link_next(void);
void trigger_sequence_link_get_status(trigger_sequence_link_status_t *status);
bool trigger_sequence_link_get_history(uint32_t ordinal, trigger_sequence_link_history_t *record);
/* Core1 only: bounded RUN FSM step, after the sequence IO owner service.
 * True means an IO intent/STOP was submitted; service the owner once more. */
bool trigger_sequence_link_service(void);
/* Core0 RefMem transport task only. RX copies complete messages to a bounded
 * SPSC inbox; neither transport API executes runtime IO actions. */
bool trigger_sequence_link_tx_fragment(uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE]);
void trigger_sequence_link_rx_fragment(uint32_t physical_source,
    const uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE]);
#endif
