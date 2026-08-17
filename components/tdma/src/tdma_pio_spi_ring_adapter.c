#include "tdma_pio_spi_ring_adapter.h"

#include <string.h>

#define TDMA_PIO_SPI_RING_ADAPTER_BEACON_HOP_LIMIT 1u

static void tdma_pio_spi_ring_adapter_set_error(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t error)
{
    if (adapter != NULL) {
        adapter->last_error = error;
    }
}

static bool tdma_pio_spi_ring_adapter_queue_push(
    tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t timestamp_ns)
{
    if (adapter == NULL || packet == NULL || packet_size == 0u ||
        packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX) {
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

static bool tdma_pio_spi_ring_adapter_queue_pop(
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
    return true;
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
    adapter->phys_context = phys_context;
}

void tdma_pio_spi_ring_adapter_set_phys_ctrl(
    tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_phys_arm_fn arm,
    tdma_pio_spi_ring_phys_disarm_fn disarm,
    void *phys_ctrl_context)
{
    if (adapter == NULL) {
        return;
    }
    adapter->phys_arm = arm;
    adapter->phys_disarm = disarm;
    adapter->phys_ctrl_context = phys_ctrl_context;
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

bool tdma_pio_spi_ring_adapter_inject_rx(tdma_pio_spi_ring_adapter_t *adapter,
                                         const uint8_t *packet,
                                         size_t packet_size,
                                         uint64_t rx_timestamp_ns)
{
    return tdma_pio_spi_ring_adapter_queue_push(adapter,
                                                packet,
                                                packet_size,
                                                rx_timestamp_ns);
}

static bool tdma_pio_spi_ring_adapter_start(
    void *context,
    const tdma_ring_runtime_config_t *config)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL || config == NULL || config->enabled == 0u ||
        config->node_count < 2u ||
        config->local_slot_id >= config->node_count ||
        config->up_group_id == 0u || config->down_group_id == 0u ||
        config->up_group_id == config->down_group_id ||
        config->ring_profile_crc32 == 0u ||
        config->schedule_crc32 == 0u ||
        config->feedback_timeout_ns == 0u) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT);
        return false;
    }
    adapter->config = *config;
    adapter->configured = true;
    adapter->role = (config->local_slot_id == config->reference_slot_id)
                        ? TDMA_PIO_SPI_RING_ROLE_REFERENCE
                        : TDMA_PIO_SPI_RING_ROLE_FORWARD;
    if (adapter->phys_arm != NULL &&
        !adapter->phys_arm(adapter->phys_ctrl_context, config)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_MISSING);
        return false;
    }
    adapter->started = 1u;
    adapter->forward_count = 0u;
    adapter->up_sequence = 0u;
    adapter->down_rx_sequence = 0u;
    adapter->up_tx_frame_crc32 = 0u;
    adapter->down_rx_frame_crc32 = 0u;
    adapter->reference_tx_timestamp_ns = 0ull;
    adapter->feedback_rx_timestamp_ns = 0ull;
    adapter->last_rx_packet_size = 0u;
    adapter->last_tx_ns = 0ull;
    adapter->last_error = TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    return true;
}

static void tdma_pio_spi_ring_adapter_stop(void *context)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL) {
        return;
    }
    if (adapter->started != 0u && adapter->phys_disarm != NULL) {
        adapter->phys_disarm(adapter->phys_ctrl_context);
    }
    adapter->started = 0u;
    adapter->up_sequence = 0u;
    adapter->down_rx_sequence = 0u;
    adapter->up_tx_frame_crc32 = 0u;
    adapter->down_rx_frame_crc32 = 0u;
    adapter->reference_tx_timestamp_ns = 0ull;
    adapter->feedback_rx_timestamp_ns = 0ull;
    adapter->last_rx_packet_size = 0u;
    adapter->rx_queue_head = 0u;
    adapter->rx_queue_count = 0u;
}

static bool tdma_pio_spi_ring_adapter_tx_beacon(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    const uint32_t sequence = adapter->up_sequence + 1u;
    const tdma_transport_frame_build_t build = {
        .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
        .origin_slot_id = adapter->config.local_slot_id,
        .transport_sequence = sequence,
        .payload_class = TDMA_PAYLOAD_CLASS_IDLE_BEACON,
        .flags = TDMA_TRANSPORT_FLAG_IDLE_BEACON,
        .schedule_crc32 = adapter->config.schedule_crc32,
        .ring_profile_crc32 = adapter->config.ring_profile_crc32,
        .hop_limit = TDMA_PIO_SPI_RING_ADAPTER_BEACON_HOP_LIMIT,
        .payload = NULL,
        .payload_size = 0u,
    };
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t packet_size = 0u;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_encode(&build,
                                     packet,
                                     sizeof(packet),
                                     &packet_size,
                                     &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    uint64_t tx_timestamp_ns = 0ull;
    if (!adapter->phys_tx(adapter->phys_context,
                          packet,
                          packet_size,
                          &tx_timestamp_ns)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    adapter->up_sequence = sequence;
    adapter->tx_count++;
    adapter->idle_beacon_tx_count++;
    /* Only a physical-layer timestamp may serve as reference TX evidence.
     * A zero timestamp keeps the correlation gate closed (TIMESTAMP_MISSING). */
    adapter->reference_tx_timestamp_ns = tx_timestamp_ns;

    tdma_transport_frame_view_t view;
    if (tdma_transport_frame_decode(packet,
                                    packet_size,
                                    &view,
                                    &result)) {
        adapter->up_tx_frame_crc32 = view.identity_crc32;
    }
    return true;
}

static bool tdma_pio_spi_ring_adapter_process_rx(
    tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t *packet,
    size_t packet_size,
    uint64_t rx_timestamp_ns)
{
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_decode(packet,
                                     packet_size,
                                     &view,
                                     &result) ||
        view.schedule_crc32 != adapter->config.schedule_crc32 ||
        view.ring_profile_crc32 != adapter->config.ring_profile_crc32) {
        adapter->rx_bad_count++;
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME);
        return false;
    }

    adapter->down_rx_sequence = view.transport_sequence;
    adapter->down_rx_frame_crc32 = view.identity_crc32;
    adapter->feedback_rx_timestamp_ns = rx_timestamp_ns;
    adapter->rx_count++;
    if ((view.flags & TDMA_TRANSPORT_FLAG_IDLE_BEACON) != 0u) {
        adapter->idle_beacon_rx_count++;
    }
    if (packet_size <= sizeof(adapter->last_rx_packet)) {
        memcpy(adapter->last_rx_packet, packet, packet_size);
        adapter->last_rx_packet_size = packet_size;
    }
    adapter->last_error = TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    return true;
}

/* FORWARD-node emission: re-emit the frame received from the previous board
 * toward the next board, keeping origin/sequence/identity CRC unchanged and
 * advancing hop/transport CRC. The feedback returns only to the reference
 * node, so this path proves ring service (up/down running) without
 * fabricating simultaneous_feedback_loop_evidence. */
static bool tdma_pio_spi_ring_adapter_tx_forward(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter->last_rx_packet_size == 0u) {
        return false;
    }
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    memcpy(packet, adapter->last_rx_packet, adapter->last_rx_packet_size);
    const size_t packet_size = adapter->last_rx_packet_size;

    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_advance_hop(packet, packet_size, &result)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    uint64_t tx_timestamp_ns = 0ull;
    if (!adapter->phys_tx(adapter->phys_context,
                          packet,
                          packet_size,
                          &tx_timestamp_ns)) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_TX_FAILED);
        return false;
    }

    tdma_transport_frame_view_t view;
    if (tdma_transport_frame_decode(packet,
                                    packet_size,
                                    &view,
                                    &result)) {
        adapter->up_sequence = view.transport_sequence;
        adapter->up_tx_frame_crc32 = view.identity_crc32;
        adapter->reference_tx_timestamp_ns = tx_timestamp_ns;
    }
    adapter->forward_count++;
    adapter->tx_count++;
    adapter->last_error = TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    return true;
}

static bool tdma_pio_spi_ring_adapter_rx_once(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t packet_size = 0u;
    uint64_t rx_timestamp_ns = 0ull;

    if (adapter->rx_queue_count != 0u) {
        if (!tdma_pio_spi_ring_adapter_queue_pop(adapter,
                                                 packet,
                                                 sizeof(packet),
                                                 &packet_size,
                                                 &rx_timestamp_ns)) {
            return false;
        }
        return tdma_pio_spi_ring_adapter_process_rx(adapter,
                                                    packet,
                                                    packet_size,
                                                    rx_timestamp_ns);
    }

    if (adapter->phys_rx == NULL) {
        return false;
    }
    if (!adapter->phys_rx(adapter->phys_context,
                          packet,
                          sizeof(packet),
                          &packet_size,
                          &rx_timestamp_ns)) {
        return false;
    }
    return tdma_pio_spi_ring_adapter_process_rx(adapter,
                                                packet,
                                                packet_size,
                                                rx_timestamp_ns);
}

static bool tdma_pio_spi_ring_adapter_service(
    void *context,
    uint64_t now_ns,
    tdma_ring_adapter_status_t *status)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL || status == NULL) {
        return false;
    }
    (void)now_ns;
    memset(status, 0, sizeof(*status));
    adapter->service_count++;

    if (adapter->started == 0u || !adapter->configured) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT);
        return false;
    }

    status->up_configured = 1u;
    status->down_configured = 1u;
    status->schedule_crc32 = adapter->config.schedule_crc32;

    /* Bring-up honesty gate: without a physical TX path the adapter has no
     * UP leg. Returning false makes the ring runtime report EVIDENCE_MISSING
     * instead of a fabricated running state. RX may come from phys_rx or from
     * the injected test queue; with neither, down_running stays 0. */
    if (adapter->phys_tx == NULL) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_MISSING);
        return false;
    }

    bool tx_ok = false;
    bool rx_ok = false;
    if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE) {
        /* Reference node is the ring origin: it emits one IDLE_BEACON per
         * TDMA cycle on the downlink TX leg and receives the same frame back
         * (hop advanced, identity preserved) on the uplink RX leg after it
         * has travelled once around the ring. Only this node may produce
         * simultaneous_feedback_loop_evidence. */
        const uint64_t cycle_ns =
            adapter->config.feedback_timeout_ns != 0u
                ? adapter->config.feedback_timeout_ns
                : 1000000ull;
        /* TSN-style deterministic emission: one IDLE_BEACON per TDMA cycle at
         * a fixed phase. The previous now-last >= interval throttle skipped
         * ~40% of the ticks because sleep_until can wake slightly early, so
         * the reference emitted at ~580 Hz instead of 1 kHz. A fixed-phase
         * emit keeps the wire schedule deterministic (the cycle guard below
         * still bounds the emission rate by the physical TX time). */
        (void)cycle_ns;
        const bool tx_due = true;
        if (tx_due) {
            tx_ok = tdma_pio_spi_ring_adapter_tx_beacon(adapter);
            if (tx_ok) {
                adapter->last_tx_ns = now_ns;
            }
        } else {
            tx_ok = true; /* throttled round: keep the UP leg running. */
        }
        rx_ok = tdma_pio_spi_ring_adapter_rx_once(adapter);
    } else {
        /* Follower node (half-duplex ring): receives the frame from the
         * previous board on the uplink RX leg and re-emits it toward the next
         * board on the downlink TX leg, keeping origin/sequence/identity CRC
         * unchanged and advancing hop/transport CRC. When nothing arrived
         * this round there is nothing to forward and nothing is emitted: a
         * foreign placeholder beacon would race the reference frame around
         * the ring and corrupt the reference's feedback correlation. The TX
         * leg stays ready (up_running=1). */
        rx_ok = tdma_pio_spi_ring_adapter_rx_once(adapter);
        if (rx_ok) {
            tx_ok = tdma_pio_spi_ring_adapter_tx_forward(adapter);
        } else {
            tx_ok = true; /* idle round: nothing received, nothing to emit. */
        }
    }
    if (!tx_ok) {
        return false;
    }

    status->up_running = 1u;
    status->down_running = rx_ok ? 1u : 0u;
    status->up_tx_sequence = adapter->up_sequence;
    status->down_rx_sequence = adapter->down_rx_sequence;
    status->up_tx_frame_crc32 = adapter->up_tx_frame_crc32;
    status->down_rx_frame_crc32 = adapter->down_rx_frame_crc32;
    status->timestamp_resolution_ns = adapter->timestamp_resolution_ns;
    status->timestamp_flags = adapter->timestamp_flags;
    status->idle_beacon_tx_count = adapter->idle_beacon_tx_count;
    status->idle_beacon_rx_count = adapter->idle_beacon_rx_count;
    status->last_error = adapter->last_error;
    status->tx_count = adapter->tx_count;
    status->rx_count = adapter->rx_count;
    status->rx_bad_count = adapter->rx_bad_count;
    status->reference_tx_timestamp_ns = adapter->reference_tx_timestamp_ns;
    status->feedback_rx_timestamp_ns = adapter->feedback_rx_timestamp_ns;
    return true;
}

static const tdma_ring_adapter_ops_t s_tdma_pio_spi_ring_adapter_ops = {
    .start = tdma_pio_spi_ring_adapter_start,
    .stop = tdma_pio_spi_ring_adapter_stop,
    .service = tdma_pio_spi_ring_adapter_service,
};

const tdma_ring_adapter_ops_t *tdma_pio_spi_ring_adapter_ops(void)
{
    return &s_tdma_pio_spi_ring_adapter_ops;
}

bool tdma_pio_spi_ring_adapter_get_snapshot(
    const tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_adapter_snapshot_t *snapshot)
{
    if (adapter == NULL || snapshot == NULL) {
        return false;
    }
    snapshot->version = TDMA_PIO_SPI_RING_ADAPTER_VERSION;
    snapshot->started = adapter->started;
    snapshot->service_count = adapter->service_count;
    snapshot->role = (uint32_t)adapter->role;
    snapshot->forward_count = adapter->forward_count;
    snapshot->up_sequence = adapter->up_sequence;
    snapshot->down_rx_sequence = adapter->down_rx_sequence;
    snapshot->up_tx_frame_crc32 = adapter->up_tx_frame_crc32;
    snapshot->down_rx_frame_crc32 = adapter->down_rx_frame_crc32;
    snapshot->idle_beacon_tx_count = adapter->idle_beacon_tx_count;
    snapshot->idle_beacon_rx_count = adapter->idle_beacon_rx_count;
    snapshot->tx_count = adapter->tx_count;
    snapshot->rx_count = adapter->rx_count;
    snapshot->rx_bad_count = adapter->rx_bad_count;
    snapshot->rx_drop_count = adapter->rx_drop_count;
    snapshot->timestamp_resolution_ns = adapter->timestamp_resolution_ns;
    snapshot->timestamp_flags = adapter->timestamp_flags;
    snapshot->reference_tx_timestamp_ns = adapter->reference_tx_timestamp_ns;
    snapshot->feedback_rx_timestamp_ns = adapter->feedback_rx_timestamp_ns;
    snapshot->last_error = adapter->last_error;
    snapshot->local_slot_id = adapter->config.local_slot_id;
    snapshot->schedule_crc32 = adapter->config.schedule_crc32;
    snapshot->ring_profile_crc32 = adapter->config.ring_profile_crc32;
    snapshot->feedback_timeout_ns = adapter->config.feedback_timeout_ns;
    return true;
}
