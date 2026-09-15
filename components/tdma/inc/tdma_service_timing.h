#ifndef TDMA_SERVICE_TIMING_H
#define TDMA_SERVICE_TIMING_H

#include <stdbool.h>
#include <stdint.h>

#define TDMA_SERVICE_TIMING_VERSION 11u
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
    /* Disjoint event-service children of PHYS_SERVICE. ENTRY includes a
     * waiting observer's start attempt; START_CUT measures the later monitor.
     * FEED includes the post-harvest fault check and raw diagnostic fields.
     * FINAL_CHECK includes fault-triggered feed, not the normal FEED interval.
     * RETAIN includes invalid-epoch retirement and service_max_us update.
     * PUBLISH includes the final snapshot copy omitted by service_max_us. */
    TDMA_TIMING_EVENT_ENTRY,
    TDMA_TIMING_EVENT_HARVEST,
    TDMA_TIMING_EVENT_CONVERT,
    TDMA_TIMING_EVENT_FEED,
    TDMA_TIMING_EVENT_FINAL_CHECK,
    TDMA_TIMING_EVENT_START_CUT,
    TDMA_TIMING_EVENT_RETAIN,
    TDMA_TIMING_EVENT_PUBLISH,
    /* Ordinary reference emission attempts, outside RX_HANDOFF. TX includes
     * preparation, launch and bookkeeping for beacon/retry/next/stale paths;
     * SUBMIT is only the nested physical callback, including backpressure. */
    TDMA_TIMING_REFERENCE_TX,
    TDMA_TIMING_REFERENCE_SUBMIT,
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

/* Aggregate diagnostics for the same RESET/phase lifecycle. Ages use the
 * owner's existing service time in ns; DMA gaps use existing clk_sys probes.
 * Neither is a wire timestamp or a production scheduling budget. */
#define TDMA_RX_TIMING_VERSION 1u
#define TDMA_RX_TIMING_STATION_STATES 5u
typedef enum {
    TDMA_RX_DROP_EPOCH = 0u,
    TDMA_RX_DROP_CLAMP,
    TDMA_RX_DROP_STALE_HINT,
    TDMA_RX_DROP_FRAME_COPY,
    TDMA_RX_DROP_DISCOVERY_COPY,
    TDMA_RX_DROP_CAUSE_COUNT,
} tdma_rx_drop_cause_t;

typedef struct {
    uint32_t version, clock_hz, reset_generation, phase_count, invalid_count;
    /* Indexed by tdma_rx_prepare_state_t: IDLE through CANCELLED. */
    uint32_t station_polls[TDMA_RX_TIMING_STATION_STATES];
    uint64_t station_age_max_ns[TDMA_RX_TIMING_STATION_STATES - 1u];
    uint32_t initial_observation_count, initial_gap_count;
    uint64_t initial_gap_max_ticks;
    uint32_t initial_backlog_max_words;
    uint32_t drop_count[TDMA_RX_DROP_CAUSE_COUNT];
    uint64_t clamp_skipped_words;
} tdma_rx_timing_snapshot_t;

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
void tdma_service_timing_rx_station(uint32_t state, uint64_t now_ns, uint64_t capture_ns);
void tdma_service_timing_rx_initial(uint64_t ticks, uint64_t backlog_words);
void tdma_service_timing_rx_drop(tdma_rx_drop_cause_t cause, uint64_t skipped_words);
bool tdma_service_timing_rx_try_snapshot(tdma_rx_timing_snapshot_t *snapshot);
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
static inline void tdma_service_timing_rx_station(uint32_t state, uint64_t now_ns, uint64_t capture_ns)
{ (void)state; (void)now_ns; (void)capture_ns; }
static inline void tdma_service_timing_rx_initial(uint64_t ticks, uint64_t backlog_words)
{ (void)ticks; (void)backlog_words; }
static inline void tdma_service_timing_rx_drop(tdma_rx_drop_cause_t cause, uint64_t skipped_words)
{ (void)cause; (void)skipped_words; }
static inline bool tdma_service_timing_rx_try_snapshot(tdma_rx_timing_snapshot_t *snapshot)
{ (void)snapshot; return false; }
#endif

#endif
