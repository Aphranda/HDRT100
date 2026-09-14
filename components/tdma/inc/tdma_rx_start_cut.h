#ifndef TDMA_RX_START_CUT_H
#define TDMA_RX_START_CUT_H

#include <stdbool.h>
#include <stdint.h>

#define TDMA_RX_START_CUT_SCHEMA 1u

enum {
    TDMA_RX_START_CUT_DIAGNOSTIC = 1u << 0,
    TDMA_RX_START_CUT_RECORDED = 1u << 1,
    TDMA_RX_START_CUT_UNRESOLVED = 1u << 2,
    TDMA_RX_START_CUT_INFLIGHT_UNKNOWN = 1u << 3,
    TDMA_RX_START_CUT_PRESTART_BACKLOG_UNEXCLUDED = 1u << 4,
    TDMA_RX_START_CUT_RETIRED = 1u << 5,
    TDMA_RX_START_CUT_STOPPED = 1u << 6,
    TDMA_RX_START_CUT_COUNT_WRAP = 1u << 7
};

/* Sticky reasons retire this diagnostic cut, never the healthy wire path or
 * the independent event observer. STOP adds its reason without erasing one
 * observed earlier. No exact-coordinate, identity or timestamp flag exists. */
enum {
    TDMA_RX_START_CUT_STOP = 1u << 0,
    TDMA_RX_START_CUT_CAPTURE_RXSTALL = 1u << 1,
    TDMA_RX_START_CUT_DMA_ERROR = 1u << 2,
    TDMA_RX_START_CUT_DMA_CONFIG = 1u << 3,
    TDMA_RX_START_CUT_OBSERVATION_EPOCH = 1u << 4,
    TDMA_RX_START_CUT_GEOMETRY = 1u << 5,
    TDMA_RX_START_CUT_ARM = 1u << 6,
    TDMA_RX_START_CUT_OBSERVER = 1u << 7,
    TDMA_RX_START_CUT_DIRTY_START = 1u << 8,
    TDMA_RX_START_CUT_COUNTER = 1u << 9,
    TDMA_RX_START_CUT_CLOCK = 1u << 10,
    TDMA_RX_START_CUT_CAPTURE_DISABLED = 1u << 11
};

/* One first-actual-enable record. The total clk_sys tick bracket encompasses
 * both DMA count reads, both pad reads and observer enable; it is deliberately
 * wider than the existing event start bracket. Counts retain all hardware mode
 * bits. Produced coordinates come from a LOCAL copy of the real counter and
 * are not a physical-frame anchor. A changed observation epoch, even with no
 * count advance, invalidates their comparison with an older capture token.
 * FIFO/PC are raw observations of the process-follower data capture SM.
 * Equal counts and empty FIFOs always retain INFLIGHT_UNKNOWN/UNRESOLVED.
 * Raw fields never change after capture; only flags/reasons may accumulate. */
typedef struct {
    uint64_t arm_epoch;
    uint64_t observation_epoch_before;
    uint64_t observation_epoch_after;
    uint64_t sample_before_ticks;
    uint64_t produced_before;
    uint64_t produced_after;
    uint64_t sample_after_ticks;
    uint32_t schema;
    uint32_t flags;
    uint32_t retire_reasons;
    uint32_t observer_epoch;
    uint32_t pio_hz;
    uint32_t physical_frame_words;
    uint32_t alignment_byte_shift;
    uint32_t alignment_bit_shift;
    uint32_t dma_count_before;
    uint32_t dma_count_after;
    uint32_t dma_ctrl_before;
    uint32_t dma_ctrl_after;
    uint32_t capture_fdebug_before;
    uint32_t capture_fdebug_after;
    uint32_t pads_before;
    uint32_t pads_after;
    uint32_t dma_channel;
    uint8_t capture_fifo_before;
    uint8_t capture_fifo_after;
    uint8_t capture_pc_before;
    uint8_t capture_pc_after;
} tdma_rx_start_cut_t;

_Static_assert(sizeof(tdma_rx_start_cut_t) == 128u,
               "First-observer cut must stay an independent bounded record");

/* Core0 copies one frozen record only after the physical STOP and cancellation
 * ACK have completed. Caller also checks its ring/workflow STOP state. There
 * is no live PIO/DMA access or query-time qualification; a timer read bounds
 * the local copy lifetime. Three bounded consistency
 * attempts; failures clear a non-NULL output. New ARM immediately retires the
 * exported generation, including unsuccessful or other-persona ARM attempts.
 * The output must not alias the owner's storage. Feature-disabled returns false. */
bool tdma_pio_spi_phys_get_rx_start_cut(tdma_rx_start_cut_t *out);

#endif
