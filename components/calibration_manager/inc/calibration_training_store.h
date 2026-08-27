#ifndef CALIBRATION_TRAINING_STORE_H
#define CALIBRATION_TRAINING_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_ring_runtime.h"

#define CALIBRATION_TRAINING_STORE_SCHEMA_VERSION 1u
#define CALIBRATION_TRAINING_STORE_OBJECT_TYPE 4u
#define CALIBRATION_TRAINING_STORE_PAYLOAD_MAGIC 0x334E5254u
#define CALIBRATION_TRAINING_STORE_PAYLOAD_VERSION 1u
#define CALIBRATION_TRAINING_STORE_PAYLOAD_SIZE 1072u

typedef enum {
    CALIBRATION_TRAINING_STORE_REJECT_NONE = 0u,
    CALIBRATION_TRAINING_STORE_REJECT_EMPTY = 1u,
    CALIBRATION_TRAINING_STORE_REJECT_FLASH = 2u,
    CALIBRATION_TRAINING_STORE_REJECT_RECORD = 3u,
    CALIBRATION_TRAINING_STORE_REJECT_PAYLOAD = 4u,
    CALIBRATION_TRAINING_STORE_REJECT_STAGE = 5u,
    CALIBRATION_TRAINING_STORE_REJECT_REPLAY = 6u,
    CALIBRATION_TRAINING_STORE_REJECT_CONTEXT = 7u,
} calibration_training_store_reject_t;

typedef struct {
    uint32_t persisted_valid;
    uint32_t loaded;
    uint32_t restore_pending;
    uint32_t reject_reason;
    uint32_t record_generation;
    uint32_t record_sequence;
    uint32_t payload_crc32;
    uint32_t calibration_generation;
    uint32_t topology_generation;
    uint32_t topology_crc32;
    uint32_t profile_crc32;
    uint32_t schedule_crc32;
} calibration_training_store_status_t;

/* Stable little-endian payload codec.  The TDMA C structure is never copied
 * directly to Flash, so compiler padding and future in-memory changes cannot
 * silently alter the persisted wire format. */
bool calibration_training_store_encode_payload(
    const tdma_ring_calibration_stage_t *stage,
    uint8_t *payload,
    size_t payload_capacity);
bool calibration_training_store_decode_payload(
    const uint8_t *payload,
    size_t payload_size,
    tdma_ring_calibration_stage_t *stage);

/* CalibrationManager is the sole caller of the physical store lifecycle. */
bool calibration_training_store_init(void);
bool calibration_training_store_get_stage(
    tdma_ring_calibration_stage_t *stage);
bool calibration_training_store_commit(
    const tdma_ring_calibration_stage_t *stage);
void calibration_training_store_set_loaded(bool loaded,
                                           uint32_t reject_reason);
void calibration_training_store_get_status(
    calibration_training_store_status_t *status);

#endif
