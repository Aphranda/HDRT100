#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdma_pio_spi_ring_adapter.h"
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
                             snapshot.simultaneous_feedback_loop_evidence, 0u);

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
        failed += expect_u32("adapter last error",
                             adapter.last_error,
                             TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME);

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
        const tdma_transport_frame_build_t build = {
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
        incoming[2] = 1u;
        incoming[4] = 0u;
        incoming[5] = 1u << 1u;
        incoming[6] = 1u;
        incoming[7] = 0u;
        phys.suppress_echo = true;
        config.local_slot_id = 1u;
        config.reference_slot_id = 0u;
        const tdma_transport_frame_build_t build = {
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

        failed += expect_bool("flight restore payload encode",
                              tdma_transport_frame_encode(&build,
                                                          incoming_packet,
                                                          sizeof(incoming_packet),
                                                          &incoming_packet_size,
                                                          &transport_result),
                              true);
        for (uint32_t i = 0u; i <= TDMA_FLIGHT_RX_FRAME_SLOT_COUNT; i++) {
            incoming[6] = (uint8_t)(i + 2u);
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

    if (failed != 0) {
        return 1;
    }
    puts("tdma_pio_spi_ring_adapter tests passed");
    return 0;
}
