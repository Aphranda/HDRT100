#ifndef VDC_DPLL_MANAGER_INTERNAL_H
#define VDC_DPLL_MANAGER_INTERNAL_H

#include "vdc_dpll_manager.h"
#include "vdc_output_timing.h"
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

#endif /* VDC_DPLL_MANAGER_INTERNAL_H */
