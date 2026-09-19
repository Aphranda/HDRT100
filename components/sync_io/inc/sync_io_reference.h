#ifndef SYNC_IO_REFERENCE_H
#define SYNC_IO_REFERENCE_H
#include <stdbool.h>
#include <stdint.h>

#define SYNC_IO_REFERENCE_SCHEMA 1u
#define SYNC_IO_REFERENCE_DMA_LATENCY_UNBOUNDED 1u
#define SYNC_IO_REFERENCE_PIO_BIAS_TICKS 1u
typedef enum { SYNC_IO_REFERENCE_IDLE, SYNC_IO_REFERENCE_PREPARED,
    SYNC_IO_REFERENCE_RUNNING, SYNC_IO_REFERENCE_RETIRED } sync_io_reference_state_t;
typedef enum { SYNC_IO_REFERENCE_OK, SYNC_IO_REFERENCE_CANCELLED,
    SYNC_IO_REFERENCE_TIMEOUT, SYNC_IO_REFERENCE_DMA_ERROR,
    SYNC_IO_REFERENCE_CLOCK_ERROR, SYNC_IO_REFERENCE_BAD_RECORD,
    SYNC_IO_REFERENCE_INVALID_CONFIG, SYNC_IO_REFERENCE_ADMISSION_BUSY,
    SYNC_IO_REFERENCE_PERSONA_UNAVAILABLE } sync_io_reference_reason_t;
typedef struct {
    uint32_t input_port, edge, nominal_hz, window_ms, timeout_ms;
} sync_io_reference_config_t;
typedef struct {
    uint32_t schema, state, reason, generation, sample_seq, valid;
    sync_io_reference_config_t config;
    uint32_t input_pin, tick_hz, reference_cycles;
    uint32_t start_raw32, end_raw32, elapsed_ticks, pio_bias_ticks;
    int32_t frequency_error_ppb;
    uint32_t measurement_flags;
    uint64_t completed_raw, deadline_raw;
} sync_io_reference_snapshot_t;

/* Pure validation; N reference periods must be integral. */
bool sync_io_reference_config_valid(const sync_io_reference_config_t *config);
/* STOP/config owner calls prepare and release on Core0. Runtime only observes
 * TIMER1; it never changes the timer or DCO. Each valid sample measures input
 * frequency using nominal local tick_hz, not an independently calibrated time.
 * DMA latency variation is not bounded by this interface. */
bool sync_io_reference_prepare(const sync_io_reference_config_t *config, uint32_t *generation);
void sync_io_reference_cancel(void);
void sync_io_reference_service_core1(void);
bool sync_io_reference_release(uint32_t generation);
bool sync_io_reference_get_snapshot(sync_io_reference_snapshot_t *out);
#endif
