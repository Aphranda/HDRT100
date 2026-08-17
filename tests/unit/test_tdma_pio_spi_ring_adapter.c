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
} loopback_phys_t;

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
        rx_timestamp_ns == NULL || phys->last_tx_size == 0u ||
        phys->last_tx_size > packet_capacity) {
        return false;
    }
    memcpy(packet, phys->last_tx, phys->last_tx_size);
    *packet_size = phys->last_tx_size;
    *rx_timestamp_ns = phys->rx_timestamp_ns;
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
        /* 1 us cycle: with the host runtime advancing now_ns by 1 us per
         * service round, the reference node sends one beacon per round. */
        .feedback_timeout_ns = 1000u,
    };
    return config;
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

        /* Second cycle advances sequence and counters. */
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
        tdma_ring_runtime_service(&runtime);

        failed += expect_bool("adapter snapshot after service",
                              tdma_pio_spi_ring_adapter_get_snapshot(&adapter,
                                                                     &snap),
                              true);
        failed += expect_u32("started", snap.started, 1u);
        failed += expect_u32("adapter service count", snap.service_count, 1u);
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

        /* Recover: echo the current TX frame again. */
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

        /* First service: no frame received yet, nothing to forward. */
        tdma_ring_runtime_service(&runtime);
        (void)tdma_ring_runtime_get_snapshot(&runtime, &snapshot);
        failed += expect_u32("forward node starts in evidence missing",
                             snapshot.last_reason,
                             TDMA_RING_RUNTIME_REASON_EVIDENCE_MISSING);

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
        failed += expect_u32("up running with phys ctrl", snapshot.up_running, 1u);

        tdma_ring_runtime_configure(&runtime, NULL);
        tdma_ring_runtime_service(&runtime);
        failed += expect_u32("phys disarm called on stop", disarm_calls, 1u);
    }

    if (failed != 0) {
        return 1;
    }
    puts("tdma_pio_spi_ring_adapter tests passed");
    return 0;
}
