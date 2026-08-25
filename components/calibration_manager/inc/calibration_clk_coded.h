#ifndef CALIBRATION_CLK_CODED_H
#define CALIBRATION_CLK_CODED_H

#include <stdbool.h>
#include <stdint.h>

#include "calibration_clk_marker.h"

#define CALIBRATION_CLK_CODED_SNAPSHOT_VERSION 1u
#define CALIBRATION_CLK_CODED_SNAPSHOT_READ_ATTEMPTS 64u

typedef enum {
    CALIBRATION_CLK_CODED_IDLE = 0u,
    CALIBRATION_CLK_CODED_CLOCK_COARSE = 1u,
    CALIBRATION_CLK_CODED_CLOCK_CODED = 2u,
    CALIBRATION_CLK_CODED_ACCEPTED = 3u,
    CALIBRATION_CLK_CODED_REJECTED = 4u,
} calibration_clk_coded_state_t;

#define CALIBRATION_CLK_CODED_FLAG_DIAGNOSTIC_ONLY (1u << 0u)
#define CALIBRATION_CLK_CODED_FLAG_TX_DMA_COMPLETE (1u << 1u)
#define CALIBRATION_CLK_CODED_FLAG_RX_DMA_COMPLETE (1u << 2u)
#define CALIBRATION_CLK_CODED_FLAG_COARSE_BRACKET_VALID (1u << 3u)
#define CALIBRATION_CLK_CODED_FLAG_CORRELATION_VALID (1u << 4u)
#define CALIBRATION_CLK_CODED_FLAG_HEADER_VALID (1u << 5u)

typedef struct {
    uint32_t version;
    uint32_t state;
    uint32_t reject_reason;
    uint32_t flags;
    uint64_t board_unique_id;
    uint64_t build_id;
    uint32_t local_node;
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t baud_hz;
    uint32_t codebook_id;
    uint32_t sample_period_ns;
    uint32_t coarse_min_sample;
    uint32_t coarse_max_sample;
    uint64_t capture_origin_tick;
    uint32_t capture_sample_count;
    uint32_t timing_field_tx_origin_sample;
    uint32_t best_lag_sample;
    uint32_t best_distance;
    uint32_t second_lag_sample;
    uint32_t second_distance;
    uint32_t margin;
    uint32_t detected_polarity;
    uint32_t marker_flags;
    uint32_t tx_dma_count;
    uint32_t rx_dma_count;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
} calibration_clk_coded_snapshot_t;

typedef struct {
    volatile uint32_t guard;
    calibration_clk_coded_snapshot_t snapshot;
} calibration_clk_coded_store_t;

typedef enum {
    CALIBRATION_CLK_CODED_REJECT_NONE = 0u,
    CALIBRATION_CLK_CODED_REJECT_BAD_ARGUMENT = 1u,
    CALIBRATION_CLK_CODED_REJECT_BAD_STATE = 2u,
    CALIBRATION_CLK_CODED_REJECT_GENERATION = 3u,
    CALIBRATION_CLK_CODED_REJECT_COARSE_BRACKET = 4u,
    CALIBRATION_CLK_CODED_REJECT_DMA = 5u,
    CALIBRATION_CLK_CODED_REJECT_PIO_STALL = 6u,
    CALIBRATION_CLK_CODED_REJECT_CORRELATION_BASE = 0x100u,
} calibration_clk_coded_reject_reason_t;

typedef struct {
    uint64_t board_unique_id;
    uint64_t build_id;
    uint32_t local_node;
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t baud_hz;
    uint32_t codebook_id;
    uint32_t sample_period_ns;
    uint32_t coarse_min_sample;
    uint32_t coarse_max_sample;
} calibration_clk_coded_request_t;

typedef struct {
    const uint32_t *capture_words;
    uint32_t capture_sample_count;
    uint64_t capture_origin_tick;
    uint32_t timing_field_tx_origin_sample;
    uint32_t tx_dma_count;
    uint32_t rx_dma_count;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t flags;
} calibration_clk_coded_evidence_t;

typedef struct {
    uint32_t expected_words[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    calibration_clk_marker_descriptor_t marker;
} calibration_clk_coded_workspace_t;

void calibration_clk_coded_store_init(calibration_clk_coded_store_t *store);
bool calibration_clk_coded_publish_core1(
    calibration_clk_coded_store_t *store,
    const calibration_clk_coded_snapshot_t *snapshot);
bool calibration_clk_coded_get_snapshot(
    const calibration_clk_coded_store_t *store,
    calibration_clk_coded_snapshot_t *snapshot);
bool calibration_clk_coded_begin_coarse_core1(
    calibration_clk_coded_store_t *store,
    const calibration_clk_coded_request_t *request);
bool calibration_clk_coded_reject_request_core1(
    calibration_clk_coded_store_t *store,
    const calibration_clk_coded_request_t *request,
    uint32_t reject_reason);
bool calibration_clk_coded_stop_core1(
    calibration_clk_coded_store_t *store);
bool calibration_clk_coded_process_core1(
    calibration_clk_coded_store_t *store,
    calibration_clk_coded_workspace_t *workspace,
    const calibration_clk_coded_evidence_t *evidence,
    const calibration_clk_correlation_gate_t *correlation_gate);

#endif
