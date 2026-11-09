#include "app_refmem_domain_config.h"

#include <stdint.h>
#include <string.h>

#include "app.h"
#include "distributed_refmem.h"
#include "guide_action_plan.h"
#include "refmem_command.h"
#include "refmem_domain_config.h"
#include "refmem_param_pool.h"
#include "refmem_vector_table.h"
#include "sync_io_sequence.h"
#include "sync_io_turntable_source.h"
#include "trigger_action_service.h"
#include "trigger_sequence_service.h"
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

static bool reject_permission(uint32_t *reason)
{
    if (reason != NULL) {
        *reason = (uint32_t)REFMEM_COMMAND_REASON_PERMISSION_DENIED;
    }
    return false;
}

static bool reject_busy(uint32_t *reason)
{
    if (reason != NULL) {
        *reason = (uint32_t)REFMEM_COMMAND_REASON_RESOURCE_BUSY;
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
    /* TDMA is the non-stop bearer here: read the cached STOP-staged grid and
     * deployment instead of the STOP-gated local snapshot.  The local SCPI
     * paths keep their STOP-only snapshot. */
    if (trigger_grid_preview_snapshot_cached(&grid) || !grid.configured) {
        return reject(reason);
    }
    if (trigger_deployment_snapshot_cached(&deployment) ||
        !deployment.active.valid) {
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
    if (params == NULL || param_words != 3u) {
        return reject(reason);
    }
    /* N8.5: clear the position TX/pending state from the same refmem service
     * context that prepares it (single writer), then start T2 before returning
     * to the service loop.  Any failure keeps the previous NOT_ONLINE_SAFE
     * behaviour. */
    const refmem_position_reset_result_t reset =
        distributed_refmem_reset_position_running();
    if (reset != REFMEM_POS_RESET_OK) {
        return reject_not_online(reason);
    }
    const uint64_t origin = ((uint64_t)params[2] << 32u) | (uint64_t)params[1];
    const bool started = trigger_t2_runtime_start(params[0], origin);
    if (!started) {
        return reject(reason);
    }
    return true;
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

static float bits_to_float(uint32_t bits)
{
    union {
        uint32_t u;
        float f;
    } value;
    value.u = bits;
    return value.f;
}

static bool decode_plan_id(const uint32_t *params, char out[33])
{
    bool present = false;
    memset(out, 0, 33u);
    for (uint32_t i = 0u; i < 4u; ++i) {
        for (uint32_t j = 0u; j < 4u; ++j) {
            const uint8_t c = (uint8_t)((params[i] >> (8u * j)) & 0xFFu);
            if (c != 0u) {
                present = true;
                out[i * 4u + j] = (char)c;
            }
        }
    }
    return present;
}

static uint32_t trigger_result_reason(trigger_sequence_service_result_t result)
{
    switch (result) {
    case TRIGGER_SEQUENCE_SERVICE_FROZEN:
    case TRIGGER_SEQUENCE_SERVICE_NOT_READY:
    case TRIGGER_SEQUENCE_SERVICE_BUSY:
    case TRIGGER_SEQUENCE_SERVICE_EXHAUSTED:
        return (uint32_t)REFMEM_COMMAND_REASON_RUN_STATE_DENIED;
    default:
        return (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
    }
}

/* TRIG:START 的节点映射（EV-2）。
 *
 * 语义：配置完成后，"等转台输入脉冲、然后执行整段序列"这件事只由**脉冲源节点**
 * 发起——即部署表里 COUNTER 角色的那块板开脉冲流（台架为 OUT4→IN1 回环，
 * 生产为转台直驱 IN1）。其余角色只等位置描述符，按本地 FIFO 推进，它们执行
 * START 属于角色越界，必须 fail-closed，不得回假 ACK。
 *
 * 参数仍是 4 word（历史 HIL 脚本以 plan_id 形式发送），本轮只改语义不动 schema；
 * 参数不参与判定。schema 收敛（4→0）留给 M5，需同步接口表与脚本。
 */
static bool trigger_start_handler(const uint32_t *params, uint32_t param_words,
                                  uint32_t *reason)
{
    if (params == NULL || param_words != 4u) {
        return reject(reason);
    }
    trigger_deployment_snapshot_t deployment;
    if (trigger_deployment_snapshot_cached(&deployment) != TRIGGER_DEPLOYMENT_OK ||
        deployment.active.valid == 0u) {
        /* 部署表未激活时无法判定本板角色，START 无从归属：fail-closed。 */
        return reject(reason);
    }
    if (deployment.active.local_role != TRIGGER_DEPLOYMENT_SLOT_COUNTER) {
        if (reason != NULL) {
            *reason = (uint32_t)REFMEM_COMMAND_REASON_PERMISSION_DENIED;
        }
        return false;
    }
    /* PWM 源只在 STOP 维护窗口预装；未预装时 START 不得假装成功。 */
    if (!sync_io_turntable_source_start()) {
        return reject(reason);
    }
    return true;
}

static bool trigger_stop_handler(const uint32_t *params, uint32_t param_words,
                                 uint32_t *reason)
{
    (void)params;
    if (param_words != 0u) {
        return reject(reason);
    }
    /* EV-2：收尾是全局语义——源节点停脉冲流，所有节点都允许 STOP。 */
    trigger_deployment_snapshot_t deployment;
    if (trigger_deployment_snapshot_cached(&deployment) == TRIGGER_DEPLOYMENT_OK &&
        deployment.active.valid != 0u &&
        deployment.active.local_role == TRIGGER_DEPLOYMENT_SLOT_COUNTER) {
        (void)sync_io_turntable_source_stop();
    }
    /* 兼容：service 侧状态仍要清（EV-1 退役它之前保持幂等 STOP）。 */
    const trigger_sequence_service_result_t result =
        trigger_sequence_service_stop();
    if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
        if (reason != NULL) {
            *reason = trigger_result_reason(result);
        }
        return false;
    }
    return true;
}

static bool trigger_pause_handler(const uint32_t *params, uint32_t param_words,
                                  uint32_t *reason)
{
    (void)params;
    if (param_words != 0u) {
        return reject(reason);
    }
    const trigger_sequence_service_result_t result =
        trigger_sequence_service_pause();
    if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
        if (reason != NULL) {
            *reason = trigger_result_reason(result);
        }
        return false;
    }
    return true;
}

static bool trigger_continue_handler(const uint32_t *params,
                                     uint32_t param_words, uint32_t *reason)
{
    (void)params;
    if (param_words != 0u) {
        return reject(reason);
    }
    const trigger_sequence_service_result_t result =
        trigger_sequence_service_continue();
    if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
        if (reason != NULL) {
            *reason = trigger_result_reason(result);
        }
        return false;
    }
    return true;
}

static bool trigger_abort_handler(const uint32_t *params, uint32_t param_words,
                                  uint32_t *reason)
{
    (void)params;
    if (param_words != 0u) {
        return reject(reason);
    }
    const trigger_sequence_service_result_t result =
        trigger_sequence_service_stop();
    if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
        if (reason != NULL) {
            *reason = trigger_result_reason(result);
        }
        return false;
    }
    return true;
}

static bool trigger_switch_handler(const uint32_t *params,
                                   uint32_t param_words, uint32_t *reason)
{
    if (params == NULL || param_words != 2u) {
        return reject(reason);
    }
    /* HAOFV owner boundary: the switch fact belongs to SWITCH_A/SWITCH_B.
     * A broadcast target_mask reaches every board, so a counter/gateway board
     * must refuse with a reason instead of silently no-oping or driving its
     * local OUT1-OUT3 (both hide the misuse from the origin's ACK aggregate).
     * The cached snapshot is the RUN-safe read used by the other unified T2
     * handlers; the STOP-gated snapshot() would reject SWITCH_A/B while the
     * ring is running. */
    trigger_deployment_snapshot_t snapshot;
    const uint32_t snap = trigger_deployment_snapshot_cached(&snapshot);
    if (snap == (uint32_t)TRIGGER_DEPLOYMENT_SERVICE_BUSY) {
        return reject_busy(reason); /* transient lock/core0 contention */
    }
    if (snap != 0u || !snapshot.active.valid) {
        return reject(reason); /* no active deployment: config prerequisite */
    }
    const uint32_t role = snapshot.active.local_role;
    if (role != TRIGGER_DEPLOYMENT_SLOT_SWITCH_A &&
        role != TRIGGER_DEPLOYMENT_SLOT_SWITCH_B) {
        return reject_permission(reason); /* deployed, but not the switch owner */
    }
    if (!sync_io_sequence_set_switch(params[0], params[1])) {
        return reject(reason);
    }
    return true;
}

static bool trigger_mode_handler(const uint32_t *params, uint32_t param_words,
                                 uint32_t *reason)
{
    if (params == NULL || param_words != 1u) {
        return reject(reason);
    }
    /* Mode owner wiring is part of IF-013; fail closed until the trigger
     * mode state machine exposes the TEST/SERVICE/DEBUG policy. */
    return reject(reason);
}

static bool trigger_configure_handler(const uint32_t *params,
                                      uint32_t param_words, uint32_t *reason)
{
    if (params == NULL || param_words != 4u) {
        return reject(reason);
    }
    if (!trigger_sequence_service_configuration_begin()) {
        return reject(reason);
    }
    const trigger_sequence_params_t config = {
        .chan_count = params[0],
        .pol = params[1],
        .freq_count = params[2],
        .wave_count = params[3],
    };
    const trigger_sequence_result_t result =
        trigger_sequence_configure(trigger_sequence_service_config(), &config);
    trigger_sequence_service_configuration_end();
    if (result != TRIGGER_SEQUENCE_OK) {
        if (reason != NULL) {
            *reason = (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
        }
        return false;
    }
    if (config.chan_count == 0u || config.chan_count > 8u) {
        return reject(reason);
    }
    const uint32_t mask = 0xFu;
    if (trigger_sequence_service_set_outputs(
            mask, 0u, TRIGGER_SEQUENCE_STATUS_NONE, 100u, 0u) !=
        TRIGGER_SEQUENCE_SERVICE_OK) {
        return reject(reason);
    }
    trigger_sequence_store_t *store = trigger_sequence_service_config();
    for (uint32_t i = 0u; i < store->state_count; ++i) {
        trigger_sequence_state_t state;
        if (trigger_sequence_map_state(store, i, &state) != TRIGGER_SEQUENCE_OK ||
            state.switch1_ch == 0u || state.switch1_ch > config.chan_count) {
            return reject(reason);
        }
        const uint32_t code = ((state.switch1_ch - 1u) & 0x7u) |
                              ((state.switch2_sel & 1u) << 3u);
        if (trigger_sequence_service_set_code(i, code) !=
            TRIGGER_SEQUENCE_SERVICE_OK) {
            return reject(reason);
        }
    }
    return true;
}

static bool trigger_angle_sweep_handler(const uint32_t *params,
                                        uint32_t param_words, uint32_t *reason)
{
    if (params == NULL || param_words != 3u) {
        return reject(reason);
    }
    if (trigger_sequence_service_set_angle_sweep(
            bits_to_float(params[0]), bits_to_float(params[1]),
            bits_to_float(params[2])) != TRIGGER_SEQUENCE_SERVICE_OK) {
        return reject(reason);
    }
    return true;
}

static bool trigger_angle_pulse_handler(const uint32_t *params,
                                        uint32_t param_words, uint32_t *reason)
{
    if (params == NULL || param_words != 3u) {
        return reject(reason);
    }
    if (trigger_sequence_service_set_angle_pulse(params[0], params[1], params[2]) !=
        TRIGGER_SEQUENCE_SERVICE_OK) {
        return reject(reason);
    }
    return true;
}

static bool trigger_angle_breakpoint_handler(const uint32_t *params,
                                             uint32_t param_words,
                                             uint32_t *reason)
{
    if (params == NULL || param_words != 2u) {
        return reject(reason);
    }
    if (trigger_sequence_service_set_angle_breakpoint(
            params[0] != 0u, bits_to_float(params[1])) !=
        TRIGGER_SEQUENCE_SERVICE_OK) {
        return reject(reason);
    }
    return true;
}

static bool trigger_angle_breakpoint_clear_handler(const uint32_t *params,
                                                   uint32_t param_words,
                                                   uint32_t *reason)
{
    (void)params;
    if (param_words != 0u) {
        return reject(reason);
    }
    if (trigger_sequence_service_clear_angle_breakpoint() !=
        TRIGGER_SEQUENCE_SERVICE_OK) {
        return reject(reason);
    }
    return true;
}

static bool trigger_angle_speed_handler(const uint32_t *params,
                                        uint32_t param_words, uint32_t *reason)
{
    if (params == NULL || param_words != 1u) {
        return reject(reason);
    }
    if (trigger_sequence_service_set_angle_speed(bits_to_float(params[0])) !=
        TRIGGER_SEQUENCE_SERVICE_OK) {
        return reject(reason);
    }
    return true;
}

static bool trigger_sequence_plan_handler(const uint32_t *params,
                                         uint32_t param_words,
                                         uint32_t *reason)
{
    if (params == NULL || param_words != 16u) {
        return reject(reason);
    }
    char plan_id[33];
    if (!decode_plan_id(params, plan_id)) {
        return reject(reason);
    }
    const uint32_t count = params[4];
    if (count == 0u || count > 11u) {
        return reject(reason);
    }
    if (!trigger_sequence_service_configuration_begin()) {
        return reject(reason);
    }
    const trigger_sequence_result_t result = trigger_sequence_write_plan(
        trigger_sequence_service_config(), plan_id, &params[5], count);
    trigger_sequence_service_configuration_end();
    if (result != TRIGGER_SEQUENCE_OK) {
        if (reason != NULL) {
            *reason = (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
        }
        return false;
    }
    return true;
}

static bool trigger_sequence_active_handler(const uint32_t *params,
                                            uint32_t param_words,
                                            uint32_t *reason)
{
    if (params == NULL || param_words != 4u) {
        return reject(reason);
    }
    char plan_id[33];
    if (!decode_plan_id(params, plan_id)) {
        return reject(reason);
    }
    if (!trigger_sequence_service_configuration_begin()) {
        return reject(reason);
    }
    const trigger_sequence_result_t result =
        trigger_sequence_activate(trigger_sequence_service_config(), plan_id);
    trigger_sequence_service_configuration_end();
    if (result != TRIGGER_SEQUENCE_OK) {
        if (reason != NULL) {
            *reason = (result == TRIGGER_SEQUENCE_PLAN_NOT_FOUND ||
                       result == TRIGGER_SEQUENCE_NO_ACTIVE_PLAN)
                          ? (uint32_t)REFMEM_COMMAND_REASON_RUN_STATE_DENIED
                          : (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
        }
        return false;
    }
    return true;
}

/* Guide ladder (P1..T3): C carries the 64-byte plan as 16 inline words and the
 * plan stays in RAM until T consumes it with the action id and epoch. Nothing is
 * persisted here - S owns that, after the training session is closed. */
typedef struct {
    bool plan_valid;
    bool trigger_pending;
    guide_action_plan_t plan;
    uint32_t trigger_epoch;
} app_cal_guide_state_t;

static app_cal_guide_state_t s_cal_guide;

/* Q readback result: written by the CTRQS executor (app.c) once the ring is
 * re-formed, read by the single-port PLAN? handler. */
static app_cal_guide_q_t s_cal_guide_q;

void app_cal_guide_set_q_result(const guide_action_progress_t *progress,
                                bool have_record,
                                const guide_action_reconcile_result_t *result)
{
    s_cal_guide_q.have_record = have_record;
    s_cal_guide_q.have_result = result != NULL;
    if (progress != NULL) {
        s_cal_guide_q.progress = *progress;
    }
    if (result != NULL) {
        s_cal_guide_q.reconcile = *result;
    }
}

bool app_cal_guide_get_q_result(app_cal_guide_q_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = s_cal_guide_q;
    return s_cal_guide_q.have_result;
}

void app_cal_guide_set_commit_result(uint32_t decision, uint32_t applicable,
                                     uint32_t ok)
{
    s_cal_guide_q.commit_decision = decision;
    s_cal_guide_q.commit_applicable = applicable;
    s_cal_guide_q.commit_ok = ok;
}

void app_cal_guide_set_ladder_result(uint32_t action_index, uint32_t step)
{
    s_cal_guide_q.ladder_index = action_index;
    s_cal_guide_q.ladder_step = step;
}

static bool cal_guide_config_handler(const uint32_t *params, uint32_t param_words,
                                     uint32_t *reason)
{
    guide_action_plan_t plan;
    if (params == NULL ||
        guide_action_plan_from_words(params, param_words, &plan) !=
            GUIDE_ACTION_PLAN_OK) {
        return reject(reason);
    }
    s_cal_guide.plan = plan;
    s_cal_guide.plan_valid = true;
    s_cal_guide.trigger_pending = false;
    s_cal_guide.trigger_epoch = 0u;
    return true;
}

/* R/Q transport for the guide ladder: the progress record lives in the refmem
 * parameter pool (plain storage, no link), so it survives the dark window and is
 * read back after the ring is re-formed. Slot 2 is reserved for this record. */
#define APP_CAL_GUIDE_PROGRESS_SLOT 2u

static uint8_t *cal_guide_progress_region(size_t *out_size)
{
    refmem_vector_table_t *table = distributed_refmem_vector_table();
    if (table == NULL) {
        return NULL;
    }
    if (out_size != NULL) {
        *out_size = sizeof(table->tlv);
    }
    return table->tlv;
}

bool app_cal_guide_publish_progress(const guide_action_progress_t *progress,
                                    uint32_t epoch)
{
    size_t region_size = 0u;
    uint8_t *region = cal_guide_progress_region(&region_size);
    uint8_t payload[GUIDE_ACTION_PROGRESS_SIZE];
    size_t size = 0u;
    if (region == NULL || progress == NULL ||
        guide_action_progress_encode(progress, payload, sizeof(payload), &size) !=
            GUIDE_ACTION_PLAN_OK) {
        return false;
    }
    return refmem_param_pool_write(region, region_size,
                                   APP_CAL_GUIDE_PROGRESS_SLOT, epoch, epoch,
                                   payload, (uint32_t)size);
}

bool app_cal_guide_read_progress(guide_action_progress_t *progress, uint32_t epoch)
{
    size_t region_size = 0u;
    uint8_t *region = cal_guide_progress_region(&region_size);
    uint8_t payload[GUIDE_ACTION_PROGRESS_SIZE];
    uint32_t size = 0u;
    uint32_t crc = 0u;
    if (region == NULL || progress == NULL ||
        !refmem_param_pool_read(region, region_size,
                                APP_CAL_GUIDE_PROGRESS_SLOT, epoch, payload,
                                sizeof(payload), &size, &crc)) {
        return false;
    }
    return guide_action_progress_decode(payload, size, progress) ==
           GUIDE_ACTION_PLAN_OK;
}
static bool cal_guide_trigger_handler(const uint32_t *params, uint32_t param_words,
                                      uint32_t *reason)
{
    /* A trigger is only meaningful for the plan it names: a mismatched or
     * missing plan is rejected instead of running the previous action. */
    if (params == NULL || param_words != 2u || !s_cal_guide.plan_valid ||
        params[0] != s_cal_guide.plan.action_id) {
        return reject(reason);
    }
    s_cal_guide.trigger_epoch = params[1];
    s_cal_guide.trigger_pending = true;
    return true;
}

bool app_cal_guide_post_trigger(uint32_t action, uint32_t epoch)
{
    /* Same admission as the REFMEM command path: a trigger is only meaningful
     * for the plan it names.  The compact ring message and the local origin
     * both enter here, so all four boards run the same check. */
    if (!s_cal_guide.plan_valid || action != s_cal_guide.plan.action_id ||
        epoch == 0u) {
        return false;
    }
    s_cal_guide.trigger_epoch = epoch;
    s_cal_guide.trigger_pending = true;
    return true;
}

bool app_cal_guide_take_trigger(guide_action_plan_t *plan, uint32_t *epoch)
{
    if (!s_cal_guide.trigger_pending) {
        return false;
    }
    if (plan != NULL) {
        *plan = s_cal_guide.plan;
    }
    if (epoch != NULL) {
        *epoch = s_cal_guide.trigger_epoch;
    }
    s_cal_guide.trigger_pending = false;
    return true;
}

bool app_cal_guide_snapshot(app_cal_guide_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->plan_valid = s_cal_guide.plan_valid;
    snapshot->trigger_pending = s_cal_guide.trigger_pending;
    snapshot->trigger_epoch = s_cal_guide.trigger_epoch;
    snapshot->action_id = s_cal_guide.plan.action_id;
    snapshot->plan_crc32 = s_cal_guide.plan.plan_crc32;
    snapshot->generation = s_cal_guide.plan.generation;
    snapshot->profile_level = s_cal_guide.plan.profile_level;
    snapshot->window_ms = s_cal_guide.plan.window_ms;
    snapshot->trial_count = s_cal_guide.plan.trial_count;
    snapshot->progress_valid =
        (s_cal_guide_q.have_record && s_cal_guide_q.have_result) ? 1u : 0u;
    snapshot->progress_status = s_cal_guide_q.progress.status;
    snapshot->trials_done = s_cal_guide_q.progress.trials_done;
    snapshot->deepest_trial = s_cal_guide_q.progress.deepest_trial;
    snapshot->progress_reason = s_cal_guide_q.progress.reason;
    snapshot->passed_mask = s_cal_guide_q.progress.passed_mask;
    snapshot->failed_mask = s_cal_guide_q.progress.failed_mask;
    snapshot->abort_site = app_cal_guide_abort_site();
    snapshot->reconcile_decision = (uint32_t)s_cal_guide_q.reconcile.decision;
    snapshot->reconcile_resume_trial = s_cal_guide_q.reconcile.resume_trial;
    snapshot->commit_decision = s_cal_guide_q.commit_decision;
    snapshot->commit_applicable = s_cal_guide_q.commit_applicable;
    snapshot->commit_ok = s_cal_guide_q.commit_ok;
    snapshot->ladder_index = s_cal_guide_q.ladder_index;
    snapshot->ladder_step = s_cal_guide_q.ladder_step;
    return true;
}
bool app_refmem_domain_config_init(void)
{
    refmem_domain_config_reset();
    return refmem_domain_config_register(
               REFMEM_COMMAND_TYPE_TDMA_RING_TOPOLOGY, 2u,
               tdma_ring_topology_handler) &&           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TDMA_SEAT_ROTATION,
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
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_MODE, 1u,
                                         trigger_mode_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_START, 4u,
                                         trigger_start_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_STOP, 0u,
                                         trigger_stop_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_PAUSE, 0u,
                                         trigger_pause_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_CONTINUE, 0u,
                                         trigger_continue_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_ABORT, 0u,
                                         trigger_abort_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_SWITCH, 2u,
                                         trigger_switch_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_CONFIGURE, 4u,
                                         trigger_configure_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_ANGLE_SWEEP,
                                         3u, trigger_angle_sweep_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_ANGLE_PULSE,
                                         3u, trigger_angle_pulse_handler) &&
           refmem_domain_config_register(
               REFMEM_COMMAND_TYPE_TRIGGER_ANGLE_BREAKPOINT, 2u,
               trigger_angle_breakpoint_handler) &&
           refmem_domain_config_register(
               REFMEM_COMMAND_TYPE_TRIGGER_ANGLE_BREAKPOINT_CLEAR, 0u,
               trigger_angle_breakpoint_clear_handler) &&
           refmem_domain_config_register(
               REFMEM_COMMAND_TYPE_TRIGGER_SEQUENCE_ACTIVE, 4u,
               trigger_sequence_active_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_SEQUENCE_PLAN,
                                         16u, trigger_sequence_plan_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_TRIGGER_ANGLE_SPEED,
                                         1u, trigger_angle_speed_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_T2_CONFIGURE, 13u,
                                         t2_configure_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_T2_MOUNT, 3u,
                                         t2_mount_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_T2_UNMOUNT, 0u,
                                         t2_unmount_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_CAL_GUIDE_CONFIG,
                                         GUIDE_ACTION_PLAN_WORDS,
                                         cal_guide_config_handler) &&
           refmem_domain_config_register(REFMEM_COMMAND_TYPE_CAL_GUIDE_TRIGGER,
                                         2u, cal_guide_trigger_handler);
}

