#include "tdma_seat_admission.h"
#include "tdma_seat_mount_store.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_pio_spi_ring_adapter_internal.h"
#include "tdma_long_frame.h"

#include <string.h>

#include "tdma_process_image_layout.h"
#include "tdma_service_timing.h"

#if TDMA_SERVICE_TIMING_ENABLED
/* Only the device build owns a real timebase (see tdma_service_timing.h). */
#include "vdc_timestamp_clock.h"
#endif

#define TDMA_PIO_SPI_RING_RESIDENT_BOOTSTRAP_RETRY_MAX 1u

#include "tdma_pio_spi_ring_origin.h"
#include "tdma_pio_spi_ring_rx_prepare.h"
#include "tdma_pio_spi_ring_adapter_api.h"
#include "tdma_seat_admission.h"
#include "tdma_seat_mount_store.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_pio_spi_ring_adapter_internal.h"
#include "tdma_long_frame.h"

#include <string.h>

#include "tdma_process_image_layout.h"
#include "tdma_service_timing.h"

#if TDMA_SERVICE_TIMING_ENABLED
/* Only the device build owns a real timebase (see tdma_service_timing.h). */
#include "vdc_timestamp_clock.h"
#endif

#define TDMA_PIO_SPI_RING_RESIDENT_BOOTSTRAP_RETRY_MAX 1u

/* On-board TDMA ring loop/beat period: the interval between successive
 * arrivals of the reference's own frame at the reference (rx->rx).  The
 * resident-origin cadence emits the next frame from a return, so one accepted
 * return is exactly one revolution of the ring as seen by the reference.
 *
 * Both endpoints are the same observation point, so the systematic core1
 * acceptance latency cancels in the difference and no absolute per-edge latch
 * is claimed.  The residual uncertainty is the run-to-run variation of that
 * latency; min/max publish it and it is never replaced by the sampling
 * quantum alone.
 *
 * The dedicated tdma_origin_rtt latch is deliberately NOT the source: on the
 * recorded hardware it reports a fixed 62-count (496 ns) local CS->CS deskew
 * of the reference's own transmission, which is not a ring revolution. */
uint32_t tdma_pio_spi_ring_adapter_loop_tick_hz(void)
{
#if TDMA_SERVICE_TIMING_ENABLED
    return vdc_timestamp_clock_tick_hz();
#else
    /* Host component tests own no TIMER1 and stub the timing module to zero. */
    return 0u;
#endif
}

/* Raw TIMER1 ticks -> ns through the timestamp clock owner, so the loop
 * measurement uses the same timebase and conversion as every other TDMA
 * timestamp.  A build without that owner has no timebase at all. */
uint64_t tdma_pio_spi_ring_adapter_loop_ns(uint64_t ticks)
{
#if TDMA_SERVICE_TIMING_ENABLED
    return vdc_timestamp_clock_ticks_to_ns(ticks);
#else
    (void)ticks;
    return 0ull;
#endif
}

void tdma_pio_spi_ring_adapter_note_origin_return(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    const uint32_t tick_hz = tdma_pio_spi_ring_adapter_loop_tick_hz();
    /* Raw 64-bit TIMER1 ticks, the same counter the owner's phases record. */
    const uint64_t ticks = tdma_service_timing_now();
    if (tick_hz == 0u || ticks == 0ull) {
        return;
    }
    tdma_ring_loop_time_t *loop = &adapter->loop_time;
    /* Same rounding rule as vdc_timestamp_clock_resolution_ns(). */
    loop->resolution_ns = (1000000000u + tick_hz - 1u) / tick_hz;
    const uint64_t previous = adapter->loop_time_last_rx_ticks;
    adapter->loop_time_last_rx_ticks = ticks;
    if (previous == 0ull || ticks <= previous) {
        /* First return of this session, or a counter that did not advance.
         * Neither is turned into an interval. */
        if (previous != 0ull) {
            loop->flags |= TDMA_RING_LOOP_TIME_FLAG_STALE;
        }
        return;
    }
    const uint64_t period_ns =
        tdma_pio_spi_ring_adapter_loop_ns(ticks - previous);
    if (period_ns == 0ull || period_ns > UINT32_MAX ||
        UINT64_MAX - adapter->loop_time_sum_ns < period_ns) {
        /* Not representable: dropped and marked, never truncated. */
        loop->flags |= TDMA_RING_LOOP_TIME_FLAG_SATURATED;
        return;
    }
    loop->last_period_ns = (uint32_t)period_ns;
    if (loop->sample_count == 0u || period_ns < loop->min_period_ns) {
        loop->min_period_ns = (uint32_t)period_ns;
    }
    if (loop->sample_count == 0u || period_ns > loop->max_period_ns) {
        loop->max_period_ns = (uint32_t)period_ns;
    }
    adapter->loop_time_sum_ns += period_ns;
    loop->sample_count++;
    loop->mean_period_ns =
        (uint32_t)(adapter->loop_time_sum_ns / loop->sample_count);
    /* A dropped interval stays visible: the mean/min/max omit it for the rest
     * of the session.  STALE is not sticky, because this return was measured. */
    loop->flags = TDMA_RING_LOOP_TIME_FLAG_VALID |
        (loop->flags & TDMA_RING_LOOP_TIME_FLAG_SATURATED) |
        (loop->sample_count == 1u ? TDMA_RING_LOOP_TIME_FLAG_FIRST_SAMPLE : 0u);
}

void tdma_pio_spi_ring_adapter_reset_loop_time(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    adapter->loop_time = (tdma_ring_loop_time_t){0};
    adapter->loop_time_sum_ns = 0ull;
    adapter->loop_time_last_rx_ticks = 0ull;
}

bool tdma_pio_spi_ring_adapter_burst_exhausted(
    const tdma_pio_spi_ring_adapter_t *adapter)
{
    const uint32_t limit = tdma_ring_diagnostic_burst_limit(adapter->config.flags);
    return limit != 0u && adapter->diagnostic_burst_launched >= limit;
}

_Static_assert(sizeof(tdma_rx_prepare_t) <= sizeof(((tdma_pio_spi_ring_adapter_t *)0)->rx_queue),
               "RX station must fit the existing injected packet pool");

static void tdma_pio_spi_ring_put_u32(uint8_t *dst, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        dst[i] = (uint8_t)(value >> (i * 8u));
    }
}

uint32_t tdma_pio_spi_ring_get_u32(const uint8_t *src)
{
    uint32_t value = 0u;
    for (uint32_t i = 0u; i < 4u; i++) {
        value |= (uint32_t)src[i] << (i * 8u);
    }
    return value;
}

void tdma_pio_spi_ring_adapter_sample_local_tx_edge(
    tdma_pio_spi_ring_adapter_t *adapter,
    bool paired_with_rx);

/* The reference node has no regenerated follower TX edge to sample. Its
 * origin TX completion is the physical edge for the same local clock. Bind
 * that latch to the exact transport identity at publication so a returned
 * process frame cannot be paired with a newer emission. A zero timestamp
 * deliberately leaves no record: missing physical completion remains
 * ineligible DPLL evidence. */
void tdma_pio_spi_ring_adapter_bind_reference_local_tx_edge(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t sequence,
    uint32_t identity_crc32,
    uint64_t timestamp_ns)
{
    if (adapter == NULL ||
        adapter->role != TDMA_PIO_SPI_RING_ROLE_REFERENCE ||
        sequence == 0u || identity_crc32 == 0u || timestamp_ns == 0ull) {
        return;
    }
    const uint32_t evidence_index = sequence %
        TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH;
    tdma_ring_local_tx_edge_evidence_t *evidence =
        &adapter->local_tx_edge_evidence[evidence_index];
    adapter->local_tx_edge_capture_generation++;
    evidence->timestamp_ns = timestamp_ns;
    evidence->sequence = sequence;
    evidence->identity_crc32 = identity_crc32;
    evidence->capture_generation = adapter->local_tx_edge_capture_generation;
    evidence->flags = TDMA_RING_LOCAL_TX_EDGE_FLAG_TIMESTAMP_VALID |
        TDMA_RING_LOCAL_TX_EDGE_FLAG_SEQUENCE_BOUND |
        TDMA_RING_LOCAL_TX_EDGE_FLAG_IDENTITY_BOUND;
    adapter->local_tx_edge_count++;
    adapter->local_tx_edge_timestamp_ns = timestamp_ns;
    adapter->local_tx_edge_sequence = sequence;
    adapter->local_tx_edge_identity_crc32 = identity_crc32;
    adapter->local_tx_edge_flags = evidence->flags;
}

bool tdma_pio_spi_ring_product_payload(const tdma_pio_spi_ring_adapter_t *adapter,
                                             size_t payload_size)
{
    return adapter != NULL && adapter->flight_engine != NULL &&
        tdma_flight_payload_slots(payload_size) != 0u &&
        adapter->flight_engine->map.payload_size == payload_size;
}

bool tdma_pio_spi_ring_adapter_build_dpll_observation(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint8_t *payload,
    size_t payload_size)
{
    if (payload == NULL ||
        !tdma_pio_spi_ring_product_payload(adapter, payload_size)) {
        return false;
    }
    tdma_pio_spi_ring_put_u32(
        &payload[payload_size - TDMA_FLIGHT_DPLL_OBSERVATION_SIZE], 0u);
    if (adapter == NULL) {
        return false;
    }
    adapter->clock_evidence_last_tx_encoded = 0u;
    uint32_t reject_reason = 0u;
    if (adapter->clock_evidence_enabled == 0u) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_DISABLED;
    }
    if (adapter->up_sequence == 0u) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_NO_SEQUENCE;
    }
    if (adapter->timestamp_resolution_ns == 0u ||
        (adapter->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) == 0u ||
        (adapter->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) != 0u) {
        reject_reason |=
            TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_TIMESTAMP_INELIGIBLE;
    }
    if (reject_reason != 0u) {
        adapter->clock_evidence_build_reject_count++;
        adapter->clock_evidence_build_last_reason = reject_reason;
        return false;
    }
    const uint32_t evidence_index = adapter->up_sequence %
        TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
    const uint32_t sequence =
        adapter->reference_tx_evidence[evidence_index].sequence;
    if (!adapter->reference_tx_evidence[evidence_index].valid) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_RECORD_INVALID;
    }
    if (sequence != adapter->up_sequence) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_SEQUENCE_MISMATCH;
    }
    if (adapter->reference_tx_evidence[evidence_index].identity_crc32 == 0u) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_IDENTITY_MISSING;
    }
    if (adapter->reference_tx_evidence[evidence_index].timestamp_ns == 0ull) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_TIMESTAMP_MISSING;
    }
    if (reject_reason != 0u) {
        adapter->clock_evidence_build_reject_count++;
        adapter->clock_evidence_build_last_reason = reject_reason;
        return false;
    }

    const uint32_t encoded =
        tdma_process_image_dpll_observation_encode_phase(
            adapter->reference_tx_evidence[evidence_index].timestamp_ns,
            adapter->config.cycle_period_ns);
    if (encoded == 0u) {
        adapter->clock_evidence_build_reject_count++;
        adapter->clock_evidence_build_last_reason =
            TDMA_PIO_SPI_CLOCK_EVIDENCE_BUILD_TIMESTAMP_INELIGIBLE;
        return false;
    }
    tdma_pio_spi_ring_put_u32(
        &payload[payload_size - TDMA_FLIGHT_DPLL_OBSERVATION_SIZE], encoded);
    adapter->clock_evidence_build_count++;
    adapter->clock_evidence_build_last_reason = 0u;
    adapter->clock_evidence_last_tx_encoded = encoded;
    return true;
}

bool tdma_pio_spi_ring_adapter_correlate_dpll_observation(
    tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_transport_frame_view_t *view)
{
    if (adapter == NULL || view == NULL ||
        adapter->clock_evidence_enabled == 0u ||
        view->payload_class != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE ||
        (view->flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) == 0u ||
        !tdma_pio_spi_ring_product_payload(adapter, view->payload_size) ||
        view->payload == NULL ||
        view->transport_sequence <=
            TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_SEQUENCE_LAG) {
        return false;
    }
    adapter->clock_observation_last_reject_reason = 0u;

    const uint32_t encoded = tdma_pio_spi_ring_get_u32(
        &view->payload[view->payload_size - TDMA_FLIGHT_DPLL_OBSERVATION_SIZE]);
    if ((encoded & TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_VALID_MASK) == 0u) {
        return false;
    }
    const uint32_t sequence = view->transport_sequence -
        TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_SEQUENCE_LAG;
    const uint32_t evidence_index = sequence %
        TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH;
    const bool local_eligible = adapter->timestamp_resolution_ns != 0u &&
        (adapter->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) != 0u &&
        (adapter->timestamp_flags &
         TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u;
    uint32_t reject_reason = 0u;
    if (!local_eligible) {
        reject_reason |=
            TDMA_PIO_SPI_CLOCK_OBSERVATION_TIMESTAMP_INELIGIBLE;
    }
    if (!adapter->local_rx_evidence[evidence_index].valid) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_OBSERVATION_RECORD_INVALID;
    }
    if (adapter->local_rx_evidence[evidence_index].sequence != sequence) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_OBSERVATION_SEQUENCE_MISMATCH;
    }
    if (adapter->local_rx_evidence[evidence_index].identity_crc32 == 0u) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_OBSERVATION_IDENTITY_MISSING;
    }
    if (adapter->local_rx_evidence[evidence_index].timestamp_ns == 0ull) {
        reject_reason |= TDMA_PIO_SPI_CLOCK_OBSERVATION_TIMESTAMP_MISSING;
    }
    const tdma_ring_local_tx_edge_evidence_t *local_tx =
        &adapter->local_tx_edge_evidence[evidence_index];
    if (adapter->phys_local_tx_edge_ex != NULL) {
        if ((local_tx->flags & TDMA_RING_LOCAL_TX_EDGE_FLAG_TIMESTAMP_VALID) == 0u ||
            local_tx->timestamp_ns == 0ull) {
            reject_reason |= TDMA_PIO_SPI_CLOCK_OBSERVATION_LOCAL_TX_INVALID;
        }
        if ((local_tx->flags & TDMA_RING_LOCAL_TX_EDGE_FLAG_SEQUENCE_BOUND) == 0u ||
            local_tx->sequence != sequence) {
            reject_reason |= TDMA_PIO_SPI_CLOCK_OBSERVATION_LOCAL_TX_SEQUENCE_MISMATCH;
        }
        if ((local_tx->flags & TDMA_RING_LOCAL_TX_EDGE_FLAG_IDENTITY_BOUND) == 0u ||
            local_tx->identity_crc32 !=
                adapter->local_rx_evidence[evidence_index].identity_crc32) {
            reject_reason |= TDMA_PIO_SPI_CLOCK_OBSERVATION_LOCAL_TX_IDENTITY_MISMATCH;
        }
    }
    if (reject_reason != 0u) {
        adapter->clock_observation_last_reject_reason = reject_reason;
        return false;
    }
    const uint32_t reference_tx_phase_ns =
        (encoded & TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_MASK) *
        TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS;
    if (adapter->config.cycle_period_ns == 0u ||
        reference_tx_phase_ns >= adapter->config.cycle_period_ns) {
        adapter->clock_observation_last_reject_reason =
            TDMA_PIO_SPI_CLOCK_OBSERVATION_COMPACT_DECODE;
        return false;
    }
    const uint64_t sequence_time_ns =
        (uint64_t)sequence * (uint64_t)adapter->config.cycle_period_ns;
    if (UINT64_MAX - sequence_time_ns < (uint64_t)reference_tx_phase_ns) {
        adapter->clock_observation_last_reject_reason =
            TDMA_PIO_SPI_CLOCK_OBSERVATION_COMMON_TIME_INVALID;
        return false;
    }
    const uint64_t common_effective_time_ns =
        sequence_time_ns + (uint64_t)reference_tx_phase_ns;

    tdma_ring_clock_observation_t observation;
    memset(&observation, 0, sizeof(observation));
    observation.valid = 1u;
    observation.node_count = adapter->config.node_count;
    observation.source_node = adapter->config.local_slot_id;
    observation.reference_node = view->origin_slot_id;
    observation.correlated_sequence = sequence;
    observation.frame_crc32 =
        adapter->local_rx_evidence[evidence_index].identity_crc32;
    observation.schedule_crc32 = view->schedule_crc32;
    observation.timestamp_resolution_ns =
        adapter->timestamp_resolution_ns >
                TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS
            ? adapter->timestamp_resolution_ns
            : TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_TICK_QUANTUM_NS;
    observation.timestamp_flags =
        TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    observation.correlated_frame_evidence = 1u;
    observation.correlation_flags =
        TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
        TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME;
    if (observation.source_node == observation.reference_node) {
        observation.correlation_flags |=
            TDMA_RING_CLOCK_OBSERVATION_FLAG_LOCAL_PHASE_SAME_CLOCK;
    } else {
        /* A forwarded frame supplies a remote origin phase and a receiver-
         * local latch. Keep the raw phase for diagnostics until the output
         * observer supplies a calibrated local-to-common mapping. */
        observation.correlation_flags |=
            TDMA_RING_CLOCK_OBSERVATION_FLAG_LOCAL_PHASE_RAW;
    }
    observation.reference_tx_phase_ns = reference_tx_phase_ns;
    observation.local_rx_phase_ns = (uint32_t)(
        adapter->local_rx_evidence[evidence_index].timestamp_ns %
        adapter->config.cycle_period_ns);
    observation.common_effective_time_ns = common_effective_time_ns;
    /* Kept solely for compatibility/diagnostics. The receiver must not use
     * this locally reconstructed value as a remote absolute TX latch. */
    observation.reference_tx_timestamp_ns = 0ull;
    observation.local_rx_timestamp_ns =
        adapter->local_rx_evidence[evidence_index].timestamp_ns;
    if (adapter->phys_local_tx_edge_ex != NULL) {
        observation.local_tx_edge = *local_tx;
        observation.local_tx_phase_ns = (uint32_t)(
            local_tx->timestamp_ns % adapter->config.cycle_period_ns);
    }
    adapter->clock_observation = observation;
    adapter->clock_observation_count++;
    adapter->clock_observation_last_reject_reason = 0u;
    adapter->local_rx_evidence[evidence_index].valid = false;
    return true;
}

void tdma_pio_spi_ring_adapter_set_error(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t error)
{
    if (adapter != NULL) {
        adapter->last_error = error;
    }
}

void tdma_pio_spi_ring_adapter_sample_local_tx_edge(
    tdma_pio_spi_ring_adapter_t *adapter,
    bool paired_with_rx)
{
    if (adapter == NULL ||
        (adapter->phys_local_tx_edge == NULL &&
         adapter->phys_local_tx_edge_ex == NULL)) {
        return;
    }
    const uint32_t expected_sequence = paired_with_rx
        ? adapter->last_rx_sequence : 0u;
    const uint32_t expected_identity = paired_with_rx
        ? adapter->last_rx_identity_crc32 : 0u;
    tdma_ring_local_tx_edge_evidence_t evidence;
    memset(&evidence, 0, sizeof(evidence));
    bool captured = false;
    if (adapter->phys_local_tx_edge_ex != NULL) {
        captured = adapter->phys_local_tx_edge_ex(
            adapter->phys_context,
            expected_sequence,
            expected_identity,
            &evidence);
    } else {
        captured = adapter->phys_local_tx_edge(
            adapter->phys_context, &evidence.timestamp_ns);
        if (captured && evidence.timestamp_ns != 0ull) {
            evidence.flags = TDMA_RING_LOCAL_TX_EDGE_FLAG_TIMESTAMP_VALID;
        }
    }
    if (!captured || evidence.timestamp_ns == 0ull) {
        adapter->local_tx_edge_miss_count++;
        return;
    }
    adapter->local_tx_edge_count++;
    adapter->local_tx_edge_timestamp_ns = evidence.timestamp_ns;
    adapter->local_tx_edge_sequence = evidence.sequence;
    adapter->local_tx_edge_identity_crc32 = evidence.identity_crc32;
    adapter->local_tx_edge_capture_generation = evidence.capture_generation;
    adapter->local_tx_edge_flags = evidence.flags;
    if (!paired_with_rx ||
        (evidence.flags & TDMA_RING_LOCAL_TX_EDGE_FLAG_SEQUENCE_BOUND) == 0u ||
        (evidence.flags & TDMA_RING_LOCAL_TX_EDGE_FLAG_IDENTITY_BOUND) == 0u) {
        adapter->local_tx_edge_unpaired_count++;
    }
    if (paired_with_rx && expected_sequence != 0u) {
        const uint32_t evidence_index = expected_sequence %
            TDMA_PIO_SPI_RING_ADAPTER_RX_EVIDENCE_DEPTH;
        adapter->local_tx_edge_evidence[evidence_index] = evidence;
    }
}

void tdma_pio_spi_ring_adapter_reset_bad_packet_diagnostic(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    adapter->last_bad_header_diff_count = 0u;
    adapter->last_bad_header_first_diff_offset = UINT32_MAX;
    adapter->last_bad_header_expected_byte = 0u;
    adapter->last_bad_header_observed_byte = 0u;
    adapter->last_bad_packet_diff_count = 0u;
    adapter->last_bad_packet_first_diff_offset = UINT32_MAX;
    adapter->last_bad_packet_expected_byte = 0u;
    adapter->last_bad_packet_observed_byte = 0u;
    adapter->last_bad_clock_evidence = 0u;
    adapter->last_bad_expected_transport_crc32 = 0u;
    adapter->last_bad_observed_transport_crc32 = 0u;
    adapter->last_bad_recomputed_transport_crc32 = 0u;
    adapter->last_bad_expected_payload_crc32 = 0u;
    adapter->last_bad_observed_payload_crc32 = 0u;
}

bool tdma_pio_spi_ring_adapter_queue_push(
    tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t timestamp_ns)
{
    if (adapter == NULL || packet == NULL || packet_size == 0u ||
        packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX ||
        adapter->rx_preparation == &adapter->rx_station) {
        return false;
    }
    if (adapter->rx_queue_count >= TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH) {
        adapter->rx_drop_count++;
        return false;
    }
    const uint32_t tail =
        (adapter->rx_queue_head + adapter->rx_queue_count) %
        TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH;
    memcpy(adapter->rx_queue[tail].packet, packet, packet_size);
    adapter->rx_queue[tail].packet_size = packet_size;
    adapter->rx_queue[tail].timestamp_ns = timestamp_ns;
    adapter->rx_queue[tail].valid = true;
    adapter->rx_queue_count++;
    return true;
}

bool tdma_pio_spi_ring_adapter_queue_pop(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint8_t *packet,
    size_t packet_capacity,
    size_t *packet_size,
    uint64_t *timestamp_ns)
{
    if (packet_size != NULL) {
        *packet_size = 0u;
    }
    if (adapter == NULL || packet == NULL || packet_capacity == 0u ||
        packet_size == NULL || timestamp_ns == NULL ||
        adapter->rx_queue_count == 0u) {
        return false;
    }
    const uint32_t head = adapter->rx_queue_head;
    if (!adapter->rx_queue[head].valid ||
        adapter->rx_queue[head].packet_size > packet_capacity) {
        return false;
    }
    memcpy(packet,
           adapter->rx_queue[head].packet,
           adapter->rx_queue[head].packet_size);
    *packet_size = adapter->rx_queue[head].packet_size;
    *timestamp_ns = adapter->rx_queue[head].timestamp_ns;
    adapter->rx_queue[head].valid = false;
    adapter->rx_queue_head =
        (adapter->rx_queue_head + 1u) % TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH;
    adapter->rx_queue_count--;
    return true;
}

bool tdma_pio_spi_ring_adapter_init(tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter == NULL) {
        return false;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->clock_evidence_enabled = 1u;
    adapter->seat_rotation_priority_share =
        TDMA_SEAT_ROTATION_DEFAULT_PRIORITY_SHARE;
    adapter->seat_rotation_first_class_share =
        TDMA_SEAT_ROTATION_DEFAULT_FIRST_CLASS_SHARE;
    tdma_adapter_comm_fsm_init(&adapter->comm_fsm);
    tdma_pio_spi_ring_adapter_reset_bad_packet_diagnostic(adapter);
    (void)tdma_receive_health_init(&adapter->receive_health);
    return true;
}

bool tdma_pio_spi_ring_adapter_enable_async_rx(tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter == NULL || adapter->started != 0u || adapter->origin.active != 0u ||
        adapter->rx_queue_count != 0u || adapter->rx_preparation != NULL) return false;
    memset(&adapter->rx_station, 0, sizeof(adapter->rx_station));
    adapter->rx_preparation = &adapter->rx_station;
    return true;
}

bool tdma_pio_spi_ring_adapter_set_calibration_topology(
    tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_ring_calibration_stage_t *stage)
{
    if (adapter == NULL || adapter->started != 0u || stage == NULL ||
        !tdma_ring_runtime_validate_calibration_stage(
            stage, stage->node_count, NULL)) {
        return false;
    }
    tdma_pio_spi_ring_topology_t topology;
    memset(&topology, 0, sizeof(topology));
    topology.valid = 1u;
    topology.node_count = stage->node_count;
    topology.topology_generation = stage->topology_generation;
    topology.topology_crc32 = stage->topology_crc32;
    for (uint32_t link_index = 0u; link_index < stage->node_count;
         link_index++) {
        const tdma_ring_calibration_link_t *link =
            &stage->links[link_index];
        topology.marker_next_node[link->marker_source_node] =
            link->marker_destination_node;
        topology.data_next_node[link->data_source_node] =
            link->data_destination_node;
    }
    adapter->topology = topology;
    adapter->topology_probe_mode = 0u;
    return true;
}

bool tdma_pio_spi_ring_adapter_set_topology_probe_mode(
    tdma_pio_spi_ring_adapter_t *adapter, bool enabled)
{
    if (adapter == NULL || adapter->started != 0u) {
        return false;
    }
    adapter->topology_probe_mode = enabled ? 1u : 0u;
    return true;
}

void tdma_pio_spi_ring_adapter_clear_calibration_topology(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter != NULL && adapter->started == 0u) {
        memset(&adapter->topology, 0, sizeof(adapter->topology));
    }
}

void tdma_pio_spi_ring_adapter_set_phys(tdma_pio_spi_ring_adapter_t *adapter,
                                        tdma_pio_spi_ring_tx_fn tx,
                                        tdma_pio_spi_ring_rx_fn rx,
                                        void *phys_context)
{
    if (adapter == NULL) {
        return;
    }
    adapter->phys_tx = tx;
    adapter->phys_rx = rx;
    adapter->phys_rx_ex = NULL;
    adapter->phys_rx_event_pin = NULL;
    adapter->phys_rx_event_query = NULL;
    adapter->phys_context = phys_context;
}

void tdma_pio_spi_ring_adapter_set_phys_rx_ex(tdma_pio_spi_ring_adapter_t *adapter,
                                             tdma_pio_spi_ring_rx_ex_fn rx)
{
    if (adapter != NULL && adapter->started == 0u) adapter->phys_rx_ex = rx;
}

void tdma_pio_spi_ring_adapter_set_phys_rx_event(tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_rx_event_pin_fn pin, tdma_pio_spi_ring_rx_event_query_fn query)
{
    if (adapter == NULL || adapter->started != 0u) return;
    adapter->phys_rx_event_pin = query != NULL ? pin : NULL;
    adapter->phys_rx_event_query = pin != NULL ? query : NULL;
}

void tdma_pio_spi_ring_adapter_set_phys_feedback(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_feedback_fn feedback)
{
    if (adapter == NULL || adapter->started != 0u) {
        return;
    }
    adapter->phys_feedback = feedback;
}

void tdma_pio_spi_ring_adapter_set_phys_ctrl(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_arm_fn arm,
    tdma_pio_spi_ring_phys_disarm_fn disarm,
    tdma_pio_spi_ring_phys_train_fn train,
    tdma_pio_spi_ring_phys_train_service_fn train_service,
    void *phys_ctrl_context)
{
    if (adapter == NULL) {
        return;
    }
    adapter->phys_arm = arm;
    adapter->phys_disarm = disarm;
    adapter->phys_train = train;
    adapter->phys_train_service = train_service;
    adapter->phys_ctrl_context = phys_ctrl_context;
}

void tdma_pio_spi_ring_adapter_set_phys_error_reader(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t (*last_error)(const void *context))
{
    if (adapter != NULL) {
        adapter->phys_last_error = last_error;
    }
}

void tdma_pio_spi_ring_adapter_set_phys_geometry_lifecycle(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_arm_fn arm_request,
    void (*stopped)(void *context))
{
    if (adapter == NULL || adapter->started != 0u) return;
    adapter->phys_arm_request = arm_request;
    adapter->phys_stopped = stopped;
}

void tdma_pio_spi_ring_adapter_set_phys_tx_retryable(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_tx_retryable_fn tx_retryable)
{
    if (adapter != NULL) {
        adapter->phys_tx_retryable = tx_retryable;
    }
}

uint32_t tdma_pio_spi_ring_adapter_last_error(
    const void *context)
{
    const tdma_pio_spi_ring_adapter_t *adapter =
        (const tdma_pio_spi_ring_adapter_t *)context;
    return adapter != NULL ? adapter->last_error
                           : TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT;
}

void tdma_pio_spi_ring_adapter_set_phys_timestamp_ready(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_timestamp_ready_fn timestamp_ready)
{
    if (adapter == NULL || adapter->started != 0u) {
        return;
    }
    adapter->phys_timestamp_ready = timestamp_ready;
}

void tdma_pio_spi_ring_adapter_set_phys_tx_complete(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_tx_complete_fn tx_complete)
{
    if (adapter == NULL || adapter->started != 0u) {
        return;
    }
    adapter->phys_tx_complete = tx_complete;
}

void tdma_pio_spi_ring_adapter_set_phys_local_tx_edge(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_local_tx_edge_fn tx_edge)
{
    if (adapter == NULL || adapter->started != 0u) {
        return;
    }
    adapter->phys_local_tx_edge = tx_edge;
}

void tdma_pio_spi_ring_adapter_set_phys_local_tx_edge_ex(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_local_tx_edge_ex_fn tx_edge)
{
    if (adapter == NULL) {
        return;
    }
    adapter->phys_local_tx_edge_ex = tx_edge;
}

void tdma_pio_spi_ring_adapter_set_phys_overlay(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_overlay_fn prepare_overlay,
    tdma_pio_spi_ring_phys_overlay_boundary_fn service_overlay_boundary,
    tdma_pio_spi_ring_phys_overlay_ready_fn overlay_ready)
{
    if (adapter == NULL || adapter->started != 0u) {
        return;
    }
    adapter->phys_prepare_overlay = prepare_overlay;
    adapter->phys_service_overlay_boundary = service_overlay_boundary;
    adapter->phys_overlay_ready = overlay_ready;
}

void tdma_pio_spi_ring_adapter_set_timestamp_metadata(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t resolution_ns,
    uint32_t flags)
{
    if (adapter == NULL) {
        return;
    }
    adapter->timestamp_resolution_ns = resolution_ns;
    adapter->timestamp_flags = flags;
}

void tdma_pio_spi_ring_adapter_snapshot_write_begin(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    (void)__atomic_add_fetch(&adapter->snapshot_guard, 1u, __ATOMIC_RELEASE);
}

void tdma_pio_spi_ring_adapter_snapshot_write_end(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    (void)__atomic_add_fetch(&adapter->snapshot_guard, 1u, __ATOMIC_RELEASE);
}

void tdma_pio_spi_ring_adapter_set_flight_fifo(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_flight_fifo_t *fifo)
{
    if (adapter == NULL) {
        return;
    }
    adapter->flight_fifo = fifo;
}

void tdma_pio_spi_ring_adapter_set_first_class(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_first_class_t *first_class)
{
    if (adapter == NULL) {
        return;
    }
    adapter->first_class = first_class;
}

void tdma_pio_spi_ring_adapter_set_flight_engine(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_flight_engine_t *engine)
{
    if (adapter == NULL) {
        return;
    }
    adapter->flight_engine = engine;
}

void tdma_pio_spi_ring_adapter_set_maintenance_service(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_ring_maintenance_service_fn service,
    void *context)
{
    if (adapter == NULL) {
        return;
    }
    adapter->maintenance_service = service;
    adapter->maintenance_service_context = context;
}

void tdma_pio_spi_ring_adapter_set_maintenance_mailbox(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_ring_maintenance_mailbox_fn provider,
    void *context)
{
    if (adapter == NULL) {
        return;
    }
    adapter->maintenance_mailbox = provider;
    adapter->maintenance_mailbox_context = context;
}

bool tdma_pio_spi_ring_adapter_set_transport_load(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_transport_load_t *load)
{
    if (adapter == NULL) {
        return false;
    }
    adapter->transport_load = load;
    return true;
}

void tdma_pio_spi_ring_adapter_set_transport_load_rx(
    tdma_pio_spi_ring_adapter_t *adapter,
    void (*callback)(void *context,
                     const uint8_t *payload,
                     size_t payload_size),
    void *context)
{
    if (adapter == NULL) {
        return;
    }
    adapter->transport_load_rx = callback;
    adapter->transport_load_rx_context = context;
}

void tdma_pio_spi_ring_adapter_get_transport_load_tx(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *ok,
    uint32_t *fail,
    uint32_t *stage)
{
    if (adapter == NULL) {
        return;
    }
    if (ok != NULL) {
        *ok = adapter->transport_load_tx_ok;
    }
    if (fail != NULL) {
        *fail = adapter->transport_load_tx_fail;
    }
    if (stage != NULL) {
        *stage = adapter->transport_load_tx_stage;
    }
}

void tdma_pio_spi_ring_adapter_get_transport_load_rx(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *seen,
    uint32_t *delivered)
{
    if (adapter == NULL) {
        return;
    }
    if (seen != NULL) {
        *seen = adapter->transport_load_rx_seen;
    }
    if (delivered != NULL) {
        *delivered = adapter->transport_load_rx_delivered;
    }
}

void tdma_pio_spi_ring_adapter_get_rx_debug(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *drop_count,
    uint32_t *last_packet_size,
    uint32_t *last_payload_class,
    uint32_t *config_control_count,
    uint32_t *process_image_count)
{
    if (adapter == NULL) {
        return;
    }
    if (drop_count != NULL) {
        *drop_count = adapter->rx_drop_count;
    }
    if (last_packet_size != NULL) {
        *last_packet_size = (uint32_t)adapter->last_rx_packet_size;
    }
    if (last_payload_class != NULL) {
        *last_payload_class = 0u;
        if (adapter->last_rx_packet_size != 0u) {
            tdma_transport_frame_view_t view;
            tdma_transport_result_t result = TDMA_TRANSPORT_OK;
            if (tdma_transport_frame_decode(adapter->last_rx_packet,
                                            adapter->last_rx_packet_size,
                                            &view, &result)) {
                *last_payload_class = view.payload_class;
            }
        }
    }
    if (config_control_count != NULL) {
        *config_control_count = adapter->rx_class_config_control;
    }
    if (process_image_count != NULL) {
        *process_image_count = adapter->rx_class_process_image;
    }
}

void tdma_pio_spi_ring_adapter_get_transport_load_build(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *payload_class,
    uint32_t *payload_size,
    uint32_t *load_frame)
{
    if (adapter == NULL) {
        return;
    }
    if (payload_class != NULL) {
        *payload_class = adapter->transport_load_build_class;
    }
    if (payload_size != NULL) {
        *payload_size = adapter->transport_load_build_size;
    }
    if (load_frame != NULL) {
        *load_frame = adapter->transport_load_build_load;
    }
}

void tdma_pio_spi_ring_adapter_get_maintenance_inject(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t *attempts,
    uint32_t *ok)
{
    if (adapter == NULL) {
        return;
    }
    if (attempts != NULL) {
        *attempts = adapter->maintenance_inject_attempts;
    }
    if (ok != NULL) {
        *ok = adapter->maintenance_inject_ok;
    }
}

void tdma_pio_spi_ring_adapter_get_maintenance_diag(
    const tdma_pio_spi_ring_adapter_t *adapter,
    tdma_maintenance_adapter_diag_t *diag)
{
    if (adapter == NULL || diag == NULL) {
        return;
    }
    memset(diag, 0, sizeof(*diag));
    diag->inject_attempts = adapter->maintenance_inject_attempts;
    diag->inject_ok = adapter->maintenance_inject_ok;
    diag->inject_launched = adapter->maintenance_inject_launched;
    diag->inject_dropped_unlaunched =
        adapter->maintenance_inject_dropped_unlaunched;
    diag->inject_new_sequence = adapter->maintenance_inject_new_sequence;
    diag->inject_repeated_sequence =
        adapter->maintenance_inject_repeated_sequence;
    diag->inject_last_sequence = adapter->maintenance_inject_last_sequence;
    diag->beacon_frames = adapter->maintenance_beacon_frames;
    diag->rx_polls = adapter->maintenance_rx_polls;
    diag->rx_seen_chunks = adapter->maintenance_rx_seen_chunks;
    diag->rx_accepted_chunks = adapter->maintenance_rx_accepted_chunks;
    diag->rx_dedup_sequence = adapter->maintenance_rx_dedup_sequence;
    diag->rx_bad_mailbox = adapter->maintenance_rx_bad_mailbox;
    diag->rx_last_sequence = adapter->maintenance_rx_last_sequence;
    diag->rx_last_poll_sequence =
        adapter->maintenance_rx_last_poll_sequence;
    diag->rx_sequence_cursor = adapter->maintenance_rx_sequence;
}

bool tdma_pio_spi_ring_adapter_set_forwarding_mode(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_forwarding_mode_t mode)
{
    if (adapter == NULL || adapter->started != 0u ||
        mode > TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE) {
        return false;
    }
    adapter->forwarding_mode = mode;
    return true;
}

bool tdma_pio_spi_ring_adapter_set_clock_evidence_enabled(
    tdma_pio_spi_ring_adapter_t *adapter, bool enabled)
{
    if (adapter == NULL || adapter->started != 0u) {
        return false;
    }
    adapter->clock_evidence_enabled = enabled ? 1u : 0u;
    return true;
}

bool tdma_pio_spi_ring_adapter_set_seat_rotation(
    tdma_pio_spi_ring_adapter_t *adapter, uint32_t priority_share,
    uint32_t first_class_share)
{
    /* Two-share entry point: no slots reserved for the ordinary path, i.e. exactly the
     * behaviour before the three-share form existed. */
    return tdma_pio_spi_ring_adapter_set_seat_rotation3(adapter, priority_share,
                                                        first_class_share, 0u);
}

