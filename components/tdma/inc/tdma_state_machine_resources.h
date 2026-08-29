#ifndef TDMA_STATE_MACHINE_RESOURCES_H
#define TDMA_STATE_MACHINE_RESOURCES_H

/* Static direction/resource contract for the cyclic flight persona.  The
 * values are board-owned in board_config.h; this header only validates their
 * relationships and provides one stable interface for DeploymentGate/tests. */
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

/* These bits are the runtime ownership projection of the board contract.
 * DREQ values are derived from (PIO, SM, direction) at configuration time,
 * so the contract reserves the endpoint class rather than duplicating SDK
 * implementation-specific DREQ numbers. */
#define TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK \
    (RESOURCE_ARBITER_RESOURCE_PIO1 | \
     RESOURCE_ARBITER_RESOURCE_PIO2 | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_OUTPUT | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_FORWARD | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DMA_SYNC_EDGE | \
     RESOURCE_ARBITER_RESOURCE_TDMA_GPIO | \
     RESOURCE_ARBITER_RESOURCE_TDMA_IRQ | \
     RESOURCE_ARBITER_RESOURCE_TDMA_DREQ)

/* Runtime view of the board-owned contract.  This is deliberately a view,
 * not a second set of constants: all values are returned from board_config.h
 * so persona code can carry the selected PIO/SM/DMA ownership as one object. */
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
