#ifndef REFMEM_CMD_LOAD_H
#define REFMEM_CMD_LOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_pio_spi_ring_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* REFMEM_CMD load registration and AO-side event intake.
 *
 * Transport only produces raw CONFIG_CONTROL payloads; this module is the
 * RefMem AO edge that decodes them and owns the single command slot/ACK. */
bool refmem_cmd_load_init(tdma_pio_spi_ring_adapter_t *adapter);
bool refmem_cmd_load_submit(const uint8_t *wire, uint32_t wire_size);
void refmem_cmd_load_service(void);

typedef struct {
    uint32_t load_initialized;
    uint32_t submitted;
    uint32_t served;
    uint32_t rejected;
    uint32_t window_closed;
    uint32_t rx_events;
    uint32_t decoded;
    uint32_t posted;
    uint32_t acked;
    uint32_t tx_ok;
    uint32_t tx_fail;
    uint32_t tx_stage;
    uint32_t rx_seen;
    uint32_t rx_delivered;
    uint32_t rx_drop_count;
    uint32_t last_rx_packet_size;
    uint32_t last_rx_payload_class;
    uint32_t rx_config_control;
    uint32_t rx_process_image;
    uint32_t build_class;
    uint32_t build_size;
    uint32_t build_load;
} refmem_cmd_load_status_t;

bool refmem_cmd_load_get_status(refmem_cmd_load_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_CMD_LOAD_H */
