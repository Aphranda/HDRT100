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

/* Core0 sole writer. One atomic signed word is a complete requested value.
 * Getter has no product-config/Flash/RTOS dependency and is safe for a future
 * Core1 output-start latch. This slice does not install that consumer. */
static int32_t s_output_delay_ns = PRODUCT_CONFIG_DPLL_OUTPUT_COMPENSATION_DEFAULT_NS;

bool vdc_dpll_manager_get_output_delay_ns(int32_t *value)
{
    if (!value) return false;
    *value = __atomic_load_n(&s_output_delay_ns, __ATOMIC_ACQUIRE);
    return true;
}

static bool output_delay_publish_stopped(void *context)
{
    if (!context) return false;
    __atomic_store_n(&s_output_delay_ns, *(const int32_t *)context, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_output_delay_ns(int32_t value)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            output_delay_publish_stopped, &value);
}

bool vdc_dpll_manager_default_output_delay(void)
{
    return vdc_dpll_manager_set_output_delay_ns(PRODUCT_CONFIG_DPLL_OUTPUT_COMPENSATION_DEFAULT_NS);
}

static bool output_delay_recall_stopped(void *context)
{
    (void)context;
    int32_t value;
    return product_config_get_dpll_output_compensation_ns(&value) &&
        output_delay_publish_stopped(&value);
}

bool vdc_dpll_manager_recall_output_delay(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service,
            output_delay_recall_stopped, NULL);
}

static bool output_delay_store_maintenance(void *context)
{
    (void)context;
    int32_t value;
    return vdc_dpll_manager_get_output_delay_ns(&value) &&
        product_config_set_dpll_output_compensation_ns(value);
}

bool vdc_dpll_manager_store_output_delay(void)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_run_stopped_maintenance(s_vdc_tdma_service,
            output_delay_store_maintenance, NULL);
}

/* Boot RAM restore only, after product_config_init and before Core1 starts. */
bool output_delay_init_from_product_config(void)
{
    return get_core_num() == 0u && !vdc_dpll_manager_feedback_session() &&
        output_delay_recall_stopped(NULL);
}
