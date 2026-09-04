#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "sync_io_logic_analyzer.h"

static uint32_t pin_mask(uint32_t pin)
{
    return 1u << pin;
}

static sync_io_logic_analyzer_config_t raw_config(void)
{
    const sync_io_logic_analyzer_config_t config = {
        .contract_version = SYNC_IO_LOGIC_ANALYZER_CONTRACT_VERSION,
        .mode = SYNC_IO_LOGIC_ANALYZER_MODE_RAW_SAMPLE,
        .source_mask = pin_mask(BOARD_SYNC_TRIG_IN_PIN) |
                       pin_mask(BOARD_TDMA_TX_CLK_OUT_PIN),
        .sample_period_ns = 100u,
        .max_records = 64u,
        .timeout_us = 1000u,
        .overwrite_oldest = 0u,
        .expected_profile_generation = 1u,
        .expected_persona_generation = 1u,
        .debug_continue_budget = 2u,
        .trigger = {
            .type = SYNC_IO_LOGIC_ANALYZER_TRIGGER_NONE,
        },
    };
    return config;
}

static void test_raw_and_edge_config(void)
{
    sync_io_logic_analyzer_config_t config = raw_config();
    assert(sync_io_logic_analyzer_config_valid(&config));

    config.source_mask = pin_mask(31u);
    assert(!sync_io_logic_analyzer_config_valid(&config));
    config = raw_config();
    config.max_records = SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS + 1u;
    assert(!sync_io_logic_analyzer_config_valid(&config));
    config = raw_config();
    config.expected_persona_generation = 0u;
    assert(!sync_io_logic_analyzer_config_valid(&config));
    config = raw_config();
    config.debug_continue_budget =
        SYNC_IO_LOGIC_ANALYZER_DEBUG_CONTINUE_LIMIT + 1u;
    assert(!sync_io_logic_analyzer_config_valid(&config));

    config = raw_config();
    config.mode = SYNC_IO_LOGIC_ANALYZER_MODE_EDGE_TIMESTAMP;
    config.sample_period_ns = 0u;
    assert(sync_io_logic_analyzer_config_valid(&config));
    config.sample_period_ns = 1u;
    assert(!sync_io_logic_analyzer_config_valid(&config));
}

static void test_gate_policy(void)
{
    assert(sync_io_logic_analyzer_gate_action(
               SYNC_IO_LOGIC_ANALYZER_GATE_RESOURCE_CONFLICT,
               false, 0u, 2u) == SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_CONTINUE);
    assert(sync_io_logic_analyzer_gate_action(
               SYNC_IO_LOGIC_ANALYZER_GATE_SIGNAL_QUALITY,
               false, 2u, 2u) ==
           SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_ROUND_END);
    assert(sync_io_logic_analyzer_gate_action(
               SYNC_IO_LOGIC_ANALYZER_GATE_CRC,
               true, 0u, 2u) == SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_REJECT);
    assert(sync_io_logic_analyzer_gate_action(
               SYNC_IO_LOGIC_ANALYZER_GATE_DMA_BOUNDS,
               false, 0u, 4u) == SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_HARD_STOP);
    assert(sync_io_logic_analyzer_gate_action(
               SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_MEMORY,
               false, 0u, 4u) == SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_HARD_STOP);
    assert(sync_io_logic_analyzer_gate_action(
               SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_FLASH,
               false, 0u, 4u) == SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_HARD_STOP);
    assert(sync_io_logic_analyzer_gate_action(
               SYNC_IO_LOGIC_ANALYZER_GATE_UNCONTROLLED_GPIO,
               true, 0u, 4u) == SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_HARD_STOP);
}

static void test_trigger_config(void)
{
    sync_io_logic_analyzer_config_t config = raw_config();
    config.mode = SYNC_IO_LOGIC_ANALYZER_MODE_TRIGGERED_CAPTURE;
    config.pre_trigger_records = 16u;
    config.post_trigger_records = 32u;
    config.trigger.type = SYNC_IO_LOGIC_ANALYZER_TRIGGER_EDGE;
    config.trigger.source_mask = pin_mask(BOARD_TDMA_TX_CLK_OUT_PIN);
    config.trigger.edge_mask = config.trigger.source_mask;
    assert(sync_io_logic_analyzer_config_valid(&config));

    config.trigger.source_mask = pin_mask(BOARD_TDMA_RX_DATA_OUT_PIN);
    assert(!sync_io_logic_analyzer_config_valid(&config));
    config = raw_config();
    config.mode = SYNC_IO_LOGIC_ANALYZER_MODE_TRIGGERED_CAPTURE;
    config.pre_trigger_records = UINT32_MAX;
    config.post_trigger_records = 1u;
    config.trigger.type = SYNC_IO_LOGIC_ANALYZER_TRIGGER_PATTERN;
    config.trigger.source_mask = config.source_mask;
    config.trigger.pattern_mask = config.source_mask;
    assert(!sync_io_logic_analyzer_config_valid(&config));
}

static void test_snapshot_seqlock(void)
{
    sync_io_logic_analyzer_snapshot_t source;
    sync_io_logic_analyzer_snapshot_payload_t snapshot;
    memset(&source, 0, sizeof(source));
    memset(&snapshot, 0, sizeof(snapshot));

    source.sequence_lock = 2u;
    source.payload.contract_version =
        SYNC_IO_LOGIC_ANALYZER_CONTRACT_VERSION;
    source.payload.state = SYNC_IO_LOGIC_ANALYZER_STATE_RUNNING;
    source.payload.capture_sequence = 17u;
    source.payload.timestamp_flags =
        SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_DIAGNOSTIC_ONLY;
    assert(sync_io_logic_analyzer_snapshot_read(&source, &snapshot));
    assert(snapshot.capture_sequence == 17u);
    assert(snapshot.state == SYNC_IO_LOGIC_ANALYZER_STATE_RUNNING);

    source.sequence_lock = 3u;
    assert(!sync_io_logic_analyzer_snapshot_read(&source, &snapshot));
}

static void test_raw_capture_ring(void)
{
    sync_io_logic_analyzer_config_t config = raw_config();
    config.max_records = 3u;
    config.overwrite_oldest = 1u;
    sync_io_logic_analyzer_record_t records[3];
    sync_io_logic_analyzer_raw_capture_t capture;
    assert(sync_io_logic_analyzer_raw_capture_init(
               &capture, records, 3u, &config));
    for (uint32_t index = 0u; index < 4u; ++index) {
        const sync_io_logic_analyzer_record_t record = {
            .hardware_tick = index + 100u,
            .record_sequence = index,
            .level_mask = index,
        };
        assert(sync_io_logic_analyzer_raw_capture_push(&capture, &record));
    }
    assert(capture.dropped_records == 1u);
    assert(capture.overrun_count == 1u);
    sync_io_logic_analyzer_record_t out;
    assert(sync_io_logic_analyzer_raw_capture_pop(&capture, &out));
    assert(out.hardware_tick == 101u);
    assert((out.flags & SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_DISCONTINUITY) == 0u);
    assert(sync_io_logic_analyzer_raw_capture_pop(&capture, &out));
    assert(out.hardware_tick == 102u);
    assert(sync_io_logic_analyzer_raw_capture_pop(&capture, &out));
    assert(out.hardware_tick == 103u);
    assert((out.flags & SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_DISCONTINUITY) != 0u);
    assert(!sync_io_logic_analyzer_raw_capture_pop(&capture, &out));

    config.overwrite_oldest = 0u;
    assert(sync_io_logic_analyzer_raw_capture_init(
               &capture, records, 3u, &config));
    for (uint32_t index = 0u; index < 3u; ++index) {
        const sync_io_logic_analyzer_record_t record = {0};
        assert(sync_io_logic_analyzer_raw_capture_push(&capture, &record));
    }
    const sync_io_logic_analyzer_record_t overflow = {0};
    assert(!sync_io_logic_analyzer_raw_capture_push(&capture, &overflow));
    assert(capture.end_reason == SYNC_IO_LOGIC_ANALYZER_END_OVERFLOW);
    sync_io_logic_analyzer_snapshot_payload_t snapshot;
    assert(sync_io_logic_analyzer_raw_capture_snapshot(&capture, &snapshot));
    assert(snapshot.capture_complete == 1u);
    assert(snapshot.end_reason == SYNC_IO_LOGIC_ANALYZER_END_OVERFLOW);
    assert(snapshot.data_crc32 != 0u);

    config.overwrite_oldest = 1u;
    assert(sync_io_logic_analyzer_raw_capture_init(
               &capture, records, 3u, &config));
    sync_io_logic_analyzer_raw_capture_finish(
        &capture, SYNC_IO_LOGIC_ANALYZER_END_CAPACITY);
    assert(capture.complete);
    assert(capture.end_reason == SYNC_IO_LOGIC_ANALYZER_END_CAPACITY);
}

int main(void)
{
    assert(sizeof(sync_io_logic_analyzer_record_t) == 32u);
    test_raw_and_edge_config();
    test_trigger_config();
    test_gate_policy();
    test_snapshot_seqlock();
    test_raw_capture_ring();
    puts("sync_io_logic_analyzer contract tests passed");
    return 0;
}
