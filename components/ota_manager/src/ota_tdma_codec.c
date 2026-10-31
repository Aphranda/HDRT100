#include "ota_tdma_codec.h"

#include <string.h>

_Static_assert(OTA_TDMA_OPEN_FRAGMENT_MAX <= OTA_TDMA_FRAME_MAX,
               "OPEN fragment must fit a frame");
_Static_assert(OTA_TDMA_DATA_MAX_PAYLOAD <= OTA_TDMA_OPEN_FRAGMENT_MAX,
               "frame payload must hold the largest body");

static void ota_tdma_put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8u) & 0xFFu);
    out[2] = (uint8_t)((value >> 16u) & 0xFFu);
    out[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint32_t ota_tdma_get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static size_t ota_tdma_write_header(uint8_t *out, uint8_t type, uint8_t flags,
                                    uint8_t seq, uint8_t total)
{
    out[0] = type;
    out[1] = flags;
    out[2] = seq;
    out[3] = total;
    return OTA_TDMA_HEADER_SIZE;
}

size_t ota_tdma_encode_open(uint8_t *out, size_t cap,
                            const uint8_t *open_wire, size_t open_wire_len,
                            uint8_t seq, uint8_t total)
{
    if (out == NULL || open_wire == NULL ||
        open_wire_len != OTA_TDMA_OPEN_WIRE_SIZE ||
        total == 0u || total > OTA_TDMA_OPEN_MAX_FRAGMENTS || seq >= total) {
        return 0u;
    }
    const size_t expected_total =
        (OTA_TDMA_OPEN_WIRE_SIZE + OTA_TDMA_OPEN_FRAGMENT_MAX - 1u) /
        OTA_TDMA_OPEN_FRAGMENT_MAX;
    if ((size_t)total != expected_total) {
        return 0u;
    }
    const size_t offset = (size_t)seq * OTA_TDMA_OPEN_FRAGMENT_MAX;
    size_t fragment = OTA_TDMA_OPEN_WIRE_SIZE - offset;
    if (fragment > OTA_TDMA_OPEN_FRAGMENT_MAX) {
        fragment = OTA_TDMA_OPEN_FRAGMENT_MAX;
    }
    const size_t frame_len = OTA_TDMA_HEADER_SIZE + fragment;
    if (cap < frame_len) {
        return 0u;
    }
    (void)ota_tdma_write_header(out, (uint8_t)OTA_TDMA_FRAME_OPEN, 0u, seq,
                                total);
    memcpy(&out[OTA_TDMA_HEADER_SIZE], &open_wire[offset], fragment);
    return frame_len;
}

size_t ota_tdma_encode_data(uint8_t *out, size_t cap, uint32_t offset,
                            uint32_t crc32, const uint8_t *payload,
                            size_t payload_len, bool is_final)
{
    if (out == NULL || payload_len > OTA_TDMA_DATA_MAX_PAYLOAD ||
        (payload == NULL && payload_len != 0u)) {
        return 0u;
    }
    const size_t frame_len =
        OTA_TDMA_HEADER_SIZE + OTA_TDMA_DATA_HEADER_SIZE + payload_len;
    if (cap < frame_len) {
        return 0u;
    }
    (void)ota_tdma_write_header(out, (uint8_t)OTA_TDMA_FRAME_DATA,
                                (uint8_t)(OTA_TDMA_FLAG_HAS_CRC |
                                          (is_final ? OTA_TDMA_FLAG_FINAL : 0u)),
                                0u, 1u);
    ota_tdma_put_u32(&out[OTA_TDMA_HEADER_SIZE], offset);
    ota_tdma_put_u32(&out[OTA_TDMA_HEADER_SIZE + 4u], crc32);
    if (payload_len != 0u) {
        memcpy(&out[OTA_TDMA_HEADER_SIZE + OTA_TDMA_DATA_HEADER_SIZE], payload,
               payload_len);
    }
    return frame_len;
}

size_t ota_tdma_encode_ack(uint8_t *out, size_t cap, uint32_t durable_offset,
                           uint8_t result, uint8_t reason)
{
    const size_t frame_len = OTA_TDMA_HEADER_SIZE + 6u;
    if (out == NULL || cap < frame_len) {
        return 0u;
    }
    (void)ota_tdma_write_header(out, (uint8_t)OTA_TDMA_FRAME_ACK, 0u, 0u, 1u);
    ota_tdma_put_u32(&out[OTA_TDMA_HEADER_SIZE], durable_offset);
    out[OTA_TDMA_HEADER_SIZE + 4u] = result;
    out[OTA_TDMA_HEADER_SIZE + 5u] = reason;
    return frame_len;
}

size_t ota_tdma_encode_close(uint8_t *out, size_t cap, uint32_t final_offset,
                             uint32_t crc32)
{
    const size_t frame_len = OTA_TDMA_HEADER_SIZE + 8u;
    if (out == NULL || cap < frame_len) {
        return 0u;
    }
    (void)ota_tdma_write_header(out, (uint8_t)OTA_TDMA_FRAME_CLOSE,
                                OTA_TDMA_FLAG_HAS_CRC, 0u, 1u);
    ota_tdma_put_u32(&out[OTA_TDMA_HEADER_SIZE], final_offset);
    ota_tdma_put_u32(&out[OTA_TDMA_HEADER_SIZE + 4u], crc32);
    return frame_len;
}

size_t ota_tdma_encode_abort(uint8_t *out, size_t cap, uint8_t reason)
{
    const size_t frame_len = OTA_TDMA_HEADER_SIZE + 1u;
    if (out == NULL || cap < frame_len) {
        return 0u;
    }
    (void)ota_tdma_write_header(out, (uint8_t)OTA_TDMA_FRAME_ABORT, 0u, 0u, 1u);
    out[OTA_TDMA_HEADER_SIZE] = reason;
    return frame_len;
}

bool ota_tdma_decode(ota_tdma_frame_t *frame, const uint8_t *data, size_t len)
{
    if (frame == NULL || data == NULL || len < OTA_TDMA_HEADER_SIZE ||
        len > OTA_TDMA_FRAME_MAX) {
        return false;
    }
    memset(frame, 0, sizeof(*frame));
    frame->type = data[0];
    frame->flags = data[1];
    frame->seq = data[2];
    frame->total = data[3];
    const size_t body = len - OTA_TDMA_HEADER_SIZE;

    switch (frame->type) {
    case (uint8_t)OTA_TDMA_FRAME_OPEN:
        if (frame->total == 0u || frame->total > OTA_TDMA_OPEN_MAX_FRAGMENTS ||
            frame->seq >= frame->total ||
            body > OTA_TDMA_OPEN_FRAGMENT_MAX) {
            return false;
        }
        frame->payload_len = (uint16_t)body;
        if (body != 0u) {
            memcpy(frame->payload, &data[OTA_TDMA_HEADER_SIZE], body);
        }
        return true;
    case (uint8_t)OTA_TDMA_FRAME_DATA:
        if (body < OTA_TDMA_DATA_HEADER_SIZE ||
            body > OTA_TDMA_DATA_HEADER_SIZE + OTA_TDMA_DATA_MAX_PAYLOAD ||
            (frame->flags & OTA_TDMA_FLAG_HAS_CRC) == 0u) {
            return false;
        }
        frame->total = 1u;
        frame->offset = ota_tdma_get_u32(&data[OTA_TDMA_HEADER_SIZE]);
        frame->crc32 = ota_tdma_get_u32(&data[OTA_TDMA_HEADER_SIZE + 4u]);
        frame->payload_len = (uint16_t)(body - OTA_TDMA_DATA_HEADER_SIZE);
        if (frame->payload_len != 0u) {
            memcpy(frame->payload,
                   &data[OTA_TDMA_HEADER_SIZE + OTA_TDMA_DATA_HEADER_SIZE],
                   frame->payload_len);
        }
        return true;
    case (uint8_t)OTA_TDMA_FRAME_ACK:
        if (body != 6u || frame->total != 1u) {
            return false;
        }
        frame->offset = ota_tdma_get_u32(&data[OTA_TDMA_HEADER_SIZE]);
        frame->result = data[OTA_TDMA_HEADER_SIZE + 4u];
        frame->reason = data[OTA_TDMA_HEADER_SIZE + 5u];
        return true;
    case (uint8_t)OTA_TDMA_FRAME_CLOSE:
        if (body != 8u || frame->total != 1u ||
            (frame->flags & OTA_TDMA_FLAG_HAS_CRC) == 0u) {
            return false;
        }
        frame->offset = ota_tdma_get_u32(&data[OTA_TDMA_HEADER_SIZE]);
        frame->crc32 = ota_tdma_get_u32(&data[OTA_TDMA_HEADER_SIZE + 4u]);
        return true;
    case (uint8_t)OTA_TDMA_FRAME_ABORT:
        if (body != 1u || frame->total != 1u) {
            return false;
        }
        frame->reason = data[OTA_TDMA_HEADER_SIZE];
        return true;
    default:
        return false;
    }
}

void ota_tdma_open_reassembly_reset(ota_tdma_open_reassembly_t *reassembly)
{
    if (reassembly == NULL) {
        return;
    }
    memset(reassembly, 0, sizeof(*reassembly));
}

bool ota_tdma_open_reassembly_step(ota_tdma_open_reassembly_t *reassembly,
                                   const ota_tdma_frame_t *frame)
{
    if (reassembly == NULL || frame == NULL ||
        frame->type != (uint8_t)OTA_TDMA_FRAME_OPEN ||
        frame->total == 0u || frame->seq >= frame->total) {
        return false;
    }
    if (reassembly->total == 0u) {
        reassembly->total = frame->total;
    } else if (reassembly->total != frame->total) {
        ota_tdma_open_reassembly_reset(reassembly);
        return false;
    }
    if (frame->payload_len == 0u) {
        /* An empty OPEN fragment would mark its bit and could "complete" the
         * reassembly with an all-zero descriptor; never accept it. */
        return false;
    }
    const size_t offset = (size_t)frame->seq * OTA_TDMA_OPEN_FRAGMENT_MAX;
    if (offset >= OTA_TDMA_OPEN_WIRE_SIZE) {
        ota_tdma_open_reassembly_reset(reassembly);
        return false;
    }
    /* Every fragment must carry exactly its slice of the descriptor.  Checking
     * this per fragment (instead of only when the bitmap completes) keeps the
     * check independent of arrival order: a late seq 0 must not be mistaken for
     * a short cover just because it is not the fragment that ends at 88. */
    const size_t remaining = OTA_TDMA_OPEN_WIRE_SIZE - offset;
    const size_t expected = remaining > OTA_TDMA_OPEN_FRAGMENT_MAX
                                ? OTA_TDMA_OPEN_FRAGMENT_MAX
                                : remaining;
    if (frame->payload_len != expected) {
        ota_tdma_open_reassembly_reset(reassembly);
        return false;
    }
    if ((reassembly->received & (1u << frame->seq)) != 0u) {
        /* Retransmission of a fragment we already hold: keep the partial
         * reassembly (a reset here would throw away progress on every retry). */
        return false;
    }
    memcpy(&reassembly->buffer[offset], frame->payload, frame->payload_len);
    reassembly->received = (uint8_t)(reassembly->received | (1u << frame->seq));
    const uint8_t all = (uint8_t)((1u << reassembly->total) - 1u);
    if (reassembly->received != all) {
        return false;
    }
    reassembly->length = OTA_TDMA_OPEN_WIRE_SIZE;
    return true;
}
