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
                          uint32_t deadline_1e3ns,
                          tdma_service_exec_status_t *status)
{
    (void)role;
    (void)baud_hz;
    (void)pins;
    (void)deadline_1e3ns;
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
                         uint32_t deadline_1e3ns,
                         tdma_service_exec_status_t *status)
{
    (void)context;
    (void)frame;
    (void)frame_capacity;
    (void)role;
    (void)baud_hz;
    (void)pins;
    (void)deadline_1e3ns;
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
        .deadline_1e3ns = 1000u,
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

int main(void)
{
    int failed = 0;
    tdma_service_service_t service;
    tdma_traffic_scheduler_t scheduler;
    tdma_traffic_scheduler_slot_t slots[TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT];
    tdma_foundation_profile_t profile;
    tdma_service_snapshot_t snapshot;
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

    if (failed != 0) {
        return 1;
    }
    puts("tdma_service_scheduler tests passed");
    return 0;
}
