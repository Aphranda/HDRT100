#ifndef DIAGNOSTICS_TDMA_RECORD_H
#define DIAGNOSTICS_TDMA_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Debug evidence format, independent of the TDMA wire and physical clock. */
#define DIAGNOSTICS_TDMA_RECORD_MAGIC 0x524D4454u
#define DIAGNOSTICS_TDMA_RECORD_SCHEMA 3u
#define DIAGNOSTICS_TDMA_RECORD_SAMPLE_MAGIC 0x504D4153u
#define DIAGNOSTICS_TDMA_RECORD_END_MAGIC 0x444E4554u
#define DIAGNOSTICS_TDMA_RECORD_VALID 0x3Fu
#define DIAGNOSTICS_TDMA_RECORD_MAX_SAMPLES 1000u
#define DIAGNOSTICS_TDMA_RECORD_MIN_INTERVAL_US 1000u
#define DIAGNOSTICS_TDMA_RECORD_MAX_INTERVAL_US 1000000u
#define RECORD_U32(group, name, expression) + 1u
#define RECORD_I32(group, name, expression) + 1u
#define RECORD_U64(group, name, expression) + 2u
enum { DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS = 0u
#include "diagnostics_tdma_record_fields.def"
};
#undef RECORD_U32
#undef RECORD_I32
#undef RECORD_U64

typedef enum {
    TDMA_RECORD_IDLE, TDMA_RECORD_PREPARING, TDMA_RECORD_REQUESTED,
    TDMA_RECORD_ARMED, TDMA_RECORD_RUNNING, TDMA_RECORD_FROZEN,
    TDMA_RECORD_SAVE_REQUESTED, TDMA_RECORD_SAVING, TDMA_RECORD_SAVED,
    TDMA_RECORD_FAILED
} diagnostics_tdma_record_state_t;
typedef enum {
    TDMA_RECORD_COMPLETE, TDMA_RECORD_CANCELLED, TDMA_RECORD_OVERFLOW,
    TDMA_RECORD_STORAGE_ERROR
} diagnostics_tdma_record_reason_t;

typedef struct {
    bool (*begin)(uint32_t epoch, uint32_t *capacity);
    bool (*append)(uint32_t offset, const uint8_t *data, size_t size);
    bool (*save)(uint32_t crc32, uint32_t *job_id);
    /* 0 pending, 1 saved, -1 failed; only called after save submission. */
    int (*save_result)(uint32_t job_id);
    uint32_t (*snapshot)(uint32_t *words);
    uint64_t (*now_us)(void);
    void (*identity)(uint64_t *build, uint64_t *board);
} diagnostics_tdma_record_port_t;

typedef struct {
    uint32_t state, epoch, interval_us, requested, written, missed;
    uint32_t reason, bytes, job_id;
} diagnostics_tdma_record_status_t;

/* Requests originate on Core0. The sole recorder service runs on the existing
 * Core0 Storage task; no Core1 callback, filesystem or hardware resource lease
 * is added to TDMA. Status is unavailable during acquisition. */
bool diagnostics_tdma_record_arm(uint32_t epoch, uint32_t interval_us,
                                 uint32_t samples);
void diagnostics_tdma_record_start(uint64_t requested_us);
void diagnostics_tdma_record_cancel(void);
bool diagnostics_tdma_record_save(void);
bool diagnostics_tdma_record_status(diagnostics_tdma_record_status_t *out);
void diagnostics_tdma_record_service(const diagnostics_tdma_record_port_t *port);

#endif
