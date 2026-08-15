#ifndef REFMEM_TRANSPORT_ADAPTER_H
#define REFMEM_TRANSPORT_ADAPTER_H

#include <stdint.h>

#define REFMEM_TRANSPORT_ADAPTER_ID_PIO_SPI 1u

typedef enum {
    REFMEM_TRANSPORT_CAP_FRAME_CRC = 0x00000001u,
    REFMEM_TRANSPORT_CAP_HALF_DUPLEX = 0x00000002u,
    REFMEM_TRANSPORT_CAP_RX_TIMESTAMP_OPTIONAL = 0x00000004u,
    REFMEM_TRANSPORT_CAP_COUNTERS = 0x00000008u,
} refmem_transport_capability_t;

typedef enum {
    REFMEM_TRANSPORT_STATE_UNINIT = 0u,
    REFMEM_TRANSPORT_STATE_IDLE = 1u,
    REFMEM_TRANSPORT_STATE_ERROR = 2u,
} refmem_transport_state_t;

typedef enum {
    REFMEM_TRANSPORT_ERROR_NONE = 0u,
    REFMEM_TRANSPORT_ERROR_BAD_ARGUMENT = 1u,
    REFMEM_TRANSPORT_ERROR_BAD_FRAME = 2u,
    REFMEM_TRANSPORT_ERROR_PAYLOAD_TOO_LARGE = 3u,
    REFMEM_TRANSPORT_ERROR_NO_RX_FRAME = 4u,
    REFMEM_TRANSPORT_ERROR_RX_BUSY = 5u,
    REFMEM_TRANSPORT_ERROR_TX_UNBOUND = 6u,
} refmem_transport_error_t;

typedef struct {
    uint32_t adapter_id;
    uint32_t capability_mask;
    uint16_t max_payload_size;
    uint16_t preferred_mtu;
    uint32_t latency_class_us;
    uint32_t flags;
} refmem_transport_caps_t;

#endif
