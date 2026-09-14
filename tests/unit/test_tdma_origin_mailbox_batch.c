#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tdma_flight_engine.h"
#include "tdma_process_image_layout.h"
#include "origin_mailbox_impl.inc"

static void crc(uint8_t *mailbox)
{
    const uint16_t value = tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET);
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] = (uint8_t)value;
    mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] = (uint8_t)(value >> 8u);
}

static void seed_packet(uint8_t *seed, size_t size, uint32_t nodes)
{
    memset(seed, 0xa5, size);
    for (uint32_t slot = 0; slot < nodes; ++slot) {
        uint8_t *p = seed + TDMA_TRANSPORT_FRAME_HEADER_SIZE + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE;
        p[0] = (uint8_t)TDMA_FLIGHT_MAILBOX_MAGIC;
        p[1] = TDMA_FLIGHT_MAILBOX_MAGIC >> 8u;
        p[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] = TDMA_FLIGHT_MAILBOX_VERSION;
        p[3] = TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
        p[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] = slot;
        p[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] = (1u << nodes) - 1u;
        crc(p);
    }
}

int main(void)
{
    assert(tdma_process_image_crc16_ccitt((const uint8_t *)"123456789", 9) == 0x29b1u);
    for (uint32_t nodes = 2; nodes <= PROJECT_NODE_CAPACITY; ++nodes) {
        const size_t size = TDMA_TRANSPORT_FRAME_HEADER_SIZE +
            nodes * TDMA_FLIGHT_SHORT_SLOT_SIZE + TDMA_FLIGHT_DPLL_OBSERVATION_SIZE;
        uint8_t *seed = malloc(size), *saved = malloc(size);
        assert(seed && saved);
        seed_packet(seed, size, nodes);
        memcpy(saved, seed, size);
        uint32_t cursor = 0, calls = 0;
        while (cursor < nodes) {
            const uint32_t previous = cursor;
            assert(tdma_pio_spi_phys_origin_mailbox_batch(seed, size, nodes, &cursor));
            assert(cursor > previous && cursor - previous <= 4);
            ++calls;
        }
        assert(calls == (nodes <= 4 ? 1u : 2u));
        assert(memcmp(seed, saved, size) == 0);
        assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, size, nodes, &cursor));
        cursor = UINT32_MAX;
        assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, size, nodes, &cursor));
        cursor = 0;
        assert(!tdma_pio_spi_phys_origin_mailbox_batch(NULL, size, nodes, &cursor));
        assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, size, nodes, NULL));
        assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, size - 1, nodes, &cursor));
        assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, 0, nodes, &cursor));
        assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, size, 1, &cursor));
        assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, size, UINT32_MAX, &cursor));
        assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, size, PROJECT_NODE_CAPACITY + 1, &cursor));
        assert(cursor == 0);
        /* Every byte of every mailbox rejects when corrupted; a corrupt later
         * batch cannot be consumed early, and its cursor never passes the bad slot. */
        for (uint32_t slot = 0; slot < nodes; ++slot) {
            uint8_t *p = seed + TDMA_TRANSPORT_FRAME_HEADER_SIZE + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE;
            for (uint32_t byte = 0; byte < TDMA_FLIGHT_SHORT_SLOT_SIZE; ++byte) {
                memcpy(seed, saved, size);
                p[byte] ^= 1u;
                cursor = 0;
                if (slot >= 4) {
                    assert(tdma_pio_spi_phys_origin_mailbox_batch(seed, size, nodes, &cursor));
                    assert(cursor == 4);
                }
                assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, size, nodes, &cursor));
                assert(cursor == slot);
            }
            /* Recompute CRC after structural corruption: framing/source/mask
             * checks must reject independently of CRC. */
            const unsigned fields[] = {0, 1, TDMA_FLIGHT_MAILBOX_VERSION_OFFSET, 3,
                TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET, TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET};
            for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
                if (fields[i] == TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET && nodes == 8) continue;
                memcpy(seed, saved, size);
                if (fields[i] == TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET) p[fields[i]] |= 1u << nodes;
                else p[fields[i]] ^= 1u;
                crc(p);
                cursor = slot;
                assert(!tdma_pio_spi_phys_origin_mailbox_batch(seed, size, nodes, &cursor));
                assert(cursor == slot);
            }
        }
        free(saved);
        free(seed);
    }
    puts("bounded physical mailbox validation passed");
    return 0;
}
