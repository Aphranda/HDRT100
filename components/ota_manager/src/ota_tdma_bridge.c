#include "ota_tdma_bridge.h"

#include <string.h>

static bool ota_tdma_bridge_post(ota_tdma_bridge_t *bridge,
                                 ota_tdma_bridge_op_t operation,
                                 const ota_tdma_bridge_begin_t *begin,
                                 const uint8_t *data, uint32_t length)
{
    return bridge->sink != NULL &&
           bridge->sink(bridge->context, operation, begin, data, length);
}

bool ota_tdma_bridge_init(ota_tdma_bridge_t *bridge,
                          ota_tdma_bridge_sink_fn sink, void *context)
{
    if (bridge == NULL || sink == NULL) {
        return false;
    }
    memset(bridge, 0, sizeof(*bridge));
    bridge->sink = sink;
    bridge->context = context;
    bridge->state = OTA_TDMA_BRIDGE_IDLE;
    return true;
}

bool ota_tdma_bridge_begin(ota_tdma_bridge_t *bridge, uint32_t size,
                           uint32_t crc32, bool package_mode)
{
    if (bridge == NULL || bridge->sink == NULL || size == 0u ||
        bridge->state != OTA_TDMA_BRIDGE_IDLE) {
        return false;
    }
    const ota_tdma_bridge_begin_t begin = {
        .size = size,
        .crc32 = crc32,
        .package_mode = package_mode,
    };
    if (!ota_tdma_bridge_post(bridge, OTA_TDMA_BRIDGE_OP_BEGIN, &begin, NULL,
                              0u)) {
        bridge->state = OTA_TDMA_BRIDGE_FAILED;
        return false;
    }
    bridge->expected_size = size;
    bridge->received_size = 0u;
    bridge->stage_offset = 0u;
    bridge->stage_length = 0u;
    bridge->block_target = OTA_TDMA_BRIDGE_FIRST_BLOCK;
    bridge->block_end = 0u;
    bridge->block_in_flight = false;
    bridge->state = OTA_TDMA_BRIDGE_RECEIVING;
    return true;
}

bool ota_tdma_bridge_block_in_flight(const ota_tdma_bridge_t *bridge)
{
    return bridge != NULL && bridge->block_in_flight;
}

bool ota_tdma_bridge_data(ota_tdma_bridge_t *bridge, uint32_t offset,
                          const uint8_t *data, uint32_t length)
{
    if (bridge == NULL || data == NULL || length == 0u ||
        bridge->state != OTA_TDMA_BRIDGE_RECEIVING) {
        return false;
    }
    if (bridge->block_in_flight) {
        /* The AO still owns the previous block: the caller retries later. */
        return false;
    }
    if (offset != bridge->stage_offset + bridge->stage_length) {
        return false; /* gap/overlap: the stream is not contiguous */
    }
    const uint32_t room = bridge->block_target - bridge->stage_length;
    if (length > room || length > bridge->expected_size - bridge->received_size) {
        return false;
    }
    memcpy(&bridge->stage[bridge->stage_length], data, length);
    bridge->stage_length += length;
    bridge->received_size += length;
    if (bridge->stage_length < bridge->block_target) {
        return true;
    }

    if (!ota_tdma_bridge_post(bridge, OTA_TDMA_BRIDGE_OP_DATA, NULL,
                              bridge->stage, bridge->stage_length)) {
        bridge->state = OTA_TDMA_BRIDGE_FAILED;
        return false;
    }
    bridge->block_end = bridge->stage_offset + bridge->stage_length;
    bridge->stage_offset = bridge->block_end;
    bridge->stage_length = 0u;
    bridge->block_target = OTA_TDMA_BRIDGE_BLOCK;
    bridge->block_in_flight = true;
    return true;
}

uint32_t ota_tdma_bridge_release(ota_tdma_bridge_t *bridge)
{
    if (bridge == NULL || !bridge->block_in_flight) {
        return 0u;
    }
    bridge->block_in_flight = false;
    return bridge->block_end;
}

bool ota_tdma_bridge_end(ota_tdma_bridge_t *bridge)
{
    if (bridge == NULL || bridge->state != OTA_TDMA_BRIDGE_RECEIVING ||
        bridge->block_in_flight ||
        bridge->stage_length != 0u ||
        bridge->received_size != bridge->expected_size) {
        return false;
    }
    if (!ota_tdma_bridge_post(bridge, OTA_TDMA_BRIDGE_OP_END, NULL, NULL, 0u)) {
        bridge->state = OTA_TDMA_BRIDGE_FAILED;
        return false;
    }
    bridge->state = OTA_TDMA_BRIDGE_DONE;
    return true;
}

bool ota_tdma_bridge_abort(ota_tdma_bridge_t *bridge)
{
    if (bridge == NULL || bridge->state == OTA_TDMA_BRIDGE_IDLE ||
        bridge->state == OTA_TDMA_BRIDGE_FAILED) {
        return false;
    }
    (void)ota_tdma_bridge_post(bridge, OTA_TDMA_BRIDGE_OP_ABORT, NULL, NULL,
                               0u);
    bridge->expected_size = 0u;
    bridge->received_size = 0u;
    bridge->stage_offset = 0u;
    bridge->stage_length = 0u;
    bridge->block_end = 0u;
    bridge->block_in_flight = false;
    bridge->state = OTA_TDMA_BRIDGE_IDLE;
    return true;
}

ota_tdma_bridge_state_t ota_tdma_bridge_state(const ota_tdma_bridge_t *bridge)
{
    return bridge == NULL ? OTA_TDMA_BRIDGE_FAILED : bridge->state;
}

uint32_t ota_tdma_bridge_received(const ota_tdma_bridge_t *bridge)
{
    return bridge == NULL ? 0u : bridge->received_size;
}
