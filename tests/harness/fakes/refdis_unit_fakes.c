/* Link-time doubles for the merged vdc_output unit.
 * Generated from the production prototypes; bodies are inert because the
 * reference-discipline scenario never reaches the run-output services. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "app.h"
#include "product_config.h"
#include "sync_io.h"
#include "sync_io_reference.h"
#include "sync_io_run_output.h"
#include "tdma_seat_mount_store.h"
#include "tdma_service.h"
#include "vdc_domain.h"
#include "vdc_dpll_manager.h"
#include "vdc_dpll_manager_internal.h"

uint32_t app_realtime_cycle_cycles_core1(void)
{
    return 0u;
}

bool app_realtime_get_schedule_snapshot( app_realtime_schedule_snapshot_t *snapshot)
{
    return false;
}

bool product_config_get_vdc_reference_profile(product_config_vdc_reference_profile_t *profile)
{
    return false;
}

bool product_config_get_vdc_reference_discipline_profile(product_config_vdc_reference_discipline_profile_t *profile)
{
    return false;
}

bool product_config_set_vdc_reference_profile(const product_config_vdc_reference_profile_t *profile)
{
    return false;
}

bool product_config_set_vdc_reference_discipline_profile(const product_config_vdc_reference_discipline_profile_t *profile)
{
    return false;
}

bool sync_io_reference_config_valid(const sync_io_reference_config_t *config)
{
    return false;
}

bool sync_io_reference_prepare(const sync_io_reference_config_t *config, uint32_t *generation)
{
    return false;
}

void sync_io_reference_cancel(void)
{
}

bool sync_io_reference_release(uint32_t generation)
{
    return false;
}

bool sync_io_run_output_can_submit_core1(uint32_t generation)
{
    return false;
}

void sync_io_run_output_cancel(void)
{
}

sync_io_run_output_submit_failure_t sync_io_run_output_last_submit_failure(void)
{
    return SYNC_IO_RUN_OUTPUT_SUBMIT_FAILURE_NONE;
}

bool sync_io_run_output_prepare_sliced(uint32_t expected_hz,uint32_t duration_ms, uint32_t period_ticks,uint32_t interval_ticks,uint32_t high_ticks, uint32_t marker_ticks,uint32_t *generation)
{
    return false;
}

bool sync_io_run_output_prepare_uniform(uint32_t expected_hz, uint32_t duration_ms, uint32_t high_ticks, uint32_t *generation)
{
    return false;
}

bool sync_io_run_output_release(uint32_t generation)
{
    return false;
}

void sync_io_run_output_service_core1(void)
{
}

bool sync_io_run_output_snapshot(sync_io_run_output_snapshot_t *out)
{
    return false;
}

bool sync_io_run_output_submit_count_core1(uint32_t generation, const sync_io_run_output_edge_t *edges, uint32_t count)
{
    return false;
}

void sync_io_sma_observer_fixed_rate_disarm(void)
{
}

void sync_io_sma_observer_fixed_rate_get_runtime( sync_io_fixed_rate_runtime_t *runtime)
{
}

void tdma_seat_mount_store_note_reject(uint32_t code)
{
}

void tdma_seat_mount_store_status(uint32_t *last_reject, uint32_t *record_generation)
{
}

bool tdma_service_run_stopped_maintenance(tdma_service_service_t *service, bool (*maintain)(void *context), void *context)
{
    return false;
}

bool tdma_service_update_stopped_metadata(tdma_service_service_t *service, bool (*publish)(void *context), void *context)
{
    return false;
}

bool vdc_domain_dco_local_to_output_ns(const vdc_dco_control_t *dco, uint64_t local_ns, uint64_t *output_ns)
{
    return false;
}

bool vdc_domain_dco_output_to_local_ns(const vdc_dco_control_t *dco, uint64_t target_output_ns, uint64_t *local_ns)
{
    return false;
}

bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{
    return false;
}

bool vdc_dpll_manager_get_output_delay_ns(int32_t *value)
{
    return false;
}

bool vdc_dpll_manager_get_output_timing_profile(vdc_output_timing_profile_t *profile)
{
    return false;
}

bool vdc_timestamp_clock_configuration_supported(uint32_t expected_hz)
{
    return false;
}

bool vdc_timestamp_clock_is_current(uint32_t expected_hz)
{
    return false;
}


/* Data symbol referenced by the observation self-test helpers. */
vdc_dpll_manager_observation_self_test_status_t s_observation_self_test;

