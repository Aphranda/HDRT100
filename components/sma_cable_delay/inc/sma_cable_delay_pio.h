#ifndef SMA_CABLE_DELAY_PIO_H
#define SMA_CABLE_DELAY_PIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMA_CABLE_DELAY_PIO_INPUT_COUNT 4u
#define SMA_CABLE_DELAY_PIO_SAMPLES_PER_WORD 8u
#define SMA_CABLE_DELAY_PIO_MIN_CAPTURE_WORDS 16u
#define SMA_CABLE_DELAY_PIO_MARK_WIDTH_NS 16u
#define SMA_CABLE_DELAY_PIO_APPOINTMENT_NS 40u
#define SMA_CABLE_DELAY_PIO_RTT_TURNAROUND_CYCLES 10u

typedef enum {
    SMA_CABLE_DELAY_PIO_OK = 0,
    SMA_CABLE_DELAY_PIO_INVALID_ARGUMENT,
    SMA_CABLE_DELAY_PIO_RESOURCE_CONFLICT,
    SMA_CABLE_DELAY_PIO_NO_STATE_MACHINE,
    SMA_CABLE_DELAY_PIO_NO_DMA_CHANNEL,
    SMA_CABLE_DELAY_PIO_NO_INSTRUCTION_SPACE,
    SMA_CABLE_DELAY_PIO_NOT_OPEN,
    SMA_CABLE_DELAY_PIO_BUSY,
    SMA_CABLE_DELAY_PIO_FREQUENCY_OUT_OF_RANGE,
    SMA_CABLE_DELAY_PIO_CAPTURE_TIMEOUT,
    SMA_CABLE_DELAY_PIO_RTT_NO_RESPONSE,
    SMA_CABLE_DELAY_PIO_WRONG_ROLE,
} sma_cable_delay_pio_status_t;

typedef enum {
    SMA_CABLE_DELAY_PIO_ROLE_SELF_LOOP = 0,
    SMA_CABLE_DELAY_PIO_ROLE_SOURCE,
    SMA_CABLE_DELAY_PIO_ROLE_VALIDATOR,
    SMA_CABLE_DELAY_PIO_ROLE_RTT_INITIATOR,
    SMA_CABLE_DELAY_PIO_ROLE_RTT_RESPONDER,
} sma_cable_delay_pio_role_t;

typedef enum {
    SMA_CABLE_DELAY_PIO_TIMING_FREE_RUNNING = 0,
    SMA_CABLE_DELAY_PIO_TIMING_MARK_APPOINTMENT,
} sma_cable_delay_pio_timing_t;

typedef struct {
    sma_cable_delay_pio_role_t role;
    sma_cable_delay_pio_timing_t timing;
    uint32_t output_index;
    uint32_t input_base_pin;
    uint32_t appointment_marker_pin;
    bool reverse_input_bits;
} sma_cable_delay_pio_config_t;

typedef struct {
    uint32_t requested_frequency_hz;
    uint32_t actual_frequency_hz;
    uint32_t sample_rate_hz;
    uint32_t period_samples;
    uint32_t captured_word_count;
    bool reverse_input_bits;
    bool phase_coherent;
} sma_cable_delay_pio_capture_t;

typedef struct {
    uint32_t sample_rate_hz;
    uint32_t sample_period_ps;
    uint32_t captured_word_count;
    uint32_t response_sample_index;
    uint32_t raw_round_trip_cycles;
    uint32_t responder_turnaround_cycles;
    bool response_detected;
} sma_cable_delay_pio_rtt_capture_t;

/* Dynamic persona: owns its claimed PIO SM/program space/DMA only while open. */
sma_cable_delay_pio_status_t sma_cable_delay_pio_open(
    const sma_cable_delay_pio_config_t *config);

sma_cable_delay_pio_status_t sma_cable_delay_pio_capture_frequency(
    uint32_t frequency_hz,
    uint32_t *capture_words,
    size_t capture_word_count,
    uint32_t timeout_us,
    sma_cable_delay_pio_capture_t *capture);

sma_cable_delay_pio_status_t sma_cable_delay_pio_source_start(
    uint32_t frequency_hz,
    uint32_t *actual_frequency_hz);

void sma_cable_delay_pio_source_stop(void);

sma_cable_delay_pio_status_t sma_cable_delay_pio_rtt_initiate(
    uint32_t *capture_words,
    size_t capture_word_count,
    uint32_t timeout_us,
    sma_cable_delay_pio_rtt_capture_t *capture);

sma_cable_delay_pio_status_t sma_cable_delay_pio_rtt_respond(
    uint32_t timeout_us,
    uint32_t *turnaround_cycles);

void sma_cable_delay_pio_close(void);
bool sma_cable_delay_pio_is_open(void);
const char *sma_cable_delay_pio_status_string(sma_cable_delay_pio_status_t status);

#ifdef __cplusplus
}
#endif

#endif
