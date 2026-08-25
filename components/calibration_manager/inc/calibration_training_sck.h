#ifndef CALIBRATION_TRAINING_SCK_H
#define CALIBRATION_TRAINING_SCK_H

#include <stdbool.h>
#include <stdint.h>

#include "calibration_training_phase.h"

#define CALIBRATION_TRAINING_SCK_SNAPSHOT_VERSION 3u
#define CALIBRATION_TRAINING_SCK_SNAPSHOT_READ_ATTEMPTS 64u
#define CALIBRATION_TRAINING_SCK_MAX_NODES \
    CALIBRATION_TRAINING_PHASE_MAX_NODES
#define CALIBRATION_TRAINING_SCK_MIN_OFFSET_SAMPLES \
    CALIBRATION_TRAINING_PHASE_MIN_OFFSET_SAMPLES
#define CALIBRATION_TRAINING_SCK_MAX_OFFSET_SAMPLES \
    CALIBRATION_TRAINING_PHASE_MAX_OFFSET_SAMPLES
#define CALIBRATION_TRAINING_SCK_MAX_GUARD_SAMPLES 64u
#define CALIBRATION_TRAINING_SCK_CAPTURE_PIPELINE_SAMPLES 1u

typedef enum {
    CALIBRATION_TRAINING_SCK_IDLE = 0u,
    CALIBRATION_TRAINING_SCK_PREPARED = 1u,
    CALIBRATION_TRAINING_SCK_RUNNING = 2u,
    CALIBRATION_TRAINING_SCK_ACCEPTED = 3u,
    CALIBRATION_TRAINING_SCK_REJECTED = 4u,
} calibration_training_sck_state_t;

typedef enum {
    CALIBRATION_TRAINING_SCK_REJECT_NONE = 0u,
    CALIBRATION_TRAINING_SCK_REJECT_BAD_ARGUMENT = 1u,
    CALIBRATION_TRAINING_SCK_REJECT_GENERATION = 2u,
    CALIBRATION_TRAINING_SCK_REJECT_EPOCH = 3u,
    CALIBRATION_TRAINING_SCK_REJECT_SEQUENCE = 4u,
    CALIBRATION_TRAINING_SCK_REJECT_CRC = 5u,
    CALIBRATION_TRAINING_SCK_REJECT_EVIDENCE_FLAGS = 6u,
    CALIBRATION_TRAINING_SCK_REJECT_CORRELATION = 7u,
    CALIBRATION_TRAINING_SCK_REJECT_POLARITY = 8u,
    CALIBRATION_TRAINING_SCK_REJECT_CAPTURE_TRUNCATED = 9u,
    CALIBRATION_TRAINING_SCK_REJECT_DMA = 10u,
    CALIBRATION_TRAINING_SCK_REJECT_PIO_STALL = 11u,
    CALIBRATION_TRAINING_SCK_REJECT_TIMEOUT = 12u,
    CALIBRATION_TRAINING_SCK_REJECT_DISTANCE = 13u,
    CALIBRATION_TRAINING_SCK_REJECT_MARGIN = 14u,
    CALIBRATION_TRAINING_SCK_REJECT_SEARCH_RANGE = 15u,
    CALIBRATION_TRAINING_SCK_REJECT_EDGE_ORDER = 16u,
} calibration_training_sck_reject_reason_t;

#define CALIBRATION_TRAINING_SCK_FLAG_DIAGNOSTIC_ONLY (1u << 0u)
#define CALIBRATION_TRAINING_SCK_FLAG_HARDWARE_SCK_ORIGIN (1u << 1u)
#define CALIBRATION_TRAINING_SCK_FLAG_HARDWARE_SCK_CAPTURE (1u << 2u)
#define CALIBRATION_TRAINING_SCK_FLAG_DMA_COMPLETE (1u << 3u)
#define CALIBRATION_TRAINING_SCK_FLAG_CRC_VALID (1u << 4u)
#define CALIBRATION_TRAINING_SCK_FLAG_EPOCH_VALID (1u << 5u)
#define CALIBRATION_TRAINING_SCK_FLAG_SEQUENCE_VALID (1u << 6u)
#define CALIBRATION_TRAINING_SCK_FLAG_POLARITY_VALID (1u << 7u)
#define CALIBRATION_TRAINING_SCK_REQUIRED_FLAGS \
    (CALIBRATION_TRAINING_SCK_FLAG_HARDWARE_SCK_ORIGIN | \
     CALIBRATION_TRAINING_SCK_FLAG_HARDWARE_SCK_CAPTURE | \
     CALIBRATION_TRAINING_SCK_FLAG_DMA_COMPLETE | \
     CALIBRATION_TRAINING_SCK_FLAG_CRC_VALID | \
     CALIBRATION_TRAINING_SCK_FLAG_EPOCH_VALID | \
     CALIBRATION_TRAINING_SCK_FLAG_SEQUENCE_VALID | \
     CALIBRATION_TRAINING_SCK_FLAG_POLARITY_VALID)

typedef struct {
    uint64_t board_unique_id;
    uint64_t build_id;
    uint32_t source_node;
    uint32_t destination_node;
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t sck_codebook_id;
    uint32_t sck_crc32;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t sample_period_ns;
    uint32_t sck_launch_guard_sample_count;
    uint32_t link_base_delay_ns;
    int32_t configured_sck_offset_sample_count;
    int32_t search_start_offset_sample;
    int32_t search_end_offset_sample;
    uint32_t guard_sample_count;
    uint32_t expected_polarity;
    uint32_t max_best_distance;
    uint32_t min_margin;
} calibration_training_sck_request_t;

typedef struct {
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t observed_crc32;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t flags;
    uint32_t polarity;
    uint32_t correlation_reject_reason;
    uint32_t best_lag_sample;
    uint32_t best_distance;
    uint32_t second_lag_sample;
    uint32_t second_distance;
    uint32_t margin;
    uint32_t captured_sample_count;
    uint32_t expected_sample_count;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint32_t timeout_count;
    uint64_t sck_capture_origin_tick;
    uint64_t sck_code_capture_tick;
} calibration_training_sck_evidence_t;

typedef struct {
    uint32_t version;
    uint32_t state;
    uint32_t reject_reason;
    uint32_t flags;
    uint64_t board_unique_id;
    uint64_t build_id;
    uint32_t source_node;
    uint32_t destination_node;
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t sck_codebook_id;
    uint32_t sck_crc32;
    uint32_t observed_crc32;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t sample_period_ns;
    uint32_t sck_launch_guard_sample_count;
    uint32_t link_base_delay_ns;
    int32_t configured_sck_offset_sample_count;
    int32_t search_start_offset_sample;
    int32_t search_end_offset_sample;
    uint32_t guard_sample_count;
    uint32_t polarity;
    uint32_t correlation_reject_reason;
    uint32_t best_lag_sample;
    uint32_t best_distance;
    uint32_t second_lag_sample;
    uint32_t second_distance;
    uint32_t margin;
    int32_t resolved_offset_sample_count;
    int32_t resolved_offset_ns;
    int32_t training_window_start_ns;
    int32_t training_window_end_ns;
    uint32_t captured_sample_count;
    uint32_t expected_sample_count;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint32_t timeout_count;
    uint64_t sck_capture_origin_tick;
    uint64_t sck_code_capture_tick;
} calibration_training_sck_snapshot_t;

typedef struct {
    volatile uint32_t guard;
    calibration_training_sck_snapshot_t snapshot;
} calibration_training_sck_store_t;

void calibration_training_sck_store_init(
    calibration_training_sck_store_t *store);
bool calibration_training_sck_publish_core1(
    calibration_training_sck_store_t *store,
    const calibration_training_sck_snapshot_t *snapshot);
bool calibration_training_sck_get_snapshot(
    const calibration_training_sck_store_t *store,
    calibration_training_sck_snapshot_t *snapshot);
bool calibration_training_sck_map_offset_to_phase_cycles(
    uint32_t link_base_delay_ns,
    uint32_t sample_period_ns,
    int32_t configured_offset_sample_count,
    uint32_t *source_phase_delay_cycles,
    uint32_t *destination_phase_delay_cycles);
bool calibration_training_sck_prepare_core1(
    calibration_training_sck_store_t *store,
    const calibration_training_sck_request_t *request);
bool calibration_training_sck_evaluate_core1(
    calibration_training_sck_store_t *store,
    const calibration_training_sck_request_t *request,
    const calibration_training_sck_evidence_t *evidence);

#endif
