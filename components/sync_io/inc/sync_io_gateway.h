#ifndef SYNC_IO_GATEWAY_H
#define SYNC_IO_GATEWAY_H
#include <stdbool.h>
#include <stdint.h>

#define SYNC_IO_GATEWAY_SCHEMA 1u
#define SYNC_IO_GATEWAY_OUTPUT_SM 1u
#define SYNC_IO_GATEWAY_READY_SM 3u
#define SYNC_IO_GATEWAY_GRANT_IRQ 6u
#define SYNC_IO_GATEWAY_OUTPUT_WORDS 7u
#define SYNC_IO_GATEWAY_READY_WORDS 6u
#define SYNC_IO_GATEWAY_INSTRUCTION_WORDS (SYNC_IO_GATEWAY_OUTPUT_WORDS + SYNC_IO_GATEWAY_READY_WORDS)

typedef enum { SYNC_IO_GATEWAY_IDLE, SYNC_IO_GATEWAY_LOADED,
    SYNC_IO_GATEWAY_STOPPED } sync_io_gateway_state_t;
typedef struct {
    uint32_t schema, generation, state, loaded;
    /* Ports are requested identities. Loaded pin fields read actual SM
     * PINCTRL routing; IDLE retains the selected pin without touching an SM. */
    uint32_t input_port, input_pin, output_port, output_pin;
    uint32_t pulse_ns, pulse_ticks, tick_hz, ready_level;
    uint32_t sm_claimed_mask, sm_enabled_mask, instruction_words;
    uint32_t output_program_offset, ready_program_offset, grant_pending;
    uint32_t output_level, trigger_count, ready_count;
    uint32_t output_tx_level, output_rx_level, ready_tx_level, ready_rx_level;
} sync_io_gateway_snapshot_t;

/* Core0 under the caller's STOP maintenance/config gate. Input/output ports
 * select any public IN1..IN4/OUT1..OUT4. LOAD hotloads both programs, clears private
 * FIFO/IRQ state and forces the selected output low, keeping BOTH SMs disabled. No FIFO pulse
 * word is queued. This API deliberately has no FIRE/ARM/start operation.
 * pulse_ns must encode an exact divider-one pulse of at least two ticks.
 * READY is the current pad level, never a reservation completion receipt.
 * STOP keeps the lease, programs and safe-low output; RELEASE restores the
 * prior output GPIO CTRL/PAD registers, SIO direction/latch and selected
 * PIO0 output/OE latch bits. Input GPIO is
 * observed without changing its configuration. No DMA, CPU IRQ or arena.
 * The identity and absolute-deadline executor are future owner work. */
bool sync_io_gateway_load(uint32_t input_port,uint32_t output_port,
    uint32_t pulse_ns,uint32_t *generation);
bool sync_io_gateway_stop(uint32_t generation);
bool sync_io_gateway_release(uint32_t generation);
bool sync_io_gateway_snapshot(sync_io_gateway_snapshot_t *out);
/* Core1 RUN owner: queue one pulse (nanoseconds) into the already-loaded
 * output/READY PIO pair and return immediately. */
bool sync_io_gateway_realtime_fire(uint32_t pulse_ns);
void sync_io_gateway_realtime_service_core1(void);
#endif
