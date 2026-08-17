#include "tdma_runtime_owner.h"

#if defined(PROJECT_USE_FREERTOS) && PROJECT_USE_FREERTOS
#include "FreeRTOS.h"
#endif

static tdma_service_service_t s_tdma_runtime_owner;
static tdma_traffic_scheduler_t s_tdma_traffic_scheduler;
#if !defined(PROJECT_USE_FREERTOS) || !PROJECT_USE_FREERTOS
static tdma_traffic_scheduler_slot_t
    s_tdma_traffic_slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
#endif
static bool s_tdma_runtime_owner_initialized;

bool tdma_runtime_owner_init(void)
{
    if (s_tdma_runtime_owner_initialized) {
        return true;
    }
    tdma_traffic_scheduler_slot_t *slots = NULL;
#if defined(PROJECT_USE_FREERTOS) && PROJECT_USE_FREERTOS
    slots = pvPortMalloc(sizeof(tdma_traffic_scheduler_slot_t) *
                         TDMA_TRAFFIC_SCHEDULER_RUNTIME_SLOT_COUNT);
#else
    slots = s_tdma_traffic_slots;
#endif
    const bool initialized =
        slots != NULL &&
        tdma_traffic_scheduler_init(&s_tdma_traffic_scheduler,
                                    slots,
                                    TDMA_TRAFFIC_SCHEDULER_RUNTIME_SLOT_COUNT) &&
        tdma_service_init(&s_tdma_runtime_owner) &&
        tdma_service_bind_traffic_scheduler(&s_tdma_runtime_owner,
                                            &s_tdma_traffic_scheduler);
    if (!initialized) {
#if defined(PROJECT_USE_FREERTOS) && PROJECT_USE_FREERTOS
        if (slots != NULL) {
            vPortFree(slots);
        }
#endif
        return false;
    }
    s_tdma_runtime_owner_initialized = true;
    return true;
}

tdma_service_service_t *tdma_runtime_owner_get(void)
{
    return s_tdma_runtime_owner_initialized ? &s_tdma_runtime_owner : NULL;
}

tdma_traffic_scheduler_t *tdma_runtime_owner_get_scheduler(void)
{
    return s_tdma_runtime_owner_initialized ? &s_tdma_traffic_scheduler : NULL;
}
