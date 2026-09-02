#ifndef TDMA_STATE_MACHINE_RESOURCES_H
#define TDMA_STATE_MACHINE_RESOURCES_H

#include <stdint.h>

/* Static direction/resource contract for the cyclic flight persona. The
 * values are board-owned in board_config.h; this header validates only their
 * relationships and provides one stable interface for host gates. */
#include "board_config.h"
#include "hardware/pio.h"
#include "resource_arbiter.h"

#if BOARD_TDMA_TX_PIO_BLOCK_ID == BOARD_TDMA_RX_PIO_BLOCK_ID
#error "TDMA TX and RX flight PIO blocks must be distinct"
#endif
#if BOARD_TDMA_TX_PIO_BLOCK_ID == BOARD_TDMA_SMA_PIO_BLOCK_ID || \
    BOARD_TDMA_RX_PIO_BLOCK_ID == BOARD_TDMA_SMA_PIO_BLOCK_ID
#error "TDMA flight PIO blocks must not overlap SMA PIO"
#endif
#if BOARD_TDMA_TX_CLK_OUT_SM == BOARD_TDMA_TX_SYNC_OUT_SM || \
    BOARD_TDMA_TX_CLK_OUT_SM == BOARD_TDMA_TX_DATA_IN_FORWARD_SM || \
    BOARD_TDMA_TX_CLK_OUT_SM == BOARD_TDMA_TX_DATA_IN_CAPTURE_SM || \
    BOARD_TDMA_TX_SYNC_OUT_SM == BOARD_TDMA_TX_DATA_IN_FORWARD_SM || \
    BOARD_TDMA_TX_SYNC_OUT_SM == BOARD_TDMA_TX_DATA_IN_CAPTURE_SM || \
    BOARD_TDMA_TX_DATA_IN_FORWARD_SM == BOARD_TDMA_TX_DATA_IN_CAPTURE_SM
#error "TDMA TX logical-port SM roles must be unique"
#endif
#if BOARD_TDMA_RX_CLK_IN_SM == BOARD_TDMA_RX_SYNC_IN_SM || \
    BOARD_TDMA_RX_CLK_IN_SM == BOARD_TDMA_RX_DATA_OUT_SM || \
    BOARD_TDMA_RX_CLK_IN_SM == BOARD_TDMA_RX_EVIDENCE_IN_SM || \
    BOARD_TDMA_RX_SYNC_IN_SM == BOARD_TDMA_RX_DATA_OUT_SM || \
    BOARD_TDMA_RX_SYNC_IN_SM == BOARD_TDMA_RX_EVIDENCE_IN_SM || \
    BOARD_TDMA_RX_DATA_OUT_SM == BOARD_TDMA_RX_EVIDENCE_IN_SM
#error "TDMA RX logical-port SM roles must be unique"
#endif
#if BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL == BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL || \
    BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL == BOARD_TDMA_TX_DATA_IN_FORWARD_DMA_CHANNEL || \
    BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL == BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL || \
    BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL == BOARD_TDMA_TX_DATA_IN_FORWARD_DMA_CHANNEL || \
    BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL == BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL || \
    BOARD_TDMA_TX_DATA_IN_FORWARD_DMA_CHANNEL == BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL
#error "TDMA logical-port DMA channels must be unique"
#endif
#if BOARD_TDMA_TX_CLK_OUT_PIN == BOARD_TDMA_TX_SYNC_OUT_PIN || \
    BOARD_TDMA_TX_CLK_OUT_PIN == BOARD_TDMA_TX_DATA_IN_PIN || \
    BOARD_TDMA_TX_SYNC_OUT_PIN == BOARD_TDMA_TX_DATA_IN_PIN || \
    BOARD_TDMA_RX_CLK_IN_PIN == BOARD_TDMA_RX_SYNC_IN_PIN || \
    BOARD_TDMA_RX_CLK_IN_PIN == BOARD_TDMA_RX_DATA_OUT_PIN || \
    BOARD_TDMA_RX_SYNC_IN_PIN == BOARD_TDMA_RX_DATA_OUT_PIN
#error "TDMA logical-port pins must be unique within each port"
#endif
#if BOARD_TDMA_TX_DATA_IN_PIN == BOARD_TDMA_RX_DATA_OUT_PIN
#error "TDMA DATA input/output pins must remain on distinct physical ports"
#endif

#define TDMA_STATE_MACHINE_RESOURCE_CONTRACT_VERSION 1u
#define TDMA_STATE_MACHINE_RESOURCE_CONTRACT_DIRECTIONAL 1u

#if BOARD_TDMA_TX_PIO_BLOCK_ID == 0u
#define TDMA_STATE_MACHINE_TX_PIO_RESOURCE RESOURCE_ARBITER_RESOURCE_PIO0
#elif BOARD_TDMA_TX_PIO_BLOCK_ID == 1u
#define TDMA_STATE_MACHINE_TX_PIO_RESOURCE RESOURCE_ARBITER_RESOURCE_PIO1
#elif BOARD_TDMA_TX_PIO_BLOCK_ID == 2u
#define TDMA_STATE_MACHINE_TX_PIO_RESOURCE RESOURCE_ARBITER_RESOURCE_PIO2
#else
#error "Unsupported TDMA TX PIO block"
#endif

#if BOARD_TDMA_RX_PIO_BLOCK_ID == 0u
#define TDMA_STATE_MACHINE_RX_PIO_RESOURCE RESOURCE_ARBITER_RESOURCE_PIO0
#elif BOARD_TDMA_RX_PIO_BLOCK_ID == 1u
#define TDMA_STATE_MACHINE_RX_PIO_RESOURCE RESOURCE_ARBITER_RESOURCE_PIO1
#elif BOARD_TDMA_RX_PIO_BLOCK_ID == 2u
#define TDMA_STATE_MACHINE_RX_PIO_RESOURCE RESOURCE_ARBITER_RESOURCE_PIO2
#else
#error "Unsupported TDMA RX PIO block"
#endif

/* Runtime ownership projection of the board contract. DREQ values are
 * derived from PIO/SM/direction by the SDK, so reserve their endpoint class
 * instead of duplicating SDK-specific request numbers. */
#define TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK \
    (TDMA_STATE_MACHINE_TX_PIO_RESOURCE | \
     TDMA_STATE_MACHINE_RX_PIO_RESOURCE | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_OUTPUT | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_FORWARD | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_SYNC_EDGE | \
     RESOURCE_ARBITER_RESOURCE_TDMA_GPIO | \
     RESOURCE_ARBITER_RESOURCE_TDMA_IRQ | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DREQ)

/* The adapter-level normal window has one local clock/sync producer and one
 * returned DATA consumer.  The two activities share the logical TX PIO port,
 * but use independent SM/FIFO/DMA roles.  This is deliberately a lower-layer
 * contract: it does not describe a transport frame or a higher communication
 * protocol. */
#define TDMA_STATE_MACHINE_NORMAL_COMM_RESOURCE_MASK \
    (TDMA_STATE_MACHINE_TX_PIO_RESOURCE | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_SYNC_EDGE | \
     RESOURCE_ARBITER_RESOURCE_TDMA_GPIO | \
     RESOURCE_ARBITER_RESOURCE_TDMA_IRQ | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DREQ)

typedef struct {
    PIO clock_tx_pio;
    PIO sync_tx_pio;
    PIO data_rx_pio;
    uint8_t clock_tx_sm;
    uint8_t sync_tx_sm;
    uint8_t data_rx_sm;
    uint8_t clock_tx_pin;
    uint8_t sync_tx_pin;
    uint8_t data_rx_pin;
    uint8_t data_rx_clock_pin;
    uint8_t data_rx_sync_pin;
    uint8_t clock_tx_dma;
    uint8_t data_rx_dma;
    uint32_t resource_mask;
} tdma_state_machine_normal_comm_contract_t;

static inline tdma_state_machine_normal_comm_contract_t
tdma_state_machine_normal_comm_contract(void)
{
    return (tdma_state_machine_normal_comm_contract_t){
        .clock_tx_pio = BOARD_TDMA_TX_PIO,
        .sync_tx_pio = BOARD_TDMA_TX_PIO,
        .data_rx_pio = BOARD_TDMA_TX_PIO,
        .clock_tx_sm = BOARD_TDMA_TX_CLK_OUT_SM,
        .sync_tx_sm = BOARD_TDMA_TX_SYNC_OUT_SM,
        .data_rx_sm = BOARD_TDMA_TX_DATA_IN_CAPTURE_SM,
        .clock_tx_pin = BOARD_TDMA_TX_CLK_OUT_PIN,
        .sync_tx_pin = BOARD_TDMA_TX_SYNC_OUT_PIN,
        .data_rx_pin = BOARD_TDMA_TX_DATA_IN_PIN,
        .data_rx_clock_pin = BOARD_TDMA_TX_CLK_OUT_PIN,
        .data_rx_sync_pin = BOARD_TDMA_TX_SYNC_OUT_PIN,
        /* The clock waveform is generated by the clock SM. This endpoint is
         * the adapter's deterministic clock/sync edge scheduling DMA role. */
        .clock_tx_dma = BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL,
        .data_rx_dma = BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL,
        .resource_mask = TDMA_STATE_MACHINE_NORMAL_COMM_RESOURCE_MASK,
    };
}

/* This view carries board-owned values through persona admission without
 * creating a second source of truth. */
typedef struct {
    PIO tx_pio;
    PIO rx_pio;
    uint8_t tx_clk_out_sm;
    uint8_t tx_sync_out_sm;
    uint8_t tx_data_in_forward_sm;
    uint8_t tx_data_in_capture_sm;
    uint8_t rx_clk_in_sm;
    uint8_t rx_sync_in_sm;
    uint8_t rx_data_out_sm;
    uint8_t rx_evidence_in_sm;
    uint8_t data_in_capture_dma;
    uint8_t data_out_dma;
    uint8_t data_in_forward_dma;
    uint8_t sync_edge_dma;
    uint8_t tx_clk_out_pin;
    uint8_t tx_sync_out_pin;
    uint8_t tx_data_in_pin;
    uint8_t rx_clk_in_pin;
    uint8_t rx_sync_in_pin;
    uint8_t rx_data_out_pin;
} tdma_state_machine_resource_contract_t;

static inline tdma_state_machine_resource_contract_t
tdma_state_machine_resource_contract(void)
{
    return (tdma_state_machine_resource_contract_t){
        .tx_pio = BOARD_TDMA_TX_PIO,
        .rx_pio = BOARD_TDMA_RX_PIO,
        .tx_clk_out_sm = BOARD_TDMA_TX_CLK_OUT_SM,
        .tx_sync_out_sm = BOARD_TDMA_TX_SYNC_OUT_SM,
        .tx_data_in_forward_sm = BOARD_TDMA_TX_DATA_IN_FORWARD_SM,
        .tx_data_in_capture_sm = BOARD_TDMA_TX_DATA_IN_CAPTURE_SM,
        .rx_clk_in_sm = BOARD_TDMA_RX_CLK_IN_SM,
        .rx_sync_in_sm = BOARD_TDMA_RX_SYNC_IN_SM,
        .rx_data_out_sm = BOARD_TDMA_RX_DATA_OUT_SM,
        .rx_evidence_in_sm = BOARD_TDMA_RX_EVIDENCE_IN_SM,
        .data_in_capture_dma = BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL,
        .data_out_dma = BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL,
        .data_in_forward_dma = BOARD_TDMA_TX_DATA_IN_FORWARD_DMA_CHANNEL,
        .sync_edge_dma = BOARD_TDMA_TX_SYNC_EDGE_DMA_CHANNEL,
        .tx_clk_out_pin = BOARD_TDMA_TX_CLK_OUT_PIN,
        .tx_sync_out_pin = BOARD_TDMA_TX_SYNC_OUT_PIN,
        .tx_data_in_pin = BOARD_TDMA_TX_DATA_IN_PIN,
        .rx_clk_in_pin = BOARD_TDMA_RX_CLK_IN_PIN,
        .rx_sync_in_pin = BOARD_TDMA_RX_SYNC_IN_PIN,
        .rx_data_out_pin = BOARD_TDMA_RX_DATA_OUT_PIN,
    };
}

#endif
