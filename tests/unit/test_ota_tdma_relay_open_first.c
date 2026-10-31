/* Host check: the relay must emit OPEN frames before any DATA frame and hold
 * DATA back for the OPEN-only grace window. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_relay.h"

static uint32_t s_open_frames;
static uint32_t s_data_frames;
static uint32_t s_other_frames;
static uint32_t s_first_data_ms = 0xFFFFFFFFu;
static uint32_t s_now_ms;

static bool mock_send(void *context, const uint8_t *frame, size_t length)
{
    (void)context;
    if (length < 4u) {
        return false;
    }
    switch (frame[0]) {
    case 1u: /* OPEN */
        s_open_frames++;
        break;
    case 2u: /* DATA */
        s_data_frames++;
        if (s_first_data_ms == 0xFFFFFFFFu) {
            s_first_data_ms = s_now_ms;
        }
        break;
    default:
        s_other_frames++;
        break;
    }
    return true;
}

static uint32_t test_crc(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint32_t b = 0; b < 8u; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

int main(void)
{
    ota_tdma_relay_t relay;
    ota_tdma_transport_t transport = {.send = mock_send, .context = NULL};
    uint8_t wire[88];
    memset(wire, 0, sizeof(wire));
    wire[8] = 7u;    /* caps */
    wire[37] = 1u;   /* relay node */

    if (!ota_tdma_relay_open(&relay, &transport, wire, 512u, 0x12345678u, 64u,
                             3u, 100u, test_crc)) {
        printf("relay_open failed\n");
        return 1;
    }
    uint8_t window[64];
    memset(window, 0x5A, sizeof(window));
    if (!ota_tdma_relay_feed(&relay, 0u, window, sizeof(window))) {
        printf("relay_feed failed\n");
        return 1;
    }

    for (s_now_ms = 0; s_now_ms <= 500u; s_now_ms += 50u) {
        (void)ota_tdma_relay_tick(&relay, s_now_ms);
    }
    printf("grace: open=%u data=%u other=%u\n", s_open_frames, s_data_frames,
           s_other_frames);
    if (s_open_frames == 0u) {
        printf("FAIL: no OPEN emitted in grace window\n");
        return 1;
    }
    if (s_data_frames != 0u) {
        printf("FAIL: DATA emitted during grace (first at %u ms)\n",
               s_first_data_ms);
        return 1;
    }

    for (; s_now_ms <= 900u; s_now_ms += 50u) {
        (void)ota_tdma_relay_tick(&relay, s_now_ms);
    }
    printf("after grace: data=%u first_data_ms=%u\n", s_data_frames,
           s_first_data_ms);
    if (s_data_frames == 0u) {
        printf("FAIL: DATA never started after grace\n");
        return 1;
    }
    return 0;
}
