#ifndef SYNC_IO_SWITCH_H
#define SYNC_IO_SWITCH_H
#include <stdbool.h>
#include <stdint.h>

#define SYNC_IO_SWITCH_SCHEMA 1u
#define SYNC_IO_SWITCH_FIRST_SM 1u
#define SYNC_IO_SWITCH_SECOND_SM 3u
#define SYNC_IO_SWITCH_INSTRUCTION_WORDS 4u
#define SYNC_IO_SWITCH_MAX_GROUPS 2u

typedef enum { SYNC_IO_SWITCH_IDLE, SYNC_IO_SWITCH_LOADED,
    SYNC_IO_SWITCH_STAGED, SYNC_IO_SWITCH_PENDING, SYNC_IO_SWITCH_COMPLETE,
    SYNC_IO_SWITCH_STOPPED, SYNC_IO_SWITCH_FAULT } sync_io_switch_state_t;
typedef struct {
    uint32_t sm, output_base_pin, output_count, code, tx_level, rx_level;
} sync_io_switch_group_t;
typedef struct {
    uint32_t schema, generation, state, loaded, output_mask, group_count;
    uint32_t desired_code, applied_code, applied_valid, stage_sequence, completed_sequence;
    uint32_t cancelled_sequence, sm_claimed_mask, sm_enabled_mask, instruction_words;
    /* actual_output_mask reads all public OUT pads, including unleased pins,
     * so diagnostics can compare unselected outputs without driving them. */
    uint32_t program_offset, actual_output_mask, receipt_mask, fault;
    sync_io_switch_group_t groups[SYNC_IO_SWITCH_MAX_GROUPS];
} sync_io_switch_snapshot_t;

/* Core0 STOP-maintenance owner only. A nonzero logical mask selects any
 * OUT1..OUT4 subset. Unselected output/input pads are not modified. LOAD
 * hotloads one shared PIO0 program, initializes up to two contiguous groups,
 * holds selected outputs low, and leaves both SMs disabled. No DMA/IRQ/arena.
 * STAGE queues one immutable local code per group, APPLY starts them together
 * and returns immediately. This is maintenance validation, NOT an absolute-q
 * reservation executor or a READY/fence. Duplicate APPLY requires new STAGE;
 * STAGE cannot replace a pending transaction. A repeated code is an explicit
 * new transaction with a new sequence. STOP safely cancels, never completes.
 * RELEASE restores selected CTRL/PAD/SIO and PIO0 output/OE latches exactly. */
bool sync_io_switch_load(uint32_t output_mask,uint32_t *generation);
bool sync_io_switch_stage(uint32_t generation,uint32_t code);
bool sync_io_switch_apply(uint32_t generation);
bool sync_io_switch_stop(uint32_t generation);
bool sync_io_switch_release(uint32_t generation);
/* One bounded poll drains at most one receipt per active group. COMPLETE
 * requires all executed-word receipts to match; it is not an external device
 * settled/READY indication. Routing/FIFO/pad fields are actual hardware reads
 * while leased. IDLE never reads released SM state. */
bool sync_io_switch_snapshot(sync_io_switch_snapshot_t *out);
/* Core1 RUN owner: fire one already-loaded local code at the current cursor.
 * This path never loads, changes routing, or waits for a receipt. */
bool sync_io_switch_realtime_fire(uint32_t code);
void sync_io_switch_realtime_service_core1(void);
#endif
