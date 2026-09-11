#ifndef SYNC_IO_ANALYZER_BURST_H
#define SYNC_IO_ANALYZER_BURST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sync_io_persona_resources.h"

/* Diagnostic capture format, not a VDC timestamp source. Two PIO instructions
 * per simultaneous six-pad sample; five samples occupy one 30-bit word. */
#define SYNC_IO_ANALYZER_BURST_SCHEMA 1u
#define SYNC_IO_ANALYZER_BURST_SAMPLE_BITS 6u
#define SYNC_IO_ANALYZER_BURST_SAMPLES_PER_WORD 5u
#define SYNC_IO_ANALYZER_BURST_SAMPLE_CYCLES 2u
#define SYNC_IO_ANALYZER_BURST_MAX_WORDS SYNC_IO_SHARED_WORKSPACE_WORDS
#define SYNC_IO_ANALYZER_BURST_MAX_DIVIDER 16u
#define SYNC_IO_ANALYZER_BURST_MAX_TIMEOUT_US 5000000u
#define SYNC_IO_ANALYZER_BURST_COPY_WORDS 1024u

typedef enum {
    SYNC_IO_ANALYZER_BURST_IDLE = 0u,
    SYNC_IO_ANALYZER_BURST_CAPTURING,
    SYNC_IO_ANALYZER_BURST_FROZEN,
    SYNC_IO_ANALYZER_BURST_RELEASED,
    SYNC_IO_ANALYZER_BURST_REJECTED,
} sync_io_analyzer_burst_state_t;

typedef enum {
    SYNC_IO_ANALYZER_BURST_END_NONE = 0u,
    SYNC_IO_ANALYZER_BURST_END_COMPLETE,
    SYNC_IO_ANALYZER_BURST_END_STOP,
    SYNC_IO_ANALYZER_BURST_END_TIMEOUT,
    SYNC_IO_ANALYZER_BURST_END_RX_STALL,
    SYNC_IO_ANALYZER_BURST_END_DMA_ERROR,
    SYNC_IO_ANALYZER_BURST_END_RESOURCE,
} sync_io_analyzer_burst_end_t;

typedef struct {
    uint32_t word_count;
    uint32_t clkdiv;
    uint32_t timeout_us;
    uint32_t trigger_rx; /* 0: local TX frame sync, 1: local RX frame sync */
    uint32_t capture_tag; /* opaque host correlation tag, never a timestamp */
} sync_io_analyzer_burst_config_t;

/* All fields have one writer (Core1), published as one versioned snapshot.
 * Sample zero is the first retained IN instruction, not a software epoch. */
typedef struct {
    uint32_t schema;
    uint32_t state;
    uint32_t capture_sequence;
    uint32_t end_reason;
    uint32_t timing_valid;
    uint32_t requested_words;
    uint32_t captured_words;
    uint32_t clk_sys_hz;
    uint32_t clkdiv_256;
    uint32_t sample_cycles;
    uint32_t samples_per_word;
    uint32_t sample_bits;
    uint32_t pin_base;
    uint32_t source_mask;
    uint32_t trigger_pin;
    uint32_t trigger_level;
    uint32_t profile_identity;
    uint32_t persona_generation;
    uint32_t pio_fdebug;
    uint32_t dma_ctrl;
    uint32_t manager_error;
    uint32_t conflict_mask;
    uint32_t capture_tag;
    uint32_t dma_remaining;
} sync_io_analyzer_burst_snapshot_t;

/* Core0 export owner facts are separate from immutable Core1 capture facts. */
typedef struct {
    uint32_t capture_sequence;
    uint32_t failure_count;
    uint32_t last_failed_job;
    uint32_t last_error;
    uint32_t retry_count;
} sync_io_analyzer_burst_export_snapshot_t;

bool sync_io_analyzer_burst_get_export_snapshot(
    sync_io_analyzer_burst_export_snapshot_t *snapshot);
void sync_io_analyzer_burst_begin_export_core0(uint32_t sequence);
void sync_io_analyzer_burst_export_failed_core0(uint32_t job, uint32_t error);
bool sync_io_analyzer_burst_take_export_retry_core0(uint32_t sequence);
bool sync_io_analyzer_burst_retry_export_core1(uint32_t sequence);

bool sync_io_analyzer_burst_config_valid(
    const sync_io_analyzer_burst_config_t *config);
bool sync_io_analyzer_burst_get_snapshot(
    sync_io_analyzer_burst_snapshot_t *snapshot);
bool sync_io_analyzer_burst_busy(void);
/* Single Core0 exporter only. Data stays immutable until that same exporter
 * acknowledges the capture through the analyzer mailbox after SD completion. */
size_t sync_io_analyzer_burst_copy_core0(
    uint32_t sequence, uint32_t first_word, uint32_t *words, uint32_t capacity);

/* Analyzer owner backend: called only by its mandatory Core1 mailbox/service. */
bool sync_io_analyzer_burst_begin_core1(
    const sync_io_analyzer_burst_config_t *config);
void sync_io_analyzer_burst_service_core1(void);
void sync_io_analyzer_burst_stop_core1(void);
bool sync_io_analyzer_burst_release_core1(uint32_t sequence);

#endif
