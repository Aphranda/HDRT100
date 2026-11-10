#ifndef USBTMC_SCPI_PORT_H
#define USBTMC_SCPI_PORT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Reply-transport diagnostics. A setter acknowledgement costs ~2 s per command
 * over USBTMC (CDC is instant) because the Bulk-In transfer is never completed:
 * the host only gets the bytes when its read times out. These counters identify
 * which part of the handshake is missing; read them through the USB statistics
 * query registered by scpi_usb_control.c.
 *
 *   bulk_in_requests  host Bulk-In requests seen
 *   zero_requests     requests with TransferSize == 0 (USBTMC poll)
 *   last_*            geometry of the most recent request/reply
 */
typedef struct {
    uint32_t bulk_in_requests;
    uint32_t zero_requests;
    uint32_t last_request_size;
    uint32_t last_tx_len;
    uint32_t last_end_flag;
    uint32_t last_reply_len;
} usbtmc_scpi_port_stats_t;

bool usbtmc_scpi_port_init(void);
void usbtmc_scpi_port_service(void);
void usbtmc_scpi_port_get_stats(usbtmc_scpi_port_stats_t *stats);

#endif
