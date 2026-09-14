#ifndef TDMA_SERVICE_TIMING_H
#define TDMA_SERVICE_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#define TDMA_SERVICE_TIMING_VERSION 10u
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
    /* Children of RX_PARSE (Core1 acceptance, including prepared frames).
     * Inspect/health and FIFO publish/commit stay separate: observing a new
     * mailbox does not commit its freshness before publication succeeds. */
    TDMA_TIMING_RX_INSPECT,
    TDMA_TIMING_RX_HEALTH,
    TDMA_TIMING_RX_EVIDENCE,
    TDMA_TIMING_RX_FIFO_PUBLISH,
    TDMA_TIMING_RX_COMMIT,
    TDMA_TIMING_RX_COMPLETE,
    /* Request-only path inside RX_HANDOFF; accepting a READY result instead
     * reports RX_PARSE. Edge read and rearm are children of LOCAL_TX_EDGE. */
    TDMA_TIMING_RX_REQUEST,
    TDMA_TIMING_RX_REQUEST_HINT,
    TDMA_TIMING_RX_LOCAL_TX_EDGE,
    TDMA_TIMING_TX_LATCH_READ,
    TDMA_TIMING_TX_LATCH_REARM,
    TDMA_TIMING_RX_REQUEST_PUBLISH,
    /* CLOCK and BIND are children of INTENT_DISPATCH. Exactly one SELECT
     * outcome is recorded per scheduler call. REFRESH is nested inside it;
     * EMPTY means all queues and recovery buffers were empty under lock. */
    TDMA_TIMING_INTENT_CLOCK,
    TDMA_TIMING_SELECT_EMPTY,
    TDMA_TIMING_SELECT_BLOCKED,
    TDMA_TIMING_SELECT_BUSY,
    TDMA_TIMING_SELECT_DISPATCH,
    TDMA_TIMING_SELECT_REFRESH,
    TDMA_TIMING_INTENT_BIND,
    /* DMA_OBSERVE includes these disjoint sites and their recording cost.
     * Copy rechecks protect different private buffers; neither is optional. */
    TDMA_TIMING_RX_DMA_INITIAL,
    TDMA_TIMING_RX_DMA_FRAME_RECHECK,
    TDMA_TIMING_RX_DMA_DISCOVERY_RECHECK,
    /* Children of RX_LATCH; capture ownership and empty FIFO skip rearm. */
    TDMA_TIMING_RX_LATCH_READ,
    TDMA_TIMING_RX_LATCH_REARM,
    /* Autonomous origin only, disjoint children of ADAPTER outside RX_HANDOFF.
     * Observe includes copying an accepted boundary; publish includes readiness,
     * FIFO acquisition, validation and deferred/unchanged publication paths. */
    TDMA_TIMING_ORIGIN_OBSERVE,
    TDMA_TIMING_ORIGIN_PUBLISH,
    /* Initial handoff only: ADMIT and BEGIN are disjoint children of ADAPTER.
     * CALIBRATION_CRC is nested inside ADMIT; it still reads every stage byte. */
    TDMA_TIMING_ORIGIN_ADMIT,
    TDMA_TIMING_ORIGIN_CALIBRATION_CRC,
    TDMA_TIMING_ORIGIN_BEGIN,
    TDMA_TIMING_STAGE_COUNT,
} tdma_service_timing_stage_t;

/* Core1 samples owner facts at both ends of the measured body. State uses
 * the low byte for this class and the high bytes for owner FSM/flags. */
#define TDMA_TIMING_STATE_AUTONOMOUS 2u
typedef struct {
    uint32_t state;
    uint32_t config_generation;
    uint32_t trial_epoch;
    uint32_t return_sequence;
} tdma_service_timing_context_t;

typedef struct {
    uint32_t sequence;
    uint64_t start_ticks;
    uint32_t total_ticks;
    uint32_t invalid_count;
    uint32_t full_phase_ticks;
    tdma_service_timing_context_t entry;
    tdma_service_timing_context_t exit;
    uint32_t elapsed_ticks[TDMA_TIMING_STAGE_COUNT];
    /* Counts are per phase. Saturation invalidates the record instead of
     * wrapping; SCPI still exports each count as an unsigned integer. */
    uint16_t calls[TDMA_TIMING_STAGE_COUNT];
} tdma_service_timing_record_t;

typedef struct {
    uint32_t version;
    uint32_t clock_hz;
    uint32_t reset_generation;
    uint32_t phase_count;
    tdma_service_timing_record_t last;
    /* One complete phase, not independent maxima assembled across phases. */
    tdma_service_timing_record_t peak;
    /* Selected by the outer scheduler interval. Transitional phases are
     * retained separately, never discarded to make RUN look faster. */
    tdma_service_timing_record_t autonomous_peak;
    tdma_service_timing_record_t other_peak;
    uint32_t autonomous_phase_count;
    uint32_t other_phase_count;
} tdma_service_timing_snapshot_t;

#if TDMA_SERVICE_TIMING_ENABLED
/* Core1-only instrumentation, owned by the existing app TDMA phase. */
void tdma_service_timing_phase_begin(void);
void tdma_service_timing_phase_end(void);
void tdma_service_timing_context(tdma_service_timing_context_t context, bool entry);
/* Called once by the Core1 scheduler after its TDMA service returns. The
 * interval includes begin/end instrumentation; this bookkeeping is outside it. */
void tdma_service_timing_scheduler_end(uint32_t elapsed_cycles);
uint64_t tdma_service_timing_now(void);
void tdma_service_timing_record(tdma_service_timing_stage_t stage, uint64_t start);
/* Core0 read attempts once. RESET is consumed at the next Core1 phase. */
bool tdma_service_timing_try_snapshot(tdma_service_timing_snapshot_t *snapshot);
uint32_t tdma_service_timing_request_reset(void);
#else
/* Standalone host component tests do not own a realtime phase or clock. */
static inline void tdma_service_timing_phase_begin(void) {}
static inline void tdma_service_timing_phase_end(void) {}
static inline void tdma_service_timing_context(tdma_service_timing_context_t context, bool entry)
{ (void)context; (void)entry; }
static inline void tdma_service_timing_scheduler_end(uint32_t elapsed_cycles)
{ (void)elapsed_cycles; }
static inline uint64_t tdma_service_timing_now(void) { return 0ull; }
static inline void tdma_service_timing_record(tdma_service_timing_stage_t stage,
                                             uint64_t start)
{ (void)stage; (void)start; }
static inline bool tdma_service_timing_try_snapshot(tdma_service_timing_snapshot_t *snapshot)
{ (void)snapshot; return false; }
static inline uint32_t tdma_service_timing_request_reset(void) { return 0u; }
#endif

#endif
