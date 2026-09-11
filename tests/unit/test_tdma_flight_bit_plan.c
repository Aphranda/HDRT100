#include "tdma_flight_overlay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t command_at(const tdma_flight_overlay_plan_t *plan, uint32_t index)
{
    for (uint32_t i = 0; i < plan->run_count; ++i) {
        const tdma_flight_overlay_dma_run_t *run = &plan->run[i];
        if (index < run->transfer_count)
            return run->control ? plan->token[run->read_address + index] : TDMA_FLIGHT_OVERLAY_LIVE_WORD;
        index -= run->transfer_count;
    }
    assert(false);
    return 0;
}

int main(void)
{
    uint8_t incoming[TDMA_TRANSPORT_SHORT_PACKET_MAX] = {0};
    uint8_t processed[sizeof incoming];
    uint32_t force[TDMA_FLIGHT_OUTPUT_BITMAP_WORDS] = {0};
    tdma_flight_overlay_plan_t plan, corrupt;
    tdma_flight_overlay_config_t config = {
        .physical_byte_count = 307, .outer_header_size = 4, .final_bit_pc = 20,
        .header_write_mask = (1u << 14) | (15u << 28),
    };
    unsigned cases = 0;
    for (unsigned slot = 0; slot < TDMA_FLIGHT_SHORT_SLOT_COUNT; ++slot) {
        config.local_slot_id = slot;
        const unsigned first = TDMA_TRANSPORT_FRAME_HEADER_SIZE + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE;
        for (unsigned shift = 0; shift < 8; ++shift) {
            config.alignment_bit_shift = shift;
            for (unsigned bytes = 0; bytes <= 10; ++bytes) {
                config.alignment_byte_shift = bytes;
                for (unsigned value = 0; value < 256; ++value) {
                    memcpy(processed, incoming, sizeof incoming);
                    memset(processed + first, value, TDMA_FLIGHT_SHORT_SLOT_SIZE);
                    memset(force, 0, sizeof force);
                    force[slot] = UINT32_MAX; /* Force equal bytes too: live may have changed. */
                    const bool accepted = tdma_flight_overlay_build_plan(incoming, processed,
                        sizeof incoming, force, TDMA_FLIGHT_OUTPUT_BITMAP_WORDS, &config, &plan);
                    const unsigned base = (bytes + 5) * 8 + shift;
                    assert(accepted == (base + sizeof incoming * 8 < config.physical_byte_count * 8));
                    if (!accepted) continue;
                    assert(tdma_flight_overlay_plan_valid(&plan, config.final_bit_pc));
                    for (unsigned bit = 0; bit < config.physical_byte_count * 8; ++bit) {
                        unsigned token = (command_at(&plan, bit / 2) >> ((bit & 1) ? 0 : 16)) & 65535;
                        unsigned expected = TDMA_FLIGHT_OVERLAY_TOKEN_LIVE;
                        if (bit >= base + first * 8 && bit < base + (first + TDMA_FLIGHT_SHORT_SLOT_SIZE) * 8)
                            expected = (value & (128u >> ((bit - base) % 8))) ? TDMA_FLIGHT_OVERLAY_TOKEN_ONE : TDMA_FLIGHT_OVERLAY_TOKEN_ZERO;
                        if (bit + 1 == config.physical_byte_count * 8) expected = config.final_bit_pc;
                        assert(token == expected);
                    }
                    ++cases;
                }
            }
        }
    }
    config.alignment_bit_shift = config.alignment_byte_shift = 0;
    config.local_slot_id = 2;
    memset(force, 0, sizeof force);
    for (unsigned i = 0; i < sizeof incoming; ++i) {
        memcpy(processed, incoming, sizeof incoming);
        processed[i] = 1;
        const bool allowed = (i >= 96 && i < 128) || i == 14 || (i >= 28 && i < 32);
        assert(tdma_flight_overlay_build_plan(incoming, processed, sizeof incoming,
            NULL, 0, &config, &plan) == allowed);
    }
    memcpy(processed, incoming, sizeof incoming);
    force[8] = 16u; /* First bit outside the fixed payload. */
    assert(!tdma_flight_overlay_build_plan(incoming, processed, sizeof incoming,
        force, TDMA_FLIGHT_OUTPUT_BITMAP_WORDS, &config, &plan));
    force[8] = 1u; /* Reference-only DPLL trailer. */
    assert(!tdma_flight_overlay_build_plan(incoming, processed, sizeof incoming,
        force, TDMA_FLIGHT_OUTPUT_BITMAP_WORDS, &config, &plan));
    force[8] = 0; force[1] = 1; /* Another owner. */
    assert(!tdma_flight_overlay_build_plan(incoming, processed, sizeof incoming,
        force, TDMA_FLIGHT_OUTPUT_BITMAP_WORDS, &config, &plan));
    assert(!tdma_flight_overlay_build_pass_plan(UINT32_MAX, 20, &plan));
    assert(!tdma_flight_overlay_build_pass_plan(307, 21, &plan));
    assert(tdma_flight_overlay_build_pass_plan(307, 20, &plan));
    for (unsigned token = 0; token < 65536; ++token) {
        corrupt = plan;
        corrupt.token[0] = (TDMA_FLIGHT_OVERLAY_TOKEN_LIVE << 16) | token;
        assert(tdma_flight_overlay_plan_valid(&corrupt, 20) == (token == 20));
    }
    corrupt = plan; corrupt.run[0].transfer_count++;
    assert(!tdma_flight_overlay_plan_valid(&corrupt, 20));
    corrupt = plan; corrupt.run[1].read_address++;
    assert(!tdma_flight_overlay_plan_valid(&corrupt, 20));
    corrupt = plan; corrupt.token[0] = (TDMA_FLIGHT_OVERLAY_TOKEN_ONE << 16) | 20;
    assert(!tdma_flight_overlay_plan_valid(&corrupt, 20));
    corrupt = plan; corrupt.run[0].write_address = 4;
    assert(!tdma_flight_overlay_plan_valid(&corrupt, 20));
    printf("bit plan: %u ownership/alignment cases; byte authority and token/descriptor rejection passed\n", cases);
    return 0;
}
