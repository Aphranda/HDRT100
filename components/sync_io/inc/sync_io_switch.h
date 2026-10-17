#ifndef SYNC_IO_SWITCH_H
#define SYNC_IO_SWITCH_H
#include <stdbool.h>
#include <stdint.h>

#define SYNC_IO_SWITCH_SCHEMA 1u
#define SYNC_IO_SWITCH_FIRST_SM 1u
#define SYNC_IO_SWITCH_SECOND_SM 3u
#define SYNC_IO_SWITCH_INSTRUCTION_WORDS 8u
#define SYNC_IO_SWITCH_MAX_GROUPS 2u
#define SYNC_IO_SWITCH_DEADLINE_GUARD_US 100u
#define SYNC_IO_SWITCH_ORIGIN_MAX_NS 256u

/* Owner-only Core1 status; COMPLETE means matching PIO receipts, not settle.
 * A rejected origin is CANCELLED if stopped before deadline; otherwise
 * UNCERTAIN explicitly records that an edge may already have happened. */
enum { SYNC_IO_SWITCH_DEADLINE_IDLE, SYNC_IO_SWITCH_DEADLINE_ARMED,
    SYNC_IO_SWITCH_DEADLINE_COMPLETE, SYNC_IO_SWITCH_DEADLINE_CANCELLED,
    SYNC_IO_SWITCH_DEADLINE_UNCERTAIN };
typedef struct {
    uint32_t state, sequence, receipt_mask;
    uint64_t deadline_tick, origin_before_tick, origin_after_tick;
} sync_io_switch_deadline_status_t;

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
    /* Refusal buckets of one-shot stage_sequence(): NO_READY, BUSY, CLOCK, TICK, PLAN, ORIGIN. */
    uint32_t stage_reject[7];
    /* actual_output_mask reads all public OUT pads, including unleased pins,
     * so diagnostics can compare unselected outputs without driving them. */
    uint32_t program_offset, actual_output_mask, receipt_mask, fault;
    /* SYNC-SCHED-001: how many absolute deadlines are still outstanding (0..2). Two
     * triggers may be in flight; the hardware runs them back-to-back from the TX FIFO,
     * so the gap between them is tick-driven, not "software saw the first completion". */
    uint32_t pending_count;
    sync_io_switch_group_t groups[SYNC_IO_SWITCH_MAX_GROUPS];
} sync_io_switch_snapshot_t;

/* Core0 STOP-maintenance owner only. A nonzero logical mask selects any
 * OUT1..OUT4 subset. Unselected output/input pads are not modified. LOAD
 * hotloads one shared PIO0 program, initializes up to two contiguous groups,
 * holds selected outputs low, and leaves both SMs disabled. No DMA/IRQ/arena.
 * STAGE preloads one immutable local code per group, APPLY starts them together
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
/* Preloaded resources only. No IRQ masking or wait. The initial PIO origin
 * adds a nonnegative error bounded by origin_after-before (<500ns).
 * Guard is setup margin, not accuracy or a mandatory 100ms prediction lead.
 * Caller owns the frozen model, generation and absolute TIMER1 deadline. */
bool sync_io_switch_realtime_schedule_at(uint32_t code,uint64_t deadline_tick);
/* Refusal buckets of stage_sequence(): NO_READY, BUSY, CLOCK, TICK, PLAN, ORIGIN. */
void sync_io_switch_realtime_stage_reject(uint32_t *out);
void sync_io_switch_realtime_origin_span(uint32_t *span, uint32_t *bound);
/* Group sequence: a position's groups on the SHARED timeline, anchored at that position's
 * time slice. `delta_ticks[i]` is group i's offset from the base tick (strictly increasing,
 * same conversion as the deadline), so the whole sequence is written once and the spacing
 * between edges is decided by the table and the PIO tick counter - not by when software
 * happens to refill the FIFO. The first group is armed with the same timer read and origin
 * window as a single deadline; the remaining word pairs are refilled as the FIFO drains. */
bool sync_io_switch_realtime_stage_sequence(uint64_t base_deadline_tick,
    const uint32_t *codes,const uint64_t *delta_ticks,uint32_t count);
bool sync_io_switch_realtime_status(sync_io_switch_deadline_status_t *out);
bool sync_io_switch_realtime_cancel(void);
#endif
