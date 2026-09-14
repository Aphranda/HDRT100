#ifndef TDMA_ORIGIN_HANDOFF_H
#define TDMA_ORIGIN_HANDOFF_H

#include "tdma_origin_plan.h"

typedef enum {
    TDMA_ORIGIN_PREPARE_IDLE = 0u,
    TDMA_ORIGIN_PREPARE_MAILBOX,
    TDMA_ORIGIN_PREPARE_STOP,
    TDMA_ORIGIN_PREPARE_PERSONA,
    TDMA_ORIGIN_PREPARE_BUILD_BEGIN,
    TDMA_ORIGIN_PREPARE_BUILD_STEP,
    TDMA_ORIGIN_PREPARE_SEED,
    TDMA_ORIGIN_PREPARE_SMS,
    TDMA_ORIGIN_PREPARE_INSTALL,
    TDMA_ORIGIN_PREPARE_COMPLETE,
    TDMA_ORIGIN_PREPARE_FAILED,
} tdma_origin_prepare_stage_t;

#define TDMA_ORIGIN_HANDOFF_SCHEMA 1u
#define TDMA_ORIGIN_HANDOFF_STAGES (TDMA_ORIGIN_PREPARE_FAILED + 1u)

/* Software preparation boundaries, NOT physical edges. first_ticks is valid
 * only when the corresponding calls is nonzero. Work sums measured poll
 * bodies; elapsed includes the intervening scheduler and Core0 wait time. */
typedef struct {
    uint32_t schema, trial_epoch, config_seq, clock_hz, result;
    uint32_t invalid_count, elapsed_ticks;
    uint64_t begin_ticks;
    uint32_t first_ticks[TDMA_ORIGIN_HANDOFF_STAGES];
    uint32_t work_ticks[TDMA_ORIGIN_HANDOFF_STAGES];
    uint16_t calls[TDMA_ORIGIN_HANDOFF_STAGES];
} tdma_origin_handoff_snapshot_t;

typedef struct {
    uint32_t guard;
    tdma_origin_handoff_snapshot_t snapshot;
} tdma_origin_handoff_t;

/* Sole Core1 writer. The caller supplies time; this recorder has no MMIO,
 * grant, scheduling or hardware control authority. */
void tdma_origin_handoff_begin(tdma_origin_handoff_t *record, uint32_t trial_epoch,
    uint32_t config_seq, uint32_t clock_hz, uint64_t begin_ticks);
void tdma_origin_handoff_record(tdma_origin_handoff_t *record, uint32_t stage,
    uint64_t started, uint64_t ended, tdma_origin_build_result_t result);
/* One guarded attempt. STOP/ACK admission belongs to the runtime owner. A
 * BUSY result after STOP describes an incomplete handoff, never success. */
bool tdma_origin_handoff_get(const tdma_origin_handoff_t *record,
    tdma_origin_handoff_snapshot_t *out);

#endif
