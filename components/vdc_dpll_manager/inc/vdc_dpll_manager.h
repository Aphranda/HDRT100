#ifndef VDC_DPLL_MANAGER_H
#define VDC_DPLL_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "vdc_domain.h"

#define VDC_DPLL_MANAGER_PLAN_NOW_NS UINT64_MAX

typedef struct {
    bool ready;
    uint32_t lock_state;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
    uint32_t sync_seq;
} vdc_dpll_manager_vdc_status_t;

typedef struct {
    bool ready;
    uint32_t state;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
    uint32_t update_seq;
} vdc_dpll_manager_dpll_status_t;

bool vdc_dpll_manager_init(void);
void vdc_dpll_manager_set_vdc_ready(bool ready);
void vdc_dpll_manager_set_dpll_ready(bool ready);
void vdc_dpll_manager_vdc_service(void);
void vdc_dpll_manager_dpll_service(void);
void vdc_dpll_manager_get_vdc_status(vdc_dpll_manager_vdc_status_t *status);
void vdc_dpll_manager_get_dpll_status(vdc_dpll_manager_dpll_status_t *status);
bool vdc_dpll_manager_get_snapshot(vdc_domain_snapshot_t *snapshot);
bool vdc_dpll_manager_plan_tdma_window(uint32_t window_class,
                                       uint64_t now_ns,
                                       vdc_tdma_window_plan_t *plan,
                                       vdc_gate_result_t *gate);
bool vdc_dpll_manager_publish_timestamp_dictionary(
    const vdc_timestamp_dictionary_t *dictionary,
    uint32_t initial_tick_l32);
bool vdc_dpll_manager_submit_compact_observation(
    const vdc_compact_observation_sample_t *compact);

#endif
