#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "tdma_pio_spi_ring_adapter.h"

int main(void)
{
    /* Every valid triple round-trips through the single packed word. */
    for (uint32_t p = 1u; p <= TDMA_SEAT_ROTATION_SHARE_MAX; ++p) {
        for (uint32_t f = 1u; f <= TDMA_SEAT_ROTATION_SHARE_MAX; ++f) {
            for (uint32_t s = 0u; s <= TDMA_SEAT_ROTATION_CYCLE_MAX; ++s) {
                if (!tdma_seat_rotation_valid3(p, f, s)) {
                    continue;
                }
                const uint32_t packed = TDMA_SEAT_ROTATION_PACK(p, f, s);
                const tdma_seat_rotation_shares_t shares =
                    tdma_seat_rotation_unpack(packed);
                assert(shares.priority == p);
                assert(shares.first_class == f);
                assert(shares.second_class == s);
            }
        }
    }

    /* Invalid triples stay rejected by the same validator the setter uses. */
    assert(!tdma_seat_rotation_valid3(0u, 1u, 0u));
    assert(!tdma_seat_rotation_valid3(1u, 0u, 0u));
    assert(!tdma_seat_rotation_valid3(TDMA_SEAT_ROTATION_SHARE_MAX + 1u, 1u, 0u));
    assert(!tdma_seat_rotation_valid3(1u, 1u,
                                      TDMA_SEAT_ROTATION_CYCLE_MAX));

    /* "Not set" still means the compiled 1:1 default. */
    assert(tdma_seat_rotation_priority(0u) ==
           TDMA_SEAT_ROTATION_DEFAULT_PRIORITY_SHARE);
    assert(tdma_seat_rotation_first_class(0u) ==
           TDMA_SEAT_ROTATION_DEFAULT_FIRST_CLASS_SHARE);
    assert(TDMA_SEAT_ROTATION_PACK(0u, 0u, 0u) == 0u);

    printf("tdma_seat_rotation_online: pack/unpack OK\n");
    return 0;
}
