#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdma_service.h"
#include "tdma_service_timing.h"

static tdma_service_service_t service;
static uint64_t now_us;
static uint32_t clock_reads, tx_calls;
static uint32_t timing_reads;
static bool replace_during_copy;
static uint8_t transmitted;
static uint32_t ring_calls, ring_stops;

uint64_t vdc_timestamp_clock_read_ticks64(void)
{
    assert(++timing_reads < 64u);
    return now_us * 250u;
}

uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000u; }

uint64_t time_us_64(void)
{
    /* Time deliberately does not advance inside an action. A software
     * polling loop fails instead of silently walking into the test window. */
    assert(++clock_reads < 32u);
    return now_us;
}

static void *interrupted_copy(void *destination, const void *source, size_t size)
{
    void *result = memcpy(destination, source, size);
    if (replace_during_copy && source == service.frame) {
        replace_during_copy = false;
        service.intent_guard++;
        service.intent_seq++;
        service.frame[0] = 0xB2u;
        service.intent_guard++;
    }
    return result;
}

/* Compile the complete production implementation. Only the hardware clock
 * and one memcpy are intercepted to reproduce preemption deterministically. */
#define PICO_ON_DEVICE 1
#define memcpy interrupted_copy
#include "../../components/tdma/src/tdma_service.c"
#undef memcpy

static bool transmit(void *context, const uint8_t *frame, size_t size,
    tdma_service_role_t role, uint32_t baud, const tdma_service_pin_config_t *pins,
    uint32_t deadline, tdma_service_exec_status_t *status)
{
    (void)context; (void)role; (void)baud; (void)pins; (void)deadline;
    assert(size == 4u);
    tx_calls++;
    transmitted = frame[0];
    status->result = tdma_service_EXEC_TX_OK;
    status->frame_size = size;
    return true;
}

static bool receive(void *context, uint8_t *frame, size_t size,
    tdma_service_role_t role, uint32_t baud, const tdma_service_pin_config_t *pins,
    uint32_t deadline, tdma_service_exec_status_t *status)
{
    (void)context; (void)frame; (void)size; (void)role;
    (void)baud; (void)pins; (void)deadline; (void)status;
    assert(!"unexpected RX");
    return false;
}

static void tick(uint64_t time_us)
{
    now_us = time_us;
    clock_reads = 0u;
    timing_reads = 0u;
    tdma_service_timing_phase_begin();
    tdma_service_core1_service(&service);
    tdma_service_timing_phase_end();
    tdma_service_timing_snapshot_t timing;
    assert(tdma_service_timing_try_snapshot(&timing));
    assert(timing.last.invalid_count == 0u);
    assert(timing.last.calls[TDMA_TIMING_RING_RUNTIME] == 1u);
    assert(timing.last.calls[TDMA_TIMING_INTENT_DISPATCH] <= 1u);
    assert((service.result_guard & 1u) == 0u);
}

static void setup(bool window)
{
    const tdma_service_ops_t ops = {.transmit = transmit, .receive = receive};
    static tdma_service_ops_t retained_ops;
    retained_ops = ops;
    assert(tdma_service_init(&service));
    assert(tdma_service_bind_ops(&service, &retained_ops, NULL));
    const tdma_service_payload_binding_t binding = {
        .used = 1u, .producer_id = 1u, .consumer_id = 1u,
        .payload_class = TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE,
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT, .max_payload_size = 4u};
    assert(tdma_service_register_payload(&service, &binding));
    const uint8_t frame[] = {0xA1u, 2u, 3u, 4u};
    const tdma_service_intent_config_t intent = {
        .role = TDMA_SERVICE_ROLE_MASTER, .baud_hz = 10000000u,
        .deadline_us = 1000u, .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .payload_class = TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE,
        .scheduled_window_valid = window, .scheduled_window_class =
            TDMA_SERVICE_WINDOW_CLASS_VDC_OBSERVATION,
        .scheduled_guard_start_ns = 1000000ull,
        .scheduled_window_start_ns = 2000000ull,
        .scheduled_window_end_ns = 2100000ull,
        .scheduled_guard_end_ns = 2200000ull,
        .frame = frame, .frame_size = sizeof(frame)};
    assert(tdma_service_submit_tx(&service, &intent));
}

static void pending(void)
{
    assert(service.completed_seq == 0u && service.dropped_seq == 0u);
    assert(service.ready_count == 0u && tx_calls == 0u);
    assert(service.core1_start_time_ns == 0ull);
}

static bool ring_start(void *context, const tdma_ring_runtime_config_t *config)
{
    (void)context;
    return config->enabled != 0u;
}

static bool ring_stop(void *context)
{
    (void)context;
    ring_stops++;
    return true;
}

static bool ring_service(void *context, uint64_t time, tdma_ring_adapter_status_t *status)
{
    (void)context; (void)time;
    ring_calls++;
    status->up_configured = status->down_configured = 1u;
    status->up_running = status->down_running = 1u;
    status->tx_count = status->rx_count = ring_calls;
    return true;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *test = argv[1];
    if (strcmp(test, "writer") == 0) {
        setup(false);
        service.intent_guard++;
        for (uint32_t i = 0u; i < 8u; ++i) tick(500u);
        pending();
        assert(service.armed == 0u && service.timing_intent_seq == 0u);
        assert(service.state == tdma_service_STATE_IDLE);
        service.intent_guard++;
        tick(500u);
        assert(tx_calls == 1u && service.completed_seq == service.intent_seq);
        tick(500u);
        assert(tx_calls == 1u);
    } else if (strcmp(test, "changed") == 0) {
        setup(false);
        replace_during_copy = true;
        tick(500u);
        pending();
        assert(service.armed == 0u && service.timing_intent_seq == 0u);
        tick(500u);
        assert(tx_calls == 1u && transmitted == 0xB2u);
        assert(service.completed_seq == 2u);
    } else if (strcmp(test, "window") == 0) {
        setup(true);
        /* Both before guard and inside guard return without polling time. */
        tick(500u); pending();
        assert(service.last_result == tdma_service_RESULT_WAITING_FOR_WINDOW);
        tick(1500u); pending();
        assert(service.scheduled_window_wait_ns == 500000u);
        const uint64_t armed_at = service.core1_arm_time_ns;
        tick(2000u);
        assert(tx_calls == 1u && transmitted == 0xA1u);
        assert(service.completed_seq == service.intent_seq);
        assert(service.core1_arm_time_ns == armed_at);
        tick(2000u); assert(tx_calls == 1u);
    } else if (strcmp(test, "miss") == 0) {
        setup(true);
        tick(1500u); pending();
        tick(2101u);
        assert(tx_calls == 0u && service.completed_seq == service.intent_seq);
        assert(service.last_result == tdma_service_RESULT_WINDOW_MISSED);
        assert(service.scheduled_window_miss_count == 1u);
        tick(2300u); assert(service.scheduled_window_miss_count == 1u);
    } else if (strcmp(test, "abort") == 0) {
        setup(true);
        tick(1500u); pending();
        service.intent_guard++;
        service.abort_seq = service.intent_seq;
        tick(1500u); pending();
        service.intent_guard++;
        tick(2000u);
        assert(tx_calls == 0u && service.completed_seq == service.intent_seq);
        assert(service.dropped_seq == service.intent_seq && service.armed == 0u);
    } else if (strcmp(test, "resident") == 0) {
        setup(true);
        const tdma_ring_runtime_config_t config = {
            .enabled = 1u, .node_count = 2u, .up_group_id = 1u, .down_group_id = 2u,
            .flags = TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN,
            .ring_profile_crc32 = 1u, .schedule_crc32 = 2u,
            .operating_profile_crc32 = 3u, .baud_hz = 10000000u,
            .cycle_period_ns = 1000000u, .feedback_timeout_ns = 2000000u,
            .tx_dma_channel_id = 4u, .rx_dma_channel_id = 5u};
        const tdma_ring_adapter_ops_t ops = {
            .start = ring_start, .stop = ring_stop, .service = ring_service};
        assert(tdma_ring_runtime_bind_adapter(&service.ring_runtime, &ops, NULL));
        assert(tdma_ring_runtime_configure(&service.ring_runtime, &config));
        service.intent_guard++;
        tick(500u); /* ARM is applied before the separate START intent. */
        assert(tdma_ring_runtime_set_data_enabled(&service.ring_runtime, true));
        for (uint32_t i = 0u; i < 8u; ++i) tick(500u);
        pending();
        assert(ring_calls == 8u && service.service_count == 9u);
        service.intent_guard++;
        tick(1500u); pending();
        assert(ring_calls == 9u);
        assert(tdma_ring_runtime_configure(&service.ring_runtime, NULL));
        service.intent_guard++;
        tick(1500u); pending();
        assert(ring_stops == 1u && service.ring_runtime.adapter_started == 0u);
        service.intent_guard++;
    } else if (strcmp(test, "map_admission") == 0) {
        assert(tdma_service_init(&service));
        tdma_process_image_map_t map = {
            .version = TDMA_PROCESS_IMAGE_MAP_VERSION,
            .payload_size = TDMA_FLIGHT_SHORT_PAYLOAD_SIZE, .segment_count = 1u};
        map.segment[0] = (tdma_process_image_segment_t){
            .used = 1u, .byte_length = TDMA_FLIGHT_SHORT_SLOT_SIZE,
            .payload_class = TDMA_PAYLOAD_CLASS_VDC_SYNC_SAMPLE,
            .flags = TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE};
        map.map_crc32 = tdma_process_image_map_crc32(&map);
        assert(tdma_process_image_map_validate(&map, NULL));
        tdma_flight_engine_t *engine = &service.flight_engine;
        assert(tdma_service_configure_flight_map_checked(NULL, &map) == TDMA_SERVICE_FLIGHT_MAP_INVALID);
        assert(tdma_service_configure_flight_map_checked(&service, NULL) == TDMA_SERVICE_FLIGHT_MAP_INVALID);
        for (unsigned guard = 0u; guard < 2u; ++guard) {
            volatile uint32_t *writer = guard ? &service.ring_runtime.result_guard : &service.ring_runtime.config_guard;
            ++*writer;
            assert(tdma_service_configure_flight_map_checked(&service, &map) == TDMA_SERVICE_FLIGHT_MAP_SNAPSHOT_UNAVAILABLE);
            assert(!tdma_service_configure_flight_map(&service, &map));
            assert(engine->map_generation == 0u && engine->map_reject_count == 0u);
            ++*writer;
        }
        service.ring_runtime.enabled = 1u;
        assert(tdma_service_configure_flight_map_checked(&service, &map) == TDMA_SERVICE_FLIGHT_MAP_RUNTIME_ACTIVE);
        service.ring_runtime.enabled = 0u;
        service.ring_runtime.adapter_started = 1u;
        assert(tdma_service_configure_flight_map_checked(&service, &map) == TDMA_SERVICE_FLIGHT_MAP_RUNTIME_ACTIVE);
        service.ring_runtime.adapter_started = 0u;
        assert(tdma_service_configure_flight_map_checked(&service, &map) == TDMA_SERVICE_FLIGHT_MAP_OK);
        assert(engine->map_generation == 1u && engine->configured == 1u);
        const tdma_process_image_map_t published = engine->map;
        engine->map_sequence++;
        const uint32_t held_guard = engine->map_sequence;
        assert(tdma_service_configure_flight_map_checked(&service, &map) == TDMA_SERVICE_FLIGHT_MAP_BUSY);
        assert(engine->map_sequence == held_guard && engine->map_generation == 1u);
        assert(memcmp(&engine->map, &published, sizeof(published)) == 0);
        engine->map_sequence++;
        assert(tdma_flight_engine_activate(engine, 0u));
        assert(tdma_service_configure_flight_map_checked(&service, &map) == TDMA_SERVICE_FLIGHT_MAP_ENGINE_ACTIVE);
        assert(tdma_flight_engine_is_active(engine) && engine->map_generation == 1u);
        assert(memcmp(&engine->map, &published, sizeof(published)) == 0);
        tdma_flight_engine_deactivate(engine);
        map.map_crc32 ^= 1u;
        assert(tdma_service_configure_flight_map_checked(&service, &map) == TDMA_SERVICE_FLIGHT_MAP_INVALID);
        assert(engine->map_generation == 1u && engine->map_reject_count == 3u);
        assert(memcmp(&engine->map, &published, sizeof(published)) == 0);
        map = published;
        assert(tdma_service_configure_flight_map(&service, &map));
        assert(engine->map_generation == 2u && (engine->map_sequence & 1u) == 0u);
        assert(tdma_flight_engine_configure_checked(NULL, &map) == TDMA_FLIGHT_MAP_CONFIG_INVALID);
        assert(!tdma_flight_engine_configure(NULL, &map));
    } else {
        assert(!"unknown test");
    }
    puts("TDMA Core1 deferred intent/window PASS");
    return 0;
}
