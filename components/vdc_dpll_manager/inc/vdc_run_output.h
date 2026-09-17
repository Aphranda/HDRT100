#ifndef VDC_RUN_OUTPUT_H
#define VDC_RUN_OUTPUT_H
#include "sync_io_run_output.h"

typedef struct {
    sync_io_run_output_snapshot_t hardware;
    uint64_t last_local_ns, last_target_vdc_ns, maximum_bridge_width_ticks;
    uint32_t request, session, period_ns, high_ns, duration_ms;
    uint32_t ring_config, role_generation, clock_epoch, clock_run;
    uint32_t prepared_config, arm_config;
    uint32_t blocks_planned, plan_rejects, binding_rejects;
    int32_t delay_ns;
} vdc_run_output_status_t;

bool vdc_run_output_prepare(uint32_t period_ns,uint32_t high_ns,
                            uint32_t duration_ms,uint32_t *request);
void vdc_run_output_cancel(void);
bool vdc_run_output_status(vdc_run_output_status_t *out);
void vdc_run_output_service_core1(void);
#endif
