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
} sma_cable_delay_pio_status_t;

typedef struct {
    uint32_t output_index;
    uint32_t input_base_pin;
    bool reverse_input_bits;
} sma_cable_delay_pio_config_t;

typedef struct {
    uint32_t requested_frequency_hz;
    uint32_t actual_frequency_hz;
    uint32_t sample_rate_hz;
    uint32_t period_samples;
    uint32_t captured_word_count;
    bool reverse_input_bits;
} sma_cable_delay_pio_capture_t;

/* Dynamic persona: owns its claimed PIO SM/program space/DMA only while open. */
sma_cable_delay_pio_status_t sma_cable_delay_pio_open(
    const sma_cable_delay_pio_config_t *config);

sma_cable_delay_pio_status_t sma_cable_delay_pio_capture_frequency(
    uint32_t frequency_hz,
    uint32_t *capture_words,
    size_t capture_word_count,
    uint32_t timeout_us,
    sma_cable_delay_pio_capture_t *capture);

void sma_cable_delay_pio_close(void);
bool sma_cable_delay_pio_is_open(void);
const char *sma_cable_delay_pio_status_string(sma_cable_delay_pio_status_t status);

#ifdef __cplusplus
}
#endif

#endif
