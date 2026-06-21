#ifndef SYNC_IO_H
#define SYNC_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
} sync_io_config_t;

typedef struct {
    bool initialized;
    bool capture_running;
    bool sync_clock_running;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    uint32_t dropped_capture_words;
} sync_io_status_t;

typedef enum {
    SYNC_IO_AUX0 = 0,
    SYNC_IO_AUX1,
    SYNC_IO_AUX2,
    SYNC_IO_AUX3,
    SYNC_IO_AUX_COUNT,
} sync_io_aux_channel_t;

typedef enum {
    SYNC_IO_AUX_MODE_INPUT = 0,
    SYNC_IO_AUX_MODE_PIO_OUTPUT,
} sync_io_aux_mode_t;

bool sync_io_init(const sync_io_config_t *config);
bool sync_io_start_capture(uint32_t sample_hz);
void sync_io_stop_capture(void);
size_t sync_io_read_capture_words(uint32_t *buffer, size_t max_words);
bool sync_io_fire_pulse_cycles(uint32_t high_cycles);
bool sync_io_fire_pulse_us(uint32_t high_us);
bool sync_io_fire_pulse_out_cycles(uint32_t high_cycles);
bool sync_io_fire_pulse_out_us(uint32_t high_us);
bool sync_io_start_clock(uint32_t frequency_hz);
void sync_io_stop_clock(void);
bool sync_io_fire_marker_cycles(uint32_t high_cycles);
bool sync_io_fire_marker_us(uint32_t high_us);
bool sync_io_aux_set_mode(sync_io_aux_channel_t channel, sync_io_aux_mode_t mode);
bool sync_io_aux_write(sync_io_aux_channel_t channel, bool level);
bool sync_io_aux_read(sync_io_aux_channel_t channel, bool *level);
void sync_io_get_status(sync_io_status_t *status);

#endif
