#ifndef TDMA_SERVICE_TIMING_H
#define TDMA_SERVICE_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#define TDMA_SERVICE_TIMING_VERSION 4u
#ifndef TDMA_SERVICE_TIMING_ENABLED
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define TDMA_SERVICE_TIMING_ENABLED 1
#else
#define TDMA_SERVICE_TIMING_ENABLED 0
#endif
#endif

/* Nested intervals are inclusive and must not be added to their parents. */
typedef enum {
    TDMA_TIMING_PHYS_SERVICE = 0u,
    TDMA_TIMING_OWNER_SERVICE,
    TDMA_TIMING_REFMEM_PUBLISH,
    TDMA_TIMING_TRAINING_GATE,
    TDMA_TIMING_ANALYZER,
    TDMA_TIMING_ACCOUNTING,
    TDMA_TIMING_ADAPTER,
    TDMA_TIMING_RX_CAPTURE,
    TDMA_TIMING_RX_PARSE,
    TDMA_TIMING_OVERLAY_PREPARE,
    TDMA_TIMING_OVERLAY_BOUNDARY,
    /* Follower/legacy RX only; all nested inside RX_CAPTURE. The origin
     * owns a separate RX path and does not report these intervals. */
    TDMA_TIMING_RX_ACQUIRE,
    TDMA_TIMING_RX_PACKET_COPY,
    TDMA_TIMING_RX_CLOCK,
    TDMA_TIMING_RX_LATCH,
    /* Async acquisition and discovery copies only; nested in RX_ACQUIRE.
     * DMA observations include both the initial and post-copy count reads. */
    TDMA_TIMING_RX_DMA_OBSERVE,
    TDMA_TIMING_RX_LOCATE,
    TDMA_TIMING_RX_HEADER_CHECK,
    TDMA_TIMING_RX_RING_COPY,
    /* Runtime and intent dispatch are separate children of OWNER_SERVICE.
     * Runtime includes ADAPTER and RING_PUBLISH. RX_HANDOFF wraps rx_once,
     * including capture/parse when that path executes; origin may bypass it. */
    TDMA_TIMING_RING_RUNTIME,
    TDMA_TIMING_RING_PUBLISH,
    TDMA_TIMING_INTENT_DISPATCH,
    TDMA_TIMING_ADAPTER_PROLOGUE,
    TDMA_TIMING_RX_HANDOFF,
    TDMA_TIMING_ADAPTER_STATUS,
    TDMA_TIMING_STAGE_COUNT,
} tdma_service_timing_stage_t;

typedef struct {
    uint32_t sequence;
    uint64_t start_ticks;
    uint32_t total_ticks;
    uint32_t invalid_count;
    uint32_t elapsed_ticks[TDMA_TIMING_STAGE_COUNT];
    uint32_t calls[TDMA_TIMING_STAGE_COUNT];
} tdma_service_timing_record_t;

typedef struct {
    uint32_t version;
    uint32_t clock_hz;
    uint32_t reset_generation;
    uint32_t phase_count;
    tdma_service_timing_record_t last;
    /* One complete phase, not independent maxima assembled across phases. */
    tdma_service_timing_record_t peak;
} tdma_service_timing_snapshot_t;

#if TDMA_SERVICE_TIMING_ENABLED
/* Core1-only instrumentation, owned by the existing app TDMA phase. */
void tdma_service_timing_phase_begin(void);
void tdma_service_timing_phase_end(void);
uint64_t tdma_service_timing_now(void);
void tdma_service_timing_record(tdma_service_timing_stage_t stage, uint64_t start);
/* Core0 read attempts once. RESET is consumed at the next Core1 phase. */
bool tdma_service_timing_try_snapshot(tdma_service_timing_snapshot_t *snapshot);
uint32_t tdma_service_timing_request_reset(void);
#else
/* Standalone host component tests do not own a realtime phase or clock. */
static inline void tdma_service_timing_phase_begin(void) {}
static inline void tdma_service_timing_phase_end(void) {}
static inline uint64_t tdma_service_timing_now(void) { return 0ull; }
static inline void tdma_service_timing_record(tdma_service_timing_stage_t stage,
                                             uint64_t start)
{ (void)stage; (void)start; }
static inline bool tdma_service_timing_try_snapshot(tdma_service_timing_snapshot_t *snapshot)
{ (void)snapshot; return false; }
static inline uint32_t tdma_service_timing_request_reset(void) { return 0u; }
#endif

#endif
