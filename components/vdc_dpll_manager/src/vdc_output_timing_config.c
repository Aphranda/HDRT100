#include "vdc_dpll_manager.h"
#include "vdc_time_mapping.h"
#include "vdc_model_projection.h"
#include "vdc_priority_tx.h"
#include "vdc_priority_rx.h"
#include "vdc_priority_match.h"
#include "vdc_priority_follow.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_config.h"
#include "board_identity.h"
#include "app.h"
#include "distributed_refmem.h"
#include "ota_crc32.h"
#include "osal.h"
#include "ota_ao.h"
#include "storage_manager.h"
#include "product_config.h"
#include "refmem_sync_frame.h"
#include "sync_io.h"
#include "tdma_runtime_owner.h"
#include "tdma_service.h"
#include "tdma_service_timing.h"
#include "vdc_domain.h"
#include "vdc_ring_observer.h"
#include "vdc_sync_io_adapter.h"
#include "vdc_tdma_payload.h"
#include "vdc_timestamp_clock.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#include "pico/time.h"
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) name
#endif

#define VDC_DPLL_MANAGER_SELF_TEST_CLEANUP_MARGIN_MS 250u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_MAGIC 0x4C504444u /* DDPL */
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_SCHEMA 6u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_MASTER 1u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_COMMAND 2u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_STATE 3u
#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_EVIDENCE 4u
#define VDC_DPLL_MANAGER_WAVEFORM_MAGIC 0x57524D53u /* SMRW */
#define VDC_DPLL_MANAGER_WAVEFORM_SCHEMA 4u
#define VDC_DPLL_MANAGER_WAVEFORM_BUFFER_COUNT 3u
#define VDC_DPLL_MANAGER_PHASE_ARM_AHEAD_PERIODS 2u


#include "vdc_dpll_manager_internal.h"

/* Core0 is the sole requested-profile writer. Every shared word is atomic;
 * a bounded seqlock observation prevents a mixed tuple without a retry loop.
 * PREPARE copies one complete tuple under the existing whole-client gate. */
_Static_assert(VDC_OUTPUT_TIMING_MIN_US == PRODUCT_CONFIG_VDC_OUTPUT_TIMING_MIN_US &&
    VDC_OUTPUT_TIMING_MAX_US == PRODUCT_CONFIG_VDC_OUTPUT_TIMING_MAX_US &&
    VDC_OUTPUT_TIMING_PLAN_DEFAULT_US == PRODUCT_CONFIG_VDC_OUTPUT_PLAN_DEFAULT_US &&
    VDC_OUTPUT_TIMING_COMMIT_DEFAULT_US == PRODUCT_CONFIG_VDC_OUTPUT_COMMIT_DEFAULT_US &&
    VDC_OUTPUT_TIMING_REFILL_DEFAULT_US == PRODUCT_CONFIG_VDC_OUTPUT_REFILL_DEFAULT_US,
    "output timing defaults and persisted ranges must agree");
static uint32_t s_output_timing_guard;
static vdc_output_timing_profile_t s_output_timing_profile = {
    VDC_OUTPUT_TIMING_PLAN_DEFAULT_US, VDC_OUTPUT_TIMING_COMMIT_DEFAULT_US,
    VDC_OUTPUT_TIMING_REFILL_DEFAULT_US};

bool vdc_dpll_manager_get_output_timing_profile(vdc_output_timing_profile_t *profile)
{
    if (!profile) return false;
    const uint32_t before = __atomic_load_n(&s_output_timing_guard, __ATOMIC_ACQUIRE);
    if (before & 1u) return false;
    const vdc_output_timing_profile_t value = {
        __atomic_load_n(&s_output_timing_profile.plan_ahead_us, __ATOMIC_RELAXED),
        __atomic_load_n(&s_output_timing_profile.commit_ahead_us, __ATOMIC_RELAXED),
        __atomic_load_n(&s_output_timing_profile.refill_low_us, __ATOMIC_RELAXED)};
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (before != __atomic_load_n(&s_output_timing_guard, __ATOMIC_ACQUIRE) ||
        !vdc_output_timing_profile_valid(&value)) return false;
    *profile = value;
    return true;
}

static bool output_timing_publish_stopped(void *context)
{
    const vdc_output_timing_profile_t *profile = context;
    if (!vdc_output_timing_profile_valid(profile) || !vdc_run_output_configuration_idle()) return false;
    (void)__atomic_add_fetch(&s_output_timing_guard, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&s_output_timing_profile.plan_ahead_us, profile->plan_ahead_us, __ATOMIC_RELAXED);
    __atomic_store_n(&s_output_timing_profile.commit_ahead_us, profile->commit_ahead_us, __ATOMIC_RELAXED);
    __atomic_store_n(&s_output_timing_profile.refill_low_us, profile->refill_low_us, __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(&s_output_timing_guard, 1u, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_output_timing_profile(const vdc_output_timing_profile_t *profile)
{
    if (get_core_num() != 0u || !s_vdc_tdma_service || !profile) return false;
    const vdc_output_timing_profile_t copy = *profile;
    if (!vdc_output_timing_profile_valid(&copy)) return false;
    return tdma_service_update_stopped_metadata(s_vdc_tdma_service,
        output_timing_publish_stopped, (void *)&copy);
}

bool vdc_dpll_manager_set_output_timing_profile_online(
    const vdc_output_timing_profile_t *profile)
{
    if (get_core_num() != 0u || !profile) return false;
    const vdc_output_timing_profile_t copy = *profile;
    if (!vdc_output_timing_profile_valid(&copy)) return false;
    /* publish() itself rejects a non-idle VDC output configuration. */
    return output_timing_publish_stopped((void *)&copy);
}

bool vdc_dpll_manager_default_output_timing(void)
{
    const vdc_output_timing_profile_t profile = {
        VDC_OUTPUT_TIMING_PLAN_DEFAULT_US, VDC_OUTPUT_TIMING_COMMIT_DEFAULT_US,
        VDC_OUTPUT_TIMING_REFILL_DEFAULT_US};
    return vdc_dpll_manager_set_output_timing_profile(&profile);
}

static bool output_timing_recall_stopped(void *context)
{
    (void)context;
    product_config_vdc_output_timing_profile_t saved;
    if (!product_config_get_vdc_output_timing_profile(&saved)) return false;
    const vdc_output_timing_profile_t profile = {
        saved.plan_ahead_us, saved.commit_ahead_us, saved.refill_low_us};
    return output_timing_publish_stopped((void *)&profile);
}

bool vdc_dpll_manager_recall_output_timing(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            output_timing_recall_stopped, NULL);
}

static bool output_timing_store_maintenance(void *context)
{
    (void)context;
    vdc_output_timing_profile_t profile;
    if (!vdc_run_output_configuration_idle() || !vdc_dpll_manager_get_output_timing_profile(&profile))
        return false;
    const product_config_vdc_output_timing_profile_t saved = {
        profile.plan_ahead_us, profile.commit_ahead_us, profile.refill_low_us};
    return product_config_set_vdc_output_timing_profile(&saved);
}

bool vdc_dpll_manager_store_output_timing(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_run_stopped_maintenance(s_vdc_tdma_service,
            output_timing_store_maintenance, NULL);
}

/* product_config_init migrates into RAM before Core1 starts; never write Flash here. */
bool output_timing_init_from_product_config(void)
{
    return get_core_num() == 0u && !vdc_dpll_manager_feedback_session() &&
        output_timing_recall_stopped(NULL);
}
