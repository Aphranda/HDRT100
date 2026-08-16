#include "refmem_tdma_payload.h"

bool refmem_tdma_payload_register(tdma_service_service_t *service)
{
    if (service == NULL) {
        return false;
    }

    const tdma_service_payload_binding_t delta = {
        .used = 1u,
        .producer_id = REFMEM_TDMA_PAYLOAD_PRODUCER_ID,
        .consumer_id = REFMEM_TDMA_PAYLOAD_CONSUMER_ID,
        .payload_class = TDMA_SERVICE_PAYLOAD_CLASS_REFMEM_DELTA,
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .max_payload_size = REFMEM_TDMA_PAYLOAD_FRAME_MAX,
        .flags = 0u,
    };
    const tdma_service_payload_binding_t ack_fence = {
        .used = 1u,
        .producer_id = REFMEM_TDMA_PAYLOAD_PRODUCER_ID,
        .consumer_id = REFMEM_TDMA_PAYLOAD_CONSUMER_ID,
        .payload_class = TDMA_SERVICE_PAYLOAD_CLASS_REFMEM_ACK_FENCE,
        .frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT,
        .max_payload_size = REFMEM_TDMA_PAYLOAD_FRAME_MAX,
        .flags = 0u,
    };

    return tdma_service_register_payload(service, &delta) &&
           tdma_service_register_payload(service, &ack_fence);
}
