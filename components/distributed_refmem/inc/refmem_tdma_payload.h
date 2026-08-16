#ifndef REFMEM_TDMA_PAYLOAD_H
#define REFMEM_TDMA_PAYLOAD_H

#include <stdbool.h>

#include "refmem_sync_frame.h"
#include "tdma_service.h"

#define REFMEM_TDMA_PAYLOAD_FRAME_MAX \
    (REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX)

#define REFMEM_TDMA_PAYLOAD_PRODUCER_ID 1u
#define REFMEM_TDMA_PAYLOAD_CONSUMER_ID 1u

bool refmem_tdma_payload_register(tdma_service_service_t *service);

#endif
