#include "refmem_cmd_codec.h"

#include <limits.h>
#include <stddef.h>

#include "algo_checksum.h"
#include "algo_wire.h"

_Static_assert(REFMEM_CMD_WIRE_SIZE == 56u, "REFMEM_CMD wire size changed");
_Static_assert(REFMEM_CMD_CRC_OFFSET == 52u,
               "REFMEM_CMD CRC offset changed");

static void put_u8(uint8_t *p, uint8_t v)
{
    p[0] = v;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    algo_le16_store(p, v);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    algo_le32_store(p, v);
}

static uint16_t get_u16(const uint8_t *p)
{
    return algo_le16_load(p);
}

static uint8_t get_u8(const uint8_t *p)
{
    return p[0];
}

static uint32_t get_u32(const uint8_t *p)
{
    return algo_le32_load(p);
}

static uint32_t cmd_crc32(const uint8_t *p, size_t n)
{
    return algo_crc32_iso_hdlc_compute(p, n);
}

refmem_cmd_codec_result_t refmem_cmd_codec_encode(
    const refmem_command_request_t *request,
    uint8_t wire[REFMEM_CMD_WIRE_SIZE])
{
    if (request == NULL || wire == NULL) {
        return REFMEM_CMD_CODEC_BAD_ARGUMENT;
    }
    if (request->source_node > UINT16_MAX ||
        request->source_instance > UINT16_MAX ||
        request->command_type > UINT16_MAX ||
        request->command_class > UINT8_MAX ||
        request->payload_kind > UINT8_MAX ||
        request->payload_size > UINT16_MAX) {
        return REFMEM_CMD_CODEC_RANGE;
    }

    put_u32(wire + 0u, REFMEM_CMD_MAGIC);
    put_u16(wire + 4u, REFMEM_CMD_VERSION);
    put_u16(wire + 6u, 0u);
    put_u32(wire + 8u, request->command_seq);
    put_u32(wire + 12u, request->target_mask);
    put_u32(wire + 16u, request->required_mask);
    put_u32(wire + 20u, request->issue_epoch);
    put_u32(wire + 24u, request->run_id);
    put_u32(wire + 28u, request->payload_ref);
    put_u32(wire + 32u, request->payload_crc32);
    put_u32(wire + 36u, request->timeout_us);
    put_u16(wire + 40u, (uint16_t)request->source_node);
    put_u16(wire + 42u, (uint16_t)request->source_instance);
    put_u16(wire + 44u, (uint16_t)request->command_type);
    put_u8(wire + 46u, (uint8_t)request->command_class);
    put_u8(wire + 47u, (uint8_t)request->payload_kind);
    put_u16(wire + 48u, (uint16_t)request->payload_size);
    put_u16(wire + 50u, 0u);
    put_u32(wire + REFMEM_CMD_CRC_OFFSET,
            cmd_crc32(wire, REFMEM_CMD_CRC_OFFSET));
    return REFMEM_CMD_CODEC_OK;
}

refmem_cmd_codec_result_t refmem_cmd_codec_decode(
    const uint8_t wire[REFMEM_CMD_WIRE_SIZE],
    refmem_command_request_t *request)
{
    if (wire == NULL || request == NULL) {
        return REFMEM_CMD_CODEC_BAD_ARGUMENT;
    }
    if (get_u32(wire + 0u) != REFMEM_CMD_MAGIC) {
        return REFMEM_CMD_CODEC_BAD_MAGIC;
    }
    if (get_u16(wire + 4u) != REFMEM_CMD_VERSION) {
        return REFMEM_CMD_CODEC_BAD_VERSION;
    }
    if (get_u16(wire + 6u) != 0u || get_u16(wire + 50u) != 0u) {
        return REFMEM_CMD_CODEC_BAD_RESERVED;
    }
    if (get_u32(wire + REFMEM_CMD_CRC_OFFSET) !=
        cmd_crc32(wire, REFMEM_CMD_CRC_OFFSET)) {
        return REFMEM_CMD_CODEC_BAD_CRC;
    }

    request->command_seq = get_u32(wire + 8u);
    request->target_mask = get_u32(wire + 12u);
    request->required_mask = get_u32(wire + 16u);
    request->issue_epoch = get_u32(wire + 20u);
    request->run_id = get_u32(wire + 24u);
    request->payload_ref = get_u32(wire + 28u);
    request->payload_crc32 = get_u32(wire + 32u);
    request->timeout_us = get_u32(wire + 36u);
    request->source_node = get_u16(wire + 40u);
    request->source_instance = get_u16(wire + 42u);
    request->command_type = get_u16(wire + 44u);
    request->command_class = get_u8(wire + 46u);
    request->payload_kind = get_u8(wire + 47u);
    request->payload_size = get_u16(wire + 48u);
    return REFMEM_CMD_CODEC_OK;
}
