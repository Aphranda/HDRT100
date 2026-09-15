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
static struct {
    sync_io_sequence_snapshot_t status;
    uint32_t last_edges, paused_edges, pause_started;
    bool paused;
} s_sequence;
static uint32_t s_plan[SYNC_IO_SEQUENCE_PLAN_MAX * SYNC_IO_SEQUENCE_PLAN_WORDS];
static void fail(uint32_t reason) { s_sequence.status.fault = reason; }
#endif
