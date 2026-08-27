#ifndef SMA_CABLE_DELAY_H
#define SMA_CABLE_DELAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMA_CABLE_DELAY_CHANNEL_COUNT 4u
#define SMA_CABLE_DELAY_MIN_FIT_POINTS 3u
#define SMA_CABLE_DELAY_MAX_FIT_POINTS 64u
#define SMA_CABLE_DELAY_FULL_TURN_MDEG 360000
#define SMA_CABLE_DELAY_HALF_TURN_MDEG 180000

typedef enum {
    SMA_CABLE_DELAY_MODE_ABSOLUTE_SELF_LOOP = 0,
    SMA_CABLE_DELAY_MODE_RELATIVE_FOUR_SOURCE,
} sma_cable_delay_mode_t;

typedef enum {
    SMA_CABLE_DELAY_STATUS_OK = 0,
    SMA_CABLE_DELAY_STATUS_INVALID_ARGUMENT,
    SMA_CABLE_DELAY_STATUS_NOT_ENOUGH_POINTS,
    SMA_CABLE_DELAY_STATUS_FREQUENCY_NOT_INCREASING,
    SMA_CABLE_DELAY_STATUS_PHASE_UNWRAP_AMBIGUOUS,
    SMA_CABLE_DELAY_STATUS_DEGENERATE_FIT,
} sma_cable_delay_status_t;

typedef struct {
    uint32_t frequency_hz;
    int32_t phase_mdeg;
} sma_cable_delay_phase_point_t;

typedef struct {
    int64_t total_delay_ps;
    int32_t intercept_mdeg;
    uint32_t phase_rms_mdeg;
    uint64_t max_unambiguous_delay_ps;
    uint32_t point_count;
    bool valid;
} sma_cable_delay_fit_t;

typedef struct {
    sma_cable_delay_mode_t mode;
    int64_t total_delay_ps;
    int64_t relative_delay_ps;
    int64_t channel_fixed_delay_ps;
    int64_t cable_delay_ps;
    uint32_t phase_rms_mdeg;
    bool total_delay_valid;
    bool relative_delay_valid;
    bool channel_fixed_delay_valid;
    bool cable_delay_valid;
} sma_cable_delay_channel_result_t;

typedef struct {
    uint32_t common_cable_delay_ps;
    uint32_t velocity_factor_ppm;
    bool common_cable_delay_valid;
    sma_cable_delay_channel_result_t channels[SMA_CABLE_DELAY_CHANNEL_COUNT];
} sma_cable_delay_coarse_result_t;

typedef struct {
    int32_t phase_mdeg;
    uint32_t rising_edge_count;
    uint32_t period_samples;
    bool valid;
} sma_cable_delay_phase_extract_t;

/* Fit phase(f) = phase0 - 2*pi*f*delay. Input phase is wrapped to one turn. */
sma_cable_delay_status_t sma_cable_delay_fit_phase_slope(
    const sma_cable_delay_phase_point_t *points,
    size_t point_count,
    sma_cable_delay_fit_t *fit);

/* Resolve a fit without conflating total path delay and cable-only delay. */
bool sma_cable_delay_resolve_channel(
    sma_cable_delay_mode_t mode,
    const sma_cable_delay_fit_t *fit,
    int64_t reference_total_delay_ps,
    bool reference_valid,
    int64_t channel_fixed_delay_ps,
    bool channel_fixed_valid,
    sma_cable_delay_channel_result_t *result);

/*
 * Coarse bring-up helper for four known-equal cables. The common cable delay
 * must come from a self-loop measurement or cable specification. Per-channel
 * residuals remain separate and are never folded into loop/link training.
 */
bool sma_cable_delay_coarse_equal_cables(
    uint32_t common_cable_delay_ps,
    bool common_cable_delay_valid,
    uint32_t velocity_factor_ppm,
    const int64_t channel_relative_delay_ps[SMA_CABLE_DELAY_CHANNEL_COUNT],
    sma_cable_delay_coarse_result_t *result);

/* Decode PIO left-shift packed 4-bit samples (8 chronological samples/word). */
bool sma_cable_delay_extract_phase_from_capture(
    const uint32_t *capture_words,
    size_t capture_word_count,
    uint32_t logical_channel,
    uint32_t period_samples,
    bool reverse_input_bits,
    sma_cable_delay_phase_extract_t *phase);

const char *sma_cable_delay_status_string(sma_cable_delay_status_t status);

#ifdef __cplusplus
}
#endif

#endif
