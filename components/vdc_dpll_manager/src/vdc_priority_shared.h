#ifndef VDC_PRIORITY_SHARED_H
#define VDC_PRIORITY_SHARED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_pio_spi_phys.h"
#include "tdma_origin_plan.h"
#include "vdc_dpll_manager.h"
#include "vdc_priority_codec.h"
#include "vdc_priority_follow.h"
#include "vdc_priority_match.h"
#include "vdc_priority_phase.h"
#include "vdc_priority_rx.h"
#include "vdc_priority_trace.h"
#include "vdc_priority_tx.h"

/* Types shared by the priority-lane units. They used to live in the
 * fragments textually included into vdc_dpll_manager.c, so any fragment
 * could reach them. Order matters: dependencies come first. */

typedef struct {
    uint64_t offer_reference_lo;
    uint32_t session, probe, consumed, config, role, schedule, source, nodes;
    int32_t delta;
    uint32_t offer, serial, offer_slot, authorization;
    uint32_t tx_count, apply_count, reject_count, last_reject, cursor, halted;
    uint32_t offer_reference_model;
    uint32_t first_reject, reject_mask, mode;
} vdc_boundary_meta_t;

typedef struct {
    uint64_t arm, source_output_lo;
    uint32_t clock_epoch, clock_run, observer, model;
    uint32_t command, measurement, offered_ms;
    uint16_t state;
    int16_t delta;
} vdc_boundary_peer_t;

typedef struct {
    uint32_t config, role, schedule, clock_epoch, clock_run, local, nodes, reference_epoch, session, rate_mode;
} vdc_feedback_match_binding_t;

typedef struct {
    uint64_t next_ordinal, arm_epoch;
    vdc_feedback_match_lifetime_t reference;
    uint32_t insertion_serial, serial, latest_sequence, has_sequence;
    uint32_t delay_ns, path_crc, missed, projection_rejects;
} vdc_local_follow_cursor_t;

typedef struct {
    union { vdc_feedback_match_peer_t peer; vdc_local_follow_cursor_t local; };
    uint32_t words[sizeof(vdc_dpll_manager_feedback_match_status_t) / 4u];
    uint32_t guard, consumed;
} vdc_feedback_match_source_t;

typedef struct {
    uint64_t local_lo, local_hi, expected_lo, expected_hi, raw_lo;
    uint64_t normalized_lo, normalized_hi;
    uint32_t sequence, model_token, rate_epoch, reserved;
} vdc_priority_follow_sample_t;

typedef struct {
    vdc_priority_match_snapshot_t match;
    vdc_dpll_manager_committed_model_t model;
    uint64_t anchor_before, anchor_after;
    uint64_t normalized_lo, normalized_hi;
    uint32_t path_table_crc, servo_crc, prepared_ms, rate_epoch;
} vdc_priority_follow_ticket_t;

typedef struct {
    vdc_priority_follow_snapshot_t status;
    vdc_priority_follow_sample_t previous;
    vdc_priority_follow_ticket_t ticket;
    uint64_t local_delta_lo, local_delta_hi, expected_delta_lo, expected_delta_hi;
    int64_t error_lo, error_hi;
    uint32_t have_baseline, pending, bound, have_consumed, consumed_sequence;
    uint32_t next_evaluation; /* Private next threshold index; never refunded. */
    uint32_t baseline_replacements; /* Bounded early refinement; Core1 only. */
    vdc_priority_follow_baseline_config_t baseline_config; /* Latched once per binding. */
} vdc_priority_follow_work_t;

typedef struct {
    vdc_priority_match_snapshot_t status;
    tdma_ring_clock_snapshot_t ring, ring_after;
    tdma_pio_spi_event_live_snapshot_t live, live_after;
    tdma_pio_spi_event_exact_t exact;
    vdc_priority_rx_snapshot_t rx, rx_after;
    vdc_dpll_manager_committed_model_t model, model_after;
    vdc_dpll_manager_timer1_projection_t projection;
    vdc_path_delay_entry_t path, path_after;
    uint64_t raw_lo, raw_hi, remote_hi, expected_lo, expected_hi;
    int64_t residual_lo, residual_hi;
    uint32_t profile_crc32, session, bound, unresolved, candidate_sequence, path_checked_crc;
    /* Private, one-service Core1 ticket. Historical snapshot reads never set it. */
    uint32_t fresh;
    /* Owner declaration from the residuals; reset with the request (memset). */
    vdc_priority_lock_word_t lock;
} vdc_priority_match_work_t;

typedef struct {
    vdc_priority_phase_snapshot_t status;
    vdc_dco_control_t expected_dco;
    int64_t next_cumulative;
    uint32_t known_model, initialized, pending, receipt_pending;
    uint32_t have_attempt, last_attempt_ms, consumed_sequence;
} vdc_priority_phase_work_t;

typedef struct __attribute__((packed, may_alias)) {
    uint16_t bin_index, flags;
    uint64_t observed_start_raw, observed_end_raw;
    uint32_t max_service_gap_ticks, max_success_gap_ticks;
    uint32_t first_event, last_event, first_success_offset_ticks, last_success_offset_ticks;
    uint16_t service_count, success_count, rejected_count, cancelled_count;
    uint16_t phase_held_count, decision_count, frequency_applied_count, phase_applied_count;
    int64_t residual_min_ns, residual_max_ns;
    uint32_t max_width_ns, first_model, last_model;
    int32_t min_ppb, max_ppb;
    uint16_t model_changes, outcome_mask;
} vdc_priority_summary_record_t;

typedef struct {
    uint64_t last_service, last_success, interval_ticks;
    uint32_t counters[7];
    uint32_t first_ms, last_ms, open, have_success, last_model;
    uint32_t clock_epoch, clock_run;
    uint32_t counter_valid_mask;
} vdc_priority_summary_work_t;

typedef struct {
    uint32_t sequence, command, capture_id, session, generation, mode, boundary_request, schema, interval_ms, guard_s;
} vdc_priority_trace_request_t;

typedef struct {
    vdc_priority_trace_status_t status;
    uint64_t last_sample_raw;
    union {
        uint8_t origin[VDC_PRIORITY_TRACE_ORIGIN_EXTENSION_BYTES];
        vdc_priority_summary_work_t summary;
    };
    uint32_t boundary_request, role_generation, seen_running, bound;
    uint32_t have_match, last_match_sequence, have_decision, last_decision_sequence;
} vdc_priority_trace_work_t;

typedef struct {
    vdc_priority_tx_snapshot_t status;
    tdma_ring_runtime_config_t binding;
    tdma_origin_raw_reference_t source, seen;
    vdc_dpll_manager_committed_model_t model, after;
    vdc_dpll_manager_projected_event_t event;
    vdc_priority_codec_record_t record;
    vdc_priority_tx_origin_evidence_t evidence;
    uint32_t bound, have_seen, tick_hz;
    uint8_t candidate[TDMA_FLIGHT_SHORT_SLOT_SIZE];
} vdc_priority_tx_work_t;

/* State shared by the priority-lane units. */
extern vdc_feedback_match_source_t s_feedback_matches[PROJECT_NODE_CAPACITY];
extern uint32_t s_feedback_match_active_generation;
extern uint32_t s_feedback_match_owner_token;
extern uint32_t s_priority_tx_request;
extern vdc_priority_tx_work_t s_priority_tx_work;
extern uint32_t s_boundary_request_priority_generation;
extern uint32_t s_priority_match_request_session;
extern vdc_priority_match_work_t s_priority_match_work;
extern vdc_priority_follow_work_t s_priority_follow_work;
extern vdc_priority_trace_work_t s_priority_trace_work;
extern bool s_priority_host_control_written;
extern vdc_priority_phase_work_t s_priority_phase_work;
extern uint32_t s_priority_phase_request;

void priority_note_host_control(void);

extern vdc_dpll_manager_dpll_capture_record_t s_dpll_capture_records[VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES];

void boundary_store(uint32_t *words, const void *input, size_t size);

void match_load_words(const uint32_t *words, void *out, size_t size);

__attribute__((noinline)) void priority_trace_service_core1(void);

__attribute__((noinline)) void priority_trace_match_core1(void);

void priority_summary_service(bool end);

void priority_guard_service_core1(void);

enum { DPLL_CAPTURE_POOL_LEGACY = 0u, DPLL_CAPTURE_POOL_TYPED = 1u,
       DPLL_CAPTURE_POOL_LEGACY_ACCESS = 2u, DPLL_CAPTURE_POOL_TYPED_READ = 3u,
       DPLL_CAPTURE_POOL_RELEASING = 4u };

extern uint32_t s_dpll_capture_pool_owner;

void priority_guard_service_core0(void);

void priority_trace_decision_core1(const vdc_priority_follow_snapshot_t *decision);

void priority_trace_phase_core1(const vdc_priority_phase_snapshot_t *phase);

static inline uint32_t priority_match_inc(uint32_t n) { return n == UINT32_MAX ? n : n + 1u; }

typedef struct { int32_t delta; uint32_t mode, priority_generation; } vdc_boundary_request_t;

extern uint32_t s_committed_model_guard, s_committed_model_serial;

static inline uint32_t match_inc(uint32_t value) { return value == UINT32_MAX ? value : value + 1u; }

extern uint32_t s_boundary_guard, s_boundary_request;

extern uint32_t s_priority_match_request, s_priority_match_last_request;

void phase_model_committed_core1(const vdc_dpll_manager_committed_model_t *model);

#endif /* VDC_PRIORITY_SHARED_H */
