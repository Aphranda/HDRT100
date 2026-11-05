#ifndef TDMA_PIO_SPI_RING_ADAPTER_H
#define TDMA_PIO_SPI_RING_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_adapter_comm_fsm.h"
#include "tdma_first_class.h"
#include "tdma_maintenance_stream.h"
#include "tdma_transport_load.h"
#include "tdma_flight_fifo.h"
#include "tdma_flight_engine.h"
#include "tdma_overlay_prepare.h"
#include "tdma_rx_prepare.h"
#include "tdma_origin_plan.h"
#include "tdma_priority_tx.h"
#include "tdma_receive_health.h"
#include "tdma_ring_runtime.h"
#include "tdma_transport_frame.h"

/* TDMA PIO SPI ring adapter (bring-up).
 *
 * This adapter implements tdma_ring_adapter_ops_t for the minimum-system PIO
 * SPI transport. It is transport-level only: it builds and parses
 * TdmaTransportFrame packets (IDLE_BEACON short frames during bring-up) and
 * publishes adapter lifecycle evidence (start/stop/service counts, UP/DOWN
 * running state, idle beacon counters, sequence/identity CRC and timestamp
 * metadata) into tdma_ring_adapter_status_t. It never fabricates
 * simultaneous_feedback_loop_evidence: the TdmaRingRuntime only sets that bit
 * when sequence/identity CRC/schedule CRC/timestamp correlation all hold.
 *
 * Physical layer contract (P0.5-3): the firmware binds optional phys_tx /
 * phys_rx callbacks once the PIO/SM/DMA bidirectional legs are available.
 * Without them the adapter reports no running legs and service() returns
 * false, so the ring runtime reports EVIDENCE_MISSING instead of pretending
 * the ring is up.
 */

#define TDMA_PIO_SPI_RING_ADAPTER_VERSION 13u

#define TDMA_PIO_SPI_RESIDENT_FEEDBACK_RESIDENT (1u << 0u)
#define TDMA_PIO_SPI_RESIDENT_FEEDBACK_SEEDED (1u << 1u)
#define TDMA_PIO_SPI_RESIDENT_FEEDBACK_ORIGIN (1u << 2u)
#define TDMA_PIO_SPI_RESIDENT_FEEDBACK_HOP (1u << 3u)
#define TDMA_PIO_SPI_RESIDENT_FEEDBACK_REQUIRED (1u << 4u)
#define TDMA_PIO_SPI_RESIDENT_FEEDBACK_SEQUENCE (1u << 5u)
#define TDMA_PIO_SPI_RESIDENT_FEEDBACK_IDENTITY (1u << 6u)
#define TDMA_PIO_SPI_RESIDENT_FEEDBACK_ALL \
    (TDMA_PIO_SPI_RESIDENT_FEEDBACK_RESIDENT | \
     TDMA_PIO_SPI_RESIDENT_FEEDBACK_SEEDED | \
     TDMA_PIO_SPI_RESIDENT_FEEDBACK_ORIGIN | \
     TDMA_PIO_SPI_RESIDENT_FEEDBACK_HOP | \
     TDMA_PIO_SPI_RESIDENT_FEEDBACK_REQUIRED | \
     TDMA_PIO_SPI_RESIDENT_FEEDBACK_SEQUENCE | \
     TDMA_PIO_SPI_RESIDENT_FEEDBACK_IDENTITY)

typedef enum {
    TDMA_PIO_SPI_RESIDENT_RESEED_NONE = 0u,
    TDMA_PIO_SPI_RESIDENT_RESEED_FEEDBACK_TIMEOUT_LAST_VALID = 1u,
    TDMA_PIO_SPI_RESIDENT_RESEED_BOOTSTRAP_EXHAUSTED_LAST_VALID = 2u,
} tdma_pio_spi_resident_reseed_reason_t;

#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_DISABLED (1u << 0u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_NO_SEQUENCE (1u << 1u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_TIMESTAMP_INELIGIBLE (1u << 2u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_RECORD_INVALID (1u << 3u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_SEQUENCE_MISMATCH (1u << 4u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_IDENTITY_MISSING (1u << 5u)
#define TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_TIMESTAMP_MISSING (1u << 6u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_TIMESTAMP_INELIGIBLE (1u << 0u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_RECORD_INVALID (1u << 1u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_SEQUENCE_MISMATCH (1u << 2u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_IDENTITY_MISSING (1u << 3u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_TIMESTAMP_MISSING (1u << 4u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_COMPACT_DECODE (1u << 5u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_COMMON_TIME_INVALID (1u << 6u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_LOCAL_TX_INVALID (1u << 7u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_LOCAL_TX_SEQUENCE_MISMATCH (1u << 8u)
#define TDMA_PIO_SPI_CLOCK_OBSERVATION_LOCAL_TX_IDENTITY_MISMATCH (1u << 9u)
#define TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH 8u
/* Per-origin-slot accepted RX accounting (live adjacency evidence). */
#define TDMA_PIO_SPI_RING_ADAPTER_SLOT_MAX 8u
#define TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH 8u
#define TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH 8u

typedef enum {
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE = 0u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT = 1u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_MISSING = 2u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED = 3u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME = 4u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_QUEUE_FULL = 5u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT = 6u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_GATE_REJECT = 7u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM = 8u,
    TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_ARM_BASE = 0x100u,
} tdma_pio_spi_ring_adapter_error_t;

#define TDMA_PIO_SPI_RING_ADAPTER_PHYS_ARM_ERROR(error) \
    (TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_ARM_BASE + (uint32_t)(error))

/* Ring node role (derived from the active ring config):
 * - REFERENCE: local_slot == reference_slot. It originates IDLE_BEACON /
 *   process-image short frames and correlates its own TX with the feedback
 *   frame that returns around the ring (this is the only node that may
 *   produce simultaneous_feedback_loop_evidence=1). It also publishes a
 *   DPLL observation for the returned frame using the calibrated complete
 *   loop path.
 * - FORWARD: any other node. It receives the frame from the previous board
 *   and re-emits it toward the next board, keeping origin/sequence/identity
 *   CRC unchanged and advancing hop/transport CRC. Its up/down running state
 *   proves the ring is serviced; each forward node consumes the DPLL trailer
 *   before forwarding. */
typedef enum {
    TDMA_PIO_SPI_RING_ROLE_REFERENCE = 0u,
    TDMA_PIO_SPI_RING_ROLE_FORWARD = 1u,
} tdma_pio_spi_ring_role_t;

typedef enum {
    /* Host fake physical layers and maintenance compatibility path. */
    TDMA_PIO_SPI_RING_FORWARDING_STORE_FORWARD = 0u,
    /* Product short-frame path: the PIO forwards while bytes are arriving. */
    TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT = 1u,
    /* Product process image: PIO overlays local bytes while forwarding. */
    TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE = 2u,
} tdma_pio_spi_ring_forwarding_mode_t;

/* Optional resident physical-layer control. When set, start() arms the
 * physical layer with the active ring config and stop() disarms it. */
typedef bool (*tdma_pio_spi_ring_phys_arm_fn)(
    void *context,
    const tdma_ring_runtime_config_t *config);
typedef bool (*tdma_pio_spi_ring_phys_disarm_fn)(void *context);
typedef bool (*tdma_pio_spi_ring_phys_train_fn)(void *context,
                                                uint32_t cycles);
typedef void (*tdma_pio_spi_ring_phys_train_service_fn)(void *context,
                                                        uint64_t now_ns);
typedef bool (*tdma_pio_spi_ring_phys_overlay_fn)(
    void *context,
    const uint8_t *incoming_packet,
    const uint8_t *processed_packet,
    size_t packet_size,
    const uint32_t *force_replace_payload_bitmap,
    size_t force_replace_payload_bitmap_words);
typedef bool (*tdma_pio_spi_ring_phys_overlay_boundary_fn)(void *context);
/* Optional recurrence backend: retire only observed DMA selection and check
 * the physical mode/role/persona and alignment gate. True may authorize reuse
 * without a new job grant; false retains the active plan and queued TX. */
typedef bool (*tdma_pio_spi_ring_phys_overlay_ready_fn)(void *context);
typedef bool (*tdma_pio_spi_ring_phys_overlay_job_fn)(
    void *context, tdma_overlay_prepare_t *job);

/* phys_tx pushes one complete packet onto the wire. On success it may fill
 * *tx_timestamp_ns with the hardware latch timestamp (0 means no hardware
 * timestamp is available for this TX). */
typedef bool (*tdma_pio_spi_ring_tx_fn)(void *context,
                                        const uint8_t *packet,
                                        size_t packet_size,
                                        uint64_t *tx_timestamp_ns);

/* phys_rx pulls one complete packet received from the wire. It returns true
 * when a packet is available and fills packet/packet_size/rx_timestamp_ns. */
typedef bool (*tdma_pio_spi_ring_rx_fn)(void *context,
                                        uint8_t *packet,
                                        size_t packet_capacity,
                                        size_t *packet_size,
                                        uint64_t *rx_timestamp_ns);
/* Optional same-call diagnostic provenance. A true receive may still have
 * flags=0 (legacy/origin/unavailable). No new timestamp authority is granted. */
typedef bool (*tdma_pio_spi_ring_rx_ex_fn)(void *context,
                                        uint8_t *packet,
                                        size_t packet_capacity,
                                        size_t *packet_size,
                                        uint64_t *rx_timestamp_ns,
                                        tdma_rx_capture_t *capture);
/* Owner-local diagnostic callbacks; neither extends the byte lease nor grants
 * timestamp authority. The query consumes only a worker-validated header. */
typedef uint32_t (*tdma_pio_spi_ring_rx_event_pin_fn)(void *context,
                                                    const tdma_rx_capture_t *capture);
typedef void (*tdma_pio_spi_ring_rx_event_query_fn)(void *context,
    const tdma_rx_capture_t *capture, uint32_t observer_epoch, uint32_t sequence,
    size_t packet_size, uint64_t station_age_ns);
typedef bool (*tdma_pio_spi_ring_feedback_fn)(void *context,
                                              uint32_t *round_trip_ns,
                                              uint32_t *resolution_ns,
                                              uint32_t *flags);
/* Optional readiness probe for the physical timestamp spine. The adapter
 * calls this only after phys_arm() succeeds and uses the returned metadata for
 * DPLL admission. A false result keeps timestamps diagnostic/invalid. */
typedef bool (*tdma_pio_spi_ring_phys_timestamp_ready_fn)(
    void *context,
    uint32_t *resolution_ns,
    uint32_t *flags);
/* A flight-origin TX is accepted at hardware launch and terminates later.
 * The adapter consumes this one-shot token to release the pending launch and
 * attach a non-zero PIO clock-latch timestamp to its reference evidence.
 * A zero timestamp is a failed/aborted terminal result, never DPLL evidence. */
typedef bool (*tdma_pio_spi_ring_phys_tx_complete_fn)(
    void *context,
    uint64_t *tx_timestamp_ns);
/* Optional follower observation token: one local regenerated TX edge for the
 * frame returned by phys_rx().  Zero/false is diagnostic-only. */
typedef bool (*tdma_pio_spi_ring_phys_local_tx_edge_fn)(
    void *context,
    uint64_t *tx_timestamp_ns);
/* Sequence-aware local TX edge callback.  expected_sequence and identity are
 * the frame currently being forwarded; the callback must return the physical
 * latch token for that forwarding edge. */
typedef bool (*tdma_pio_spi_ring_phys_local_tx_edge_ex_fn)(
    void *context,
    uint32_t expected_sequence,
    uint32_t expected_identity_crc32,
    tdma_ring_local_tx_edge_evidence_t *evidence);
typedef bool (*tdma_pio_spi_ring_phys_tx_retryable_fn)(
    const void *context);

/* Bound by the TDMA owner while stopped. begin is called only after the
 * owner has admitted a timing/resource budget at a completed bootstrap
 * boundary. No callback is an authorization to choose that budget here. */
typedef enum {
    TDMA_ORIGIN_ADMISSION_NONE = 0u,
    TDMA_ORIGIN_ADMISSION_READY = 1u,
    TDMA_ORIGIN_ADMISSION_REJECTED = 2u,
} tdma_origin_admission_result_t;

typedef struct {
    tdma_origin_admission_result_t (*admit)(void *context,
        const tdma_ring_runtime_config_t *config,
        uint32_t *rearm_budget_ticks, uint32_t *abort_poll_count);
    bool (*begin)(void *context, const tdma_ring_runtime_config_t *config,
                  const uint8_t *packet, size_t size,
                  uint32_t rearm_budget_ticks, uint32_t abort_poll_count);
    tdma_origin_build_result_t (*poll)(void *context);
    bool (*healthy)(const void *context);
    bool (*ready)(void *context);
    bool (*publish)(void *context, const uint8_t *mailbox, uint32_t *generation);
    bool (*observe)(void *context, tdma_origin_observation_t *observation);
    bool (*take_rx_observation)(void *context, tdma_origin_observation_t *observation);
    /* Optional paired callbacks use the existing cancellable preparation
     * station. Commit admits a READY private mailbox; it does not wait for it. */
    bool (*grant_tx)(void *context, tdma_overlay_prepare_t *job);
    bool (*commit_tx)(void *context, tdma_overlay_prepare_t *job, uint32_t *generation);
} tdma_pio_spi_ring_origin_ops_t;

typedef struct {
    uint32_t active; /* lifetime latch, retained on failure until STOP succeeds */
    uint32_t observed_return_count;
    uint32_t unobserved_cycle_count; /* gaps, never fabricated RX timeouts */
    uint32_t rejected_observation_count;
    uint32_t published_generation;
    uint32_t published_owner_generation;
    uint32_t published_owner_sequence;
    uint32_t matched_owner_generation;
    uint32_t matched_owner_sequence;
    tdma_origin_observation_t returned;
    tdma_origin_observation_t boundary; /* independent, may be newer than RX */
    /* TDMA-SEAT-01: fast-path opportunities and seat boarding on this origin. */
    uint32_t seat_opportunity_count;
    uint32_t seat_priority_count;
    uint32_t seat_first_class_count;
    uint32_t seat_priority_yield_count; /* priority new, first class boarded */
    uint32_t seat_first_class_reject_count;
    /* A frame was acquired from the flight FIFO and then rejected before boarding. The
     * view is released on every such path (a leaked CORE1_ACTIVE slot wedges the FIFO for
     * the whole run), and this counter says how often it happened. */
    uint32_t flight_publish_reject_count;
    uint32_t seat_first_class_wait_max; /* opportunities a record waited */
    uint32_t seat_first_class_wait;
    /* Bounded-service evidence: times a waiting first-class record reached the
     * declared wait budget while the priority seat kept boarding. */
    uint32_t seat_first_class_wait_violation_count;
    /* Loop-plan starvation: publish attempts deferred because the requested seat
     * has no window in the plan (every such attempt would otherwise defer
     * silently forever). The first-class counter is the seat-specific subset. */
    uint32_t seat_starved_count;
    uint32_t seat_first_class_starved_count;
    /* First-class boarding attempts whose hardware publish was deferred: the
     * record stays leased, and each deferral is a waiting opportunity. */
    uint32_t seat_first_class_deferred_count;
    uint32_t priority_boarded; /* last_priority holds the last priority boarding */
    uint8_t last_priority[TDMA_FLIGHT_SHORT_SLOT_SIZE];
} tdma_pio_spi_ring_origin_status_t;

/* TDMA-SEAT-01 rotation M:N (priority:first-class) per publication opportunity.
 * The defaults apply when a node has no explicit STOP-time setting; the live
 * ratio is carried by tdma_pio_spi_ring_adapter_t so each origin can configure
 * it. A zero share always falls back to the compiled default. */
#define TDMA_SEAT_ROTATION_DEFAULT_PRIORITY_SHARE 1u
#define TDMA_SEAT_ROTATION_DEFAULT_FIRST_CLASS_SHARE 1u
/* Both shares and the M+N cycle are bounded: the arbitration is integer-only
 * and a waiting record must never wait an unbounded number of opportunities. */
#define TDMA_SEAT_ROTATION_SHARE_MAX 8u
#define TDMA_SEAT_ROTATION_CYCLE_MAX 12u
/* Declared first-class service budget, in publication opportunities: a record
 * that keeps waiting while the priority seat boards reaches this bound and is
 * counted as a violation. One crossing is counted per wait episode, so the
 * counter is an explicit bounded-service fact rather than a raw maximum. */
#define TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES 8u

/* TDMA-SEAT-03 seat health verdict: one read that answers "is the first-class
 * seat behaving like the priority seat, and if not, why". Every bit is evidence
 * of a *degradation*; by-design backpressure (a stage refusal while a record is
 * already waiting) is reported as a counter, never as a reason bit. */
#define TDMA_SEAT_HEALTH_REASON_HOLD_DROPPED (1u << 0u)   /* refused record dropped, queue full */
#define TDMA_SEAT_HEALTH_REASON_HOLD_EXPIRED (1u << 1u)   /* record retired on retry budget */
#define TDMA_SEAT_HEALTH_REASON_ACK_OVERRIDE (1u << 2u)   /* shared control bytes overridden */
#define TDMA_SEAT_HEALTH_REASON_WAIT_VIOLATION (1u << 3u) /* service wait budget exceeded */
#define TDMA_SEAT_HEALTH_REASON_LANE_REJECT (1u << 4u)    /* lane refused a first-class record */
#define TDMA_SEAT_HEALTH_REASON_RETIRED_WAITING (1u << 5u) /* fact retired before boarding */
#define TDMA_SEAT_HEALTH_REASON_FIRST_CLASS_STARVED (1u << 6u) /* no loop window */
#define TDMA_SEAT_HEALTH_REASON_DECODE_REJECT (1u << 7u) /* slice did not decode */
/* A first-class position was lost END TO END: either the origin gave up
 * completing its freeze after the position window closed, or a follower was
 * asked to freeze a position it never had. The lane counters above only prove
 * what the seat did with the records it saw; without this bit a delivery that
 * never arrived left ok=1. */
#define TDMA_SEAT_HEALTH_REASON_FIRST_CLASS_MISSED (1u << 8u)
#define TDMA_SEAT_HEALTH_REASON_MASK_ALL ((1u << 9u) - 1u)

typedef struct {
    uint32_t lane_hold_dropped_count;
    uint32_t lane_hold_expired_count;
    uint32_t lane_ack_override_count;    uint32_t wait_violation_count;
    uint32_t lane_reject_first_class_count;
    uint32_t retired_waiting_count;
    uint32_t first_class_starved_count;
    uint32_t lane_decode_reject_count;
    /* Consumer-side loss: origin freeze abandonments plus follower FREEZEs for a
     * position with no local plan. The page reports both raw counters. */
    uint32_t first_class_missed_count;
} tdma_seat_health_facts_t;

/* Pure verdict: 0 means every seat invariant that must stay zero is zero. */
static inline uint32_t tdma_seat_health_reason(const tdma_seat_health_facts_t *facts)
{
    if (facts == NULL) return TDMA_SEAT_HEALTH_REASON_MASK_ALL;
    uint32_t mask = 0u;
    if (facts->lane_hold_dropped_count) mask |= TDMA_SEAT_HEALTH_REASON_HOLD_DROPPED;
    if (facts->lane_hold_expired_count) mask |= TDMA_SEAT_HEALTH_REASON_HOLD_EXPIRED;
    if (facts->lane_ack_override_count) mask |= TDMA_SEAT_HEALTH_REASON_ACK_OVERRIDE;
    if (facts->wait_violation_count) mask |= TDMA_SEAT_HEALTH_REASON_WAIT_VIOLATION;
    if (facts->lane_reject_first_class_count) mask |= TDMA_SEAT_HEALTH_REASON_LANE_REJECT;
    if (facts->retired_waiting_count) mask |= TDMA_SEAT_HEALTH_REASON_RETIRED_WAITING;
    if (facts->first_class_starved_count) mask |= TDMA_SEAT_HEALTH_REASON_FIRST_CLASS_STARVED;
    if (facts->lane_decode_reject_count) mask |= TDMA_SEAT_HEALTH_REASON_DECODE_REJECT;
    if (facts->first_class_missed_count) mask |= TDMA_SEAT_HEALTH_REASON_FIRST_CLASS_MISSED;
    return mask;
}

static inline bool tdma_seat_rotation_valid(uint32_t priority_share,
    uint32_t first_class_share)
{
    return priority_share >= 1u && first_class_share >= 1u &&
        priority_share <= TDMA_SEAT_ROTATION_SHARE_MAX &&
        first_class_share <= TDMA_SEAT_ROTATION_SHARE_MAX &&
        priority_share + first_class_share <= TDMA_SEAT_ROTATION_CYCLE_MAX;
}

/* Three-share form (owner decision 2026-10-16): the third share is the time reserved
 * for the path that does NOT use the fast channel ("second class" = ordinary frames /
 * refmem image traffic). Because that class is defined by exclusion, reserving its time
 * means the fast path must NOT claim the whole cycle: cycle = priority + first_class +
 * second_class, and in the second_class slots neither fast seat boards.
 * second_class == 0 reproduces today's behaviour exactly (cycle = p + f), so the
 * two-share wrapper above stays the compatible entry point.
 * Validation: each share within SHARE_MAX, at least one fast seat present, and the whole
 * cycle within CYCLE_MAX. */
static inline bool tdma_seat_rotation_valid3(uint32_t priority_share,
    uint32_t first_class_share, uint32_t second_class_share)
{
    return priority_share >= 1u && first_class_share >= 1u &&
        priority_share <= TDMA_SEAT_ROTATION_SHARE_MAX &&
        first_class_share <= TDMA_SEAT_ROTATION_SHARE_MAX &&
        second_class_share <= TDMA_SEAT_ROTATION_CYCLE_MAX &&
        priority_share + first_class_share + second_class_share <=
            TDMA_SEAT_ROTATION_CYCLE_MAX;
}

/* Cycle length the rotation walks: the two fast seats today, plus the reserved
 * second-class slots once that share is configured. */
static inline uint32_t tdma_seat_rotation_cycle(uint32_t priority_share,
    uint32_t first_class_share, uint32_t second_class_share)
{
    return priority_share + first_class_share + second_class_share;
}

/* Configured share, or the compiled default when nothing was set. */
static inline uint32_t tdma_seat_rotation_priority(uint32_t configured)
{
    return configured != 0u ? configured :
        TDMA_SEAT_ROTATION_DEFAULT_PRIORITY_SHARE;
}

static inline uint32_t tdma_seat_rotation_first_class(uint32_t configured)
{
    return configured != 0u ? configured :
        TDMA_SEAT_ROTATION_DEFAULT_FIRST_CLASS_SHARE;
}

/* Online-safe representation: the three shares fit in one 32-bit word (4 bits
 * each).  Core1 reads this word once per publication opportunity, so an online
 * change is never observed as a torn p/f/s mix. */
#define TDMA_SEAT_ROTATION_SHARE_MASK 0xFu
#define TDMA_SEAT_ROTATION_PACK(p, f, s) \
    (((uint32_t)(p) & TDMA_SEAT_ROTATION_SHARE_MASK) | \
     (((uint32_t)(f) & TDMA_SEAT_ROTATION_SHARE_MASK) << 4) | \
     (((uint32_t)(s) & TDMA_SEAT_ROTATION_SHARE_MASK) << 8))

typedef struct {
    uint32_t priority;
    uint32_t first_class;
    uint32_t second_class;
} tdma_seat_rotation_shares_t;

static inline tdma_seat_rotation_shares_t tdma_seat_rotation_unpack(
    uint32_t packed)
{
    const tdma_seat_rotation_shares_t shares = {
        packed & TDMA_SEAT_ROTATION_SHARE_MASK,
        (packed >> 4) & TDMA_SEAT_ROTATION_SHARE_MASK,
        (packed >> 8) & TDMA_SEAT_ROTATION_SHARE_MASK,
    };
    return shares;
}

typedef struct {
    uint32_t version;
    uint32_t started;
    uint32_t service_count;
    uint32_t role;
    uint32_t forwarding_mode;
    /* TDMA-SEAT-01 rotation ratio in effect (compiled default when unset). */
    uint32_t seat_rotation_priority_share;
    uint32_t seat_rotation_first_class_share;
    /* Reserved slots per rotation cycle for the path that does NOT use the fast
     * channel (owner decision 2026-10-16: "second class = whatever does not go
     * through the fast channel"). Zero keeps today's behaviour, where the cycle is
     * exactly priority + first_class and the ordinary/refmem path only gets the
     * leftovers; a non-zero value makes the fast path yield those slots. */
    uint32_t seat_rotation_second_class_share;
    /* First-class seat service facts (zero while no seat object is attached):
     * stage attempts refused by the single-depth slot, and waiting records
     * retired before boarding. */
    uint32_t first_class_stage_refused_count;
    uint32_t first_class_retired_waiting_count;
    uint32_t forward_count;
    uint32_t up_sequence;
    uint32_t down_rx_sequence;
    uint32_t up_tx_frame_crc32;
    uint32_t down_rx_frame_crc32;
    uint32_t idle_beacon_tx_count;
    uint32_t idle_beacon_rx_count;
    uint32_t tx_count;
    uint32_t diagnostic_burst_limit;
    uint32_t diagnostic_burst_launched;
    uint32_t diagnostic_burst_valid;
    uint32_t rx_count;
    uint32_t rx_bad_count;
    /* Which origin slots this board actually hears: bit i = slot i accepted. */
    uint32_t rx_slot_mask;
    uint32_t rx_slot_count[TDMA_PIO_SPI_RING_ADAPTER_SLOT_MAX];
    uint32_t rx_transport_bad_count;
    uint32_t rx_schedule_bad_count;
    uint32_t rx_profile_bad_count;
    uint32_t last_bad_transport_result;
    uint32_t last_bad_sequence;
    uint32_t last_bad_schedule_crc32;
    uint32_t last_bad_profile_crc32;
    uint32_t last_bad_header_diff_count;
    uint32_t last_bad_header_first_diff_offset;
    uint32_t last_bad_header_expected_byte;
    uint32_t last_bad_header_observed_byte;
    uint32_t last_bad_packet_diff_count;
    uint32_t last_bad_packet_first_diff_offset;
    uint32_t last_bad_packet_expected_byte;
    uint32_t last_bad_packet_observed_byte;
    uint32_t last_bad_clock_evidence;
    uint32_t last_bad_expected_transport_crc32;
    uint32_t last_bad_observed_transport_crc32;
    uint32_t last_bad_recomputed_transport_crc32;
    uint32_t last_bad_expected_payload_crc32;
    uint32_t last_bad_observed_payload_crc32;
    uint32_t clock_evidence_enabled;
    uint32_t rx_drop_count;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t feedback_reference_sequence;
    uint32_t feedback_reference_frame_crc32;
    uint64_t reference_tx_timestamp_ns;
    uint64_t feedback_rx_timestamp_ns;
    uint64_t last_rx_service_ns;
    uint64_t last_service_ns;
    tdma_ring_clock_observation_t clock_observation;
    uint32_t clock_observation_count;
    uint32_t clock_observation_reject_count;
    uint32_t clock_observation_last_reject_reason;
    uint32_t clock_evidence_build_count;
    uint32_t clock_evidence_build_reject_count;
    uint32_t clock_evidence_build_last_reason;
    uint32_t clock_evidence_last_tx_encoded;
    uint32_t clock_evidence_last_rx_encoded;
    uint32_t pending_tx_evidence;
    uint32_t pending_tx_evidence_sequence;
    uint32_t last_error;
    uint32_t local_slot_id;
    uint32_t schedule_crc32;
    uint32_t ring_profile_crc32;
    uint32_t feedback_timeout_ns;
    uint32_t loop_delay_ns;
    uint32_t loop_delay_tolerance_ns;
    uint64_t rx_ready_timestamp_ns;
    uint32_t flight_map_configured;
    uint32_t flight_map_active;
    uint32_t flight_map_crc32;
    uint32_t flight_map_generation;
    uint32_t flight_map_apply_count;
    uint32_t flight_input_bytes;
    uint32_t flight_output_bytes;
    uint32_t flight_tx_stale_reuse_count;
    uint32_t flight_map_reject_count;
    uint32_t flight_length_reject_count;
    uint32_t flight_tx_unavailable_count;
    uint32_t last_rx_origin_slot_id;
    uint32_t last_rx_hop_count;
    uint32_t last_rx_hop_limit;
    uint32_t last_rx_flags;
    uint32_t last_rx_sequence;
    uint32_t last_rx_identity_crc32;
    uint32_t resident_feedback_condition_mask;
    uint32_t resident_feedback_all_mask;
    uint32_t comm_fsm_state;
    uint32_t resident_seeded;
    uint32_t resident_return_ready;
    uint32_t resident_bootstrap_retry_count;
    uint32_t resident_overlay_bootstrap_prepared;
    uint32_t resident_overlay_target_sequence;
    uint32_t comm_fsm_timed_out_window_count;
    uint32_t resident_stale_cycle_count;
    tdma_receive_health_snapshot_t receive_health;
    uint32_t comm_fsm_window_sequence;
    uint32_t comm_fsm_completed_window_count;
    uint32_t resident_last_completed_cycle;
    uint32_t resident_last_completed_segment_mask;
    uint32_t comm_fsm_last_error;
    uint32_t resident_reseed_count;
    uint32_t resident_last_reseed_reason;
    uint64_t local_tx_edge_timestamp_ns;
    uint32_t local_tx_edge_count;
    uint32_t local_tx_edge_miss_count;
    uint32_t local_tx_edge_unpaired_count;
    uint32_t local_tx_edge_sequence;
    uint32_t local_tx_edge_identity_crc32;
    uint32_t local_tx_edge_capture_generation;
    uint32_t local_tx_edge_flags;
    tdma_pio_spi_ring_origin_status_t origin;
} tdma_pio_spi_ring_adapter_snapshot_t;

typedef struct {
    uint32_t valid;
    uint32_t node_count;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t marker_next_node[TDMA_RING_CALIBRATION_LINK_MAX];
    uint32_t data_next_node[TDMA_RING_CALIBRATION_LINK_MAX];
} tdma_pio_spi_ring_topology_t;

/* HAOFV payload hook: a registered maintenance service is serviced after the
 * T2 flight/priority path and only gets the remaining cycle budget. */
typedef bool (*tdma_ring_maintenance_service_fn)(void *context,
                                                 uint64_t now_ns);
typedef bool (*tdma_ring_maintenance_mailbox_fn)(
    void *context,
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE]);

typedef struct {
    tdma_ring_runtime_config_t config;
    bool configured;
    tdma_pio_spi_ring_tx_fn phys_tx;
    tdma_pio_spi_ring_rx_fn phys_rx;
    tdma_pio_spi_ring_rx_ex_fn phys_rx_ex;
    tdma_pio_spi_ring_rx_event_pin_fn phys_rx_event_pin;
    tdma_pio_spi_ring_rx_event_query_fn phys_rx_event_query;
    tdma_pio_spi_ring_feedback_fn phys_feedback;
    void *phys_context;
    tdma_pio_spi_ring_phys_arm_fn phys_arm;
    uint32_t (*phys_last_error)(const void *context);
    tdma_pio_spi_ring_phys_tx_retryable_fn phys_tx_retryable;
    tdma_pio_spi_ring_phys_disarm_fn phys_disarm;
    tdma_pio_spi_ring_phys_arm_fn phys_arm_request;
    void (*phys_stopped)(void *context);
    tdma_pio_spi_ring_phys_timestamp_ready_fn phys_timestamp_ready;
    tdma_pio_spi_ring_phys_tx_complete_fn phys_tx_complete;
    tdma_pio_spi_ring_phys_local_tx_edge_fn phys_local_tx_edge;
    tdma_pio_spi_ring_phys_local_tx_edge_ex_fn phys_local_tx_edge_ex;
    tdma_pio_spi_ring_phys_train_fn phys_train;
    tdma_pio_spi_ring_phys_train_service_fn phys_train_service;
    tdma_pio_spi_ring_phys_overlay_fn phys_prepare_overlay;
    tdma_pio_spi_ring_phys_overlay_boundary_fn phys_service_overlay_boundary;
    tdma_pio_spi_ring_phys_overlay_ready_fn phys_overlay_ready;
    tdma_overlay_prepare_t *overlay_preparation;
    tdma_rx_prepare_t *rx_preparation;
    tdma_pio_spi_ring_phys_overlay_job_fn phys_grant_overlay;
    tdma_pio_spi_ring_phys_overlay_job_fn phys_commit_overlay;
    tdma_pio_spi_ring_origin_ops_t phys_origin;
    tdma_priority_tx_provider_t priority_tx_provider;
    tdma_pio_spi_ring_origin_status_t origin;
    struct {
        uint32_t generation;
        uint32_t owner_generation;
        uint32_t owner_sequence;
        uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    } origin_shadow[TDMA_ORIGIN_PLAN_BANK_COUNT];
    void *phys_ctrl_context;
    tdma_flight_fifo_t *flight_fifo;
    tdma_first_class_t *first_class;
    tdma_flight_engine_t *flight_engine;
    tdma_ring_maintenance_service_fn maintenance_service;
    void *maintenance_service_context;
    tdma_ring_maintenance_mailbox_fn maintenance_mailbox;
    void *maintenance_mailbox_context;
    volatile uint32_t maintenance_rx_sequence;
    volatile uint32_t maintenance_inject_attempts;
    volatile uint32_t maintenance_inject_ok;
    /* N3.2.1 diagnostics: distinguish "the mailbox callback produced a chunk"
     * from "the frame carrying it was actually launched", and count how often
     * a frame sequence repeats (the follower dedups the immediate previous
     * sequence).  Read-only; never changes transport behaviour. */
    volatile uint32_t maintenance_inject_launched;
    volatile uint32_t maintenance_inject_dropped_unlaunched;
    volatile uint32_t maintenance_inject_new_sequence;
    volatile uint32_t maintenance_inject_repeated_sequence;
    volatile uint32_t maintenance_inject_last_sequence;
    volatile uint32_t maintenance_beacon_frames;
    volatile uint32_t maintenance_rx_polls;
    volatile uint32_t maintenance_rx_seen_chunks;
    volatile uint32_t maintenance_rx_accepted_chunks;
    volatile uint32_t maintenance_rx_dedup_sequence;
    volatile uint32_t maintenance_rx_bad_mailbox;
    volatile uint32_t maintenance_rx_last_sequence;
    volatile uint32_t maintenance_rx_last_poll_sequence;
    struct tdma_transport_load *transport_load;
    void (*transport_load_rx)(void *context,
                              const uint8_t *payload,
                              size_t payload_size);
    void *transport_load_rx_context;
    volatile uint32_t transport_load_tx_ok;
    volatile uint32_t transport_load_tx_fail;
    volatile uint32_t transport_load_tx_stage;
    volatile uint32_t transport_load_rx_seen;
    volatile uint32_t transport_load_rx_delivered;
    volatile uint32_t rx_class_config_control;
    volatile uint32_t rx_class_process_image;
    volatile uint32_t rx_class_other;
    volatile uint32_t transport_load_build_class;
    volatile uint32_t transport_load_build_size;
    volatile uint32_t transport_load_build_load;
    volatile uint32_t maintenance_priority_calls;
    volatile uint32_t maintenance_yield_slots;
    volatile uint32_t maintenance_attempts;
    volatile uint32_t maintenance_publish_ok;
    tdma_receive_health_t receive_health;
    tdma_pio_spi_ring_topology_t topology;
    uint32_t topology_probe_mode;
    volatile uint32_t snapshot_guard;
    tdma_pio_spi_ring_role_t role;
    tdma_pio_spi_ring_forwarding_mode_t forwarding_mode;
    /* TDMA-SEAT-01 origin arbitration ratio (M:N), packed into one word so a
     * running ring can switch it atomically at the next publication
     * opportunity.  Zero means "not set" and the runtime falls back to the
     * compiled default. */
    volatile uint32_t seat_rotation_packed;
    volatile uint32_t seat_rotation_generation;
    uint32_t started;
    uint32_t service_count;
    uint32_t forward_count;
    uint32_t up_sequence;
    uint32_t down_rx_sequence;
    uint32_t up_tx_frame_crc32;
    uint32_t down_rx_frame_crc32;
    uint32_t idle_beacon_tx_count;
    uint32_t idle_beacon_rx_count;
    uint32_t tx_count;
    /* Accepted physical submissions in this ARM; retained across STOP. */
    uint32_t diagnostic_burst_launched;
    uint32_t diagnostic_burst_valid;
    uint32_t rx_count;
    uint32_t rx_bad_count;
    /* Which origin slots this board actually hears: bit i = slot i accepted. */
    uint32_t rx_slot_mask;
    uint32_t rx_slot_count[TDMA_PIO_SPI_RING_ADAPTER_SLOT_MAX];
    uint32_t rx_transport_bad_count;
    uint32_t rx_schedule_bad_count;
    uint32_t rx_profile_bad_count;
    uint32_t last_bad_transport_result;
    uint32_t last_bad_sequence;
    uint32_t last_bad_schedule_crc32;
    uint32_t last_bad_profile_crc32;
    uint32_t last_bad_header_diff_count;
    uint32_t last_bad_header_first_diff_offset;
    uint32_t last_bad_header_expected_byte;
    uint32_t last_bad_header_observed_byte;
    uint32_t last_bad_packet_diff_count;
    uint32_t last_bad_packet_first_diff_offset;
    uint32_t last_bad_packet_expected_byte;
    uint32_t last_bad_packet_observed_byte;
    uint32_t last_bad_clock_evidence;
    uint32_t last_bad_expected_transport_crc32;
    uint32_t last_bad_observed_transport_crc32;
    uint32_t last_bad_recomputed_transport_crc32;
    uint32_t last_bad_expected_payload_crc32;
    uint32_t last_bad_observed_payload_crc32;
    uint32_t clock_evidence_enabled;
    uint32_t rx_drop_count;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t feedback_timestamp_resolution_ns;
    uint32_t feedback_timestamp_flags;
    uint32_t feedback_reference_sequence;
    uint32_t feedback_reference_frame_crc32;
    uint64_t reference_tx_timestamp_ns;
    uint64_t feedback_rx_timestamp_ns;
    uint64_t last_rx_service_ns;
    uint64_t last_service_ns;
    tdma_ring_clock_observation_t clock_observation;
    uint32_t clock_observation_count;
    uint32_t clock_observation_reject_count;
    uint32_t clock_observation_last_reject_reason;
    uint32_t clock_evidence_build_count;
    uint32_t clock_evidence_build_reject_count;
    uint32_t clock_evidence_build_last_reason;
    uint32_t clock_evidence_last_tx_encoded;
    uint32_t clock_evidence_last_rx_encoded;
    uint32_t last_error;
    uint8_t last_rx_packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t last_rx_packet_size;
    bool last_rx_gate_accepted;
    uint32_t last_rx_new_segment_mask;
    uint32_t last_rx_origin_slot_id;
    uint32_t last_rx_hop_count;
    uint32_t last_rx_hop_limit;
    uint32_t last_rx_flags;
    uint32_t last_rx_sequence;
    uint32_t last_rx_identity_crc32;
    uint32_t resident_feedback_condition_mask;
    uint64_t next_tx_deadline_ns;
    uint64_t rx_ready_timestamp_ns;
    tdma_adapter_comm_fsm_t comm_fsm;
    bool resident_seeded;
    bool resident_return_ready;
    uint32_t resident_return_new_segment_mask;
    bool resident_completion_pending;
    uint32_t resident_pending_completed_cycle;
    uint32_t resident_pending_completed_segment_mask;
    bool resident_bootstrap_tx_pending;
    uint32_t resident_bootstrap_retry_count;
    uint8_t resident_packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t resident_packet_size;
    bool resident_overlay_bootstrap_prepared;
    uint32_t resident_overlay_target_sequence;
    uint32_t resident_overlay_tx_generation;
    uint32_t resident_overlay_tx_sequence;
    uint32_t resident_stale_cycle_count;
    uint32_t resident_last_completed_cycle;
    uint32_t resident_last_completed_segment_mask;
    uint32_t resident_reseed_count;
    uint32_t resident_last_reseed_reason;
    /* Follower regenerated TX latches and reference origin-completion latches
     * are retained as sequence/identity-bound diagnostic provenance. */
    uint64_t local_tx_edge_timestamp_ns;
    uint32_t local_tx_edge_count;
    uint32_t local_tx_edge_miss_count;
    uint32_t local_tx_edge_unpaired_count;
    uint32_t local_tx_edge_sequence;
    uint32_t local_tx_edge_identity_crc32;
    uint32_t local_tx_edge_capture_generation;
    uint32_t local_tx_edge_flags;
    tdma_ring_local_tx_edge_evidence_t local_tx_edge_evidence[
        TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH];
    /* Reference flight TX completes asynchronously.  A passed emission
     * deadline must not resubmit while the physical completion token is
     * outstanding. */
    bool reference_tx_completion_pending;
    uint32_t pending_tx_evidence_sequence;
    uint32_t pending_tx_evidence_identity_crc32;
    bool pending_tx_evidence;
    struct {
        uint32_t sequence;
        uint32_t identity_crc32;
        uint64_t timestamp_ns;
        uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t packet_size;
        bool clock_evidence;
        bool valid;
    } reference_tx_evidence[TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH];
    struct {
        uint32_t sequence;
        uint32_t identity_crc32;
        uint64_t timestamp_ns;
        bool valid;
    } local_rx_evidence[TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH];
    /* Runtime physical station and legacy injected queue are exclusive
     * stopped-time bindings. DMA never owns this storage. */
    union {
        struct {
            uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
            size_t packet_size;
            uint64_t timestamp_ns;
            bool valid;
        } rx_queue[TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH];
        tdma_rx_prepare_t rx_station;
    };
    uint32_t rx_queue_head;
    uint32_t rx_queue_count;
    /* Reference-origin loop/beat period (see tdma_ring_loop_time_t).  The
     * accumulator shares the adapter's single core1 writer and is read only
     * through the status snapshot, so the published statistic is always the
     * quotient of the guarded sample_count. */
    tdma_ring_loop_time_t loop_time;
    uint64_t loop_time_sum_ns;
    /* Raw TIMER1 ticks of the previous accepted own-frame return. */
    uint64_t loop_time_last_rx_ticks;
    /* Anchor of the previous accepted own-frame return for the autonomous
     * origin's feedback correlation.  The cadence emits the frame that follows
     * a return from that return, so this anchor is the transmission boundary of
     * the frame that returns next: see tdma_pio_spi_ring_origin.inc.  The
     * identity words prove the pair spans one emission and not two. */
    struct {
        uint64_t ticks;
        uint32_t sequence;
        uint32_t generation;
    } origin_feedback_tx;
} tdma_pio_spi_ring_adapter_t;

bool tdma_pio_spi_ring_adapter_init(tdma_pio_spi_ring_adapter_t *adapter);
/* Reuses the existing injection pool. Once bound, inject_rx is rejected;
 * only physical admission can write this station. Call only while stopped. */
bool tdma_pio_spi_ring_adapter_enable_async_rx(tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_adapter_set_phys_origin(
    tdma_pio_spi_ring_adapter_t *adapter, const tdma_pio_spi_ring_origin_ops_t *ops);
/* Core1 owner only. The caller supplies an admitted budget; this function
 * never derives admission from a measured/nominal physical period. Success
 * begins PREPARING; subsequent service calls poll once until installed. */
bool tdma_pio_spi_ring_adapter_start_origin(tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t rearm_budget_ticks, uint32_t abort_poll_count);
bool tdma_pio_spi_ring_adapter_set_calibration_topology(
    tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_ring_calibration_stage_t *stage);
bool tdma_pio_spi_ring_adapter_set_bringup_topology(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t node_count, uint32_t reference_slot);
void tdma_pio_spi_ring_adapter_clear_calibration_topology(
    tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_adapter_set_topology_probe_mode(
    tdma_pio_spi_ring_adapter_t *adapter, bool enabled);
void tdma_pio_spi_ring_adapter_set_phys(tdma_pio_spi_ring_adapter_t *adapter,
                                        tdma_pio_spi_ring_tx_fn tx,
                                        tdma_pio_spi_ring_rx_fn rx,
                                        void *phys_context);
/* STOP setup only; set_phys clears this callback when replacing the backend. */
void tdma_pio_spi_ring_adapter_set_phys_rx_ex(tdma_pio_spi_ring_adapter_t *adapter,
                                             tdma_pio_spi_ring_rx_ex_fn rx);
/* STOP-only paired registration; a missing member disables both callbacks. */
void tdma_pio_spi_ring_adapter_set_phys_rx_event(tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_rx_event_pin_fn pin, tdma_pio_spi_ring_rx_event_query_fn query);
void tdma_pio_spi_ring_adapter_set_phys_feedback(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_feedback_fn feedback);
void tdma_pio_spi_ring_adapter_set_phys_ctrl(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_arm_fn arm,
    tdma_pio_spi_ring_phys_disarm_fn disarm,
    tdma_pio_spi_ring_phys_train_fn train,
    tdma_pio_spi_ring_phys_train_service_fn train_service,
    void *phys_ctrl_context);
void tdma_pio_spi_ring_adapter_set_phys_geometry_lifecycle(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_arm_fn arm_request,
    void (*stopped)(void *context));
void tdma_pio_spi_ring_adapter_set_phys_error_reader(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t (*last_error)(const void *context));
void tdma_pio_spi_ring_adapter_set_phys_tx_retryable(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_tx_retryable_fn tx_retryable);
void tdma_pio_spi_ring_adapter_set_phys_timestamp_ready(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_timestamp_ready_fn timestamp_ready);
void tdma_pio_spi_ring_adapter_set_phys_tx_complete(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_tx_complete_fn tx_complete);
void tdma_pio_spi_ring_adapter_set_phys_local_tx_edge(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_local_tx_edge_fn tx_edge);
void tdma_pio_spi_ring_adapter_set_phys_local_tx_edge_ex(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_local_tx_edge_ex_fn tx_edge);
void tdma_pio_spi_ring_adapter_set_phys_overlay(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_overlay_fn prepare_overlay,
    tdma_pio_spi_ring_phys_overlay_boundary_fn service_overlay_boundary,
    tdma_pio_spi_ring_phys_overlay_ready_fn overlay_ready);
void tdma_pio_spi_ring_adapter_set_timestamp_metadata(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t resolution_ns,
    uint32_t flags);
void tdma_pio_spi_ring_adapter_set_flight_fifo(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_flight_fifo_t *fifo);
void tdma_pio_spi_ring_adapter_set_first_class(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_first_class_t *first_class);
void tdma_pio_spi_ring_adapter_set_flight_engine(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_flight_engine_t *engine);
void tdma_pio_spi_ring_adapter_set_maintenance_service(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_ring_maintenance_service_fn service,
    void *context);
void tdma_pio_spi_ring_adapter_set_maintenance_mailbox(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_ring_maintenance_mailbox_fn provider,
    void *context);
bool tdma_pio_spi_ring_adapter_set_transport_load(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_transport_load_t *load);
void tdma_pio_spi_ring_adapter_set_transport_load_rx(
    tdma_pio_spi_ring_adapter_t *adapter,
    void (*callback)(void *context,
                     const uint8_t *payload,
                     size_t payload_size),
    void *context);
void tdma_pio_spi_ring_adapter_get_transport_load_tx(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *ok,
    uint32_t *fail,
    uint32_t *stage);
void tdma_pio_spi_ring_adapter_get_transport_load_rx(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *seen,
    uint32_t *delivered);
void tdma_pio_spi_ring_adapter_get_rx_debug(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *drop_count,
    uint32_t *last_packet_size,
    uint32_t *last_payload_class,
    uint32_t *config_control_count,
    uint32_t *process_image_count);
void tdma_pio_spi_ring_adapter_get_transport_load_build(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *payload_class,
    uint32_t *payload_size,
    uint32_t *load_frame);
void tdma_pio_spi_ring_adapter_get_maintenance_inject(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *attempts,
    uint32_t *ok);

/* N3.2.1 read-only maintenance diagnostics.  The caller must not infer a
 * delivery guarantee from these counters; they only localize where a chunk is
 * lost (callback, unlaunched frame, repeated sequence, follower dedup, or bad
 * mailbox). */
typedef struct {
    uint32_t inject_attempts;
    uint32_t inject_ok;
    uint32_t inject_launched;
    uint32_t inject_dropped_unlaunched;
    uint32_t inject_new_sequence;
    uint32_t inject_repeated_sequence;
    uint32_t inject_last_sequence;
    uint32_t beacon_frames;
    uint32_t rx_polls;
    uint32_t rx_seen_chunks;
    uint32_t rx_accepted_chunks;
    uint32_t rx_dedup_sequence;
    uint32_t rx_bad_mailbox;
    uint32_t rx_last_sequence;
    uint32_t rx_last_poll_sequence;
    uint32_t rx_sequence_cursor;
} tdma_maintenance_adapter_diag_t;

void tdma_pio_spi_ring_adapter_get_maintenance_diag(
    const tdma_pio_spi_ring_adapter_t *adapter,
    tdma_maintenance_adapter_diag_t *diag);
bool tdma_pio_spi_ring_adapter_set_forwarding_mode(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_forwarding_mode_t mode);
bool tdma_pio_spi_ring_adapter_set_clock_evidence_enabled(
    tdma_pio_spi_ring_adapter_t *adapter, bool enabled);
/* TDMA-SEAT-01: set the origin rotation ratio (M:N) before the adapter starts.
 * Both shares must satisfy tdma_seat_rotation_valid(). Rejects a running
 * adapter so the ratio cannot change under a live emission sequence. */
/* Three-share entry point (owner decision 2026-10-16). `second_class_share` reserves
 * that many slots of the rotation cycle for the path that does not use the fast channel;
 * 0 is exactly the two-share behaviour. Rejects a running adapter and any share set that
 * cannot form a cycle. */
bool tdma_pio_spi_ring_adapter_set_seat_rotation3(
    tdma_pio_spi_ring_adapter_t *adapter, uint32_t priority_share,
    uint32_t first_class_share, uint32_t second_class_share);
uint32_t tdma_pio_spi_ring_adapter_seat_rotation_second_class(
    const tdma_pio_spi_ring_adapter_t *adapter);
bool tdma_pio_spi_ring_adapter_set_seat_rotation(
    tdma_pio_spi_ring_adapter_t *adapter, uint32_t priority_share,
    uint32_t first_class_share);
bool tdma_pio_spi_ring_adapter_inject_rx(tdma_pio_spi_ring_adapter_t *adapter,
                                         const uint8_t *packet,
                                         size_t packet_size,
                                         uint64_t rx_timestamp_ns);
/* Core1 maintenance payload only: read one independent LONG frame that the
 * origin scheduler already admitted. The T2 flight FIFO is never touched. */
bool tdma_pio_spi_ring_adapter_read_maintenance(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint8_t *payload,
    size_t payload_capacity,
    size_t *payload_size,
    uint32_t *transport_sequence);
bool tdma_pio_spi_ring_adapter_take_maintenance_chunk(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint8_t chunk[TDMA_MAINTENANCE_CHUNK_SIZE],
    uint32_t *source_slot);
void tdma_pio_spi_ring_adapter_get_maintenance_counters(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *priority_calls,
    uint32_t *yield_slots,
    uint32_t *attempts,
    uint32_t *publish_ok);
const tdma_ring_adapter_ops_t *tdma_pio_spi_ring_adapter_ops(void);
/* One bounded read. A writer in progress or changed version returns false. */
bool tdma_pio_spi_ring_adapter_try_get_snapshot(
    const tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_adapter_snapshot_t *snapshot);
/* Legacy Core0 maintenance query; may retry until the owner publishes. */
bool tdma_pio_spi_ring_adapter_get_snapshot(
    const tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_adapter_snapshot_t *snapshot);
bool tdma_pio_spi_ring_adapter_read_accepted_image(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint64_t now_ns,
    tdma_receive_image_t *image);

/* Latched streaming-schedule evidence. These are monotonic totals kept outside
 * the per-handoff origin state, so a post-run read still shows whether the
 * window gate allowed or blocked priority emissions. */
typedef struct {
    uint32_t allow_total;
    uint32_t block_total;
} tdma_origin_schedule_evidence_t;

void tdma_pio_spi_ring_adapter_schedule_evidence(tdma_origin_schedule_evidence_t *out);

#endif
