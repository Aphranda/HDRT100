#ifndef SYNC_IO_SEQUENCE_FAKE_H
#define SYNC_IO_SEQUENCE_FAKE_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sync_io_sequence.h"
typedef unsigned uint;
#define BOARD_SYNC_INPUT_PIN_COUNT 4u
#define BOARD_SYNC_OUTPUT_PIN_COUNT 4u
#define SMA_MASK 15u
#define PLAN_INDEX_SHIFT 4u
#define ACTIVE_VALUE_SHIFT 12u
static struct {
    sync_io_sequence_snapshot_t status;
    uint32_t last_edges, paused_edges, pause_started;
    uint32_t prime_receipts_remaining;
    bool paused;
} s_sequence;
static uint32_t s_plan[SYNC_IO_SEQUENCE_PLAN_MAX * SYNC_IO_SEQUENCE_PLAN_WORDS];
/* Step code bit0 (OUT0/SP2T) is applied outside the extracted PIO plan. */
static uint8_t s_step_sp2t[SYNC_IO_SEQUENCE_PLAN_MAX];
static bool s_sp2t_level_valid;
static bool s_sp2t_level;
static void apply_step_sp2t(uint32_t logical_index) {
    if (logical_index >= s_sequence.status.plan_count) return;
    s_sp2t_level = s_step_sp2t[logical_index] != 0u;
    s_sp2t_level_valid = true;
}
static void fail(uint32_t reason) { s_sequence.status.fault = reason; }
#endif
