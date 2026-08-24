#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "calibration_pio_loopback.h"
#include "calibration_bidirectional.h"
#include "calibration_bias.h"
#include "calibration_clk_coded.h"
#include "calibration_training_marker.h"
#include "calibration_training_data.h"
#include "tdma_pio_spi_phys.h"

typedef struct {
    bool ready;
    uint32_t state;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
    uint32_t command_seq;
    uint32_t link_count;
    uint32_t delay_count;
    uint32_t active_crc32;
    uint32_t last_error;
} calibration_manager_status_t;

typedef struct {
    calibration_pio_loopback_snapshot_t raw;
    calibration_bidirectional_result_t result;
    uint32_t result_valid;
} calibration_manager_loopback_snapshot_t;

typedef struct {
    tdma_pio_spi_p3_snapshot_t raw;
    uint32_t result_valid;
} calibration_manager_p3_snapshot_t;

bool calibration_manager_init(void);
void calibration_manager_set_ready(bool ready);
void calibration_manager_service(void);
void calibration_manager_service_core1(void);
void calibration_manager_get_status(calibration_manager_status_t *status);
bool calibration_manager_start_loopback(uint32_t sample_words);
void calibration_manager_stop_loopback(void);
bool calibration_manager_get_loopback_snapshot(
    calibration_manager_loopback_snapshot_t *snapshot);
bool calibration_manager_start_bias(uint32_t expected_path_sum_ns,
                                    uint32_t minimum_samples,
                                    uint32_t maximum_samples,
                                    uint32_t maximum_spread_ns,
                                    uint32_t maximum_clock_error_ns);
void calibration_manager_stop_bias(void);
bool calibration_manager_get_bias_snapshot(
    calibration_bias_snapshot_t *snapshot);
/* Queue the validated accepted bias package to the Storage manager for an
 * atomic SD evidence file.  This is a source artifact for the future
 * Calibration NVS; it never promotes diagnostic evidence to active state. */
bool calibration_manager_save_bias_snapshot(uint32_t *job_id);
bool calibration_manager_get_clk_coded_snapshot(
    calibration_clk_coded_snapshot_t *snapshot);
/* Core0 command-slot publication only.  The marker is generated, executed
 * and correlated later by calibration_manager_service_core1(). */
bool calibration_manager_start_clk_coded(
    const calibration_clk_coded_request_t *request,
    const calibration_clk_correlation_gate_t *gate);
/* Core0 convenience API: bind the request to the current board identity,
 * build and stopped TDMA topology, then publish the same guarded command
 * slot used by calibration_manager_start_clk_coded(). */
bool calibration_manager_request_clk_coded(
    uint32_t codebook_id,
    uint32_t min_lag_sample,
    uint32_t max_lag_sample,
    uint32_t max_best_distance,
    uint32_t min_margin);
void calibration_manager_stop_clk_coded(void);
bool calibration_manager_request_marker_training(
    uint32_t codebook_id,
    uint32_t train_epoch,
    uint32_t train_sequence,
    uint32_t marker_id,
    uint32_t calibration_generation,
    int32_t offset_sample_count);
bool calibration_manager_inject_marker_training(void);
void calibration_manager_stop_marker_training(void);
bool calibration_manager_get_marker_training_snapshot(
    calibration_training_marker_snapshot_t *snapshot);
bool calibration_manager_save_marker_capture(
    uint32_t *job_id, char *path, size_t path_size);
bool calibration_manager_request_data_training(
    uint32_t source_node,
    uint32_t destination_node,
    uint32_t codebook_id,
    uint32_t train_epoch,
    uint32_t train_sequence,
    uint32_t calibration_generation,
    uint32_t marker_to_data_samples,
    uint32_t base_delay_ns,
    int32_t marker_offset_sample_count,
    int32_t configured_data_offset_sample_count,
    int32_t search_start_offset_sample,
    int32_t search_end_offset_sample,
    uint32_t guard_sample_count,
    uint32_t max_best_distance,
    uint32_t min_margin);
bool calibration_manager_inject_data_training(void);
void calibration_manager_stop_data_training(void);
bool calibration_manager_get_data_training_snapshot(
    calibration_training_data_snapshot_t *snapshot);
bool calibration_manager_save_data_capture(
    uint32_t *job_id, char *path, size_t path_size);
bool calibration_manager_request_p3(
    uint32_t role, uint32_t baud_hz, uint32_t pulse_count,
    uint32_t capture_words, uint32_t epoch, uint32_t signal_group);
void calibration_manager_stop_p3(void);
bool calibration_manager_get_p3_snapshot(
    calibration_manager_p3_snapshot_t *snapshot);

#endif
