#ifndef SYNC_IO_RUN_OUTPUT_H
#define SYNC_IO_RUN_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#define SYNC_IO_RUN_OUTPUT_BLOCK_EDGES 4u
#define SYNC_IO_RUN_OUTPUT_BLOCK_WORDS (2u * SYNC_IO_RUN_OUTPUT_BLOCK_EDGES)
#define SYNC_IO_RUN_OUTPUT_MAX_EDGES 16u
#define SYNC_IO_RUN_OUTPUT_MAX_WORDS (2u * SYNC_IO_RUN_OUTPUT_MAX_EDGES)
#define SYNC_IO_RUN_OUTPUT_MIN_GUARD_US 100u
#define SYNC_IO_RUN_OUTPUT_SCHEMA 6u

enum {
    SYNC_IO_RUN_OUTPUT_IDLE, SYNC_IO_RUN_OUTPUT_PREPARED,
    SYNC_IO_RUN_OUTPUT_RUNNING, SYNC_IO_RUN_OUTPUT_RETIRING,
    SYNC_IO_RUN_OUTPUT_RETIRED
};
enum {
    SYNC_IO_RUN_OUTPUT_OK, SYNC_IO_RUN_OUTPUT_CANCELLED,
    SYNC_IO_RUN_OUTPUT_CLOCK, SYNC_IO_RUN_OUTPUT_STARVED,
    SYNC_IO_RUN_OUTPUT_DEADLINE, SYNC_IO_RUN_OUTPUT_ARGUMENT,
    SYNC_IO_RUN_OUTPUT_DMA, SYNC_IO_RUN_OUTPUT_EXPIRED
};

typedef struct {
    /* Last edge ticks are requested coordinates. Actual edges retain the
     * common nonnegative enable offset enclosed by anchor_after-before.
     * No stall/pause is permitted within that timing epoch. */
    uint64_t rising_tick, falling_tick;
    uint64_t ordinal;
    uint32_t model_token;
} sync_io_run_output_edge_t;

typedef struct {
    uint64_t anchor_before, anchor_after, last_rising_tick, last_falling_tick;
    uint64_t first_ordinal, last_ordinal, expires_tick;
    uint32_t schema, generation, state, reason, blocks, edges;
    uint32_t source_retirements, first_model, last_model, model_changes;
    uint32_t tick_hz, transfer_count, pio_enabled, dma_busy;
    uint32_t start_pc, program_offset, start_raw_flags;
    uint64_t start_raw_observed, start_raw_after;
    /* Diagnostic raw observations, not physical edge timestamps. First
     * retirement freezes registers before disable/abort (including CANCEL).
     * A zero valid flag makes retire_raw_tick unavailable, not time zero. */
    uint64_t service_last_tick, service_last_gap_ticks, service_max_gap_ticks;
    uint64_t submit_last_tick, submit_max_gap_ticks, refill_min_margin_ticks;
    uint64_t retire_raw_tick;
    uint32_t service_observations, submit_service_observation;
    uint32_t retire_raw_valid, retire_pc, retire_fstat, retire_fdebug;
    uint32_t retire_dma_ctrl, retire_dma_remaining, retire_pio_ctrl;
    /* Immutable encoding selected at STOP preparation: paired=2/0;
     * uniform=1/fixed high width. DMA word counts are not physical edges. */
    uint32_t fifo_words_per_edge, fixed_high_ticks;
} sync_io_run_output_snapshot_t;

/* Core0 STOP preparation only. Reserves the existing SYNC_IO scheduler,
 * shared arena, scheduled persona and physical SM/DMA before TDMA ARM.
 * Preparation forces safe low but emits no planned pulses. Clock lifetime and product STOP guard belong
 * to the capability caller. Duration is a finite debug lease in raw ticks. */
bool sync_io_run_output_prepare(uint32_t expected_hz, uint32_t duration_ms,
                                uint32_t *generation);
/* Fixed-width variant: one FIFO low-count word per pulse, high countdown
 * preloaded into the SM's ISR at STOP. high_ticks >= 2; every submitted edge
 * must have exactly this width. The paired prepare entry remains unchanged. */
bool sync_io_run_output_prepare_uniform(uint32_t expected_hz, uint32_t duration_ms,
                                      uint32_t high_ticks, uint32_t *generation);
/* Atomic cancellation intent; no hardware access. */
void sync_io_run_output_cancel(void);
/* Core1 mandatory service: bounded cancellation/fault/expiry and DMA abort
 * retirement. Does not allocate, log, wait or release the shared arena. */
void sync_io_run_output_service_core1(void);
/* Core1 only. True authorizes writing a new finite block to retired source
 * storage. DMA completion is not physical edge completion. */
bool sync_io_run_output_can_submit_core1(uint32_t generation);
/* True means the block was irrevocably admitted; subsequent hardware fault
 * remains visible in state/reason. False means this block was not admitted.
 * Count is 1..MAX_EDGES. Validate the whole finite block before writing the
 * retired DMA source; unused workspace words are never submitted. */
bool sync_io_run_output_submit_count_core1(uint32_t generation,
    const sync_io_run_output_edge_t *edges, uint32_t count);
/* Compatibility entry: exactly BLOCK_EDGES pulses. */
bool sync_io_run_output_submit_core1(uint32_t generation,
    const sync_io_run_output_edge_t edges[SYNC_IO_RUN_OUTPUT_BLOCK_EDGES]);
/* Core0 only, after Core1 RETIRED acknowledgement. Does not cancel live DMA. */
bool sync_io_run_output_release(uint32_t generation);
bool sync_io_run_output_snapshot(sync_io_run_output_snapshot_t *out);

#endif
