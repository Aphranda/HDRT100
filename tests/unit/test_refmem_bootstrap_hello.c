#include "refmem_bootstrap_hello.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Host stubs for the frame CRC: the round-trip only needs determinism. */
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0u; i < length; i++) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

uint32_t ota_crc32_compute(const uint8_t *data, size_t length)
{
    return ~ota_crc32_update(0xFFFFFFFFu, data, length);
}

static int expect(const char *name, int actual, int expected)
{
    if ((actual != 0) != (expected != 0)) {
        (void)printf("%s: expected %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;
    uint8_t frame[256];
    size_t frame_size = 0u;
    uint8_t source_slot = 0xFFu;
    bool is_ack = false;

    failed += expect("build",
                     refmem_bootstrap_hello_build(0u, 1u, 0u, frame,
                                                  sizeof(frame), &frame_size),
                     1);
    failed += expect("size", frame_size > 0u, 1);
    failed += expect("parse",
                     refmem_bootstrap_hello_parse(frame, frame_size,
                                                  &source_slot, &is_ack),
                     1);
    failed += expect("source", source_slot == 0u, 1);
    failed += expect("ack flag", is_ack, 1);

    /* Invalid target slot is refused before encoding. */
    failed += expect("bad target",
                     refmem_bootstrap_hello_build(0u, 8u, 0u, frame,
                                                  sizeof(frame), &frame_size),
                     0);

    /* Truncated frames must not parse. */
    uint8_t other[256];
    size_t other_size = 0u;
    failed += expect("build other",
                     refmem_bootstrap_hello_build(2u, 0u, 3u, other,
                                                  sizeof(other), &other_size),
                     1);
    failed += expect("truncated",
                     refmem_bootstrap_hello_parse(other, other_size - 1u,
                                                  &source_slot, &is_ack),
                     0);
    failed += expect("parse other",
                     refmem_bootstrap_hello_parse(other, other_size,
                                                  &source_slot, &is_ack),
                     1);
    failed += expect("source other", source_slot == 2u, 1);

    if (failed != 0) {
        return 1;
    }
    (void)printf("refmem_bootstrap_hello tests passed\n");
    return 0;
}
