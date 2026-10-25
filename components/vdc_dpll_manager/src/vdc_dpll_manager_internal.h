#ifndef VDC_DPLL_MANAGER_INTERNAL_H
#define VDC_DPLL_MANAGER_INTERNAL_H

#include "vdc_dpll_manager.h"
#include "vdc_output_timing.h"
#include "vdc_priority_shared.h"
#include "vdc_reference.h"
#include "vdc_run_output.h"

/* State and helpers shared by the VDC command units that used to be textually
 * included into vdc_dpll_manager.c. */

/* Registered TDMA service used by the configuration command units. */
extern tdma_service_service_t *s_vdc_tdma_service;

/* Discipline request latch written by the reference configuration unit and
 * read by the discipline unit. */
extern uint32_t s_reference_discipline_request;

bool priority_follow_baseline_init_from_product_config(void);

bool output_delay_init_from_product_config(void);

bool output_timing_init_from_product_config(void);

bool reference_init_from_product_config(void);

void reference_release_core0(void);

__attribute__((noinline)) void vdc_priority_ingress_core1(void);

extern uint32_t s_reference_discipline_capture_generation;

extern uint32_t s_reference_discipline_armed[5];

void priority_gap_reset(uint32_t generation);

void priority_tx_publish(void);

/* Trace callback invoked by the priority-tx unit. */
void priority_trace_origin_core1(const vdc_priority_tx_origin_evidence_t *evidence);

extern vdc_domain_context_t s_vdc_domain;

extern bool s_dpll_capture_armed;

extern bool s_dpll_capture_complete;

extern uint32_t s_dpll_capture_count;

extern uint32_t s_dpll_capture_dropped;

extern uint32_t s_dpll_capture_first_update_seq;

extern uint32_t s_dpll_capture_last_update_seq;

extern uint32_t s_dpll_capture_start_ms;

extern uint32_t s_dpll_capture_end_ms;

bool boundary_request(uint32_t *generation, uint32_t *session, int32_t *delta, uint32_t *mode);

void boundary_capture_u32(uint8_t *p, uint32_t value);

void boundary_capture_u64(uint8_t *p, uint64_t value);

extern volatile uint32_t s_dpll_role_requested_generation;

extern volatile uint32_t s_dpll_role_applied_generation;

void model_feedback_load(vdc_dpll_manager_committed_model_t *out);

uint64_t vdc_dpll_manager_now_ns(void);

void vdc_dpll_manager_publish_runtime_snapshot_locked(void);

bool boundary_set_stopped(void *context);

__attribute__((noinline)) void vdc_boundary_service_core1(void);

void priority_lock_publish(void);

void priority_match_publish(void);

__attribute__((noinline)) void priority_follow_prepare_core1(void);

__attribute__((noinline)) void priority_follow_apply_core1(void);

void priority_follow_health_service_core1(void);

__attribute__((noinline)) void vdc_dpll_manager_feedback_match_retire(void);

__attribute__((noinline)) void vdc_dpll_manager_feedback_match_service(void);

__attribute__((noinline)) void model_feedback_end_core1(uint32_t session);

extern vdc_dpll_manager_observation_self_test_status_t s_observation_self_test;

extern bool s_published_snapshot_valid;

extern volatile uint32_t s_published_snapshot_guard;

extern vdc_domain_snapshot_t s_published_snapshot;

void reference_discipline_service_core1(uint32_t session);

void run_output_release_core0(void);

void vdc_fixed_output_service_core0(void);

#endif /* VDC_DPLL_MANAGER_INTERNAL_H */
