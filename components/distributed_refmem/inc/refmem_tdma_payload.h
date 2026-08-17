#ifndef REFMEM_TDMA_PAYLOAD_H
#define REFMEM_TDMA_PAYLOAD_H

#include <stdbool.h>

#include "refmem_sync_frame.h"
#include "tdma_service.h"
#include "tdma_transport_frame.h"

#define REFMEM_TDMA_PAYLOAD_FRAME_MAX \
    TDMA_TRANSPORT_SHORT_PAYLOAD_MAX
#define REFMEM_TDMA_CRITICAL_DELTA_PAYLOAD_MAX \
    (REFMEM_TDMA_PAYLOAD_FRAME_MAX - REFMEM_SYNC_FRAME_HEADER_SIZE)

#if REFMEM_TDMA_PAYLOAD_FRAME_MAX > TDMA_TRANSPORT_SHORT_PAYLOAD_MAX
#error "RefMem realtime frame must fit the TDMA short payload"
#endif

#define REFMEM_TDMA_PAYLOAD_PRODUCER_ID 1u
#define REFMEM_TDMA_PAYLOAD_CONSUMER_ID 1u

bool refmem_tdma_payload_register(tdma_service_service_t *service);

#endif
