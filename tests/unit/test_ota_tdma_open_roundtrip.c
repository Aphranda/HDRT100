/* Host roundtrip: relay OPEN wire -> TDMA OPEN fragments -> reassembly -> decode.
 * Reproduces the bench failure where the target saw wire[38]/[39] != 0. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_codec.h"
#include "pota_stream_wire.h"

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void build_relay_wire(uint8_t wire[POTA_STREAM_OPEN_WIRE_SIZE])
{
    memset(wire, 0, POTA_STREAM_OPEN_WIRE_SIZE);
    put_u32(&wire[0], 0x11223344u);  /* session */
    put_u32(&wire[4], 0x55667788u);  /* generation */
    put_u32(&wire[8], 0x7u);         /* caps: inactive|durable|relay */
    put_u32(&wire[12], 2u);          /* map version */
    put_u32(&wire[16], 3u);          /* partition */
    put_u32(&wire[20], 2u);          /* destination slot */
    put_u32(&wire[24], 1u);          /* object id */
    put_u32(&wire[28], 4096u);       /* total size */
    put_u32(&wire[32], 0x1B7DC8E7u); /* package crc32 */
    wire[36] = 0u;                   /* not package mode */
    wire[37] = 1u;                   /* relay node */
    for (uint8_t i = 0; i < 16u; ++i) {
        wire[40u + i] = (uint8_t)(0xA0u + i);
    }
    for (uint8_t i = 0; i < 32u; ++i) {
        wire[56u + i] = (uint8_t)(0x10u + i);
    }
}

int main(void)
{
    uint8_t wire[POTA_STREAM_OPEN_WIRE_SIZE];
    build_relay_wire(wire);

    uint8_t reassembly[POTA_STREAM_OPEN_WIRE_SIZE];
    memset(reassembly, 0xEE, sizeof(reassembly));
    ota_tdma_open_reassembly_t state;
    ota_tdma_open_reassembly_reset(&state);

    const uint8_t total = (uint8_t)((OTA_TDMA_OPEN_WIRE_SIZE +
                                     OTA_TDMA_OPEN_FRAGMENT_MAX - 1u) /
                                    OTA_TDMA_OPEN_FRAGMENT_MAX);
    bool complete = false;
    for (uint8_t seq = 0; seq < total; ++seq) {
        uint8_t frame[OTA_TDMA_FRAME_MAX];
        const size_t len = ota_tdma_encode_open(frame, sizeof(frame), wire,
                                                sizeof(wire), seq, total);
        if (len == 0u) {
            printf("encode failed seq=%u\n", seq);
            return 1;
        }
        ota_tdma_frame_t decoded;
        if (!ota_tdma_decode(&decoded, frame, len)) {
            printf("frame decode failed seq=%u\n", seq);
            return 1;
        }
        if (ota_tdma_open_reassembly_step(&state, &decoded)) {
            complete = true;
        }
    }
    if (!complete) {
        printf("reassembly incomplete total=%u received=0x%02x\n",
               state.total, state.received);
        return 1;
    }
    memcpy(reassembly, state.buffer, sizeof(reassembly));

    {
        extern uint32_t pota_crc32_compute(const void *data, size_t length);
        printf("expected_crc_mod=%u\n",
               (unsigned)(pota_crc32_compute(reassembly, sizeof(reassembly))
                          % 100000u));
    }
    printf("total=%u wire[36..39]=%02x %02x %02x %02x\n", total,
           reassembly[36], reassembly[37], reassembly[38], reassembly[39]);
    if (memcmp(reassembly, wire, sizeof(wire)) != 0) {
        for (size_t i = 0; i < sizeof(wire); ++i) {
            if (reassembly[i] != wire[i]) {
                printf("mismatch at %zu: got %02x want %02x\n", i,
                       reassembly[i], wire[i]);
                break;
            }
        }
        return 1;
    }

    pota_stream_open_t open;
    if (!pota_stream_open_decode_le(reassembly, sizeof(reassembly), &open)) {
        printf("decode rejected wire[36]=%02x wire[37]=%02x "
               "wire[38]=%02x wire[39]=%02x\n",
               reassembly[36], reassembly[37], reassembly[38],
               reassembly[39]);
        return 1;
    }
    printf("decode ok caps=0x%x relay=%u\n", (unsigned)open.capability_mask,
           (unsigned)open.relay_node);
    return 0;
}
