#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "refmem_cmd_fragment.h"

int main(void)
{
    uint8_t frame[220];
    for (uint32_t i = 0u; i < sizeof(frame); i++) {
        frame[i] = (uint8_t)(i * 7u + 3u);
    }
    /* Make the frame look self-describing: header_size + payload_size = 220. */
    frame[4] = 36u;
    frame[6] = 184u;
    frame[7] = 0u;

    const uint32_t count = refmem_cmd_fragment_count(sizeof(frame));
    assert(count == 55u);

    refmem_cmd_fragment_ctx_t ctx;
    refmem_cmd_fragment_reset(&ctx);

    uint8_t out[REFMEM_CMD_FRAGMENT_MAX_BYTES];
    size_t out_size = 0u;
    bool complete = false;
    for (uint32_t index = 0u; index < count; index++) {
        uint32_t word = 0u;
        assert(refmem_cmd_fragment_get(frame, sizeof(frame), index, &word));
        complete = refmem_cmd_fragment_accept(&ctx, 2u, 99u, (uint8_t)count,
                                              (uint8_t)index, word, out,
                                              sizeof(out), &out_size);
        if (index + 1u < count) {
            assert(!complete);
        }
    }
    assert(complete);
    assert(out_size == sizeof(frame));
    assert(memcmp(out, frame, sizeof(frame)) == 0);

    /* Out-of-order fragment is rejected. */
    refmem_cmd_fragment_reset(&ctx);
    uint32_t word = 0u;
    assert(refmem_cmd_fragment_get(frame, sizeof(frame), 1u, &word));
    assert(!refmem_cmd_fragment_accept(&ctx, 2u, 100u, (uint8_t)count, 1u, word,
                                       out, sizeof(out), &out_size));
    assert(ctx.rejected == 1u);
    assert(refmem_cmd_fragment_get(frame, sizeof(frame), 0u, &word));
    assert(!refmem_cmd_fragment_accept(&ctx, 2u, 100u, (uint8_t)count, 0u, word,
                                       out, sizeof(out), &out_size));

    /* A different source resets the reassembly. */
    assert(refmem_cmd_fragment_get(frame, sizeof(frame), 0u, &word));
    assert(!refmem_cmd_fragment_accept(&ctx, 3u, 101u, (uint8_t)count, 0u, word,
                                       out, sizeof(out), &out_size));
    assert(ctx.source_slot == 3u && ctx.frame_seq == 101u);
    assert(ctx.next_fragment == 1u);

    printf("refmem_cmd_fragment: split/reassemble/reject OK\n");
    return 0;
}
