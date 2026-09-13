#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tdma_origin_build_job.h"
#include "tdma_origin_exchange.h"
#include "tdma_receive_health.h"
#include "tdma_profile.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/dma.h"

static void check_packet_and_receive_lengths(uint32_t nodes)
{
    uint8_t payload[TDMA_TRANSPORT_SHORT_PAYLOAD_MAX] = {0};
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    for (uint32_t peer_capacity = 2u; peer_capacity <= 8u; ++peer_capacity) {
        const size_t peer_payload_size = peer_capacity * 32u + 4u;
        tdma_transport_frame_build_t build = {
            .frame_class = TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id = 0u, .transport_sequence = 7u,
            .payload_class = TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
            .flags = TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK | TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE,
            .schedule_crc32 = 0x123u, .ring_profile_crc32 = 0x456u,
            .hop_limit = 1u, .payload = payload, .payload_size = peer_payload_size};
        size_t size = 0u;
        tdma_transport_result_t result;
        assert(tdma_transport_frame_encode(&build, packet, sizeof(packet), &size, &result));
        assert(size == peer_payload_size + 32u);
        tdma_transport_frame_view_t view;
        assert(tdma_transport_frame_decode(packet, size, &view, &result));
        tdma_receive_health_t health;
        assert(tdma_receive_health_init(&health));
        tdma_receive_health_config_t config = {
            .schedule_crc32 = build.schedule_crc32, .ring_profile_crc32 = build.ring_profile_crc32,
            .map_generation = 1u, .expected_payload_size = nodes * 32u + 4u,
            .expected_segment_mask = 1u, .stale_timeout_ns = 1000u};
        assert(tdma_receive_health_configure_stopped(&health, &config));
        tdma_receive_reason_t reason;
        const bool accepted = tdma_receive_health_evaluate(&health, &view, result, 1u, 1u, &reason);
        assert(accepted == (peer_capacity == nodes));
        assert(reason == (accepted ? TDMA_RECEIVE_REASON_NONE : TDMA_RECEIVE_REASON_PAYLOAD_SIZE));
        assert(health.accepted_count == (accepted ? 1u : 0u));
    }
}

static void check_origin_copy_extent(uint32_t nodes)
{
    const uint32_t packet_size = nodes * 32u + 4u + 32u;
    uint8_t capture[2][TDMA_TRANSPORT_SHORT_PACKET_MAX];
    memset(capture, 0x37, sizeof(capture));
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX + 16u];
    memset(packet, 0xa5, sizeof(packet));
    uint32_t shadows[2][TDMA_ORIGIN_PLAN_SHADOW_BYTES / sizeof(uint32_t)] = {{0}};
    shadows[0][TDMA_FLIGHT_SHORT_SLOT_SIZE / sizeof(uint32_t)] = 1u;
    tdma_origin_plan_state_t state = {.capture_bank = 1u,
        .bank_version = {2u, 2u}, .local_next_address = 16u, .local_selected_generation = 1u};
    tdma_origin_exchange_t exchange;
    assert(tdma_origin_exchange_bind(&exchange, &state, capture[0], capture[1],
                                   shadows[0], shadows[1], 16u, 32u, packet_size));
    state.bank_version[0] = 4u;
    state.bank_observation[0].sequence = 7u;
    tdma_origin_observation_t observation;
    assert(!tdma_origin_exchange_copy_rx_observation(&exchange, packet, packet_size - 1u, &observation));
    assert(tdma_origin_exchange_copy_rx_observation(&exchange, packet, sizeof(packet), &observation));
    assert(observation.sequence == 7u);
    for (size_t i = 0u; i < packet_size; ++i) assert(packet[i] == 0x37);
    for (size_t i = packet_size; i < sizeof(packet); ++i) assert(packet[i] == 0xa5);
    assert(!tdma_origin_exchange_copy_rx_observation(&exchange, packet, sizeof(packet), &observation));
}

static void check_dma_extents(const tdma_origin_plan_config_t *c, const tdma_origin_plan_t *p)
{
    unsigned copies = 0u, stages = 0u, captures = 0u, padding = 0u, trailers = 0u;
    const uint32_t packet_size = c->packet_size;
    for (uint32_t i = 0u; i < p->run_count; ++i) {
        const tdma_flight_overlay_dma_run_t *r = &p->runs[i];
        if (r->read_address == c->address.rx_packet + packet_size - 4u) ++trailers;
        if (r->write_address == c->address.rx_packet) {
            assert(r->transfer_count == packet_size);
            assert(r->read_address == c->address.capture_bank[0] ||
                   r->read_address == c->address.capture_bank[1]);
            ++copies;
        }
        if (r->write_address == c->address.stage + c->outer_header_bytes * 2u + 1u &&
            (r->read_address == c->address.capture_bank[0] ||
             r->read_address == c->address.capture_bank[1])) {
            assert(r->transfer_count == packet_size);
            ++stages;
        }
        if (r->write_address == DMA_BASE + c->capture_dma * 0x40u + DMA_CH0_AL3_CTRL_OFFSET) {
            assert(r->transfer_count == 4u);
            assert(r->read_address >= c->address.literals);
            const uint32_t offset = (r->read_address - c->address.literals) / sizeof(uint32_t);
            assert(offset + 4u <= p->literal_count);
            assert(p->literals[offset + 2u] == packet_size);
            ++captures;
        }
        if (r->write_address == c->address.stage + (c->outer_header_bytes + packet_size) * 2u) {
            assert(r->transfer_count == c->physical_bytes - c->outer_header_bytes - packet_size);
            ++padding;
        }
    }
    assert(copies == 2u && stages == 2u && captures == 2u && padding == 1u);
    assert(trailers == (c->diagnostic_skip_records ? 0u : 1u));
}

int main(void)
{
    unsigned cases = 0;
    for (unsigned nodes = 2; nodes <= TDMA_FLIGHT_SHORT_SLOT_COUNT; ++nodes) {
        check_packet_and_receive_lengths(nodes);
        check_origin_copy_extent(nodes);
        for (unsigned skip = 0; skip <= 1; ++skip) {
            tdma_origin_plan_config_t c = {
                .address = {.capture_bank = {0x20000000, 0x20000800},
                    .stage = 0x20001000, .tx_header = 0x20002000,
                    .rx_packet = 0x20003000, .state = 0x20004000,
                    .local_shadow = {0x20005000, 0x20005800}, .scratch = 0x20006000,
                    .runs = 0x20008000, .literals = 0x2000a000, .records = 0x2000b000},
                .physical_bytes = nodes * 32u + 4u + 32u + 15u,
                .packet_size = nodes * 32u + 4u + 32u,
                .outer_header_bytes = 4, .capture_prefix_bits = 36,
                .guard_count = 1, .abort_poll_count = 8,
                .local_slot = 0, .active_slot_mask = (1u << nodes) - 1u,
                .returned_route_word = 0x502 | ((nodes - 1) * 0x1010000),
                .capture_dma = 4, .output_dma = 5, .loader_dma = 6, .executor_dma = 8,
                .control_pio = 1, .data_pio = 2, .control_sm = 0, .capture_sm = 3,
                .data_sm = 2, .helper_sm = 0, .control_pc = TDMA_ORIGIN_CONTROL_PC,
                .capture_pc = TDMA_ORIGIN_CAPTURE_PC, .data_pc = TDMA_ORIGIN_DATA_PC,
                .helper_pc = TDMA_ORIGIN_HELPER_PC, .compare_pc = TDMA_ORIGIN_COMPARE_PC,
                .rtt_sm = 1, .rtt_pc = TDMA_ORIGIN_RTT_PC,
                .diagnostic_skip_records = skip};
            tdma_flight_overlay_dma_run_t expected_runs[TDMA_ORIGIN_PLAN_RUN_MAX] = {0};
            tdma_flight_overlay_dma_run_t actual_runs[TDMA_ORIGIN_PLAN_RUN_MAX] = {0};
            uint32_t expected_literals[TDMA_ORIGIN_PLAN_LITERAL_MAX] = {0};
            uint32_t actual_literals[TDMA_ORIGIN_PLAN_LITERAL_MAX] = {0};
            tdma_origin_plan_t expected = {.runs = expected_runs, .literals = expected_literals,
                .run_capacity = TDMA_ORIGIN_PLAN_RUN_MAX, .literal_capacity = TDMA_ORIGIN_PLAN_LITERAL_MAX};
            tdma_origin_plan_t actual = {.runs = actual_runs, .literals = actual_literals,
                .run_capacity = TDMA_ORIGIN_PLAN_RUN_MAX, .literal_capacity = TDMA_ORIGIN_PLAN_LITERAL_MAX};
            assert(tdma_origin_plan_build(&c, &expected));
            tdma_origin_plan_builder_t b = {0};
            tdma_origin_build_job_t job = {0};
            assert(tdma_origin_plan_begin(&b, &c, &actual));
            assert(tdma_origin_build_job_request(&job, &b));
            assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_BUSY);
            assert(actual.seed_entry == 0);
            tdma_origin_build_job_core0_service(&job);
            assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_DONE);
            assert(memcmp(expected_runs, actual_runs, sizeof(actual_runs)) == 0);
            assert(memcmp(expected_literals, actual_literals, sizeof(actual_literals)) == 0);
            expected.runs = actual.runs;
            expected.literals = actual.literals;
            assert(memcmp(&expected, &actual, sizeof(actual)) == 0);
            check_dma_extents(&c, &actual);
            assert(tdma_origin_build_job_cancel(&job));
            tdma_origin_plan_config_t invalid = c;
            invalid.active_slot_mask |= 1u << nodes;
            assert(!tdma_origin_plan_build(&invalid, &actual));
            assert(actual.seed_entry == 0u);
            invalid = c;
            ++invalid.packet_size;
            assert(!tdma_origin_plan_build(&invalid, &actual));
            assert(actual.seed_entry == 0u);
            ++cases;
        }
    }
    printf("%u actual-builder graph pairs identical\n", cases);
    return 0;
}
