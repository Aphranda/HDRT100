#ifndef SYNC_IO_PULSE_COUNTER_H
#define SYNC_IO_PULSE_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

#define SYNC_IO_PULSE_COUNTER_SCHEMA 1u
#define SYNC_IO_PULSE_COUNTER_SM 3u
#define SYNC_IO_PULSE_COUNTER_INSTRUCTION_WORDS 8u

typedef enum {
    SYNC_IO_PULSE_COUNTER_IDLE,
    SYNC_IO_PULSE_COUNTER_LOADED,
    SYNC_IO_PULSE_COUNTER_RUNNING,
    SYNC_IO_PULSE_COUNTER_STOPPED
} sync_io_pulse_counter_state_t;

typedef struct {
    uint32_t schema, generation, clear_generation, state;
    /* input_pin reads actual EXECCTRL.JMP_PIN while leased, cached in IDLE. */
    uint32_t input_port, input_pin, count32, valid;
    uint32_t tick_hz, timestamp_valid;
    uint64_t sample_before_tick, sample_after_tick;
} sync_io_pulse_counter_snapshot_t;

/* Core0 maintenance owner only. The caller holds the product STOP/config
 * gate. Public input ports use the board's IN1..IN4 mapping (GPIO23..20 on
 * this board), independent of legacy encoder pins. No GPIO mux/direction/pull is
 * changed, no output is driven, and no DMA/IRQ/shared arena is consumed.
 * load returns a new lease generation and zero count. start resumes the
 * cumulative count, first requiring low before accepting a new rising edge.
 * stop freezes the count; clear is permitted only while not running.
 * release requires LOADED/STOPPED and retains the last count for readback. */
bool sync_io_pulse_counter_load(uint32_t input_port, uint32_t *generation);
bool sync_io_pulse_counter_start(uint32_t generation);
bool sync_io_pulse_counter_stop(uint32_t generation);
bool sync_io_pulse_counter_clear(uint32_t generation);
bool sync_io_pulse_counter_release(uint32_t generation);
/* count32 wraps modulo 2^32. A difference is unambiguous only when fewer
 * than 2^32 edges occurred and generation/clear_generation did not change.
 * Running snapshots do not pause counting. timestamp_valid brackets the
 * hardware count observation with TIMER1 reads; it is NOT an input-edge
 * latch or a synchronized VDC event. Timer unavailability leaves count valid
 * with timestamp_valid=0. Bounded FIFO contention may return false and
 * preserves *out. IDLE readback never accesses released hardware. */
bool sync_io_pulse_counter_snapshot(sync_io_pulse_counter_snapshot_t *out);

#endif
