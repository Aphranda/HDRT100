#include "app_refmem_domain_config.h"

#include <stdint.h>

#include "app.h"
#include "refmem_command.h"
#include "refmem_domain_config.h"
#include "tdma_runtime_owner.h"
#include "trigger_action_template_store.h"
#include "trigger_deployment_service.h"
#include "trigger_grid_preview.h"
#include "trigger_t2_runtime.h"
#include "trigger_turntable_predictor.h"
#include "vdc_dpll_manager.h"
#include "vdc_output_delay.h"
#include "vdc_output_timing.h"
#include "vdc_reference.h"
#include "vdc_run_output.h"

/* N6.2: DPLL.  N6.3: TDMA online seat/load; topology/period stay
 * deployment-time and fail closed with NOT_ONLINE_SAFE.  VDC/T2 handlers are
 * added in N6.3/N6.4. */

static bool reject(uint32_t *reason)
{
    if (reason != NULL) {
        *reason = (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
    }
    return false;
}

static bool reject_not_online(uint32_t *reason)
{
    if (reason != NULL) {
        *reason = (uint32_t)REFMEM_COMMAND_REASON_NOT_ONLINE_SAFE;
    }
    return false;
}

static bool reject_run_state(uint32_t *reason)
{
    if (reason != NULL) {
        *reason = (uint32_t)REFMEM_COMMAND_REASON_RUN_STATE_DENIED;
    }
    return false;
}

static bool dpll_role_handler(const uint32_t *params, uint32_t param_words,
                              uint32_t *reason)
{
    uint32_t generation = 0u;
    if (params == NULL || param_words != 2u ||
        !vdc_dpll_manager_request_dpll_role(params[0], params[1],
                                            &generation)) {
        return reject(reason);
    }
    return true;
}

static bool dpll_tune_handler(const uint32_t *params, uint32_t param_words,
                              uint32_t *reason)
{
    uint32_t generation = 0u;
    if (params == NULL || param_words != 5u ||
        !vdc_dpll_manager_request_debug_servo_tune(
            (int32_t)params[0], (int32_t)params[1], params[2], params[3],
            params[4], &generation)) {
        return reject(reason);
    }
    return true;
}

static bool dpll_override_handler(const uint32_t *params, uint32_t param_words,
                                  uint32_t *reason)
{
    uint32_t generation = 0u;
    if (params == NULL || param_words != 1u ||
        !vdc_dpll_manager_request_debug_continue(params[0] != 0u,
                                                 &generation)) {
        return reject(reason);
    }
    return true;
}

static bool tdma_ring_topology_handler(const uint32_t *params,
                                       uint32_t param_words, uint32_t *reason)
{
    (void)params;
    (void)param_words;
    return reject_not_online(reason);
}

static bool tdma_seat_rotation_handler(const uint32_t *params,
                                       uint32_t param_words, uint32_t *reason)
{
    if (params == NULL || param_words != 3u ||
        !tdma_runtime_owner_commit_seat_rotation3(params[0], params[1],
                                                  params[2])) {
        return reject(reason);
    }
    return true;
}

static bool tdma_period_handler(const uint32_t *params, uint32_t param_words,
                                uint32_t *reason)
{
    (void)params;
    (void)param_words;
    return reject_not_online(reason);
}

static bool tdma_load_mask_handler(const uint32_t *params, uint32_t param_words,
                                   uint32_t *reason)
{
    if (params == NULL || param_words != 1u ||
        !app_realtime_set_load_mask(params[0])) {
        return reject(reason);
    }
    return true;
}

static bool t2_configure_handler(const uint32_t *params, uint32_t param_words,
                                 uint32_t *reason)
{
    if (params == NULL || param_words != 13u) {
        return reject(reason);
    }
    if (params[3] > 1u || params[6] > 999u ||
        params[12] > TRIGGER_T2_DESCRIPTOR_WINDOW_MAX) {
        return reject(reason);
    }

    trigger_t2_runtime_snapshot_t state;
    if (!trigger_t2_runtime_snapshot(&state) || state.active) {
        return reject(reason);
    }

    trigger_grid_preview_snapshot_t grid;
    trigger_deployment_snapshot_t deployment;
    trigger_action_template_store_snapshot_t templates;
    trigger_turntable_scan_geometry_t geometry;
    if (trigger_grid_preview_snapshot(&grid) || !grid.configured) {
        return reject(reason);
    }
    if (trigger_deployment_snapshot(&deployment) || !deployment.active.valid) {
        return reject(reason);
    }
    if (trigger_action_template_snapshot(&templates) || !templates.installed ||
        !templates.resources_loaded || !templates.binding_current) {
        return reject(reason);
    }

    const int32_t step_mdeg = (int32_t)params[10];
    if (!trigger_t2_runtime_scan_step(step_mdeg)) {
        return reject(reason);
    }

    trigger_t2_runtime_config_t config = {0};
    config.grid = grid.grid;
    config.predictor = (trigger_turntable_predictor_config_t){
        .pulses_per_degree = params[11],
        .start_angle_mdeg = (int32_t)params[7],
        .zero_angle_mdeg = (int32_t)params[8],
        .terminal_angle_mdeg = (int32_t)params[9],
        .pre_roll_mdeg = params[4],
        .post_roll_mdeg = params[5],
        .threshold_per_mille = params[6]};
    config.templates = templates.table;
    config.deployment = deployment.active.table;
    config.template_crc = templates.crc;
    config.run_id = params[0];
    config.local_role = deployment.active.local_role;
    config.source_slot = params[1];
    config.target_mask = params[2];
    config.descriptor_window_sync = params[12] != 0u
        ? params[12] : TRIGGER_T2_DESCRIPTOR_TARGET_GRACE_SYNC;
    config.origin = params[3];

    if (trigger_turntable_scan_validate(&config.predictor, step_mdeg, 0u,
                                        &geometry) != TRIGGER_TURNTABLE_SCAN_OK) {
        return reject(reason);
    }
    if (geometry.step_pulses * (1000u - params[6]) / 1000u <
        TRIGGER_T2_MIN_DECLARE_LEAD_PULSES) {
        return reject(reason);
    }
    if (!trigger_t2_runtime_configure(&config)) {
        return reject(reason);
    }
    return true;
}

static bool t2_mount_handler(const uint32_t *params, uint32_t param_words,
                             uint32_t *reason)
{
    (void)params;
    (void)param_words;
    /* TDMA is the non-stop bearer, so the STOP-only position reset that
     * precedes the local T2 MOUNT cannot run here.  T2 start stays a local
     * action until a running-safe position-reset path is proven; the unified
     * plane only stops and configures T2. */
    return reject_not_online(reason);
}

static bool t2_unmount_handler(const uint32_t *params, uint32_t param_words,
                               uint32_t *reason)
{
    (void)params;
    (void)param_words;
    if (param_words != 0u) {
        return reject(reason);
    }
    trigger_t2_runtime_stop();
    return true;
}

static bool vdc_stop_handler(const uint32_t *params, uint32_t param_words,
                             uint32_t *reason)
{
    (void)params;
    if (param_words != 0u) {
        return reject(reason);
    }
    /* Stop VDC run output and disable the reference while TDMA keeps running;
     * the following config steps then install into an idle VDC. */
    vdc_run_output_cancel();
    (void)vdc_dpll_manager_set_reference_enabled(false);
    return true;
}

static bool vdc_reference_config_handler(const uint32_t *params,
                                         uint32_t param_words,
                                         uint32_t *reason)
{
    if (params == NULL || param_words != 5u) {
        return reject(reason);
    }
    const sync_io_reference_config_t config = {
        .input_port = params[0],
        .edge = params[1],
        .nominal_hz = params[2],
        .window_ms = params[3],
        .timeout_ms = params[4],
    };
    if (!vdc_dpll_manager_set_reference_config_online(&config)) {
        return reject_run_state(reason);
    }
    return true;
}

static bool vdc_output_delay_handler(const uint32_t *params,
                                     uint32_t param_words, uint32_t *reason)
{
    if (params == NULL || param_words != 1u) {
        return reject(reason);
    }
    if (!vdc_dpll_manager_set_output_delay_ns_online((int32_t)params[0])) {
        return reject_run_state(reason);
    }
    return true;
}

static bool vdc_output_timing_handler(const uint32_t *params,
                                      uint32_t param_words, uint32_t *reason)
{
    if (params == NULL || param_words != 3u) {
        return reject(reason);
    }
    const vdc_output_timing_profile_t profile = {
        .plan_ahead_us = params[0],
        .commit_ahead_us = params[1],
        .refill_low_us = params[2],
    };
    if (!vdc_dpll_manager_set_output_timing_profile_online(&profile)) {
        return reject_run_state(reason);
    }
    return true;
}

static bool vdc_start_handler(const uint32_t *params, uint32_t param_words,
                              uint32_t *reason)
{
    (void)params;
    (void)param_words;
    /* VDC run output needs period/high/duration, so it is not a generic
     * zero-argument start; leave the restart to the local VDC path. */
    return reject_not_online(reason);
}

bool app_refmem_domain_config_init(void)
{
    refmem_domain_config_reset();
    return refmem_domain_config_register(
               REFMEM_COMMAND_TYPE_TDMA_RING_TOPOLOGY, 2u,
               tdma_ring_topology_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TDMA_SEAT_ROTATION,
                                         3u, tdma_seat_rotation_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TDMA_PERIOD, 1u,
                                         tdma_period_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TDMA_LOAD_MASK, 1u,
                                         tdma_load_mask_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_DPLL_ROLE, 2u,
                                         dpll_role_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_DPLL_TUNE, 5u,
                                         dpll_tune_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_DPLL_OVERRIDE,
                                         1u, dpll_override_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_VDC_REFERENCE_CONFIG,
                                         5u, vdc_reference_config_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_VDC_OUTPUT_DELAY,
                                         1u, vdc_output_delay_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_VDC_OUTPUT_TIMING,
                                         3u, vdc_output_timing_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_VDC_STOP, 0u,
                                         vdc_stop_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_VDC_START, 0u,
                                         vdc_start_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_T2_CONFIGURE, 13u,
                                         t2_configure_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_T2_MOUNT, 3u,
                                         t2_mount_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_T2_UNMOUNT, 0u,
                                         t2_unmount_handler);
}
