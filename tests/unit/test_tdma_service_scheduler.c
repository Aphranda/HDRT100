#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdma_service.h"

typedef struct {
    uint32_t count;
    uint8_t marker[8];
} mock_adapter_t;

static bool mock_transmit(void *context,
                          const uint8_t *frame,
                          size_t frame_size,
                          tdma_service_role_t role,
                          uint32_t baud_hz,
                          const tdma_service_pin_config_t *pins,
                          uint32_t deadline_us,
                          tdma_service_exec_status_t *status)
{
    (void)role;
    (void)baud_hz;
    (void)pins;
    (void)deadline_us;
    mock_adapter_t *adapter = (mock_adapter_t *)context;
    if (adapter == NULL || frame == NULL || frame_size == 0u ||
        status == NULL || adapter->count >= 8u) {
        return false;
    }
    adapter->marker[adapter->count++] = frame[0];
    status->result = tdma_service_EXEC_TX_OK;
    status->frame_size = frame_size;
    status->timestamp_source = tdma_service_TIMESTAMP_SOURCE_HARDWARE_TICK;
    status->timestamp_resolution_ns = frame[0];
    status->timestamp_flags = 0u;
    return true;
}

static bool mock_receive(void *context,
                         uint8_t *frame,
                         size_t frame_capacity,
                         tdma_service_role_t role,
                         uint32_t baud_hz,
                         const tdma_service_pin_config_t *pins,
                         uint32_t deadline_us,
                         tdma_service_exec_status_t *status)
{
    (void)context;
    (void)frame;
    (void)frame_capacity;
    (void)role;
    (void)baud_hz;
    (void)pins;
    (void)deadline_us;
    (void)status;
    return false;
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

static bool register_payload(tdma_service_service_t *service,
                             uint32_t payload_class)
{
    const tdma_service_payload_binding_t binding = {
        .used = 1u,
        .producer_id = payload_class,
        .consumer_id = 1u,
        .payload_class = payload_class,
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .max_payload_size = 128u,
    };
    return tdma_service_register_payload(service, &binding);
}

static tdma_service_intent_config_t make_intent(uint32_t payload_class,
                                                 uint8_t *frame)
{
    const tdma_service_intent_config_t intent = {
        .deadline_us = 1000u,
        .role = TDMA_SERVICE_ROLE_MASTER,
        .baud_hz = 10000000u,
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .payload_class = payload_class,
        .scheduled_window_valid = 1u,
        .scheduled_window_class = TDMA_SERVICE_WINDOW_CLASS_VDC_OBSERVATION,
        .schedule_crc32 = 0x12345678u,
        .scheduled_window_start_ns = 0u,
        .scheduled_window_end_ns = 10000000ull,
        .scheduled_guard_start_ns = 0u,
        .scheduled_guard_end_ns = 10000000ull,
        .frame = frame,
        .frame_size = 64u,
    };
    return intent;
}

typedef struct {
    uint32_t started;
    uint32_t service_count;
    uint32_t marker;
} mock_ring_adapter_t;

static bool mock_ring_start(void *context,
                            const tdma_ring_runtime_config_t *config)
{
    mock_ring_adapter_t *adapter = (mock_ring_adapter_t *)context;
    if (adapter == NULL || config == NULL || config->enabled == 0u) {
        return false;
    }
    adapter->started = 1u;
    return true;
}

static void mock_ring_stop(void *context)
{
    mock_ring_adapter_t *adapter = (mock_ring_adapter_t *)context;
    if (adapter != NULL) {
        adapter->started = 0u;
    }
}

static bool mock_ring_service(void *context,
                              uint64_t now_ns,
                              tdma_ring_adapter_status_t *status)
{
    mock_ring_adapter_t *adapter = (mock_ring_adapter_t *)context;
    (void)now_ns;
    if (adapter == NULL || status == NULL || adapter->started == 0u) {
        return false;
    }
    adapter->service_count++;
    status->up_configured = 1u;
    status->down_configured = 1u;
    status->up_running = 1u;
    status->down_running = 1u;
    status->up_tx_sequence = adapter->marker;
    status->down_rx_sequence = adapter->marker;
    return true;
}

static const tdma_ring_adapter_ops_t s_mock_spi_ring_ops = {
    .start = mock_ring_start,
    .stop = mock_ring_stop,
    .service = mock_ring_service,
};

static const tdma_ring_adapter_ops_t s_mock_bissc_ring_ops = {
    .start = mock_ring_start,
    .stop = mock_ring_stop,
    .service = mock_ring_service,
};

int main(void)
{
    int failed = 0;
    tdma_service_service_t service;
    tdma_traffic_scheduler_t scheduler;
    tdma_traffic_scheduler_slot_t slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
    tdma_foundation_profile_t profile;
    tdma_service_snapshot_t snapshot;
    tdma_flight_fifo_snapshot_t flight_snapshot;
    mock_adapter_t adapter = {0};
    const tdma_service_ops_t ops = {
        .transmit = mock_transmit,
        .receive = mock_receive,
    };
    uint8_t config_frame[64] = {0xC1u};
    uint8_t refmem_frame[64] = {0xB1u};
    uint8_t vdc_frame[64] = {0xA1u};

    failed += expect_u32("service init", tdma_service_init(&service), 1u);
    failed += expect_u32(
        "scheduler init",
        tdma_traffic_scheduler_init(&scheduler,
                                    slots,
                                    TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT),
        1u);
    failed += expect_u32("bind scheduler",
                         tdma_service_bind_traffic_scheduler(&service,
                                                             &scheduler),
                         1u);
    failed += expect_u32("bind ops",
                         tdma_service_bind_ops(&service, &ops, &adapter),
                         1u);
    failed += expect_u32("register config",
                         register_payload(
                             &service, TDMA_PAYLOAD_CLASS_CONFIG_CONTROL),
                         1u);
    failed += expect_u32("register refmem",
                         register_payload(
                             &service, TDMA_PAYLOAD_CLASS_REFMEM_DELTA),
                         1u);
    failed += expect_u32("register vdc",
                         register_payload(
                             &service, TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE),
                         1u);
    (void)tdma_foundation_profile_default(
        &profile, 1u, 0u, 0u, TDMA_ADAPTER_PIO_SPI);
    failed += expect_u32("configure profile",
                         tdma_service_configure_foundation_profile(
                             &service, &profile, 0x12345678u),
                         1u);

    tdma_service_intent_config_t config = make_intent(
        TDMA_PAYLOAD_CLASS_CONFIG_CONTROL, config_frame);
    tdma_service_intent_config_t refmem = make_intent(
        TDMA_PAYLOAD_CLASS_REFMEM_DELTA, refmem_frame);
    refmem.scheduled_window_class = TDMA_SERVICE_WINDOW_CLASS_REFMEM_DATA;
    tdma_service_intent_config_t vdc = make_intent(
        TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE, vdc_frame);
    failed += expect_u32("queue config",
                         tdma_service_submit_tx(&service, &config),
                         1u);
    failed += expect_u32("queue refmem",
                         tdma_service_submit_tx(&service, &refmem),
                         1u);
    failed += expect_u32("queue vdc",
                         tdma_service_submit_tx(&service, &vdc),
                         1u);

    tdma_service_core1_service(&service);
    tdma_service_core1_service(&service);
    failed += expect_u32("maintenance remains gated", adapter.count, 2u);
    failed += expect_u32("open maintenance gate",
                         tdma_service_set_maintenance_gate(&service, true),
                         1u);
    tdma_service_core1_service(&service);
    failed += expect_u32("three frames executed", adapter.count, 3u);
    failed += expect_u32("vdc first", adapter.marker[0], 0xA1u);
    failed += expect_u32("refmem second", adapter.marker[1], 0xB1u);
    failed += expect_u32("config third", adapter.marker[2], 0xC1u);

    failed += expect_u32("snapshot",
                         tdma_service_get_snapshot(&service, &snapshot),
                         1u);
    service.intent_guard |= 1u;
    failed += expect_u32("odd intent guard is bounded",
                         tdma_service_get_snapshot(&service, &snapshot),
                         0u);
    service.intent_guard++;
    service.result_guard |= 1u;
    failed += expect_u32("odd result guard is bounded",
                         tdma_service_get_snapshot(&service, &snapshot),
                         0u);
    service.result_guard++;
    failed += expect_u32("snapshot recovers after guard closes",
                         tdma_service_get_snapshot(&service, &snapshot),
                         1u);
    failed += expect_u32("flight snapshot",
                         tdma_service_get_flight_fifo_snapshot(&service,
                                                               &flight_snapshot),
                         1u);
    failed += expect_u32("flight fifo version",
                         flight_snapshot.version,
                         TDMA_FLIGHT_FIFO_VERSION);
    failed += expect_u32("queue drained",
                         snapshot.traffic_scheduler_queued_count,
                         0u);
    failed += expect_u32("vdc completion token",
                         snapshot.traffic_scheduler_completed_seq[
                             TDMA_TRAFFIC_VDC_REALTIME],
                         3u);
    failed += expect_u32("refmem completion token",
                         snapshot.traffic_scheduler_completed_seq[
                             TDMA_TRAFFIC_REFMEM_REALTIME],
                         2u);
    failed += expect_u32("config completion token",
                         snapshot.traffic_scheduler_completed_seq[
                             TDMA_TRAFFIC_CONFIG_CONTROL],
                         1u);
    failed += expect_u32("vdc completion metadata retained",
                         snapshot.traffic_class_timestamp_resolution_ns[
                             TDMA_TRAFFIC_VDC_REALTIME],
                         0xA1u);
    failed += expect_u32("refmem completion metadata retained",
                         snapshot.traffic_class_timestamp_resolution_ns[
                             TDMA_TRAFFIC_REFMEM_REALTIME],
                         0xB1u);
    failed += expect_u32("config completion metadata retained",
                         snapshot.traffic_class_timestamp_resolution_ns[
                             TDMA_TRAFFIC_CONFIG_CONTROL],
                         0xC1u);

    /* --- HAOFV adapter boundary: registered implementations are selected by
     * the active profile adapter_type; unregistered types unbind. --- */
    {
        mock_ring_adapter_t spi_ring = {.marker = 0x11u};
        mock_ring_adapter_t bissc_ring = {.marker = 0x22u};
        tdma_ring_runtime_snapshot_t ring_snap;

        failed += expect_u32("register spi impl",
                             tdma_service_register_adapter_impl(
                                 &service,
                                 TDMA_ADAPTER_PIO_SPI,
                                 &s_mock_spi_ring_ops,
                                 &spi_ring),
                             1u);
        failed += expect_u32("register bissc impl",
                             tdma_service_register_adapter_impl(
                                 &service,
                                 TDMA_ADAPTER_BISS_C,
                                 &s_mock_bissc_ring_ops,
                                 &bissc_ring),
                             1u);

        /* PIO SPI profile binds the SPI implementation. */
        (void)tdma_foundation_profile_default(
            &profile, 1u, 0u, 0u, TDMA_ADAPTER_PIO_SPI);
        failed += expect_u32("configure pio spi profile",
                             tdma_service_configure_foundation_profile(
                                 &service, &profile, 0x12345678u),
                             1u);
        failed += expect_u32("arm pio spi ring",
                             tdma_service_ring_arm(&service), 1u);
        tdma_service_core1_service(&service);
        failed += expect_u32("start pio spi ring",
                             tdma_service_ring_start(&service), 1u);
        tdma_service_core1_service(&service);
        failed += expect_u32("ring runtime adapter bound (spi)",
                             tdma_ring_runtime_get_snapshot(
                                 &service.ring_runtime, &ring_snap),
                             1u);
        failed += expect_u32("spi adapter started",
                             ring_snap.adapter_started, 1u);
        failed += expect_u32("spi adapter running",
                             ring_snap.up_running |
                                 ring_snap.down_running,
                             1u);
        failed += expect_u32("spi adapter marker",
                             ring_snap.up_tx_sequence, 0x11u);
        failed += expect_u32("spi impl context serviced",
                             spi_ring.service_count, 1u);

        /* BISS-C profile switches to the BISS-C implementation. */
        (void)tdma_foundation_profile_default(
            &profile, 1u, 0u, 0u, TDMA_ADAPTER_BISS_C);
        failed += expect_u32("configure bissc profile",
                             tdma_service_configure_foundation_profile(
                                 &service, &profile, 0x12345678u),
                             1u);
        failed += expect_u32("arm bissc ring",
                             tdma_service_ring_arm(&service), 1u);
        tdma_service_core1_service(&service);
        failed += expect_u32("start bissc ring",
                             tdma_service_ring_start(&service), 1u);
        tdma_service_core1_service(&service);
        (void)tdma_ring_runtime_get_snapshot(&service.ring_runtime, &ring_snap);
        failed += expect_u32("bissc adapter marker",
                             ring_snap.up_tx_sequence, 0x22u);
        failed += expect_u32("bissc impl context serviced",
                             bissc_ring.service_count, 1u);
        failed += expect_u32("spi impl stopped after switch",
                             spi_ring.started, 0u);

        /* UART is not registered: the ring runtime unbinds and reports
         * ADAPTER_MISSING instead of running the wrong transport. */
        (void)tdma_foundation_profile_default(
            &profile, 1u, 0u, 0u, TDMA_ADAPTER_UART);
        failed += expect_u32("configure uart profile",
                             tdma_service_configure_foundation_profile(
                                 &service, &profile, 0x12345678u),
                             1u);
        failed += expect_u32("arm uart ring",
                             tdma_service_ring_arm(&service), 1u);
        tdma_service_core1_service(&service);
        (void)tdma_ring_runtime_get_snapshot(&service.ring_runtime, &ring_snap);
        failed += expect_u32("unregistered adapter reports missing",
                             ring_snap.last_reason,
                             TDMA_RING_RUNTIME_REASON_ADAPTER_MISSING);
        failed += expect_u32("unregistered adapter not running",
                             ring_snap.up_running | ring_snap.down_running,
                             0u);
    }

    if (failed != 0) {
        return 1;
    }
    puts("tdma_service_scheduler tests passed");
    return 0;
}
