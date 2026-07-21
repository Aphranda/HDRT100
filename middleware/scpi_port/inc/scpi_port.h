#ifndef SCPI_PORT_H
#define SCPI_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t trigger_width_us;
    uint32_t pulse_width_us;
    uint32_t rj45_trigger_width_us;
    uint32_t marker_width_us;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    bool sync_clock_enabled;
} scpi_port_config_t;

bool scpi_port_init(void);
void scpi_port_service(void);
bool scpi_port_execute(const char *data,
                       size_t len,
                       char *response,
                       size_t response_capacity,
                       size_t *response_len);
void scpi_port_get_config(scpi_port_config_t *config);

#endif
