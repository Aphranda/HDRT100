#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_process_image_layout.h"
#include "tdma_ring_runtime.h"
#include "tdma_transport_frame.h"

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: got %u expected %u\n",
            name, actual ? 1u : 0u, expected ? 1u : 0u);
    return 1;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: got %lu expected %lu\n",
            name, (unsigned long)actual, (unsigned long)expected);
    return 1;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "FAIL %s: got %llu expected %llu\n",
            name, (unsigned long long)actual, (unsigned long long)expected);
    return 1;
}

/* Full-duplex loopback physical stub: every TX is echoed back as RX with a
 * configurable RX timestamp. This models the bring-up master/slave SPI
 * exchange where the remote returns the same transport identity. */
typedef struct {
    uint8_t last_tx[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t last_tx_size;
    uint64_t tx_timestamp_ns;
    uint64_t rx_timestamp_ns;
    uint32_t tx_calls;
    uint32_t rx_calls;
    bool rx_pending;
    bool suppress_echo;
} loopback_phys_t;

/* One-frame flight stub: TX N makes TX N-1 available to RX.  This models the
 * hardware ring observed on four boards, where the reference has already
 * launched the next process frame before the prior sequence returns. */
typedef struct {
    uint8_t latest_tx[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t latest_tx_size;
    uint64_t latest_tx_timestamp_ns;
    uint8_t pending_rx[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    size_t pending_rx_size;
    uint64_t pending_rx_timestamp_ns;
    uint32_t tx_calls;
    bool rx_pending;
} flight_phys_t;

typedef struct {
    uint32_t tx_calls;
    uint32_t busy_tx_calls;
    bool tx_in_flight;
    bool completion_ready;
    uint64_t completion_timestamp_ns;
} async_phys_t;

static bool flight_tx(void *context,
                      const uint8_t *packet,
                      size_t packet_size,
                      uint64_t *tx_timestamp_ns)
{
    flight_phys_t *phys = (flight_phys_t *)context;
    if (phys == NULL || packet == NULL || packet_size == 0u ||
        packet_size > sizeof(phys->latest_tx)) {
        return false;
    }
    if (phys->latest_tx_size != 0u) {
        memcpy(phys->pending_rx, phys->latest_tx, phys->latest_tx_size);
        phys->pending_rx_size = phys->latest_tx_size;
        phys->pending_rx_timestamp_ns = phys->latest_tx_timestamp_ns + 500ull;
        phys->rx_pending = true;
    }
    phys->latest_tx_timestamp_ns = 1000000ull +
        (uint64_t)phys->tx_calls * 2000ull;
    memcpy(phys->latest_tx, packet, packet_size);
    phys->latest_tx_size = packet_size;
    phys->tx_calls++;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = phys->latest_tx_timestamp_ns;
    }
    return true;
}

static bool async_tx(void *context,
                     const uint8_t *packet,
                     size_t packet_size,
                     uint64_t *tx_timestamp_ns)
{
    async_phys_t *phys = (async_phys_t *)context;
    if (phys == NULL || packet == NULL || packet_size == 0u) {
        return false;
    }
    if (phys->tx_in_flight) {
        phys->busy_tx_calls++;
        return false;
    }
    phys->tx_calls++;
    phys->tx_in_flight = true;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = 0ull;
    }
    return true;
}

static bool async_tx_complete(void *context, uint64_t *tx_timestamp_ns)
{
    async_phys_t *phys = (async_phys_t *)context;
    if (phys == NULL || !phys->tx_in_flight || !phys->completion_ready) {
        return false;
    }
    phys->tx_in_flight = false;
    phys->completion_ready = false;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = phys->completion_timestamp_ns;
    }
    return true;
}

static void test_put_u32_le(uint8_t *dst, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        dst[i] = (uint8_t)(value >> (i * 8u));
    }
}

static bool flight_rx(void *context,
                      uint8_t *packet,
                      size_t packet_capacity,
                      size_t *packet_size,
                      uint64_t *rx_timestamp_ns)
{
    flight_phys_t *phys = (flight_phys_t *)context;
    if (phys == NULL || packet == NULL || packet_size == NULL ||
        rx_timestamp_ns == NULL || !phys->rx_pending ||
        phys->pending_rx_size == 0u ||
        phys->pending_rx_size > packet_capacity) {
        return false;
    }
    memcpy(packet, phys->pending_rx, phys->pending_rx_size);
    *packet_size = phys->pending_rx_size;
    *rx_timestamp_ns = phys->pending_rx_timestamp_ns;
    phys->rx_pending = false;
    return true;
}

static bool loopback_tx(void *context,
                        const uint8_t *packet,
                        size_t packet_size,
                        uint64_t *tx_timestamp_ns)
{
    loopback_phys_t *phys = (loopback_phys_t *)context;
    if (phys == NULL || packet == NULL || packet_size == 0u ||
        packet_size > sizeof(phys->last_tx)) {
        return false;
    }
    memcpy(phys->last_tx, packet, packet_size);
    phys->last_tx_size = packet_size;
    if (!phys->suppress_echo) {
        phys->rx_pending = true;
    }
    phys->tx_calls++;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = phys->tx_timestamp_ns;
    }
    return true;
}

static bool loopback_rx(void *context,
                        uint8_t *packet,
                        size_t packet_capacity,
                        size_t *packet_size,
                        uint64_t *rx_timestamp_ns)
{
    loopback_phys_t *phys = (loopback_phys_t *)context;
    if (phys == NULL || packet == NULL || packet_size == NULL ||
        rx_timestamp_ns == NULL || !phys->rx_pending ||
        phys->last_tx_size == 0u ||
        phys->last_tx_size > packet_capacity) {
        return false;
    }
    memcpy(packet, phys->last_tx, phys->last_tx_size);
    *packet_size = phys->last_tx_size;
    *rx_timestamp_ns = phys->rx_timestamp_ns;
    phys->rx_pending = false;
    phys->rx_calls++;
    return true;
}

typedef struct {
    uint32_t *arm_calls;
    uint32_t *disarm_calls;
    uint32_t *arm_schedule_crc;
    uint32_t *arm_result;
    bool timestamp_ready;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t *timestamp_ready_calls;
} phys_ctrl_stub_t;

static bool phys_ctrl_stub_arm(void *context,
                               const tdma_ring_runtime_config_t *config)
{
    phys_ctrl_stub_t *ctrl = (phys_ctrl_stub_t *)context;
    if (ctrl == NULL || ctrl->arm_calls == NULL || config == NULL) {
        return false;
    }
    (*ctrl->arm_calls)++;
    if (ctrl->arm_schedule_crc != NULL) {
        *ctrl->arm_schedule_crc = config->schedule_crc32;
    }
    return ctrl->arm_result == NULL ? true : *ctrl->arm_result;
}

static void phys_ctrl_stub_disarm(void *context)
{
    phys_ctrl_stub_t *ctrl = (phys_ctrl_stub_t *)context;
    if (ctrl != NULL && ctrl->disarm_calls != NULL) {
        (*ctrl->disarm_calls)++;
    }
}

static uint32_t test_get_u32_le(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8u) |
           ((uint32_t)src[2] << 16u) |
           ((uint32_t)src[3] << 24u);
}

static bool phys_timestamp_stub_ready(void *context,
                                      uint32_t *resolution_ns,
                                      uint32_t *flags)
{
    phys_ctrl_stub_t *stub = (phys_ctrl_stub_t *)context;
    if (stub == NULL || resolution_ns == NULL || flags == NULL) {
        return false;
    }
    if (stub->timestamp_ready_calls != NULL) {
        (*stub->timestamp_ready_calls)++;
    }
    *resolution_ns = stub->timestamp_resolution_ns;
    *flags = stub->timestamp_flags;
    return stub->timestamp_ready;
}

static tdma_ring_runtime_config_t make_valid_config(void)
{
    tdma_ring_runtime_config_t config = {
        .enabled = 1u,
        .node_count = 2u,
        .local_slot_id = 0u,
        .reference_slot_id = 0u,
        .up_group_id = 1u,
        .down_group_id = 2u,
        .flags = TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN,
        .ring_profile_crc32 = 0x11223344u,
        .schedule_crc32 = 0x55667788u,
        .operating_profile_crc32 = 0x99AABBCCu,
        .baud_hz = 10000000u,
        .cycle_period_ns = 1000u,
        /* Two nodes at a 1 us slot period form a 2 us ring round. With the
         * host runtime advancing now_ns by 1 us per service round, the
         * reference node sends every second round. */
        .feedback_timeout_ns = 2000u,
        .tx_dma_channel_id = TDMA_PROFILE_DEFAULT_TX_DMA_CHANNEL_ID,
        .rx_dma_channel_id = TDMA_PROFILE_DEFAULT_RX_DMA_CHANNEL_ID,
    };
    return config;
}

static void set_test_sequential_topology(
    tdma_pio_spi_ring_adapter_t *adapter, uint32_t node_count)
{
    memset(&adapter->topology, 0, sizeof(adapter->topology));
    adapter->topology.valid = 1u;
    adapter->topology.node_count = node_count;
    adapter->topology.topology_generation = 1u;
    adapter->topology.topology_crc32 = 1u;
    for (uint32_t node = 0u; node < node_count; node++) {
        adapter->topology.marker_next_node[node] =
            (node + 1u) % node_count;
        adapter->topology.data_next_node[node] =
            (node + node_count - 1u) % node_count;
    }
}

static bool start_ring_data(tdma_ring_runtime_t *runtime)
{
    if (runtime == NULL) {
        return false;
    }
    tdma_ring_runtime_service(runtime);
    return tdma_ring_runtime_set_data_enabled(runtime, true);
}

static tdma_process_image_map_t make_flight_map(void)
{
    tdma_process_image_map_t map = {
        .version = TDMA_PROCESS_IMAGE_MAP_VERSION,
        .payload_size = 64u,
        .segment_count = 2u,
        .segment = {
            {
                .used = 1u,
                .segment_id = 0u,
                .owner_slot_id = 0u,
                .payload_class = 1u,
                .byte_offset = 0u,
                .byte_length = 32u,
                .flags = TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE,
            },
            {
                .used = 1u,
                .segment_id = 1u,
                .owner_slot_id = 1u,
                .payload_class = 1u,
                .byte_offset = 32u,
                .byte_length = 32u,
                .flags = TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE,
            },
        },
    };
    map.map_crc32 = tdma_process_image_map_crc32(&map);
    return map;
}

static tdma_process_image_map_t make_eight_slot_flight_map(void)
{
    tdma_process_image_map_t map;
    memset(&map, 0, sizeof(map));
    map.version = TDMA_PROCESS_IMAGE_MAP_VERSION;
    map.payload_size = TDMA_FLIGHT_SHORT_PAYLOAD_SIZE;
    map.segment_count = TDMA_FLIGHT_SHORT_SLOT_COUNT;
    for (uint32_t slot = 0u; slot < TDMA_FLIGHT_SHORT_SLOT_COUNT; slot++) {
        map.segment[slot].used = 1u;
        map.segment[slot].segment_id = slot;
        map.segment[slot].owner_slot_id = slot;
        map.segment[slot].payload_class = 1u;
        map.segment[slot].byte_offset = slot * TDMA_FLIGHT_SHORT_SLOT_SIZE;
        map.segment[slot].byte_length = TDMA_FLIGHT_SHORT_SLOT_SIZE;
        map.segment[slot].flags = TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE;
    }
    map.map_crc32 = tdma_process_image_map_crc32(&map);
    return map;
}

int main(void)
{
    int failed = 0;

    /* --- Adapter without physical TX: honest EVIDENCE_MISSING. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t snapshot;
        tdma_pio_spi_ring_adapter_t adapter;
        const tdma_ring_runtime_config_t config = make_valid_config();

        failed += expect_bool("init runtime",
                              tdma_ring_runtime_init(&runtime), true);
        failed += expect_bool("configure ring",
                              tdma_ring_runtime_configure(&runtime, &config),
                              true);

        /* No adapter bound yet. */
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("adapter missing before bind",
                             snapshot.last_reason,
                             TDMA_RING_RUNTIME_REASON_ADAPTER_MISSING);

        /* Bind the real PIO SPI ring adapter without a physical TX path. */
        failed += expect_bool("init adapter",
                              tdma_pio_spi_ring_adapter_init(&adapter),
                              true);
        failed += expect_bool("bind adapter",
                              tdma_ring_runtime_bind_adapter(
                                  &runtime,
                                  tdma_pio_spi_ring_adapter_ops(),
                                  &adapter),
                              true);
        failed += expect_bool("start data", start_ring_data(&runtime), true);
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("adapter started", snapshot.adapter_started, 1u);
        failed += expect_u32("adapter start count", snapshot.adapter_start_count, 1u);
        failed += expect_u32("adapter service count", snapshot.adapter_service_count, 1u);
        failed += expect_u32("ADAPTER_MISSING eliminated",
                             snapshot.last_reason,
                             TDMA_RING_RUNTIME_REASON_EVIDENCE_MISSING);
        failed += expect_u32("up not running without phys",
                             snapshot.up_running, 0u);
        failed += expect_u32("down not running without phys",
                             snapshot.down_running, 0u);
        failed += expect_u32("no evidence without phys",
                             snapshot.simultaneous_feedback_loop_evidence, 0u);

        /* Service again: counters grow. */
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("adapter service count grows",
                             snapshot.adapter_service_count, 2u);
    }

    /* --- Loopback physical path: idle beacons flow and feedback correlates. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t snapshot;
        tdma_pio_spi_ring_adapter_t adapter;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();

        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 1000000ull;
        phys.rx_timestamp_ns = 1000500ull;

        failed += expect_bool("init runtime 2",
                              tdma_ring_runtime_init(&runtime), true);
        failed += expect_bool("configure ring 2",
                              tdma_ring_runtime_configure(&runtime, &config),
                              true);
        failed += expect_bool("init adapter 2",
                              tdma_pio_spi_ring_adapter_init(&adapter),
                              true);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        /* Hardware-latched timestamp metadata, resolution <= 100 ns. */
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter,
            100u,
            TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        failed += expect_bool("bind adapter 2",
                              tdma_ring_runtime_bind_adapter(
                                  &runtime,
                                  tdma_pio_spi_ring_adapter_ops(),
                                  &adapter),
                              true);
        failed += expect_bool("start data 2", start_ring_data(&runtime), true);

        /* First service is the deterministic 500 Hz throttle margin: the
         * core1 service path runs at about 1 kHz, so the reference emits on
         * every second service round. */
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("pre-beacon up running", snapshot.up_running, 1u);
        failed += expect_u32("pre-beacon down idle", snapshot.down_running, 0u);
        failed += expect_u32("pre-beacon tx count",
                             snapshot.idle_beacon_tx_count, 0u);
        failed += expect_u32("pre-beacon rx count",
                             snapshot.idle_beacon_rx_count, 0u);

        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("up running loopback", snapshot.up_running, 1u);
        failed += expect_u32("down running loopback", snapshot.down_running, 1u);
        failed += expect_u32("feedback loop evidence",
                             snapshot.simultaneous_feedback_loop_evidence, 1u);
        failed += expect_u32("feedback round trip",
                             snapshot.feedback_round_trip_ns, 500u);
        failed += expect_u32("idle beacon tx", snapshot.idle_beacon_tx_count, 1u);
        failed += expect_u32("idle beacon rx", snapshot.idle_beacon_rx_count, 1u);
        failed += expect_u32("ring seq advances", snapshot.ring_seq, 1u);
        failed += expect_u32("no ring fault",
                             snapshot.last_reason,
                             TDMA_RING_RUNTIME_REASON_NONE);
        failed += expect_u32("up tx sequence", snapshot.up_tx_sequence, 1u);
        failed += expect_u32("down rx sequence", snapshot.down_rx_sequence, 1u);
        failed += expect_bool("identity CRC matches",
                              snapshot.up_tx_frame_crc32 ==
                                  snapshot.down_rx_frame_crc32,
                              true);
        failed += expect_bool("identity CRC nonzero",
                              snapshot.up_tx_frame_crc32 != 0u,
                              true);
        failed += expect_u32("hardware timestamp resolution",
                             snapshot.timestamp_resolution_ns, 100u);
        failed += expect_u32("hardware latch flag",
                             snapshot.timestamp_flags,
                             TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        failed += expect_u64("reference tx timestamp",
                             snapshot.reference_tx_timestamp_ns,
                             1000000ull);
        failed += expect_u64("feedback rx timestamp",
                             snapshot.feedback_rx_timestamp_ns,
                             1000500ull);

        /* The next service round is the deterministic 500 Hz throttle
         * margin: UP stays ready and DOWN stays fresh, but no new feedback
         * sequence is produced for closed-loop evidence. */
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("throttled up running", snapshot.up_running, 1u);
        failed += expect_u32("throttled down fresh", snapshot.down_running, 1u);
        failed += expect_u32("throttled beacon tx unchanged",
                             snapshot.idle_beacon_tx_count, 1u);
        failed += expect_u32("throttled beacon rx unchanged",
                             snapshot.idle_beacon_rx_count, 1u);
        failed += expect_u32("throttled feedback closed",
                             snapshot.simultaneous_feedback_loop_evidence, 1u);
        failed += expect_u32("throttled feedback RTT remains readable",
                             snapshot.feedback_round_trip_ns, 500u);

        /* The following service round emits the next beacon and feedback
         * correlates. */
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("beacon tx advances",
                             snapshot.idle_beacon_tx_count, 2u);
        failed += expect_u32("beacon rx advances",
                             snapshot.idle_beacon_rx_count, 2u);
        failed += expect_u32("up sequence advances",
                             snapshot.up_tx_sequence, 2u);
        failed += expect_u32("feedback still correlated",
                             snapshot.simultaneous_feedback_loop_evidence, 1u);
    }

    /* A late RTOS service tick must not restart the emission phase. With a
     * 2 ms ring-round period and 1.4 ms service spacing, an elapsed-from-last
     * algorithm emits every 2.8 ms. Absolute deadlines alternate one/two
     * ticks and retain the configured average cadence. */
    {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_ring_adapter_status_t status;
        loopback_phys_t phys;
        tdma_ring_runtime_config_t config = make_valid_config();
        static const uint64_t service_times_ns[] = {
            1400000ull,
            2800000ull,
            4200000ull,
            5600000ull,
            7000000ull,
            8400000ull,
        };
        config.cycle_period_ns = 1000000u;
        config.feedback_timeout_ns = 2000000u;
        memset(&phys, 0, sizeof(phys));
        memset(&status, 0, sizeof(status));
        phys.tx_timestamp_ns = 1000ull;
        phys.rx_timestamp_ns = 1500ull;
        failed += expect_bool("phase adapter init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        failed += expect_bool("phase adapter start",
                              tdma_pio_spi_ring_adapter_ops()->start(
                                  &adapter, &config),
                              true);
        for (uint32_t i = 0u;
             i < (uint32_t)(sizeof(service_times_ns) /
                            sizeof(service_times_ns[0]));
             i++) {
            failed += expect_bool("phase adapter service",
                                  tdma_pio_spi_ring_adapter_ops()->service(
                                      &adapter,
                                      service_times_ns[i],
                                      &status),
                                  true);
        }
        failed += expect_u32("absolute phase beacon count",
                             adapter.idle_beacon_tx_count,
                             3u);
        adapter.rx_count = 7u;
        adapter.rx_bad_count = 2u;
        adapter.rx_drop_count = 1u;
        adapter.rx_queue_head = 1u;
        adapter.rx_queue_count = 1u;
        tdma_pio_spi_ring_adapter_ops()->stop(&adapter);
        failed += expect_bool("phase adapter restart",
                              tdma_pio_spi_ring_adapter_ops()->start(
                                  &adapter, &config),
                              true);
        failed += expect_u32("restart clears beacon count",
                             adapter.idle_beacon_tx_count,
                             0u);
        failed += expect_u32("restart clears tx count", adapter.tx_count, 0u);
        failed += expect_u32("restart clears rx count", adapter.rx_count, 0u);
        failed += expect_u32("restart clears bad count",
                             adapter.rx_bad_count,
                             0u);
        failed += expect_u32("restart clears drop count",
                             adapter.rx_drop_count,
                             0u);
        failed += expect_u32("restart clears rx queue",
                             adapter.rx_queue_count,
                             0u);
    }

    /* --- Timestamp gate: no hardware timestamp keeps evidence closed. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t snapshot;
        tdma_pio_spi_ring_adapter_t adapter;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();

        memset(&phys, 0, sizeof(phys));
        /* phys returns no hardware timestamp (0). */
        tdma_ring_runtime_init(&runtime);
        tdma_ring_runtime_configure(&runtime, &config);
        tdma_pio_spi_ring_adapter_init(&adapter);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter,
            100u,
            TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        tdma_ring_runtime_bind_adapter(&runtime,
                                       tdma_pio_spi_ring_adapter_ops(),
                                       &adapter);
        start_ring_data(&runtime);

        tdma_ring_runtime_service(&runtime);
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("up running", snapshot.up_running, 1u);
        failed += expect_u32("down running", snapshot.down_running, 1u);
        failed += expect_u32("no evidence without hw timestamp",
                             snapshot.simultaneous_feedback_loop_evidence, 0u);
        failed += expect_u32("timestamp missing reason",
                             snapshot.last_reason,
                             TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING);
    }

    /* --- Timestamp gate: diagnostic-only flags keep evidence closed. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t snapshot;
        tdma_pio_spi_ring_adapter_t adapter;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();

        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 1000000ull;
        phys.rx_timestamp_ns = 1000500ull;
        tdma_ring_runtime_init(&runtime);
        tdma_ring_runtime_configure(&runtime, &config);
        tdma_pio_spi_ring_adapter_init(&adapter);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter,
            1000u,
            TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
        tdma_ring_runtime_bind_adapter(&runtime,
                                       tdma_pio_spi_ring_adapter_ops(),
                                       &adapter);
        start_ring_data(&runtime);

        tdma_ring_runtime_service(&runtime);
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("diagnostic evidence rejected",
                             snapshot.simultaneous_feedback_loop_evidence, 0u);
        failed += expect_u32("timestamp missing for diagnostic",
                             snapshot.last_reason,
                             TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING);
    }

    /* --- Reference bad-header evidence: correlate the returned sequence
     * with the exact TX header and retain the first changed byte. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t snapshot;
        tdma_pio_spi_ring_adapter_t adapter;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();

        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 1000000ull;
        phys.rx_timestamp_ns = 1000500ull;
        phys.suppress_echo = true;
        tdma_ring_runtime_init(&runtime);
        tdma_ring_runtime_configure(&runtime, &config);
        tdma_pio_spi_ring_adapter_init(&adapter);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter,
            100u,
            TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        tdma_ring_runtime_bind_adapter(&runtime,
                                       tdma_pio_spi_ring_adapter_ops(),
                                       &adapter);
        failed += expect_bool("start bad-header evidence ring",
                              start_ring_data(&runtime), true);
        tdma_ring_runtime_service(&runtime);
        tdma_ring_runtime_service(&runtime);

        uint8_t bad_header[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        const uint32_t corrupt_offset = 16u;
        memcpy(bad_header, phys.last_tx, phys.last_tx_size);
        const uint8_t expected_byte = bad_header[corrupt_offset];
        bad_header[corrupt_offset] ^= 0x04u;
        const uint8_t observed_byte = bad_header[corrupt_offset];
        failed += expect_bool("inject correlated bad header",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, bad_header, phys.last_tx_size,
                                  0ull),
                              true);
        tdma_ring_runtime_service(&runtime);
        failed += expect_bool("snapshot correlated bad header",
                              tdma_ring_runtime_get_snapshot(
                                  &runtime, &snapshot),
                              true);
        failed += expect_u32("correlated transport result",
                             snapshot.adapter_last_bad_transport_result,
                             TDMA_TRANSPORT_CRC_MISMATCH);
        failed += expect_u32("correlated bad sequence",
                             snapshot.adapter_last_bad_sequence, 1u);
        failed += expect_u32("correlated header diff count",
                             snapshot.adapter_last_bad_header_diff_count,
                             1u);
        failed += expect_u32("correlated header first diff offset",
                             snapshot.adapter_last_bad_header_first_diff_offset,
                             corrupt_offset);
        failed += expect_u32("correlated header expected byte",
                             snapshot.adapter_last_bad_header_expected_byte,
                             expected_byte);
        failed += expect_u32("correlated header observed byte",
                             snapshot.adapter_last_bad_header_observed_byte,
                             observed_byte);
    }

    /* --- Full-packet diagnostic: a clock-evidence payload mutation must be
     * classified independently from the unchanged transport header. --- */
    {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_pio_spi_ring_adapter_snapshot_t snapshot;
        loopback_phys_t phys;
        tdma_ring_adapter_status_t status;
        tdma_ring_runtime_config_t config = make_valid_config();
        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 1000000ull;
        phys.suppress_echo = true;
        failed += expect_bool("packet diagnostic init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        failed += expect_u32("clock evidence defaults enabled",
                             adapter.clock_evidence_enabled, 1u);
        failed += expect_bool("clock evidence disable while stopped",
                              tdma_pio_spi_ring_adapter_set_clock_evidence_enabled(
                                  &adapter, false), true);
        failed += expect_bool("clock evidence re-enable while stopped",
                              tdma_pio_spi_ring_adapter_set_clock_evidence_enabled(
                                  &adapter, true), true);
        failed += expect_bool(
            "packet diagnostic fixed physical length",
            tdma_pio_spi_ring_adapter_set_forwarding_mode(
                &adapter,
                TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT),
            true);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter, 8u, TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        failed += expect_bool("packet diagnostic start",
                              tdma_pio_spi_ring_adapter_ops()->start(
                                  &adapter, &config), true);
        failed += expect_bool("clock evidence reject change while running",
                              tdma_pio_spi_ring_adapter_set_clock_evidence_enabled(
                                  &adapter, false), false);
        for (uint32_t tick = 0u; tick < 32u && adapter.tx_count < 2u; tick++) {
            (void)tdma_pio_spi_ring_adapter_ops()->service(
                &adapter, (uint64_t)tick * 1000000ull, &status);
        }
        tdma_transport_frame_view_t evidence_view;
        tdma_transport_result_t evidence_result = TDMA_TRANSPORT_OK;
        failed += expect_bool("clock evidence tx decode",
                              tdma_transport_frame_decode(
                                  phys.last_tx, phys.last_tx_size,
                                  &evidence_view, &evidence_result), true);
        failed += expect_u32("clock evidence tx sequence",
                             evidence_view.transport_sequence, 2u);
        failed += expect_u32("clock evidence tx payload size",
                             evidence_view.payload_size,
                             TDMA_TRANSPORT_SHORT_PAYLOAD_MAX);
        uint8_t corrupted[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        memcpy(corrupted, phys.last_tx, phys.last_tx_size);
        const uint32_t corrupt_offset =
            TDMA_TRANSPORT_FRAME_HEADER_SIZE + 20u;
        const uint8_t expected_byte = corrupted[corrupt_offset];
        corrupted[corrupt_offset] ^= 0x08u;
        const uint8_t observed_byte = corrupted[corrupt_offset];
        failed += expect_bool("inject bad clock evidence payload",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, corrupted, phys.last_tx_size,
                                  1000500ull), true);
        (void)tdma_pio_spi_ring_adapter_ops()->service(
            &adapter, 33000000ull, &status);
        failed += expect_bool("packet diagnostic snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &snapshot), true);
        failed += expect_u32("payload bad transport result",
                             snapshot.last_bad_transport_result,
                             TDMA_TRANSPORT_CRC_MISMATCH);
        failed += expect_u32("payload header unchanged",
                             snapshot.last_bad_header_diff_count, 0u);
        failed += expect_u32("payload packet diff count",
                             snapshot.last_bad_packet_diff_count, 1u);
        failed += expect_u32("payload first diff offset",
                             snapshot.last_bad_packet_first_diff_offset,
                             corrupt_offset);
        failed += expect_u32("payload expected byte",
                             snapshot.last_bad_packet_expected_byte,
                             expected_byte);
        failed += expect_u32("payload observed byte",
                             snapshot.last_bad_packet_observed_byte,
                             observed_byte);
        failed += expect_u32("idle payload is not DPLL evidence",
                             snapshot.last_bad_clock_evidence, 0u);
        failed += expect_bool("payload CRC differs",
                              snapshot.last_bad_expected_payload_crc32 !=
                                  snapshot.last_bad_observed_payload_crc32,
                              true);
        failed += expect_u32("wire transport CRC unchanged",
                             snapshot.last_bad_expected_transport_crc32,
                             snapshot.last_bad_observed_transport_crc32);
        failed += expect_bool("recomputed transport CRC differs",
                              snapshot.last_bad_recomputed_transport_crc32 !=
                                  snapshot.last_bad_observed_transport_crc32,
                              true);
    }

    /* --- Adapter snapshot and error accounting. --- */
    {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_pio_spi_ring_adapter_snapshot_t snap;
        loopback_phys_t phys;
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t ring_snap;
        const tdma_ring_runtime_config_t config = make_valid_config();

        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 1000000ull;
        phys.rx_timestamp_ns = 1000500ull;

        tdma_pio_spi_ring_adapter_init(&adapter);
        failed += expect_bool("snapshot after init",
                              tdma_pio_spi_ring_adapter_get_snapshot(&adapter,
                                                                     &snap),
                              true);
        failed += expect_u32("not started after init", snap.started, 0u);

        tdma_ring_runtime_init(&runtime);
        tdma_ring_runtime_configure(&runtime, &config);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter,
            100u,
            TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        tdma_ring_runtime_bind_adapter(&runtime,
                                       tdma_pio_spi_ring_adapter_ops(),
                                       &adapter);
        start_ring_data(&runtime);
        tdma_ring_runtime_service(&runtime);
        tdma_ring_runtime_service(&runtime);

        failed += expect_bool("adapter snapshot after service",
                              tdma_pio_spi_ring_adapter_get_snapshot(&adapter,
                                                                     &snap),
                              true);
        failed += expect_u32("started", snap.started, 1u);
        failed += expect_u32("adapter service count", snap.service_count, 2u);
        failed += expect_u32("adapter local slot",
                             snap.local_slot_id,
                             config.local_slot_id);
        failed += expect_u32("adapter schedule crc",
                             snap.schedule_crc32,
                             config.schedule_crc32);

        /* Bad RX frame: corrupt the magic and inject. */
        uint8_t bad[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        memcpy(bad, phys.last_tx, phys.last_tx_size);
        bad[0] = 0xFFu;
        failed += expect_bool("inject bad frame",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, bad, phys.last_tx_size, 0ull),
                              true);
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &ring_snap);
        failed += expect_u32("down running drops on bad frame",
                             ring_snap.down_running, 0u);
        failed += expect_u32("ring seq does not advance on bad frame",
                             ring_snap.ring_seq, 1u);
        failed += expect_u32("adapter rx bad count",
                             adapter.rx_bad_count, 1u);
        failed += expect_u32("transport bad count",
                             ring_snap.adapter_rx_transport_bad_count, 1u);
        failed += expect_u32("schedule bad count",
                             ring_snap.adapter_rx_schedule_bad_count, 0u);
        failed += expect_u32("profile bad count",
                             ring_snap.adapter_rx_profile_bad_count, 0u);
        failed += expect_u32("last bad transport result",
                             ring_snap.adapter_last_bad_transport_result,
                             TDMA_TRANSPORT_BAD_MAGIC);
        failed += expect_u32("adapter last error",
                             adapter.last_error,
                             TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME);

        /* A decodable frame with a foreign schedule is classified separately
         * and retains its identity fields for four-board diagnosis. */
        uint8_t identity_bad[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t identity_bad_size = 0u;
        tdma_transport_result_t transport_result = TDMA_TRANSPORT_OK;
        tdma_transport_frame_build_t identity_build = {
            .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id = 0u,
            .transport_sequence = 77u,
            .payload_class = TDMA_PAYLOAD_CLASS_IDLE_BEACON,
            .flags = TDMA_TRANSPORT_FLAG_IDLE_BEACON,
            .schedule_crc32 = config.schedule_crc32 + 1u,
            .ring_profile_crc32 = config.ring_profile_crc32,
            .hop_limit = config.node_count,
            .payload = NULL,
            .payload_size = 0u,
        };
        failed += expect_bool("encode schedule mismatch",
                              tdma_transport_frame_encode(
                                  &identity_build,
                                  identity_bad,
                                  sizeof(identity_bad),
                                  &identity_bad_size,
                                  &transport_result),
                              true);
        failed += expect_bool("inject schedule mismatch",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, identity_bad, identity_bad_size,
                                  0ull),
                              true);
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &ring_snap);
        failed += expect_u32("schedule mismatch total",
                             ring_snap.adapter_rx_bad_count, 2u);
        failed += expect_u32("schedule mismatch classified",
                             ring_snap.adapter_rx_schedule_bad_count, 1u);
        failed += expect_u32("schedule mismatch sequence",
                             ring_snap.adapter_last_bad_sequence, 77u);
        failed += expect_u32("schedule mismatch observed crc",
                             ring_snap.adapter_last_bad_schedule_crc32,
                             config.schedule_crc32 + 1u);
        failed += expect_u32("schedule mismatch decode result",
                             ring_snap.adapter_last_bad_transport_result,
                             TDMA_TRANSPORT_OK);

        identity_build.transport_sequence = 78u;
        identity_build.schedule_crc32 = config.schedule_crc32;
        identity_build.ring_profile_crc32 = config.ring_profile_crc32 + 1u;
        failed += expect_bool("encode profile mismatch",
                              tdma_transport_frame_encode(
                                  &identity_build,
                                  identity_bad,
                                  sizeof(identity_bad),
                                  &identity_bad_size,
                                  &transport_result),
                              true);
        failed += expect_bool("inject profile mismatch",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, identity_bad, identity_bad_size,
                                  0ull),
                              true);
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &ring_snap);
        failed += expect_u32("profile mismatch total",
                             ring_snap.adapter_rx_bad_count, 3u);
        failed += expect_u32("profile mismatch classified",
                             ring_snap.adapter_rx_profile_bad_count, 1u);
        failed += expect_u32("profile mismatch sequence",
                             ring_snap.adapter_last_bad_sequence, 78u);
        failed += expect_u32("profile mismatch observed crc",
                             ring_snap.adapter_last_bad_profile_crc32,
                             config.ring_profile_crc32 + 1u);

        /* Recover: the next emitted beacon is echoed by the physical stub. */
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &ring_snap);
        failed += expect_u32("down running recovers",
                             ring_snap.down_running, 1u);
        failed += expect_u32("feedback evidence recovers",
                             ring_snap.simultaneous_feedback_loop_evidence, 1u);

        /* Queue overflow accounting. */
        for (uint32_t i = 0u; i < (TDMA_PIO_SPI_RING_ADAPTER_RX_QUEUE_DEPTH + 2u);
             i++) {
            tdma_pio_spi_ring_adapter_inject_rx(&adapter,
                                                phys.last_tx,
                                                phys.last_tx_size,
                                                1000000ull);
        }
        failed += expect_u32("rx queue drop count",
                             adapter.rx_drop_count, 2u);

        /* Stop clears running state. */
        tdma_ring_runtime_configure(&runtime, NULL);
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &ring_snap);
        failed += expect_u32("disabled legs after stop",
                             ring_snap.up_running | ring_snap.down_running,
                             0u);
        failed += expect_u32("adapter stop count",
                             ring_snap.adapter_stop_count, 1u);
    }

    /* --- Forward node: receives and re-emits the frame with hop advanced
     * and identity preserved; feedback evidence stays closed. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t snapshot;
        tdma_pio_spi_ring_adapter_t adapter;
        loopback_phys_t phys;
        tdma_ring_runtime_config_t config = make_valid_config();
        config.local_slot_id = 1u;   /* follower slot, reference is 0 */
        config.reference_slot_id = 0u;

        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 2000000ull;
        phys.rx_timestamp_ns = 1500000ull;
        phys.suppress_echo = true;

        tdma_ring_runtime_init(&runtime);
        tdma_ring_runtime_configure(&runtime, &config);
        tdma_pio_spi_ring_adapter_init(&adapter);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter,
            100u,
            TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        tdma_ring_runtime_bind_adapter(&runtime,
                                       tdma_pio_spi_ring_adapter_ops(),
                                       &adapter);
        start_ring_data(&runtime);

        /* First service: no frame received yet; the slave pushes a
         * placeholder beacon to keep the full-duplex PIO pull fed, so the
         * ring stays up (up=1) but down stays 0. */
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("forward node placeholder up", snapshot.up_running, 1u);
        failed += expect_u32("forward node down idle", snapshot.down_running, 0u);
        failed += expect_u32("forward node no ring fault",
                             snapshot.last_reason,
                             TDMA_RING_RUNTIME_REASON_NONE);

        /* Inject a reference beacon (hop 0) and service: it must be forwarded
         * with hop advanced and identity preserved. */
        uint8_t beacon[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t beacon_size = 0u;
        const tdma_transport_frame_build_t build = {
            .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id = 0u,
            .transport_sequence = 7u,
            .payload_class = TDMA_PAYLOAD_CLASS_IDLE_BEACON,
            .flags = TDMA_TRANSPORT_FLAG_IDLE_BEACON,
            .schedule_crc32 = config.schedule_crc32,
            .ring_profile_crc32 = config.ring_profile_crc32,
            .hop_limit = 4u,
            .payload = NULL,
            .payload_size = 0u,
        };
        tdma_transport_result_t tresult = TDMA_TRANSPORT_OK;
        failed += expect_bool("encode reference beacon",
                              tdma_transport_frame_encode(&build,
                                                          beacon,
                                                          sizeof(beacon),
                                                          &beacon_size,
                                                          &tresult),
                              true);
        failed += expect_bool("inject reference beacon",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, beacon, beacon_size, 1500000ull),
                              true);

        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("forward up running", snapshot.up_running, 1u);
        failed += expect_u32("forward down running", snapshot.down_running, 1u);
        failed += expect_u32("forward ring seq advances", snapshot.ring_seq, 1u);
        failed += expect_u32("forward tx sequence preserved",
                             snapshot.up_tx_sequence, 7u);
        failed += expect_u32("forward rx sequence matches",
                             snapshot.down_rx_sequence, 7u);
        failed += expect_bool("forward identity preserved",
                              snapshot.up_tx_frame_crc32 ==
                                  snapshot.down_rx_frame_crc32,
                              true);
        failed += expect_u32("forward feedback stays closed",
                             snapshot.simultaneous_feedback_loop_evidence, 0u);
        failed += expect_u32("forward count", adapter.forward_count, 1u);

        /* The emitted packet must have hop_count=1 and matching identity. */
        tdma_transport_frame_view_t fwd_view;
        tresult = TDMA_TRANSPORT_OK;
        failed += expect_bool("decode forwarded packet",
                              tdma_transport_frame_decode(phys.last_tx,
                                                          phys.last_tx_size,
                                                          &fwd_view,
                                                          &tresult),
                              true);
        failed += expect_u32("forwarded hop count", fwd_view.hop_count, 1u);
        failed += expect_u32("forwarded origin preserved",
                             fwd_view.origin_slot_id, 0u);
        failed += expect_u32("forwarded sequence preserved",
                             fwd_view.transport_sequence, 7u);
    }

    /* --- Sequence-indexed TX latch survives one in-flight frame. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t snapshot;
        tdma_pio_spi_ring_adapter_t adapter;
        flight_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();

        memset(&phys, 0, sizeof(phys));
        failed += expect_bool("flight runtime init",
                              tdma_ring_runtime_init(&runtime), true);
        failed += expect_bool("flight configure",
                              tdma_ring_runtime_configure(&runtime, &config),
                              true);
        failed += expect_bool("flight adapter init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        set_test_sequential_topology(&adapter, config.node_count);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           flight_tx,
                                           flight_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter,
            4u,
            TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        failed += expect_bool("flight bind",
                              tdma_ring_runtime_bind_adapter(
                                  &runtime,
                                  tdma_pio_spi_ring_adapter_ops(),
                                  &adapter),
                              true);
        failed += expect_bool("flight start", start_ring_data(&runtime), true);

        /* Phase, TX1, throttle, TX2+RX1. */
        for (uint32_t i = 0u; i < 4u; i++) {
            tdma_ring_runtime_service(&runtime);
        }
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("flight latest tx sequence",
                             snapshot.up_tx_sequence, 2u);
        failed += expect_u32("flight returned sequence",
                             snapshot.down_rx_sequence, 1u);
        failed += expect_u32("flight sequence-correlated evidence",
                             snapshot.simultaneous_feedback_loop_evidence, 1u);
        failed += expect_u32("flight sequence-correlated RTT",
                             snapshot.feedback_round_trip_ns, 500u);
        failed += expect_u64("flight matched TX latch",
                             snapshot.reference_tx_timestamp_ns, 1000000ull);
        failed += expect_u64("flight matched RX latch",
                             snapshot.feedback_rx_timestamp_ns, 1000500ull);
    }

    /* --- A reference deadline cannot resubmit an asynchronous physical TX
     * until the one-shot completion token has been consumed. --- */
    {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_ring_adapter_status_t status;
        async_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();

        memset(&phys, 0, sizeof(phys));
        phys.completion_timestamp_ns = 1000000ull;
        failed += expect_bool("async backpressure adapter init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           async_tx,
                                           NULL,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_phys_tx_complete(
            &adapter, async_tx_complete);
        failed += expect_bool("async backpressure start",
                              tdma_pio_spi_ring_adapter_ops()->start(
                                  &adapter, &config), true);

        failed += expect_bool("async backpressure phase",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 1ull, &status), true);
        failed += expect_bool("async backpressure first launch",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 2ull, &status), true);
        failed += expect_u32("async first tx count", phys.tx_calls, 1u);

        failed += expect_bool("async pending service stays live",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 3000ull, &status), true);
        failed += expect_u32("async pending does not resubmit",
                             phys.tx_calls, 1u);
        failed += expect_u32("async physical busy never called",
                             phys.busy_tx_calls, 0u);
        failed += expect_u32("async pending up remains running",
                             status.up_running, 1u);

        phys.completion_ready = true;
        failed += expect_bool("async completion releases next launch",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 4000ull, &status), true);
        failed += expect_u32("async second tx count", phys.tx_calls, 2u);
        failed += expect_u32("async adapter tx count", status.tx_count, 2u);

        phys.completion_timestamp_ns = 0ull;
        phys.completion_ready = true;
        failed += expect_bool("async failed terminal token releases retry",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 6000ull, &status), true);
        failed += expect_u32("async retry after failed terminal token",
                             phys.tx_calls, 3u);
        failed += expect_u32("async failed terminal never calls busy",
                             phys.busy_tx_calls, 0u);
    }

    /* --- The bounded TX-evidence ring retains exact sequence identity while
     * stale or identity-mismatched feedback remains fail-closed. --- */
    {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_pio_spi_ring_adapter_snapshot_t snapshot;
        tdma_ring_adapter_status_t status;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();
        uint8_t first_packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t first_packet_size = 0u;

        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 1000000ull;
        phys.rx_timestamp_ns = 1000500ull;
        failed += expect_bool("delayed evidence init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter, 8u, TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        failed += expect_bool("delayed evidence start",
                              tdma_pio_spi_ring_adapter_ops()->start(
                                  &adapter, &config), true);

        for (uint32_t frame = 0u; frame < 20u; frame++) {
            failed += expect_bool("delayed evidence service",
                                  tdma_pio_spi_ring_adapter_ops()->service(
                                      &adapter,
                                      (uint64_t)frame * 2000ull,
                                      &status),
                                  true);
            tdma_transport_frame_view_t view;
            tdma_transport_result_t result = TDMA_TRANSPORT_OK;
            const bool decoded = tdma_transport_frame_decode(phys.last_tx,
                                                              phys.last_tx_size,
                                                              &view,
                                                              &result);
            if (decoded &&
                view.transport_sequence == 1u && first_packet_size == 0u) {
                first_packet_size = phys.last_tx_size;
                memcpy(first_packet, phys.last_tx, first_packet_size);
            }
        }
        failed += expect_bool("TX evidence source frame captured",
                              first_packet_size != 0u, true);

        /* Sequence 1 was overwritten by sequence 9 in the modulo-8 ring;
         * injecting that stale frame must not recreate feedback evidence. */
        phys.suppress_echo = true;
        failed += expect_bool("stale sequence inject",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, first_packet, first_packet_size,
                                  1000500ull),
                              true);
        failed += expect_bool("stale sequence service",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 40000ull, &status), true);
        failed += expect_bool("stale sequence snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &snapshot), true);
        failed += expect_u32("stale sequence feedback closed",
                             snapshot.feedback_reference_sequence, 0u);

        /* A valid frame with the current sequence but a different identity is
         * equally ineligible. */
        uint8_t mismatch_packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t mismatch_size = 0u;
        const uint8_t mismatch_payload[] = {0xA5u};
        const tdma_transport_frame_build_t mismatch_build = {
            .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id = config.local_slot_id,
            .transport_sequence = 17u,
            .payload_class = TDMA_PAYLOAD_CLASS_IDLE_BEACON,
            .flags = TDMA_TRANSPORT_FLAG_IDLE_BEACON,
            .schedule_crc32 = config.schedule_crc32,
            .ring_profile_crc32 = config.ring_profile_crc32,
            .hop_limit = config.node_count - 1u,
            .payload = mismatch_payload,
            .payload_size = sizeof(mismatch_payload),
        };
        tdma_transport_result_t mismatch_result = TDMA_TRANSPORT_OK;
        failed += expect_bool("identity mismatch encode",
                              tdma_transport_frame_encode(
                                  &mismatch_build,
                                  mismatch_packet,
                                  sizeof(mismatch_packet),
                                  &mismatch_size,
                                  &mismatch_result),
                              true);
        failed += expect_bool("identity mismatch inject",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, mismatch_packet, mismatch_size,
                                  1000500ull),
                              true);
        failed += expect_bool("identity mismatch service",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 42000ull, &status), true);
        failed += expect_bool("identity mismatch snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &snapshot), true);
        failed += expect_u32("identity mismatch feedback closed",
                             snapshot.feedback_reference_sequence, 0u);
    }

    /* --- Product follower: PIO has already forwarded the bytes before the
     * complete RX frame is parsed. The service path must not call phys_tx and
     * create a duplicate store-and-forward frame. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_pio_spi_ring_adapter_snapshot_t adapter_snapshot;
        loopback_phys_t phys;
        tdma_ring_runtime_config_t config = make_valid_config();
        config.local_slot_id = 1u;
        config.reference_slot_id = 0u;

        memset(&phys, 0, sizeof(phys));
        phys.suppress_echo = true;
        tdma_ring_runtime_init(&runtime);
        tdma_ring_runtime_configure(&runtime, &config);
        tdma_pio_spi_ring_adapter_init(&adapter);
        failed += expect_bool(
            "select physical flight forwarding",
            tdma_pio_spi_ring_adapter_set_forwarding_mode(
                &adapter,
                TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT),
            true);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_ring_runtime_bind_adapter(&runtime,
                                       tdma_pio_spi_ring_adapter_ops(),
                                       &adapter);
        start_ring_data(&runtime);

        uint8_t beacon[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t beacon_size = 0u;
        tdma_transport_frame_build_t build = {
            .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id = 0u,
            .transport_sequence = 11u,
            .payload_class = TDMA_PAYLOAD_CLASS_IDLE_BEACON,
            .flags = TDMA_TRANSPORT_FLAG_IDLE_BEACON,
            .schedule_crc32 = config.schedule_crc32,
            .ring_profile_crc32 = config.ring_profile_crc32,
            .hop_limit = 4u,
        };
        tdma_transport_result_t result = TDMA_TRANSPORT_OK;
        failed += expect_bool(
            "encode physical flight beacon",
            tdma_transport_frame_encode(&build,
                                        beacon,
                                        sizeof(beacon),
                                        &beacon_size,
                                        &result),
            true);
        failed += expect_bool(
            "inject physical flight capture",
            tdma_pio_spi_ring_adapter_inject_rx(
                &adapter, beacon, beacon_size, 2000000ull),
            true);
        tdma_ring_runtime_service(&runtime);
        failed += expect_u32("physical flight skips phys_tx",
                             phys.tx_calls, 0u);
        failed += expect_u32("physical flight forward count",
                             adapter.forward_count, 1u);
        failed += expect_u32("physical flight physical tx evidence",
                             adapter.tx_count, 1u);
        failed += expect_bool(
            "physical flight adapter snapshot",
            tdma_pio_spi_ring_adapter_get_snapshot(&adapter,
                                                    &adapter_snapshot),
            true);
        failed += expect_u32(
            "physical flight mode snapshot",
            adapter_snapshot.forwarding_mode,
            TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT);
        failed += expect_bool(
            "cannot switch flight mode while started",
            tdma_pio_spi_ring_adapter_set_forwarding_mode(
                &adapter,
                TDMA_PIO_SPI_RING_FORWARDING_STORE_FORWARD),
            false);
    }

    /* --- phys_ctrl: arm/disarm callbacks driven by adapter start/stop. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t snapshot;
        tdma_pio_spi_ring_adapter_t adapter;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();
        uint32_t arm_calls = 0u;
        uint32_t disarm_calls = 0u;
        uint32_t arm_config_seq = 0u;
        uint32_t arm_result = 1u;

        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 1000000ull;
        phys.rx_timestamp_ns = 1000500ull;

        phys_ctrl_stub_t ctrl = {0};
        ctrl.arm_calls = &arm_calls;
        ctrl.disarm_calls = &disarm_calls;
        ctrl.arm_schedule_crc = &arm_config_seq;
        ctrl.arm_result = &arm_result;

        tdma_ring_runtime_init(&runtime);
        tdma_ring_runtime_configure(&runtime, &config);
        tdma_pio_spi_ring_adapter_init(&adapter);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter,
                                                phys_ctrl_stub_arm,
                                                phys_ctrl_stub_disarm,
                                                NULL,
                                                NULL,
                                                &ctrl);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter,
            100u,
            TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        tdma_ring_runtime_bind_adapter(&runtime,
                                       tdma_pio_spi_ring_adapter_ops(),
                                       &adapter);

        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("phys arm called on start", arm_calls, 1u);
        failed += expect_u32("phys arm got schedule crc",
                             arm_config_seq,
                             config.schedule_crc32);
        failed += expect_u32("enable data after arm",
                             tdma_ring_runtime_set_data_enabled(&runtime, true),
                             1u);
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("up running with phys ctrl", snapshot.up_running, 1u);

        tdma_ring_runtime_configure(&runtime, NULL);
        tdma_ring_runtime_service(&runtime);
        failed += expect_u32("phys disarm called on stop", disarm_calls, 1u);
    }

    /* --- Hardware timestamp eligibility follows the physical arm lifetime.
     * A ready probe that is false must not leak boot metadata into DPLL; a
     * later arm may publish the latch and STOP must revoke it again. --- */
    {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_pio_spi_ring_adapter_snapshot_t snapshot;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();
        uint32_t arm_calls = 0u;
        uint32_t disarm_calls = 0u;
        uint32_t arm_schedule_crc = 0u;
        uint32_t arm_result = 1u;
        uint32_t timestamp_ready_calls = 0u;
        phys_ctrl_stub_t ctrl = {
            .arm_calls = &arm_calls,
            .disarm_calls = &disarm_calls,
            .arm_schedule_crc = &arm_schedule_crc,
            .arm_result = &arm_result,
            .timestamp_ready = false,
            .timestamp_resolution_ns = 8u,
            .timestamp_flags = TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED,
            .timestamp_ready_calls = &timestamp_ready_calls,
        };

        memset(&phys, 0, sizeof(phys));
        failed += expect_bool("timestamp lifetime init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter,
                                                phys_ctrl_stub_arm,
                                                phys_ctrl_stub_disarm,
                                                NULL,
                                                NULL,
                                                &ctrl);
        tdma_pio_spi_ring_adapter_set_phys_timestamp_ready(
            &adapter, phys_timestamp_stub_ready);
        /* Simulate stale boot metadata; start must revoke it before arm. */
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter, 8u, TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        failed += expect_bool("timestamp lifetime start without latch",
                              tdma_pio_spi_ring_adapter_ops()->start(
                                  &adapter, &config), true);
        failed += expect_bool("timestamp lifetime snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &snapshot), true);
        failed += expect_u32("timestamp not eligible before latch",
                             snapshot.timestamp_resolution_ns, 0u);
        failed += expect_u32("timestamp diagnostic before latch",
                             snapshot.timestamp_flags,
                             TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
        failed += expect_u32("timestamp ready probe called",
                             timestamp_ready_calls, 1u);

        tdma_pio_spi_ring_adapter_ops()->stop(&adapter);
        failed += expect_u32("timestamp disarm after first stop",
                             disarm_calls, 1u);
        ctrl.timestamp_ready = true;
        failed += expect_bool("timestamp lifetime start with latch",
                              tdma_pio_spi_ring_adapter_ops()->start(
                                  &adapter, &config), true);
        failed += expect_bool("timestamp ready snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &snapshot), true);
        failed += expect_u32("timestamp resolution after latch",
                             snapshot.timestamp_resolution_ns, 8u);
        failed += expect_u32("timestamp hardware after latch",
                             snapshot.timestamp_flags,
                             TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        tdma_pio_spi_ring_adapter_ops()->stop(&adapter);
        failed += expect_u32("timestamp disarm after second stop",
                             disarm_calls, 2u);
        failed += expect_bool("timestamp stopped snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &snapshot), true);
        failed += expect_u32("timestamp revoked on stop",
                             snapshot.timestamp_resolution_ns, 0u);
        failed += expect_u32("timestamp diagnostic on stop",
                             snapshot.timestamp_flags,
                             TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY);
    }

    /* --- Reference hop limit follows the deployed ring node count. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_pio_spi_ring_adapter_t adapter;
        loopback_phys_t phys;
        tdma_ring_runtime_config_t config = make_valid_config();
        tdma_transport_frame_view_t view;
        tdma_transport_result_t result = TDMA_TRANSPORT_OK;

        memset(&phys, 0, sizeof(phys));
        phys.suppress_echo = true;
        config.node_count = 4u;
        tdma_ring_runtime_init(&runtime);
        tdma_ring_runtime_configure(&runtime, &config);
        tdma_pio_spi_ring_adapter_init(&adapter);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_ring_runtime_bind_adapter(&runtime,
                                       tdma_pio_spi_ring_adapter_ops(),
                                       &adapter);
        failed += expect_bool("four-node reference start",
                              start_ring_data(&runtime), true);
        tdma_ring_runtime_service(&runtime);
        tdma_ring_runtime_service(&runtime);
        failed += expect_bool("four-node beacon decode",
                              tdma_transport_frame_decode(phys.last_tx,
                                                          phys.last_tx_size,
                                                          &view,
                                                          &result),
                              true);
        failed += expect_u32("four-node hop limit", view.hop_limit, 3u);
    }

    /* --- Flight FIFO bridge: core0 publishes a process-image descriptor,
     * core1 emits it on the reference leg, and the echoed payload is mirrored
     * back to core0 through the RX descriptor ring. --- */
    {
        tdma_ring_runtime_t runtime;
        tdma_ring_runtime_snapshot_t snapshot;
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_flight_fifo_t fifo;
        tdma_flight_rx_view_t rx_view;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();
        const uint8_t payload[] = {0xA5u, 0xA6u, 0xA7u, 0xA8u};

        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 3000000ull;
        phys.rx_timestamp_ns = 3000500ull;

        tdma_ring_runtime_init(&runtime);
        tdma_ring_runtime_configure(&runtime, &config);
        tdma_pio_spi_ring_adapter_init(&adapter);
        tdma_flight_fifo_init(&fifo);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter,
            100u,
            TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        tdma_ring_runtime_bind_adapter(&runtime,
                                       tdma_pio_spi_ring_adapter_ops(),
                                       &adapter);
        start_ring_data(&runtime);

        failed += expect_bool("publish flight tx",
                              tdma_flight_fifo_core0_publish_tx(&fifo,
                                                                payload,
                                                                sizeof(payload),
                                                                9u,
                                                                77u,
                                                                0x3u),
                              true);
        tdma_ring_runtime_service(&runtime);
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("flight sequence on wire",
                             snapshot.up_tx_sequence, 1u);
        if (tdma_flight_fifo_core0_acquire_rx(&fifo, &rx_view)) {
            failed += expect_u32("flight rx generation", rx_view.generation, 1u);
            failed += expect_u32("flight rx sequence", rx_view.sequence, 1u);
            failed += expect_u32("flight rx size",
                                 rx_view.data_size,
                                 (uint32_t)sizeof(payload));
            failed += expect_bool("flight payload mirrored",
                                  memcmp(rx_view.data,
                                         payload,
                                         sizeof(payload)) == 0,
                                  true);
            failed += expect_bool("release flight rx",
                                  tdma_flight_fifo_core0_release_rx(
                                      &fifo, rx_view.slot_index),
                                  true);
        } else {
            failed += expect_bool("acquire flight rx", false, true);
        }
    }

    /* Physical flight PIO is armed for one fixed-size short frame. Even the
     * first idle beacon (before timestamp evidence exists) must carry enough
     * alignment symbols to satisfy that physical byte count. */
    {
        tdma_ring_runtime_t runtime;
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_flight_engine_t engine;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();
        tdma_process_image_map_t map = make_flight_map();
        tdma_transport_frame_view_t view;
        tdma_transport_result_t result = TDMA_TRANSPORT_OK;
        tdma_ring_adapter_status_t status;
        uint8_t expected[64];

        memset(&phys, 0, sizeof(phys));
        phys.tx_timestamp_ns = 3000000ull;
        phys.rx_timestamp_ns = 3000500ull;
        tdma_flight_engine_fill_alignment_symbols(expected,
                                                  sizeof(expected));

        failed += expect_bool("fixed flight runtime init",
                              tdma_ring_runtime_init(&runtime), true);
        failed += expect_bool("fixed flight runtime config",
                              tdma_ring_runtime_configure(&runtime, &config),
                              true);
        failed += expect_bool("fixed flight adapter init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        failed += expect_bool("fixed flight engine init",
                              tdma_flight_engine_init(&engine), true);
        failed += expect_bool("fixed flight map config",
                              tdma_flight_engine_configure(&engine, &map), true);
        set_test_sequential_topology(&adapter, config.node_count);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
        failed += expect_bool(
            "fixed flight forwarding mode",
            tdma_pio_spi_ring_adapter_set_forwarding_mode(
                &adapter,
                TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT),
            true);
        failed += expect_bool("fixed flight bind",
                              tdma_ring_runtime_bind_adapter(
                                  &runtime,
                                  tdma_pio_spi_ring_adapter_ops(),
                                  &adapter),
                              true);
        failed += expect_bool("fixed flight start",
                              start_ring_data(&runtime), true);
        failed += expect_bool("raw flight engine remains inactive",
                              tdma_flight_engine_is_active(&engine), false);
        failed += expect_u32("raw flight receive gate remains disabled",
                             adapter.receive_health.configured, 0u);
        tdma_ring_runtime_service(&runtime);
        tdma_ring_runtime_service(&runtime);
        failed += expect_bool("fixed flight decode",
                              tdma_transport_frame_decode(
                                  phys.last_tx,
                                  phys.last_tx_size,
                                  &view,
                                  &result),
                              true);
        failed += expect_u32("fixed flight idle payload size",
                             (uint32_t)view.payload_size,
                             (uint32_t)sizeof(expected));
        failed += expect_bool("fixed flight idle alignment symbols",
                              memcmp(view.payload,
                                     expected,
                                     sizeof(expected)) == 0,
                              true);
        for (uint32_t tick = 0u; tick < 32u && adapter.tx_count < 2u; tick++) {
            (void)tdma_pio_spi_ring_adapter_ops()->service(
                &adapter, (uint64_t)tick * 1000000ull, &status);
        }
        failed += expect_bool("fixed clock evidence decode",
                              tdma_transport_frame_decode(
                                  phys.last_tx,
                                  phys.last_tx_size,
                                  &view,
                                  &result),
                              true);
        failed += expect_u32("fixed clock evidence sequence",
                             view.transport_sequence, 2u);
        failed += expect_u32("fixed clock evidence payload size",
                             (uint32_t)view.payload_size,
                             (uint32_t)sizeof(expected));
    }

    /* Process-image origin expands one local 32-byte FIFO mailbox into the
     * fixed map-sized wire image before the first PIO flight cycle. */
    {
        tdma_ring_runtime_t runtime;
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_flight_fifo_t fifo;
        tdma_flight_engine_t engine;
        loopback_phys_t phys;
        const tdma_ring_runtime_config_t config = make_valid_config();
        tdma_process_image_map_t map = make_flight_map();
        uint8_t mailbox[32];
        tdma_transport_frame_view_t view;
        tdma_transport_result_t result = TDMA_TRANSPORT_OK;

        memset(&phys, 0, sizeof(phys));
        memset(mailbox, 0x5Au, sizeof(mailbox));
        phys.tx_timestamp_ns = 3100000ull;
        phys.rx_timestamp_ns = 3100500ull;

        failed += expect_bool("process origin runtime init",
                              tdma_ring_runtime_init(&runtime), true);
        failed += expect_bool("process origin runtime config",
                              tdma_ring_runtime_configure(&runtime, &config),
                              true);
        failed += expect_bool("process origin adapter init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        set_test_sequential_topology(&adapter, config.node_count);
        failed += expect_bool("process origin fifo init",
                              tdma_flight_fifo_init(&fifo), true);
        failed += expect_bool("process origin engine init",
                              tdma_flight_engine_init(&engine), true);
        failed += expect_bool("process origin map config",
                              tdma_flight_engine_configure(&engine, &map), true);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
        tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
        failed += expect_bool(
            "process origin forwarding mode",
            tdma_pio_spi_ring_adapter_set_forwarding_mode(
                &adapter,
                TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE),
            true);
        failed += expect_bool("process origin bind",
                              tdma_ring_runtime_bind_adapter(
                                  &runtime,
                                  tdma_pio_spi_ring_adapter_ops(),
                                  &adapter),
                              true);
        failed += expect_bool("process origin start",
                              start_ring_data(&runtime), true);
        failed += expect_bool("process origin mailbox publish",
                              tdma_flight_fifo_core0_publish_tx(
                                  &fifo,
                                  mailbox,
                                  sizeof(mailbox),
                                  10u,
                                  78u,
                                  1u),
                              true);
        tdma_ring_runtime_service(&runtime);
        tdma_ring_runtime_service(&runtime);
        failed += expect_bool("process origin decode",
                              tdma_transport_frame_decode(
                                  phys.last_tx,
                                  phys.last_tx_size,
                                  &view,
                                  &result),
                              true);
        failed += expect_u32("process origin fixed payload size",
                             (uint32_t)view.payload_size, 64u);
        failed += expect_bool("process origin local mailbox expanded",
                              memcmp(view.payload, mailbox,
                                     sizeof(mailbox)) == 0,
                              true);
        uint8_t alignment_payload[64];
        tdma_flight_engine_fill_alignment_symbols(alignment_payload,
                                                  sizeof(alignment_payload));
        failed += expect_bool("process origin remote alignment symbols",
                              memcmp(view.payload + 32u,
                                     alignment_payload + 32u,
                                     32u) == 0,
                              true);
        uint32_t transition_count = 0u;
        for (uint32_t bit = 257u; bit < 512u; bit++) {
            const uint32_t previous = bit - 1u;
            const uint8_t previous_symbol =
                (alignment_payload[previous >> 3u] >>
                 (7u - (previous & 7u))) & 1u;
            const uint8_t symbol =
                (alignment_payload[bit >> 3u] >>
                 (7u - (bit & 7u))) & 1u;
            transition_count += previous_symbol != symbol ? 1u : 0u;
        }
        failed += expect_bool("process origin alignment symbols transition",
                              transition_count > 64u,
                              true);
    }

    /* A writer that dies while holding the map seqlock must not spin core1
     * forever. All readers fail closed after the bounded retry budget. */
    {
        tdma_flight_engine_t engine;
        tdma_process_image_map_t map = make_flight_map();
        tdma_flight_engine_apply_t applied;
        tdma_flight_engine_result_t result = TDMA_FLIGHT_ENGINE_OK;
        tdma_flight_engine_snapshot_t engine_snapshot;
        uint8_t input[64] = {0};
        uint8_t output[64] = {0};
        uint32_t input_mask = 0u;

        failed += expect_bool("bounded map engine init",
                              tdma_flight_engine_init(&engine), true);
        failed += expect_bool("bounded map configure",
                              tdma_flight_engine_configure(&engine, &map), true);
        failed += expect_bool("bounded map activate",
                              tdma_flight_engine_activate(&engine, 1u), true);
        __atomic_store_n(&engine.map_sequence, 1u, __ATOMIC_RELEASE);
        failed += expect_bool("bounded map apply rejects busy writer",
                              tdma_flight_engine_apply(&engine,
                                                       input,
                                                       sizeof(input),
                                                       NULL,
                                                       output,
                                                       sizeof(output),
                                                       &applied,
                                                       &result),
                              false);
        failed += expect_u32("bounded map unavailable result",
                             result,
                             TDMA_FLIGHT_ENGINE_MAP_UNAVAILABLE);
        failed += expect_bool("bounded map classify rejects busy writer",
                              tdma_flight_engine_classify_input(&engine,
                                                                 input,
                                                                 sizeof(input),
                                                                 &input_mask),
                              false);
        failed += expect_bool("bounded map snapshot rejects busy writer",
                              tdma_flight_engine_get_snapshot(
                                  &engine, &engine_snapshot),
                              false);
    }

    /* The wire map is always eight slots. Active 2/3/4/8-node topologies
     * only change which mailbox headers are present and the target mask. */
    {
        static const uint32_t node_counts[] = {2u, 3u, 4u, 8u};
        for (uint32_t topology = 0u;
             topology < (uint32_t)(sizeof(node_counts) /
                                   sizeof(node_counts[0]));
             topology++) {
            const uint32_t node_count = node_counts[topology];
            const uint32_t active_mask = (1u << node_count) - 1u;
            for (uint32_t local_slot = 0u;
                 local_slot < node_count;
                 local_slot++) {
                tdma_flight_engine_t engine;
                tdma_process_image_map_t map = make_eight_slot_flight_map();
                tdma_flight_engine_snapshot_t engine_snapshot;
                uint8_t input[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE] = {0};
                uint32_t input_mask = 0u;

                for (uint32_t source = 0u; source < node_count; source++) {
                    uint8_t *mailbox =
                        &input[source * TDMA_FLIGHT_SHORT_SLOT_SIZE];
                    mailbox[0] = (uint8_t)(TDMA_FLIGHT_MAILBOX_MAGIC & 0xFFu);
                    mailbox[1] =
                        (uint8_t)(TDMA_FLIGHT_MAILBOX_MAGIC >> 8u);
                    mailbox[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] =
                        TDMA_FLIGHT_MAILBOX_VERSION;
                    mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] =
                        (uint8_t)source;
                    mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] =
                        (uint8_t)active_mask;
                    mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET] =
                        (uint8_t)(source + 1u);
                }

                failed += expect_bool("topology engine init",
                                      tdma_flight_engine_init(&engine), true);
                failed += expect_bool("topology map configure",
                                      tdma_flight_engine_configure(
                                          &engine, &map), true);
                failed += expect_bool("topology map activate",
                                      tdma_flight_engine_activate(
                                          &engine, local_slot), true);
                failed += expect_bool("topology classify",
                                      tdma_flight_engine_classify_input(
                                          &engine,
                                          input,
                                          sizeof(input),
                                          &input_mask),
                                      true);
                failed += expect_u32("topology remote bitmap",
                                     input_mask,
                                     active_mask & ~(1u << local_slot));
                failed += expect_bool("topology commit",
                                      tdma_flight_engine_commit_input(
                                          &engine,
                                          input,
                                          sizeof(input),
                                          input_mask),
                                      true);
                failed += expect_bool("topology duplicate classify",
                                      tdma_flight_engine_classify_input(
                                          &engine,
                                          input,
                                          sizeof(input),
                                          &input_mask),
                                      true);
                failed += expect_u32("topology duplicate bitmap",
                                     input_mask,
                                     0u);
                failed += expect_bool("topology snapshot",
                                      tdma_flight_engine_get_snapshot(
                                          &engine, &engine_snapshot),
                                      true);
                failed += expect_u32("topology hit count",
                                     engine_snapshot.rx_bitmap_hit_count,
                                     node_count - 1u);
                failed += expect_u32(
                    "topology duplicate count",
                    engine_snapshot.rx_bitmap_duplicate_count,
                    node_count - 1u);
            }
        }
    }

    /* Calibration step 1 may assign Node values in an order unrelated to
     * wiring.  Freeze a deliberately permuted physical cycle so a future
     * refactor cannot derive receive masks from numeric Node adjacency. */
    {
        static const uint32_t marker_next[4] = {3u, 2u, 0u, 1u};
        static const uint32_t expected_by_node[4] = {
            (1u << 1u) | (1u << 2u) | (1u << 3u),
            (1u << 0u) | (1u << 2u),
            (1u << 0u),
            (1u << 0u) | (1u << 1u) | (1u << 2u),
        };
        for (uint32_t local_node = 0u; local_node < 4u; local_node++) {
            tdma_ring_runtime_t runtime;
            tdma_pio_spi_ring_adapter_t adapter;
            tdma_pio_spi_ring_adapter_snapshot_t snapshot;
            tdma_flight_engine_t engine;
            loopback_phys_t phys;
            tdma_ring_runtime_config_t config = make_valid_config();
            tdma_process_image_map_t map = make_eight_slot_flight_map();
            tdma_ring_calibration_stage_t topology;
            memset(&phys, 0, sizeof(phys));
            memset(&topology, 0, sizeof(topology));
            phys.suppress_echo = true;
            config.node_count = 4u;
            config.local_slot_id = local_node;
            config.reference_slot_id = 0u;
            config.feedback_timeout_ns = config.cycle_period_ns * 4u;
            topology.enabled = 1u;
            topology.node_count = 4u;
            topology.evidence_flags = TDMA_RING_CALIBRATION_REQUIRED_FLAGS;
            topology.calibration_generation = 1u;
            topology.topology_generation = 2u;
            topology.topology_crc32 = 3u;
            topology.profile_crc32 = 4u;
            topology.schedule_crc32 = 5u;
            for (uint32_t link_index = 0u; link_index < 4u; link_index++) {
                tdma_ring_calibration_link_t *link =
                    &topology.links[link_index];
                link->valid = 1u;
                link->link_index = link_index;
                link->marker_source_node = link_index;
                link->marker_destination_node = marker_next[link_index];
                link->data_source_node = marker_next[link_index];
                link->data_destination_node = link_index;
                link->evidence_flags = topology.evidence_flags;
                link->calibration_generation = topology.calibration_generation;
                link->topology_generation = topology.topology_generation;
                link->topology_crc32 = topology.topology_crc32;
                link->profile_crc32 = topology.profile_crc32;
                link->schedule_crc32 = topology.schedule_crc32;
                link->pio_persona = 1u;
                link->clkdiv_q16 = 1u;
                link->clk_sys_hz = 1u;
                link->instruction_period_ns = 4u;
                link->bit_cycles = 1u;
                link->marker_to_data_cycles = 1u;
                link->codeword_cycles = 1u;
                link->link_budget_cycles = 2u;
                link->sample_period_ns = 4u;
                link->link_base_delay_ns = 40u;
                link->marker_phase_delay_cycles = 10u;
                link->sck_phase_delay_cycles = 10u;
                link->data_phase_delay_cycles = 10u;
            }

            failed += expect_bool("reverse mask engine init",
                                  tdma_flight_engine_init(&engine), true);
            failed += expect_bool("reverse mask map configure",
                                  tdma_flight_engine_configure(&engine, &map),
                                  true);
            failed += expect_bool("reverse mask runtime init",
                                  tdma_ring_runtime_init(&runtime), true);
            failed += expect_bool("reverse mask runtime configure",
                                  tdma_ring_runtime_configure(&runtime,
                                                              &config),
                                  true);
            failed += expect_bool("reverse mask adapter init",
                                  tdma_pio_spi_ring_adapter_init(&adapter),
                                  true);
            failed += expect_bool(
                "measured topology loaded",
                tdma_pio_spi_ring_adapter_set_calibration_topology(
                    &adapter, &topology), true);
            tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                               loopback_tx,
                                               loopback_rx,
                                               &phys);
            tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
            failed += expect_bool("reverse mask bind",
                                  tdma_ring_runtime_bind_adapter(
                                      &runtime,
                                      tdma_pio_spi_ring_adapter_ops(),
                                      &adapter),
                                  true);
            failed += expect_bool("reverse mask start",
                                  start_ring_data(&runtime), true);
            failed += expect_bool("reverse mask snapshot",
                                  tdma_pio_spi_ring_adapter_get_snapshot(
                                      &adapter, &snapshot),
                                  true);
            failed += expect_u32("reverse expected segment mask",
                                 snapshot.receive_health.expected_segment_mask,
                                 expected_by_node[local_node]);
            failed += expect_u64("receive stale timeout is four rings",
                                 adapter.receive_health.config.stale_timeout_ns,
                                 (uint64_t)config.feedback_timeout_ns * 4ull);
        }
    }

    /* --- Fixed-offset follower flight processing. The complete TX image is
     * acquired once at frame boundary and shared by all local write slices. */
    {
        tdma_ring_runtime_t runtime;
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_pio_spi_ring_adapter_snapshot_t adapter_snapshot;
        tdma_flight_fifo_t fifo;
        tdma_flight_engine_t engine;
        tdma_flight_rx_view_t rx_view;
        loopback_phys_t phys;
        tdma_ring_runtime_config_t config = make_valid_config();
        tdma_process_image_map_t map = make_flight_map();
        uint8_t incoming[64];
        uint8_t tx_mailbox[32];
        uint8_t incoming_packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t incoming_packet_size = 0u;
        tdma_transport_result_t transport_result = TDMA_TRANSPORT_OK;
        tdma_transport_frame_view_t forwarded_view;

        memset(&phys, 0, sizeof(phys));
        memset(incoming, 0x10, sizeof(incoming));
        memset(tx_mailbox, 0x80, sizeof(tx_mailbox));
        incoming[0] = 0x52u;
        incoming[1] = 0x46u;
        incoming[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] =
            TDMA_FLIGHT_MAILBOX_VERSION;
        incoming[4] = 0u;
        incoming[5] = 1u << 1u;
        incoming[6] = 1u;
        incoming[7] = 0u;
        phys.suppress_echo = true;
        config.local_slot_id = 1u;
        config.reference_slot_id = 0u;
        tdma_transport_frame_build_t build = {
            .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id = 0u,
            .transport_sequence = 41u,
            .payload_class = TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
            .flags = TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK |
                     TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE,
            .schedule_crc32 = config.schedule_crc32,
            .ring_profile_crc32 = config.ring_profile_crc32,
            .hop_limit = 2u,
            .payload = incoming,
            .payload_size = sizeof(incoming),
        };

        failed += expect_bool("flight engine init",
                              tdma_flight_engine_init(&engine), true);
        failed += expect_bool("flight map configure",
                              tdma_flight_engine_configure(&engine, &map), true);
        failed += expect_bool("flight fifo init",
                              tdma_flight_fifo_init(&fifo), true);
        failed += expect_bool("flight forward runtime init",
                              tdma_ring_runtime_init(&runtime), true);
        failed += expect_bool("flight forward config",
                              tdma_ring_runtime_configure(&runtime, &config), true);
        failed += expect_bool("flight forward adapter init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        set_test_sequential_topology(&adapter, config.node_count);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_flight_fifo(&adapter, &fifo);
        tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
        failed += expect_bool("flight forward bind",
                              tdma_ring_runtime_bind_adapter(
                                  &runtime,
                                  tdma_pio_spi_ring_adapter_ops(),
                                  &adapter),
                              true);
        failed += expect_bool("flight forward start",
                              start_ring_data(&runtime), true);
        failed += expect_bool("flight adapter start snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &adapter_snapshot),
                              true);
        failed += expect_u32("flight map configured",
                             adapter_snapshot.flight_map_configured, 1u);
        failed += expect_u32("flight map active",
                             adapter_snapshot.flight_map_active, 1u);
        failed += expect_u32("flight map generation",
                             adapter_snapshot.flight_map_generation, 1u);
        failed += expect_bool("flight map reconfigure while active rejected",
                              tdma_flight_engine_configure(&engine, &map), false);
        failed += expect_bool("flight tx image publish",
                              tdma_flight_fifo_core0_publish_tx(
                                  &fifo,
                                  tx_mailbox,
                                  sizeof(tx_mailbox),
                                  7u,
                                  40u,
                                  1u << 1u),
                              true);
        failed += expect_bool("flight incoming encode",
                              tdma_transport_frame_encode(&build,
                                                          incoming_packet,
                                                          sizeof(incoming_packet),
                                                          &incoming_packet_size,
                                                          &transport_result),
                              true);
        failed += expect_bool("flight inject",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter,
                                  incoming_packet,
                                  incoming_packet_size,
                                  4100ull),
                              true);
        tdma_ring_runtime_service(&runtime);
        failed += expect_bool("flight forwarded decode",
                              tdma_transport_frame_decode(phys.last_tx,
                                                          phys.last_tx_size,
                                                          &forwarded_view,
                                                          &transport_result),
                              true);
        failed += expect_u32("flight forwarded hop",
                             forwarded_view.hop_count, 1u);
        failed += expect_bool("flight input segment unchanged",
                              memcmp(forwarded_view.payload, incoming, 32u) == 0,
                              true);
        failed += expect_bool("flight local slot replaced",
                              memcmp(forwarded_view.payload + 32u,
                                     tx_mailbox,
                                     32u) == 0,
                              true);
        if (tdma_flight_fifo_core0_acquire_rx(&fifo, &rx_view)) {
            failed += expect_u32("flight input mask",
                                 rx_view.segment_mask, 1u << 0u);
            failed += expect_bool("flight input mirror original",
                                  memcmp(rx_view.data,
                                         incoming,
                                         sizeof(incoming)) == 0,
                                  true);
            failed += expect_bool("flight input release",
                                  tdma_flight_fifo_core0_release_rx(
                                      &fifo, rx_view.slot_index),
                                  true);
        } else {
            failed += expect_bool("flight input acquire", false, true);
        }

        failed += expect_bool("flight stale inject",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter,
                                  incoming_packet,
                                  incoming_packet_size,
                                  4200ull),
                              true);
        tdma_ring_runtime_service(&runtime);
        failed += expect_bool("flight adapter snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &adapter_snapshot),
                              true);
        failed += expect_u32("flight map applied twice",
                             adapter_snapshot.flight_map_apply_count, 2u);
        failed += expect_u32("flight stale generation reused",
                             adapter_snapshot.flight_tx_stale_reuse_count, 1u);
        failed += expect_u32("flight output byte accounting",
                             adapter_snapshot.flight_output_bytes, 64u);

        tdma_transport_frame_build_t short_build = build;
        short_build.payload_size = sizeof(incoming) - 1u;
        failed += expect_bool("flight short payload encode",
                              tdma_transport_frame_encode(&short_build,
                                                          incoming_packet,
                                                          sizeof(incoming_packet),
                                                          &incoming_packet_size,
                                                          &transport_result),
                              true);
        failed += expect_bool("flight short payload inject",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter,
                                  incoming_packet,
                                  incoming_packet_size,
                                  4300ull),
                              true);
        tdma_ring_runtime_service(&runtime);
        failed += expect_bool("flight short payload forwarded",
                              tdma_transport_frame_decode(phys.last_tx,
                                                          phys.last_tx_size,
                                                          &forwarded_view,
                                                          &transport_result),
                              true);
        failed += expect_bool("flight short payload unchanged",
                              memcmp(forwarded_view.payload,
                                     incoming,
                                     sizeof(incoming) - 1u) == 0,
                              true);
        (void)tdma_pio_spi_ring_adapter_get_snapshot(&adapter,
                                                     &adapter_snapshot);
        failed += expect_u32("flight length reject count",
                             adapter_snapshot.flight_length_reject_count, 1u);
        failed += expect_u32("flight map reject evidence",
                             adapter_snapshot.last_error,
                             TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
        failed += expect_u32("flight active reconfigure reject count",
                             adapter_snapshot.flight_map_reject_count, 1u);

        for (uint32_t i = 0u; i <= TDMA_FLIGHT_RX_FRAME_SLOT_COUNT; i++) {
            incoming[6] = (uint8_t)(i + 2u);
            build.transport_sequence = 42u + i;
            failed += expect_bool("flight rx fill encode",
                                  tdma_transport_frame_encode(
                                      &build,
                                      incoming_packet,
                                      sizeof(incoming_packet),
                                      &incoming_packet_size,
                                      &transport_result),
                                  true);
            failed += expect_bool("flight rx fill inject",
                                  tdma_pio_spi_ring_adapter_inject_rx(
                                      &adapter,
                                      incoming_packet,
                                      incoming_packet_size,
                                      4400ull + i),
                                  true);
            tdma_ring_runtime_service(&runtime);
        }
        tdma_flight_fifo_snapshot_t fifo_snapshot;
        failed += expect_bool("flight fifo full snapshot",
                              tdma_flight_fifo_get_snapshot(&fifo,
                                                            &fifo_snapshot),
                              true);
        failed += expect_u32("flight rx mirror full drops",
                             fifo_snapshot.rx_mirror_drop_count, 1u);
        failed += expect_bool("flight release one before retry",
                              tdma_flight_fifo_core0_acquire_rx(&fifo,
                                                                &rx_view),
                              true);
        failed += expect_bool("flight release retry space",
                              tdma_flight_fifo_core0_release_rx(
                                  &fifo, rx_view.slot_index),
                              true);
        build.transport_sequence++;
        failed += expect_bool("flight retry encode",
                              tdma_transport_frame_encode(
                                  &build,
                                  incoming_packet,
                                  sizeof(incoming_packet),
                                  &incoming_packet_size,
                                  &transport_result),
                              true);
        failed += expect_bool("flight retry uncommitted mailbox",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter,
                                  incoming_packet,
                                  incoming_packet_size,
                                  4500ull),
                              true);
        tdma_ring_runtime_service(&runtime);
        bool retried_seq_seen = false;
        while (tdma_flight_fifo_core0_acquire_rx(&fifo, &rx_view)) {
            if (rx_view.data != NULL && rx_view.data_size == sizeof(incoming) &&
                rx_view.data[6] == incoming[6]) {
                retried_seq_seen = true;
            }
            failed += expect_bool("flight drain retry queue",
                                  tdma_flight_fifo_core0_release_rx(
                                      &fifo, rx_view.slot_index),
                                  true);
        }
        failed += expect_bool("flight retry delivered after fifo space",
                              retried_seq_seen,
                              true);
        failed += expect_u32("flight forward survives rx full",
                             adapter.forward_count, 9u);
        (void)tdma_pio_spi_ring_adapter_get_snapshot(&adapter,
                                                     &adapter_snapshot);
        failed += expect_u32("flight successful map count",
                             adapter_snapshot.flight_map_apply_count, 8u);
        failed += expect_u32("flight successful stale count",
                             adapter_snapshot.flight_tx_stale_reuse_count, 7u);
        failed += expect_u32("flight accumulated output bytes",
                             adapter_snapshot.flight_output_bytes, 256u);
        tdma_flight_engine_snapshot_t engine_snapshot;
        failed += expect_bool("flight bitmap snapshot",
                              tdma_flight_engine_get_snapshot(
                                  &engine, &engine_snapshot),
                              true);
        failed += expect_u32("flight bitmap scans",
                             engine_snapshot.rx_bitmap_scan_count, 8u);
        failed += expect_u32("flight bitmap commits",
                             engine_snapshot.rx_bitmap_hit_count, 6u);
        failed += expect_u32("flight bitmap duplicates",
                             engine_snapshot.rx_bitmap_duplicate_count, 1u);
    }

    /* --- A follower correlates the fixed DPLL trailer in process frame N+1
     * with its cached local RX latch for process frame N. --- */
    {
        tdma_pio_spi_ring_adapter_t adapter;
        tdma_pio_spi_ring_adapter_snapshot_t snapshot;
        loopback_phys_t phys;
        tdma_ring_runtime_config_t config = make_valid_config();
        uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
        size_t packet_size = 0u;
        tdma_transport_result_t result = TDMA_TRANSPORT_OK;
        tdma_transport_frame_view_t view;

        memset(&phys, 0, sizeof(phys));
        phys.suppress_echo = true;
        config.local_slot_id = 1u;
        failed += expect_bool("clock follower init",
                              tdma_pio_spi_ring_adapter_init(&adapter), true);
        set_test_sequential_topology(&adapter, config.node_count);
        tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                           loopback_tx,
                                           loopback_rx,
                                           &phys);
        tdma_pio_spi_ring_adapter_set_timestamp_metadata(
            &adapter, 8u, TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
        failed += expect_bool("clock follower start",
                              tdma_pio_spi_ring_adapter_ops()->start(
                                  &adapter, &config), true);

        uint8_t payload[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE];
        tdma_flight_engine_fill_alignment_symbols(payload, sizeof(payload));
        test_put_u32_le(
            &payload[TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET], 0u);
        tdma_transport_frame_build_t build = {
            .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id = 0u,
            .transport_sequence = 1u,
            .payload_class = TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
            .flags = TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK |
                     TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE,
            .schedule_crc32 = config.schedule_crc32,
            .ring_profile_crc32 = config.ring_profile_crc32,
            .hop_limit = config.node_count - 1u,
            .payload = payload,
            .payload_size = sizeof(payload),
        };
        failed += expect_bool("clock source frame encode",
                              tdma_transport_frame_encode(
                                  &build, packet, sizeof(packet), &packet_size,
                                  &result), true);
        failed += expect_bool("clock source frame decode",
                              tdma_transport_frame_decode(
                                  packet, packet_size, &view, &result), true);
        const uint32_t source_frame_crc32 = view.identity_crc32;
        failed += expect_bool("clock source frame inject",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, packet, packet_size, 2000100ull),
                              true);
        tdma_ring_adapter_status_t status;
        failed += expect_bool("clock source frame service",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 1000ull, &status), true);

        test_put_u32_le(
            &payload[TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET],
            tdma_process_image_dpll_observation_encode_phase(
                2000000ull, config.cycle_period_ns));
        build.transport_sequence = 2u;
        failed += expect_bool("clock evidence frame encode",
                              tdma_transport_frame_encode(
                                  &build, packet, sizeof(packet), &packet_size,
                                  &result), true);
        failed += expect_bool("clock evidence frame inject",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, packet, packet_size, 2002100ull),
                              true);
        failed += expect_bool("clock evidence frame service",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 2000ull, &status), true);
        failed += expect_bool("clock follower snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &snapshot), true);
        failed += expect_u32("clock observation valid",
                             snapshot.clock_observation.valid, 1u);
        failed += expect_u32("clock observation sequence",
                             snapshot.clock_observation.correlated_sequence,
                             1u);
        failed += expect_u32("clock observation frame crc",
                             snapshot.clock_observation.frame_crc32,
                             source_frame_crc32);
        failed += expect_u64("clock observation reference tx",
                             snapshot.clock_observation
                                 .reference_tx_timestamp_ns,
                             2000000ull);
        failed += expect_u64("clock observation local rx",
                             snapshot.clock_observation.local_rx_timestamp_ns,
                             2000100ull);
        failed += expect_u32("clock observation resolution",
                             snapshot.clock_observation
                                 .timestamp_resolution_ns,
                             8u);
        failed += expect_u32("clock observation count",
                             snapshot.clock_observation_count, 1u);
        failed += expect_u32("clock observation rejects",
                             snapshot.clock_observation_reject_count, 0u);
        failed += expect_u32("clock observation reject reason",
                             snapshot.clock_observation_last_reject_reason,
                             0u);

        test_put_u32_le(
            &payload[TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET],
            tdma_process_image_dpll_observation_encode_phase(
                4000000000ull, config.cycle_period_ns));
        build.transport_sequence = 3u;
        failed += expect_bool("clock epoch frame encode",
                              tdma_transport_frame_encode(
                                  &build, packet, sizeof(packet), &packet_size,
                                  &result), true);
        failed += expect_bool("clock epoch frame inject",
                              tdma_pio_spi_ring_adapter_inject_rx(
                                  &adapter, packet, packet_size, 2003100ull),
                              true);
        failed += expect_bool("clock epoch frame service",
                              tdma_pio_spi_ring_adapter_ops()->service(
                                  &adapter, 3000ull, &status), true);
        failed += expect_bool("clock epoch snapshot",
                              tdma_pio_spi_ring_adapter_get_snapshot(
                                  &adapter, &snapshot), true);
        failed += expect_u32("clock epoch observation count",
                             snapshot.clock_observation_count, 2u);
        failed += expect_u32("clock epoch reject count",
                             snapshot.clock_observation_reject_count, 0u);
        failed += expect_u32("clock epoch reject reason",
                             snapshot.clock_observation_last_reject_reason,
                             0u);
        failed += expect_u64("clock epoch mapped reference",
                             snapshot.clock_observation
                                 .reference_tx_timestamp_ns,
                             2002000ull);
    }

    /* --- Compact DPLL phase mapping is independent of reference/follower
     * boot epochs and the trailer closes the fixed SHORT process image. --- */
    {
        const uint32_t cycle_period_ns = 1000000u;
        const uint64_t reference_tx = 4000123000ull;
        const uint64_t local_rx = 2000456000ull;
        uint64_t decoded_reference_tx = 0ull;
        failed += expect_u32("Node image bytes",
                             TDMA_FLIGHT_NODE_IMAGE_SIZE, 256u);
        failed += expect_u32("fixed process payload bytes",
                             TDMA_FLIGHT_SHORT_PAYLOAD_SIZE, 260u);
        failed += expect_bool(
            "DPLL observation phase map",
            tdma_process_image_dpll_observation_map_phase(
                tdma_process_image_dpll_observation_encode_phase(
                    reference_tx, cycle_period_ns),
                cycle_period_ns,
                local_rx,
                &decoded_reference_tx),
            true);
        failed += expect_u64("DPLL observation mapped timestamp",
                             decoded_reference_tx, 2000123000ull);
    }

    /* --- Enabling DPLL evidence changes only the fixed trailer value.  The
     * product frame class, flags, length and sequence stay identical. --- */
    {
        uint32_t observed_class[2] = {0u, 0u};
        uint32_t observed_flags[2] = {0u, 0u};
        uint32_t observed_size[2] = {0u, 0u};
        uint32_t observed_sequence[2] = {0u, 0u};
        uint32_t observed_trailer[2] = {0u, 0u};
        for (uint32_t enabled = 0u; enabled < 2u; enabled++) {
            tdma_pio_spi_ring_adapter_t adapter;
            tdma_flight_engine_t engine;
            loopback_phys_t phys;
            tdma_ring_adapter_status_t status;
            tdma_ring_runtime_config_t config = make_valid_config();
            tdma_process_image_map_t map = make_eight_slot_flight_map();
            tdma_transport_frame_view_t view;
            tdma_transport_result_t result = TDMA_TRANSPORT_OK;
            memset(&phys, 0, sizeof(phys));
            phys.suppress_echo = true;
            phys.tx_timestamp_ns = 4000000ull;
            failed += expect_bool("fixed load adapter init",
                                  tdma_pio_spi_ring_adapter_init(&adapter),
                                  true);
            set_test_sequential_topology(&adapter, config.node_count);
            failed += expect_bool("fixed load engine init",
                                  tdma_flight_engine_init(&engine), true);
            failed += expect_bool("fixed load map config",
                                  tdma_flight_engine_configure(&engine, &map),
                                  true);
            tdma_pio_spi_ring_adapter_set_phys(&adapter,
                                               loopback_tx,
                                               loopback_rx,
                                               &phys);
            tdma_pio_spi_ring_adapter_set_flight_engine(&adapter, &engine);
            tdma_pio_spi_ring_adapter_set_timestamp_metadata(
                &adapter, 4u, TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED);
            failed += expect_bool(
                "fixed load forwarding mode",
                tdma_pio_spi_ring_adapter_set_forwarding_mode(
                    &adapter,
                    TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE),
                true);
            failed += expect_bool(
                "fixed load evidence setting",
                tdma_pio_spi_ring_adapter_set_clock_evidence_enabled(
                    &adapter, enabled != 0u),
                true);
            failed += expect_bool("fixed load start",
                                  tdma_pio_spi_ring_adapter_ops()->start(
                                      &adapter, &config),
                                  true);
            for (uint32_t tick = 0u;
                 tick < 8u && adapter.tx_count < 2u;
                 tick++) {
                (void)tdma_pio_spi_ring_adapter_ops()->service(
                    &adapter, (uint64_t)tick * 2000ull, &status);
            }
            failed += expect_bool("fixed load decode",
                                  tdma_transport_frame_decode(
                                      phys.last_tx,
                                      phys.last_tx_size,
                                      &view,
                                      &result),
                                  true);
            observed_class[enabled] = view.payload_class;
            observed_flags[enabled] = view.flags;
            observed_size[enabled] = view.payload_size;
            observed_sequence[enabled] = view.transport_sequence;
            observed_trailer[enabled] = test_get_u32_le(
                &view.payload[TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_OFFSET]);
        }
        failed += expect_u32("DPLL fixed frame class",
                             observed_class[1], observed_class[0]);
        failed += expect_u32("DPLL fixed frame flags",
                             observed_flags[1], observed_flags[0]);
        failed += expect_u32("DPLL fixed frame size",
                             observed_size[1], observed_size[0]);
        failed += expect_u32("DPLL fixed frame sequence",
                             observed_sequence[1], observed_sequence[0]);
        failed += expect_u32("DPLL disabled trailer",
                             observed_trailer[0], 0u);
        failed += expect_bool("DPLL enabled trailer valid",
                              (observed_trailer[1] &
                               TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_VALID_MASK) !=
                                  0u,
                              true);
    }

    if (failed != 0) {
        return 1;
    }
    puts("tdma_pio_spi_ring_adapter tests passed");
    return 0;
}
