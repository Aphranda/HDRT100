#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ota_tdma_codec.h"
#include "ota_tdma_pump.h"

static unsigned s_send_calls;
static uint8_t s_last_frame[OTA_TDMA_FRAME_MAX];
static size_t s_last_len;
static bool s_send_ok = true;

static bool send_fn(void *context, const uint8_t *frame, size_t length)
{
    (void)context;
    s_send_calls++;
    if (!s_send_ok) {
        return false;
    }
    memcpy(s_last_frame, frame, length);
    s_last_len = length;
    return true;
}

static uint32_t test_crc(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < length; ++i) {
        crc = (crc >> 8u) ^ data[i];
    }
    return crc;
}

static void reset_send(void)
{
    s_send_calls = 0u;
    s_last_len = 0u;
    s_send_ok = true;
}

static ota_tdma_frame_t decode_last(void)
{
    ota_tdma_frame_t frame;
    assert(ota_tdma_decode(&frame, s_last_frame, s_last_len));
    return frame;
}

static ota_tdma_frame_t make_ack(uint32_t offset, uint8_t result)
{
    uint8_t wire[OTA_TDMA_FRAME_MAX];
    const size_t len = ota_tdma_encode_ack(wire, sizeof(wire), offset, result, 0u);
    ota_tdma_frame_t frame;
    assert(ota_tdma_decode(&frame, wire, len));
    return frame;
}

static void test_happy_path(void)
{
    uint8_t image[100];
    for (size_t i = 0u; i < sizeof(image); ++i) {
        image[i] = (uint8_t)i;
    }
    ota_tdma_pump_t pump;
    assert(ota_tdma_pump_init(&pump, image, sizeof(image), 0xCAFEBABEu, 68u, 68u, 3u,
                              50u, test_crc));
    reset_send();
    assert(ota_tdma_pump_start(&pump, 1000u, send_fn, NULL));
    assert(s_send_calls == 1u);
    ota_tdma_frame_t frame = decode_last();
    assert(frame.type == (uint8_t)OTA_TDMA_FRAME_DATA);
    assert(frame.offset == 0u && frame.payload_len == 68u);
    assert((frame.flags & OTA_TDMA_FLAG_FINAL) == 0u);
    assert(frame.crc32 == test_crc(image, 68u));

    assert(ota_tdma_pump_on_ack(&pump, 1001u, &(ota_tdma_frame_t){0}, send_fn,
                                NULL) == false); /* not an ACK frame */

    ota_tdma_frame_t ack = make_ack(68u, 0u);
    assert(ota_tdma_pump_on_ack(&pump, 1001u, &ack, send_fn, NULL));
    assert(s_send_calls == 2u);
    frame = decode_last();
    assert(frame.offset == 68u && frame.payload_len == 32u);
    assert((frame.flags & OTA_TDMA_FLAG_FINAL) != 0u);
    assert(ota_tdma_pump_durable(&pump) == 68u);

    ack = make_ack(100u, 0u);
    assert(ota_tdma_pump_on_ack(&pump, 1002u, &ack, send_fn, NULL));
    assert(s_send_calls == 3u);
    frame = decode_last();
    assert(frame.type == (uint8_t)OTA_TDMA_FRAME_CLOSE);
    assert(frame.offset == 100u && frame.crc32 == 0xCAFEBABEu);
    assert(ota_tdma_pump_state(&pump) == OTA_TDMA_PUMP_WAIT_CLOSE_ACK);
    assert(ota_tdma_pump_durable(&pump) == 100u);

    /* The target confirms CLOSE before the source reports READY. */
    ack = make_ack(100u, 0u);
    assert(ota_tdma_pump_on_ack(&pump, 1003u, &ack, send_fn, NULL));
    assert(ota_tdma_pump_state(&pump) == OTA_TDMA_PUMP_DONE);
}

static void test_retry_and_exhaustion(void)
{
    uint8_t image[10] = {0};
    ota_tdma_pump_t pump;
    assert(ota_tdma_pump_init(&pump, image, sizeof(image), 0u, 4u, 4u, 2u, 50u,
                              test_crc));
    reset_send();
    assert(ota_tdma_pump_start(&pump, 0u, send_fn, NULL));
    assert(s_send_calls == 1u);

    assert(!ota_tdma_pump_tick(&pump, 49u, send_fn, NULL)); /* not yet */
    assert(s_send_calls == 1u);

    assert(ota_tdma_pump_tick(&pump, 50u, send_fn, NULL)); /* resend block */
    assert(s_send_calls == 2u);
    const ota_tdma_frame_t frame = decode_last();
    assert(frame.offset == 0u);

    assert(!ota_tdma_pump_tick(&pump, 100u, send_fn, NULL)); /* exhausted */
    assert(pump.state == OTA_TDMA_PUMP_FAILED);
    assert(ota_tdma_pump_failure(&pump) == OTA_TDMA_PUMP_FAIL_RETRIES);
}

static void test_ack_classification(void)
{
    uint8_t image[8] = {0};
    ota_tdma_pump_t pump;
    assert(ota_tdma_pump_init(&pump, image, sizeof(image), 0u, 4u, 4u, 3u, 50u,
                              test_crc));
    reset_send();
    assert(ota_tdma_pump_start(&pump, 0u, send_fn, NULL));

    /* Stale ACK (same durable offset) is ignored, state stays waiting. */
    ota_tdma_frame_t ack = make_ack(0u, 0u);
    assert(!ota_tdma_pump_on_ack(&pump, 1u, &ack, send_fn, NULL));
    assert(pump.state == OTA_TDMA_PUMP_WAIT_ACK);

    /* Protocol violation: ACK beyond what was sent. */
    ack = make_ack(999u, 0u);
    assert(!ota_tdma_pump_on_ack(&pump, 2u, &ack, send_fn, NULL));
    assert(pump.state == OTA_TDMA_PUMP_FAILED);
    assert(ota_tdma_pump_failure(&pump) == OTA_TDMA_PUMP_FAIL_PROTOCOL);
}

static void test_rejected_and_send_failure(void)
{
    uint8_t image[8] = {0};
    ota_tdma_pump_t pump;
    assert(ota_tdma_pump_init(&pump, image, sizeof(image), 0u, 4u, 4u, 3u, 50u,
                              test_crc));
    reset_send();
    /* A closed maintenance window is transient: the frame is retried, the
     * pump must stay usable and must not report a protocol failure. */
    s_send_ok = false;
    assert(!ota_tdma_pump_start(&pump, 0u, send_fn, NULL));
    assert(pump.state != OTA_TDMA_PUMP_FAILED);
    assert(ota_tdma_pump_failure(&pump) == OTA_TDMA_PUMP_FAIL_NONE);
    s_send_ok = true;
    assert(ota_tdma_pump_start(&pump, 0u, send_fn, NULL));
    assert(pump.state == OTA_TDMA_PUMP_WAIT_ACK);

    ota_tdma_pump_t second;
    assert(ota_tdma_pump_init(&second, image, sizeof(image), 0u, 4u, 4u, 3u, 50u,
                              test_crc));
    assert(ota_tdma_pump_start(&second, 0u, send_fn, NULL));
    ota_tdma_frame_t ack = make_ack(6u, 7u); /* rejected, wrong offset+result */
    assert(!ota_tdma_pump_on_ack(&second, 1u, &ack, send_fn, NULL));
    assert(second.state == OTA_TDMA_PUMP_FAILED);
    assert(ota_tdma_pump_failure(&second) == OTA_TDMA_PUMP_FAIL_REJECTED);
}

static void test_init_rejections(void)
{
    uint8_t image[8] = {0};
    ota_tdma_pump_t pump;
    assert(!ota_tdma_pump_init(NULL, image, 8u, 0u, 4u, 4u, 1u, 10u, test_crc));
    assert(!ota_tdma_pump_init(&pump, NULL, 8u, 0u, 4u, 4u, 1u, 10u, test_crc));
    assert(!ota_tdma_pump_init(&pump, image, 0u, 0u, 4u, 4u, 1u, 10u, test_crc));
    assert(!ota_tdma_pump_init(&pump, image, 8u, 0u, 0u, 0u, 1u, 10u, test_crc));
    assert(!ota_tdma_pump_init(&pump, image, 8u, 0u,
                               OTA_TDMA_DATA_MAX_PAYLOAD + 1u,
                               OTA_TDMA_DATA_MAX_PAYLOAD + 1u, 1u, 10u,
                               test_crc));
    assert(!ota_tdma_pump_init(&pump, image, 8u, 0u, 4u, 4u, 0u, 10u, test_crc));
    assert(!ota_tdma_pump_init(&pump, image, 8u, 0u, 4u, 4u, 1u, 10u, NULL));
}

int main(void)
{
    test_happy_path();
    test_retry_and_exhaustion();
    test_ack_classification();
    test_rejected_and_send_failure();
    test_init_rejections();
    printf("OTA TDMA pump host unit tests passed\n");
    return 0;
}
