#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

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

bool calibration_manager_init(void);
void calibration_manager_set_ready(bool ready);
void calibration_manager_service(void);
void calibration_manager_get_status(calibration_manager_status_t *status);

#endif
