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

typedef size_t (*scpi_port_write_fn_t)(const char *data, size_t len, void *context);
typedef void (*scpi_port_flush_fn_t)(void *context);

bool scpi_port_init(void);
void scpi_port_service(void);
void scpi_port_feed(const char *data, size_t len);
void scpi_port_set_stream(scpi_port_write_fn_t write_fn, scpi_port_flush_fn_t flush_fn, void *context);
bool scpi_port_execute(const char *data,
                       size_t len,
                       char *response,
                       size_t response_capacity,
                       size_t *response_len);
void scpi_port_get_config(scpi_port_config_t *config);

#endif
