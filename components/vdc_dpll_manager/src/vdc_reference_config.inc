/* Core0 control calls may come from different tasks. A single nonblocking
 * gate serializes config/lease bookkeeping; hardware runtime belongs to
 * Core1 and is copied only through its guarded snapshot. */
#include "vdc_reference.h"
static uint32_t s_reference_control_gate;
static uint32_t s_reference_config_generation = 1u;
static uint32_t s_reference_active_generation;
static bool s_reference_enabled;
static sync_io_reference_config_t s_reference_config = {
    .input_port = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_INPUT_PORT,
    .edge = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_EDGE,
    .nominal_hz = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_NOMINAL_HZ,
    .window_ms = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_WINDOW_MS,
    .timeout_ms = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_TIMEOUT_MS,
};
static sync_io_reference_snapshot_t s_reference_last;

static bool reference_control_enter(void)
{
    uint32_t expected = 0u;
    return get_core_num() == 0u && __atomic_compare_exchange_n(
        &s_reference_control_gate, &expected, 1u, false,
        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
static void reference_control_leave(void)
{
    __atomic_store_n(&s_reference_control_gate, 0u, __ATOMIC_RELEASE);
}

static bool reference_config_install(void *context)
{
    const sync_io_reference_config_t *config = context;
    if (s_reference_active_generation || !sync_io_reference_config_valid(config) ||
        s_reference_config_generation == UINT32_MAX) return false;
    s_reference_config = *config;
    ++s_reference_config_generation;
    return true;
}

bool vdc_dpll_manager_get_reference_config(sync_io_reference_config_t *config)
{
    if (!config || !reference_control_enter()) return false;
    *config = s_reference_config;
    reference_control_leave();
    return true;
}

bool vdc_dpll_manager_set_reference_config(const sync_io_reference_config_t *config)
{
    if (!config || !sync_io_reference_config_valid(config) ||
        !reference_control_enter()) return false;
    const sync_io_reference_config_t copy = *config;
    const bool ok = s_vdc_tdma_service && tdma_service_update_stopped_metadata(
        s_vdc_tdma_service, reference_config_install, (void *)&copy);
    reference_control_leave();
    return ok;
}

static bool reference_enable_stopped(void *context)
{
    (void)context;
    if (s_reference_active_generation) return false;
    uint32_t generation;
    if (!sync_io_reference_prepare(&s_reference_config, &generation)) {
        sync_io_reference_snapshot_t failure;
        if (sync_io_reference_get_snapshot(&failure)) s_reference_last = failure;
        return false;
    }
    s_reference_active_generation = generation;
    s_reference_enabled = true;
    return true;
}

bool vdc_dpll_manager_set_reference_enabled(bool enabled)
{
    if (!reference_control_enter()) return false;
    bool ok = true;
    if (enabled) {
        ok = s_vdc_tdma_service && tdma_service_update_stopped_metadata(
            s_vdc_tdma_service, reference_enable_stopped, NULL);
    } else {
        if (s_reference_active_generation) sync_io_reference_cancel();
        s_reference_enabled = false;
    }
    reference_control_leave();
    return ok;
}

/* Ordinary Core0 service performs retirement cleanup; no SCPI polling needed.
 * The last completed generation remains queryable after its lease is gone. */
static void reference_release_core0(void)
{
    if (!reference_control_enter()) return;
    if (s_reference_active_generation) {
        sync_io_reference_snapshot_t snapshot;
        if (sync_io_reference_get_snapshot(&snapshot) &&
            snapshot.generation == s_reference_active_generation &&
            snapshot.state == SYNC_IO_REFERENCE_RETIRED) {
            s_reference_last = snapshot;
            if (sync_io_reference_release(snapshot.generation)) {
                s_reference_active_generation = 0u;
                s_reference_enabled = false;
            }
        }
    }
    reference_control_leave();
}

bool vdc_dpll_manager_get_reference_status(vdc_reference_status_t *status)
{
    if (!status || !reference_control_enter()) return false;
    vdc_reference_status_t value = {
        .config_generation = s_reference_config_generation,
        .enabled = s_reference_enabled ? 1u : 0u,
        .resource_held = s_reference_active_generation ? 1u : 0u,
        .monitor = s_reference_last,
    };
    const bool ok = !s_reference_active_generation ||
        (sync_io_reference_get_snapshot(&value.monitor) &&
         value.monitor.generation == s_reference_active_generation);
    reference_control_leave();
    if (ok) *status = value;
    return ok;
}

static bool reference_recall_stopped(void *context)
{
    (void)context;
    product_config_vdc_reference_profile_t profile;
    if (!product_config_get_vdc_reference_profile(&profile)) return false;
    const sync_io_reference_config_t config = {
        .input_port = profile.input_port, .edge = profile.edge,
        .nominal_hz = profile.nominal_hz, .window_ms = profile.window_ms,
        .timeout_ms = profile.timeout_ms,
    };
    return reference_config_install((void *)&config);
}

bool vdc_dpll_manager_recall_reference(void)
{
    if (!reference_control_enter()) return false;
    const bool ok = s_vdc_tdma_service && tdma_service_update_stopped_metadata(
        s_vdc_tdma_service, reference_recall_stopped, NULL);
    reference_control_leave();
    return ok;
}

bool vdc_dpll_manager_default_reference(void)
{
    const sync_io_reference_config_t config = {
        .input_port = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_INPUT_PORT,
        .edge = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_EDGE,
        .nominal_hz = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_NOMINAL_HZ,
        .window_ms = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_WINDOW_MS,
        .timeout_ms = PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_TIMEOUT_MS,
    };
    return vdc_dpll_manager_set_reference_config(&config);
}

static bool reference_store_maintenance(void *context)
{
    (void)context;
    if (s_reference_active_generation) return false;
    const product_config_vdc_reference_profile_t profile = {
        .input_port = s_reference_config.input_port, .edge = s_reference_config.edge,
        .nominal_hz = s_reference_config.nominal_hz,
        .window_ms = s_reference_config.window_ms,
        .timeout_ms = s_reference_config.timeout_ms,
    };
    return product_config_set_vdc_reference_profile(&profile);
}

bool vdc_dpll_manager_store_reference(void)
{
    if (!reference_control_enter()) return false;
    const bool ok = s_vdc_tdma_service && !s_reference_active_generation &&
        tdma_service_run_stopped_maintenance(s_vdc_tdma_service,
            reference_store_maintenance, NULL);
    reference_control_leave();
    return ok;
}

static bool reference_init_from_product_config(void)
{
    if (!reference_control_enter()) return false;
    const bool ok = !s_reference_active_generation &&
        !vdc_dpll_manager_feedback_session() && reference_recall_stopped(NULL);
    reference_control_leave();
    return ok;
}
