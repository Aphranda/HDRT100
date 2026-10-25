#ifndef TDMA_SERVICE_INTERNAL_H
#define TDMA_SERVICE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_service.h"

/* N5.0.4a: shared intent/result publication contract.  The single
 * definition of each helper stays in tdma_service.c; the Core1 execution
 * unit calls them through this header only.  Do not duplicate these
 * helpers (or their guards) in a second translation unit. */
#define tdma_service_OWNER_CORE1 1u
#define tdma_service_ERROR_NO_OPS 100u
#define tdma_service_DEFAULT_TIMESTAMP_RESOLUTION_NS 1000u
#define tdma_service_DEFAULT_TIMESTAMP_SOURCE \
    tdma_service_TIMESTAMP_SOURCE_SOFTWARE_US
#define tdma_service_DEFAULT_TIMESTAMP_FLAGS \
    tdma_service_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY
#define tdma_service_ERROR_WINDOW_MISSED 101u
#define TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT 64u
#define TDMA_STOPPED_UPDATE_REQUESTED (1u << 31u)
#define TDMA_STOPPED_UPDATE_APPLYING (1u << 30u)
#define TDMA_STOPPED_UPDATE_TOKEN_MASK (TDMA_STOPPED_UPDATE_APPLYING - 1u)

enum {
    TDMA_RING_CONTROL_NONE = 0u,
    TDMA_RING_CONTROL_RETIRE = 1u,
    TDMA_RING_CONTROL_ARM = 2u,
};

uint64_t tdma_service_now_ns(void);
uint32_t tdma_service_elapsed_ns(uint64_t start_ns, uint64_t done_ns);
uint32_t tdma_service_delta_ns(uint64_t end_ns, uint64_t start_ns);
void tdma_service_split_u64(uint64_t value, uint32_t *lo, uint32_t *hi);
uint32_t tdma_service_load(const volatile uint32_t *value);
void tdma_service_begin_intent_write(tdma_service_service_t *service);
void tdma_service_end_intent_write(tdma_service_service_t *service);
void tdma_service_begin_result_write(tdma_service_service_t *service);
void tdma_service_end_result_write(tdma_service_service_t *service);
void tdma_service_publish_exec_timestamp(
    tdma_service_service_t *service,
    const tdma_service_exec_status_t *status);

#endif /* TDMA_SERVICE_INTERNAL_H */
