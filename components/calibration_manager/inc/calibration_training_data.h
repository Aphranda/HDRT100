#ifndef CALIBRATION_TRAINING_DATA_H
#define CALIBRATION_TRAINING_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "calibration_training_phase.h"

#define CALIBRATION_TRAINING_DATA_SNAPSHOT_VERSION 4u
#define CALIBRATION_TRAINING_DATA_SNAPSHOT_READ_ATTEMPTS 64u
#define CALIBRATION_TRAINING_DATA_MAX_NODES \
    CALIBRATION_TRAINING_PHASE_MAX_NODES
#define CALIBRATION_TRAINING_DATA_MIN_OFFSET_SAMPLES \
    CALIBRATION_TRAINING_PHASE_MIN_OFFSET_SAMPLES
#define CALIBRATION_TRAINING_DATA_MAX_OFFSET_SAMPLES \
    CALIBRATION_TRAINING_PHASE_MAX_OFFSET_SAMPLES
#define CALIBRATION_TRAINING_DATA_MAX_GUARD_SAMPLES 64u

typedef enum {
    CALIBRATION_TRAINING_DATA_IDLE = 0u,
    CALIBRATION_TRAINING_DATA_PREPARED = 1u,
    CALIBRATION_TRAINING_DATA_RUNNING = 2u,
    CALIBRATION_TRAINING_DATA_ACCEPTED = 3u,
    CALIBRATION_TRAINING_DATA_REJECTED = 4u,
} calibration_training_data_state_t;

typedef enum {
    CALIBRATION_TRAINING_DATA_REJECT_NONE = 0u,
    CALIBRATION_TRAINING_DATA_REJECT_BAD_ARGUMENT = 1u,
    CALIBRATION_TRAINING_DATA_REJECT_GENERATION = 2u,
    CALIBRATION_TRAINING_DATA_REJECT_EPOCH = 3u,
    CALIBRATION_TRAINING_DATA_REJECT_SEQUENCE = 4u,
    CALIBRATION_TRAINING_DATA_REJECT_CRC = 5u,
    CALIBRATION_TRAINING_DATA_REJECT_EVIDENCE_FLAGS = 6u,
    CALIBRATION_TRAINING_DATA_REJECT_CORRELATION = 7u,
    CALIBRATION_TRAINING_DATA_REJECT_POLARITY = 8u,
    CALIBRATION_TRAINING_DATA_REJECT_CAPTURE_TRUNCATED = 9u,
    CALIBRATION_TRAINING_DATA_REJECT_DMA = 10u,
    CALIBRATION_TRAINING_DATA_REJECT_PIO_STALL = 11u,
    CALIBRATION_TRAINING_DATA_REJECT_TIMEOUT = 12u,
    CALIBRATION_TRAINING_DATA_REJECT_DISTANCE = 13u,
    CALIBRATION_TRAINING_DATA_REJECT_MARGIN = 14u,
    CALIBRATION_TRAINING_DATA_REJECT_SEARCH_RANGE = 15u,
    CALIBRATION_TRAINING_DATA_REJECT_EDGE_ORDER = 16u,
} calibration_training_data_reject_reason_t;

#define CALIBRATION_TRAINING_DATA_FLAG_DIAGNOSTIC_ONLY (1u << 0u)
#define CALIBRATION_TRAINING_DATA_FLAG_HARDWARE_MARKER (1u << 1u)
#define CALIBRATION_TRAINING_DATA_FLAG_HARDWARE_DATA_CAPTURE (1u << 2u)
#define CALIBRATION_TRAINING_DATA_FLAG_DMA_COMPLETE (1u << 3u)
#define CALIBRATION_TRAINING_DATA_FLAG_CRC_VALID (1u << 4u)
#define CALIBRATION_TRAINING_DATA_FLAG_EPOCH_VALID (1u << 5u)
#define CALIBRATION_TRAINING_DATA_FLAG_SEQUENCE_VALID (1u << 6u)
#define CALIBRATION_TRAINING_DATA_FLAG_POLARITY_VALID (1u << 7u)
#define CALIBRATION_TRAINING_DATA_REQUIRED_FLAGS \
    (CALIBRATION_TRAINING_DATA_FLAG_HARDWARE_MARKER | \
     CALIBRATION_TRAINING_DATA_FLAG_HARDWARE_DATA_CAPTURE | \
     CALIBRATION_TRAINING_DATA_FLAG_DMA_COMPLETE | \
     CALIBRATION_TRAINING_DATA_FLAG_CRC_VALID | \
     CALIBRATION_TRAINING_DATA_FLAG_EPOCH_VALID | \
     CALIBRATION_TRAINING_DATA_FLAG_SEQUENCE_VALID | \
     CALIBRATION_TRAINING_DATA_FLAG_POLARITY_VALID)

typedef struct {
    uint64_t board_unique_id;
    uint64_t build_id;
    uint32_t source_node;
    uint32_t destination_node;
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t data_codebook_id;
    uint32_t data_crc32;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t sample_period_ns;
    uint32_t marker_to_data_samples;
    uint32_t link_base_delay_ns;
    int32_t marker_offset_sample_count;
    int32_t configured_data_offset_sample_count;
    int32_t search_start_offset_sample;
    int32_t search_end_offset_sample;
    uint32_t guard_sample_count;
    uint32_t expected_polarity;
    uint32_t max_best_distance;
    uint32_t min_margin;
    uint32_t diagnostic_fault_flags;
    uint32_t diagnostic_wire_epoch;
    uint32_t diagnostic_header_crc8_xor;
} calibration_training_data_request_t;

typedef struct {
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t observed_crc32;
    uint32_t observed_header_fields_valid;
    uint32_t observed_header;
    uint32_t observed_header_inverse;
    uint32_t observed_header_crc8;
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
    uint64_t marker_capture_tick;
    uint64_t data_capture_tick;
} calibration_training_data_evidence_t;

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
    uint32_t data_codebook_id;
    uint32_t data_crc32;
    uint32_t observed_crc32;
    uint32_t observed_header_fields_valid;
    uint32_t observed_header;
    uint32_t observed_header_inverse;
    uint32_t observed_header_crc8;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t sample_period_ns;
    uint32_t marker_to_data_samples;
    uint32_t link_base_delay_ns;
    int32_t marker_offset_sample_count;
    int32_t configured_data_offset_sample_count;
    int32_t search_start_offset_sample;
    int32_t search_end_offset_sample;
    uint32_t guard_sample_count;
    uint32_t max_best_distance;
    uint32_t min_margin;
    uint32_t diagnostic_fault_flags;
    uint32_t diagnostic_wire_epoch;
    uint32_t diagnostic_header_crc8_xor;
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
    int32_t marker_data_skew_ns;
    uint32_t captured_sample_count;
    uint32_t expected_sample_count;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint32_t timeout_count;
    uint64_t marker_capture_tick;
    uint64_t data_capture_tick;
} calibration_training_data_snapshot_t;

typedef struct {
    volatile uint32_t guard;
    calibration_training_data_snapshot_t snapshot;
} calibration_training_data_store_t;

void calibration_training_data_store_init(
    calibration_training_data_store_t *store);
bool calibration_training_data_publish_core1(
    calibration_training_data_store_t *store,
    const calibration_training_data_snapshot_t *snapshot);
bool calibration_training_data_get_snapshot(
    const calibration_training_data_store_t *store,
    calibration_training_data_snapshot_t *snapshot);
bool calibration_training_data_prepare_core1(
    calibration_training_data_store_t *store,
    const calibration_training_data_request_t *request);
bool calibration_training_data_evaluate_core1(
    calibration_training_data_store_t *store,
    const calibration_training_data_request_t *request,
    const calibration_training_data_evidence_t *evidence);

#endif
