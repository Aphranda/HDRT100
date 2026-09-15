#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tdma_origin_build_job.h"
#include "tdma_origin_exchange.h"
#include "tdma_receive_health.h"
#include "tdma_profile.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/dma.h"

/* Only the real plan translation unit renames this symbol. The production
 * worker calls this wrapper, allowing deterministic STOP interleavings on
 * both sides of every real graph block, including its final publication. */
tdma_origin_build_result_t tdma_origin_plan_step_actual(tdma_origin_plan_builder_t *b);
static tdma_origin_build_job_t *cancel_job;
static uint32_t step_calls, cancel_step, cancel_after, cancellation_cases, probe_cases;

static void interrupt_worker(tdma_origin_plan_builder_t *b)
{
    assert(!tdma_origin_build_job_cancel(cancel_job));
    assert(!tdma_origin_build_job_cancel(cancel_job));
    assert(!tdma_origin_build_job_request(cancel_job, b));
    assert(tdma_origin_build_job_take(cancel_job) == TDMA_ORIGIN_BUILD_FAILED);
    /* The owner must retain storage even when the last block has already
     * produced entry addresses. Only the worker may finish retirement. */
    assert(tdma_origin_build_job_state(cancel_job) == TDMA_ORIGIN_JOB_CANCELLED);
}

tdma_origin_build_result_t tdma_origin_plan_step(tdma_origin_plan_builder_t *b)
{
    ++step_calls;
    const bool interrupt = cancel_job != NULL && step_calls == cancel_step;
    if (interrupt && !cancel_after) interrupt_worker(b);
    const tdma_origin_build_result_t result = tdma_origin_plan_step_actual(b);
    if (interrupt && cancel_after) interrupt_worker(b);
    return result;
}

static void check_cancellation(const tdma_origin_plan_config_t *config,
                               tdma_origin_plan_t *plan, uint32_t steps,
                               const tdma_flight_overlay_dma_run_t *expected_runs,
                               const uint32_t *expected_literals)
{
    const size_t run_bytes = plan->run_capacity * sizeof(*plan->runs);
    const size_t literal_bytes = plan->literal_capacity * sizeof(*plan->literals);
    for (uint32_t after = 0u; after <= 1u; ++after) {
        for (uint32_t cut = 1u; cut <= steps; ++cut) {
            tdma_origin_build_job_t job = {0};
            tdma_origin_plan_builder_t builder = {0};
            memset(plan->runs, 0, run_bytes);
            memset(plan->literals, 0, literal_bytes);
            assert(tdma_origin_plan_begin(&builder, config, plan));
            assert(tdma_origin_build_job_request(&job, &builder));
            cancel_job = &job;
            cancel_step = cut;
            cancel_after = after;
            step_calls = 0u;
            tdma_origin_build_job_core0_service(&job);
            cancel_job = NULL;
            assert(step_calls == cut);
            assert(tdma_origin_build_job_state(&job) == TDMA_ORIGIN_JOB_IDLE);
            assert(!builder.active && !builder.complete && builder.failed);
            assert(plan->seed_entry == 0u && plan->boundary_entry == 0u &&
                   plan->fault_entry == 0u && plan->record_entry == 0u);
            for (uint32_t bank = 0u; bank < TDMA_ORIGIN_PLAN_BANK_COUNT; ++bank)
                assert(plan->local_entry[bank] == 0u);
            assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_FAILED);
            assert(tdma_origin_build_job_cancel(&job));

            /* Reuse is legal only after IDLE. Late worker entry points must
             * leave even a completely replaced builder and graph intact. */
            memset(&builder, 0xa5, sizeof(builder));
            const tdma_origin_plan_builder_t poisoned = builder;
            memset(plan->runs, 0xa5, run_bytes);
            memset(plan->literals, 0xa5, literal_bytes);
            tdma_origin_build_job_core0_service(&job);
            tdma_origin_build_job_core0_build_claimed(&job);
            assert(memcmp(&builder, &poisoned, sizeof(builder)) == 0);
            for (size_t i = 0u; i < run_bytes; ++i)
                assert(((const uint8_t *)plan->runs)[i] == 0xa5u);
            for (size_t i = 0u; i < literal_bytes; ++i)
                assert(((const uint8_t *)plan->literals)[i] == 0xa5u);

            memset(&builder, 0, sizeof(builder));
            memset(plan->runs, 0, run_bytes);
            memset(plan->literals, 0, literal_bytes);
            assert(tdma_origin_plan_begin(&builder, config, plan));
            assert(tdma_origin_build_job_request(&job, &builder));
            tdma_origin_build_job_core0_service(&job);
            assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_DONE);
            assert(memcmp(plan->runs, expected_runs, run_bytes) == 0);
            assert(memcmp(plan->literals, expected_literals, literal_bytes) == 0);
            assert(plan->seed_entry != 0u);
            ++cancellation_cases;
        }
    }
}

static void check_build_probe(const tdma_origin_plan_config_t *config,
    tdma_origin_plan_t *plan, const tdma_flight_overlay_dma_run_t *expected_runs,
    const uint32_t *expected_literals)
{
    uint32_t pause_step = 0u;
    const size_t run_bytes = plan->run_capacity * sizeof(*plan->runs);
    const size_t literal_bytes = plan->literal_capacity * sizeof(*plan->literals);
    for (uint32_t mode = 0u; mode < 5u; ++mode) {
        tdma_origin_build_job_t job = {0};
        tdma_origin_plan_builder_t builder = {0};
        tdma_origin_build_probe_t out;
        assert(!tdma_origin_build_job_get_stopped_probe(&job, &out));
        assert(tdma_origin_plan_begin(&builder, config, plan));
        assert(tdma_origin_build_job_request_probe(&job, &builder, 42u + mode, 8u));
        assert(!tdma_origin_build_job_get_stopped_probe(&job, &out));
        step_calls = 0u;
        if (mode == 1u || mode == 2u) {
            cancel_job = &job; cancel_step = pause_step; cancel_after = mode == 2u;
        }
        if (mode == 3u) assert(tdma_origin_build_job_cancel(&job));
        if (mode == 4u) {
            assert(tdma_origin_build_job_core0_claim(&job));
            assert(!tdma_origin_build_job_cancel(&job));
            tdma_origin_build_job_core0_build_claimed(&job);
        }
        tdma_origin_build_job_core0_service(&job);
        cancel_job = NULL;
        if (mode < 3u) {
            assert(tdma_origin_build_job_probe_paused(&job));
            assert(builder.active && builder.emitting && plan->run_count > 0u);
            assert(job.probe.emitted_runs == plan->run_count);
            assert(!tdma_origin_build_job_request(&job, &builder));
            if (mode == 0u) {
                pause_step = step_calls;
                const tdma_origin_plan_builder_t paused = builder;
                for (unsigned i = 0u; i < 100u; ++i) {
                    tdma_origin_build_job_core0_service(&job);
                    tdma_origin_build_job_core0_build_claimed(&job);
                    assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_BUSY);
                    assert(memcmp(&builder, &paused, sizeof(builder)) == 0);
                    assert(step_calls == pause_step);
                }
            }
            assert(!tdma_origin_build_job_cancel(&job));
            assert(!tdma_origin_build_job_get_stopped_probe(&job, &out));
            tdma_origin_build_job_core0_service(&job);
        }
        assert(tdma_origin_build_job_state(&job) == TDMA_ORIGIN_JOB_IDLE);
        assert(!tdma_origin_build_job_probe_paused(&job));
        assert(tdma_origin_build_job_get_stopped_probe(&job, &out));
        assert(out.state == TDMA_ORIGIN_BUILD_PROBE_RETIRED && out.entries_cleared == 1u);
        assert(out.trial_epoch == 42u + mode && out.config_seq == 8u);
        assert((out.emitted_runs != 0u) == (mode < 3u));
        assert((out.deferred_cancels != 0u) == (mode != 3u));
        assert(!builder.active && !builder.complete && builder.failed);
        memset(&builder, 0xa5, sizeof(builder));
        const tdma_origin_plan_builder_t poisoned = builder;
        memset(plan->runs, 0xa5, run_bytes);
        memset(plan->literals, 0xa5, literal_bytes);
        /* Simulate Core1's transient IDLE -> CANCELLED exchange after
         * retirement. A historical probe must not resurrect the writer. */
        job.state = TDMA_ORIGIN_JOB_CANCELLED;
        tdma_origin_build_job_core0_service(&job);
        job.state = TDMA_ORIGIN_JOB_IDLE;
        tdma_origin_build_job_core0_service(&job);
        assert(memcmp(&builder, &poisoned, sizeof(builder)) == 0);
        for (size_t i = 0u; i < run_bytes; ++i) assert(((uint8_t *)plan->runs)[i] == 0xa5u);
        for (size_t i = 0u; i < literal_bytes; ++i) assert(((uint8_t *)plan->literals)[i] == 0xa5u);
        memset(&builder, 0, sizeof(builder));
        memset(plan->runs, 0, run_bytes);
        memset(plan->literals, 0, literal_bytes);
        assert(tdma_origin_plan_begin(&builder, config, plan));
        assert(tdma_origin_build_job_request(&job, &builder));
        tdma_origin_build_job_core0_service(&job);
        assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_DONE);
        assert(!tdma_origin_build_job_get_stopped_probe(&job, &out));
        assert(memcmp(plan->runs, expected_runs, run_bytes) == 0);
        assert(memcmp(plan->literals, expected_literals, literal_bytes) == 0);
        ++probe_cases;
    }
}

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
    unsigned first_copies = 0u, first_commits = 0u;
    const uint32_t packet_size = c->packet_size;
    for (uint32_t i = 0u; i < p->run_count; ++i) {
        const tdma_flight_overlay_dma_run_t *r = &p->runs[i];
        if (r->write_address == c->address.first_record) {
            assert(r->read_address == c->address.state + offsetof(tdma_origin_plan_state_t,observation_sequence));
            assert(r->transfer_count == sizeof(tdma_origin_record_t)/sizeof(uint32_t));
            assert(p->record_entry == c->address.runs+i*sizeof(*r));
            ++first_copies;
        }
        if (r->write_address == c->address.first_record + offsetof(tdma_origin_first_record_t,published_version)) {
            assert(first_copies==1u && r->transfer_count==1u);
            assert(i>0u && p->runs[i-1u].write_address==c->address.first_record);
            const uint32_t literal=(r->read_address-c->address.literals)/sizeof(uint32_t);
            assert(literal<p->literal_count && p->literals[literal]==TDMA_ORIGIN_FIRST_RECORD_VERSION);
            ++first_commits;
        }
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
    assert(first_copies==(c->diagnostic_skip_records?0u:1u));
    assert(first_commits==first_copies);
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
                    .runs = 0x20008000, .literals = 0x2000a000, .records = 0x2000b000,
                    .first_record = 0x2000c000},
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
            step_calls = 0u;
            tdma_origin_build_job_core0_service(&job);
            const uint32_t actual_steps = step_calls;
            assert(actual_steps > 0u && actual_steps <= 2u * TDMA_ORIGIN_BUILD_LABEL_CAPACITY);
            assert(tdma_origin_build_job_take(&job) == TDMA_ORIGIN_BUILD_DONE);
            assert(memcmp(expected_runs, actual_runs, sizeof(actual_runs)) == 0);
            assert(memcmp(expected_literals, actual_literals, sizeof(actual_literals)) == 0);
            expected.runs = actual.runs;
            expected.literals = actual.literals;
            assert(memcmp(&expected, &actual, sizeof(actual)) == 0);
            check_dma_extents(&c, &actual);
            assert(tdma_origin_build_job_cancel(&job));
            check_cancellation(&c, &actual, actual_steps, expected_runs, expected_literals);
            check_build_probe(&c, &actual, expected_runs, expected_literals);
            tdma_origin_plan_config_t invalid = c;
            invalid.active_slot_mask |= 1u << nodes;
            assert(!tdma_origin_plan_build(&invalid, &actual));
            assert(actual.seed_entry == 0u);
            invalid = c;
            ++invalid.packet_size;
            assert(!tdma_origin_plan_build(&invalid, &actual));
            assert(actual.seed_entry == 0u);
            /* Dedicated first storage is a whole SRAM extent, including its
             * commit word, and cannot alias any live/cyclic graph region. */
            const uint32_t invalid_first[] = {0u, 0x2000c002u, SRAM_END - 88u,
                c.address.state, c.address.records,
                c.address.records + TDMA_ORIGIN_RECORD_COUNT * sizeof(tdma_origin_record_t) - 4u,
                c.address.runs, c.address.literals, c.address.capture_bank[0],
                c.address.stage, c.address.tx_header, c.address.rx_packet,
                c.address.local_shadow[0], c.address.scratch};
            for(size_t bad=0u;bad<sizeof(invalid_first)/sizeof(invalid_first[0]);++bad) {
                invalid=c;invalid.address.first_record=invalid_first[bad];
                assert(!tdma_origin_plan_build(&invalid,&actual));
                assert(actual.seed_entry==0u && actual.record_entry==0u);
            }
            ++cases;
        }
    }
    printf("{\"graph_pairs\":%u,\"cancellation_cases\":%u,\"probe_cases\":%u}\n",
        cases, cancellation_cases, probe_cases);
    return 0;
}
