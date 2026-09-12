#ifndef TDMA_PIO_SPI_ORIGIN_WORKSPACE_H
#define TDMA_PIO_SPI_ORIGIN_WORKSPACE_H

#include "tdma_origin_plan.h"
#include "tdma_origin_exchange.h"
#include "tdma_pio_spi_phys.h"

/* Physical SRAM allocation is tighter than the builder's construction
 * bounds. All supported slot masks must fit these capacities before ARM. */
#define TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY 320u
#define TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY 128u

/* Persona storage, owned by the physical TDMA owner. The three service
 * arrays may be active together; none may be accessed while the origin
 * graph owns this union. A transition first stops the complete DMA tree
 * and retires any pending observation-copy job. Calibration RX/TX buffers
 * are deliberately separate: completed training reads outlive a persona.
 *
 * Apply RX-ring alignment to the static variable, not this type; otherwise
 * sizeof would round the whole union up to a multiple of the ring size. */
typedef union {
    struct {
        uint32_t rx_ring[TDMA_PIO_SPI_RX_RING_WORDS];
        uint32_t tx_words[TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS];
        tdma_flight_overlay_plan_t follower_plan[TDMA_ORIGIN_PLAN_BANK_COUNT];
    } service;
    struct {
        tdma_flight_overlay_dma_run_t runs[TDMA_PIO_SPI_ORIGIN_RUN_CAPACITY];
        uint32_t literals[TDMA_PIO_SPI_ORIGIN_LITERAL_CAPACITY];
        uint8_t capture[TDMA_ORIGIN_PLAN_BANK_COUNT][TDMA_TRANSPORT_SHORT_PACKET_MAX];
        /* No DMA reads these fields during construction. Seed banks and the
         * graph lie outside this union; complete/cancel construction before
         * initializing the live fields, then never use builder while armed. */
        union {
            struct {
                uint16_t stage[TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS];
                uint8_t tx_header[TDMA_TRANSPORT_FRAME_HEADER_SIZE];
                uint8_t rx_packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
            };
            tdma_origin_plan_builder_t builder;
        };
        tdma_origin_plan_state_t state;
        uint32_t local_shadow[TDMA_ORIGIN_PLAN_BANK_COUNT]
                             [TDMA_ORIGIN_PLAN_SHADOW_BYTES / sizeof(uint32_t)];
        uint32_t scratch[2u];
        tdma_origin_plan_t plan;
        tdma_origin_exchange_t exchange;
        tdma_origin_observation_t rx_observation;
    } origin;
} tdma_pio_spi_workspace_t;

_Static_assert(offsetof(tdma_pio_spi_workspace_t, service.rx_ring) == 0u,
               "DMA ring must begin at the aligned variable base");
_Static_assert(offsetof(tdma_pio_spi_workspace_t, origin.runs) % 16u == 0u,
               "AL3 descriptor base must preserve word-ring alignment");
_Static_assert(TDMA_ORIGIN_PLAN_SHADOW_BYTES % sizeof(uint32_t) == 0u,
               "Generation must follow a whole aligned mailbox");
_Static_assert(sizeof(tdma_origin_plan_builder_t) <=
    sizeof(((tdma_pio_spi_workspace_t *)0)->origin.stage) +
    sizeof(((tdma_pio_spi_workspace_t *)0)->origin.tx_header) +
    sizeof(((tdma_pio_spi_workspace_t *)0)->origin.rx_packet),
    "Builder must reuse unpublished live storage without enlarging it");

#endif
