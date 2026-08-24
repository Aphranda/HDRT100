#ifndef CALIBRATION_TRAINING_MARKER_H
#define CALIBRATION_TRAINING_MARKER_H

#include <stdbool.h>
#include <stdint.h>

#define CALIBRATION_TRAINING_MARKER_SNAPSHOT_VERSION 2u
#define CALIBRATION_TRAINING_MARKER_SNAPSHOT_READ_ATTEMPTS 64u
#define CALIBRATION_TRAINING_MARKER_MAX_SLOTS 8u
#define CALIBRATION_TRAINING_MARKER_MAX_ABS_OFFSET_SAMPLES 1

typedef enum {
    CALIBRATION_TRAINING_MARKER_IDLE = 0u,
    CALIBRATION_TRAINING_MARKER_PREPARED = 1u,
    CALIBRATION_TRAINING_MARKER_RUNNING = 2u,
    CALIBRATION_TRAINING_MARKER_ACCEPTED = 3u,
    CALIBRATION_TRAINING_MARKER_REJECTED = 4u,
} calibration_training_marker_state_t;

typedef enum {
    CALIBRATION_TRAINING_MARKER_ROLE_NONE = 0u,
    CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR = 1u,
    CALIBRATION_TRAINING_MARKER_ROLE_FOLLOWER = 2u,
} calibration_training_marker_role_t;

typedef enum {
    CALIBRATION_TRAINING_MARKER_REJECT_NONE = 0u,
    CALIBRATION_TRAINING_MARKER_REJECT_BAD_ARGUMENT = 1u,
    CALIBRATION_TRAINING_MARKER_REJECT_GENERATION = 2u,
    CALIBRATION_TRAINING_MARKER_REJECT_EPOCH = 3u,
    CALIBRATION_TRAINING_MARKER_REJECT_SEQUENCE = 4u,
    CALIBRATION_TRAINING_MARKER_REJECT_MARKER_ID = 5u,
    CALIBRATION_TRAINING_MARKER_REJECT_CRC = 6u,
    CALIBRATION_TRAINING_MARKER_REJECT_EVIDENCE_FLAGS = 7u,
    CALIBRATION_TRAINING_MARKER_REJECT_EDGE_ORDER = 8u,
    CALIBRATION_TRAINING_MARKER_REJECT_DMA = 9u,
    CALIBRATION_TRAINING_MARKER_REJECT_PIO_STALL = 10u,
    CALIBRATION_TRAINING_MARKER_REJECT_TIMEOUT = 11u,
} calibration_training_marker_reject_reason_t;

#define CALIBRATION_TRAINING_MARKER_FLAG_DIAGNOSTIC_ONLY (1u << 0u)
#define CALIBRATION_TRAINING_MARKER_FLAG_HARDWARE_LATCHED (1u << 1u)
#define CALIBRATION_TRAINING_MARKER_FLAG_CAPTURE_VALID (1u << 2u)
#define CALIBRATION_TRAINING_MARKER_FLAG_FORWARD_VALID (1u << 3u)
#define CALIBRATION_TRAINING_MARKER_FLAG_CRC_VALID (1u << 4u)
#define CALIBRATION_TRAINING_MARKER_FLAG_EPOCH_VALID (1u << 5u)
#define CALIBRATION_TRAINING_MARKER_FLAG_SEQUENCE_VALID (1u << 6u)
#define CALIBRATION_TRAINING_MARKER_FLAG_POLARITY_VALID (1u << 7u)
#define CALIBRATION_TRAINING_MARKER_FLAG_DMA_COMPLETE (1u << 8u)
#define CALIBRATION_TRAINING_MARKER_REQUIRED_FLAGS \
    (CALIBRATION_TRAINING_MARKER_FLAG_HARDWARE_LATCHED | \
     CALIBRATION_TRAINING_MARKER_FLAG_CAPTURE_VALID | \
     CALIBRATION_TRAINING_MARKER_FLAG_FORWARD_VALID | \
     CALIBRATION_TRAINING_MARKER_FLAG_CRC_VALID | \
     CALIBRATION_TRAINING_MARKER_FLAG_EPOCH_VALID | \
     CALIBRATION_TRAINING_MARKER_FLAG_SEQUENCE_VALID | \
     CALIBRATION_TRAINING_MARKER_FLAG_POLARITY_VALID | \
     CALIBRATION_TRAINING_MARKER_FLAG_DMA_COMPLETE)

typedef struct {
    uint64_t board_unique_id;
    uint64_t build_id;
    uint32_t role;
    uint32_t logical_slot;
    uint32_t reference_slot;
    uint32_t predecessor_slot;
    uint32_t successor_slot;
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t marker_id;
    uint32_t marker_codebook_id;
    uint32_t marker_crc32;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t tick_resolution_ns;
    int32_t offset_sample_count;
} calibration_training_marker_request_t;

typedef struct {
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t marker_id;
    uint32_t observed_crc32;
    uint32_t polarity;
    uint32_t marker_flags;
    uint32_t correlation_reject_reason;
    uint32_t best_lag_sample;
    uint32_t best_distance;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t flags;
    uint64_t marker_capture_tick;
    uint64_t marker_forward_tick;
    uint64_t marker_return_tick;
    uint32_t dma_capture_count;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint32_t timeout_count;
    int32_t offset_sample_count;
} calibration_training_marker_evidence_t;

typedef struct {
    uint32_t version;
    uint32_t state;
    uint32_t reject_reason;
    uint32_t flags;
    uint64_t board_unique_id;
    uint64_t build_id;
    uint32_t role;
    uint32_t logical_slot;
    uint32_t reference_slot;
    uint32_t predecessor_slot;
    uint32_t successor_slot;
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t marker_id;
    uint32_t marker_codebook_id;
    uint32_t marker_crc32;
    uint32_t observed_crc32;
    uint32_t polarity;
    uint32_t marker_flags;
    uint32_t correlation_reject_reason;
    uint32_t best_lag_sample;
    uint32_t best_distance;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
    uint32_t tick_resolution_ns;
    int32_t offset_sample_count;
    uint64_t marker_capture_tick;
    uint64_t marker_forward_tick;
    uint64_t marker_return_tick;
    uint64_t forward_residence_ticks;
    uint64_t loop_rtt_ticks;
    uint32_t dma_capture_count;
    uint32_t dma_overrun_count;
    uint32_t pio_stall_count;
    uint32_t timeout_count;
} calibration_training_marker_snapshot_t;

typedef struct {
    volatile uint32_t guard;
    calibration_training_marker_snapshot_t snapshot;
} calibration_training_marker_store_t;

void calibration_training_marker_store_init(
    calibration_training_marker_store_t *store);
bool calibration_training_marker_publish_core1(
    calibration_training_marker_store_t *store,
    const calibration_training_marker_snapshot_t *snapshot);
bool calibration_training_marker_get_snapshot(
    const calibration_training_marker_store_t *store,
    calibration_training_marker_snapshot_t *snapshot);
bool calibration_training_marker_prepare_core1(
    calibration_training_marker_store_t *store,
    const calibration_training_marker_request_t *request);
bool calibration_training_marker_evaluate_core1(
    calibration_training_marker_store_t *store,
    const calibration_training_marker_request_t *request,
    const calibration_training_marker_evidence_t *evidence);

#endif
