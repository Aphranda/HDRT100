#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "calibration_pio_loopback.h"
#include "calibration_bidirectional.h"
#include "calibration_clk_coded.h"

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

bool calibration_manager_init(void);
void calibration_manager_set_ready(bool ready);
void calibration_manager_service(void);
void calibration_manager_service_core1(void);
void calibration_manager_get_status(calibration_manager_status_t *status);
bool calibration_manager_start_loopback(uint32_t sample_words);
void calibration_manager_stop_loopback(void);
bool calibration_manager_get_loopback_snapshot(
    calibration_manager_loopback_snapshot_t *snapshot);
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

#endif
