#ifndef SYNC_IO_CORE_INTERNAL_H
#define SYNC_IO_CORE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/types.h"
#include "sync_io.h"

#define SYNC_IO_TRACE_INFO  1u
#define SYNC_IO_TRACE_WARN  2u
#define SYNC_IO_TRACE_ERROR 3u
#define SYNC_IO_SEQ_STEP_DMA_CH  0u
#define SYNC_IO_ENC_COUNT_DMA_CH 1u
#define SYNC_IO_SHARED_DMA_IRQ   DMA_IRQ_0
#define SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD 1u

typedef enum {
    SYNC_IO_TRACE_INIT_OK           = 10u,
    SYNC_IO_TRACE_INIT_FAIL         = 11u,
    SYNC_IO_TRACE_CAPTURE_START     = 20u,
    SYNC_IO_TRACE_CAPTURE_STOP      = 21u,
    SYNC_IO_TRACE_CAPTURE_DROP      = 22u,
    SYNC_IO_TRACE_CAPTURE_FAIL      = 23u,
    SYNC_IO_TRACE_PULSE_FIFO_FULL   = 30u,
    SYNC_IO_TRACE_PULSE_INVALID     = 31u,
    SYNC_IO_TRACE_CLOCK_START       = 40u,
    SYNC_IO_TRACE_CLOCK_STOP        = 41u,
    SYNC_IO_TRACE_CLOCK_FAIL        = 42u,
    SYNC_IO_TRACE_SEQ_ARM_FAIL      = 50u,
    SYNC_IO_TRACE_SEQ_ARMED         = 51u,
    SYNC_IO_TRACE_SEQ_DISARM        = 52u,
    SYNC_IO_TRACE_SEQ_GATE_INVALID  = 53u,
    SYNC_IO_TRACE_SEQ_PIO_NO_SPACE  = 54u,
    SYNC_IO_TRACE_SEQ_RUNTIME       = 55u,
    SYNC_IO_TRACE_SEQ_PIO_STATE     = 56u,
    SYNC_IO_TRACE_SEQ_DMA_RESTART   = 57u,
    SYNC_IO_TRACE_SEQ_DMA_OVERFLOW  = 58u,
    SYNC_IO_TRACE_ENC_ARM_FAIL      = 60u,
    SYNC_IO_TRACE_ENC_ARMED         = 61u,
    SYNC_IO_TRACE_ENC_DISARM        = 62u,
    SYNC_IO_TRACE_ENC_PIO_NO_SPACE  = 63u,
    SYNC_IO_TRACE_ENC_RUNTIME       = 64u,
    SYNC_IO_TRACE_ENC_PIO_STATE     = 65u,
    SYNC_IO_TRACE_ENC_DMA_RESTART   = 66u,
    SYNC_IO_TRACE_ENC_DMA_OVERFLOW  = 67u,
    SYNC_IO_TRACE_AUX_SNAPSHOT      = 70u,
    SYNC_IO_TRACE_READY_REDY        = 71u,
    SYNC_IO_TRACE_AUX_TIMEOUT       = 72u,
    SYNC_IO_TRACE_AUX_BUSY          = 73u,
    SYNC_IO_TRACE_AUX_DIRECTION     = 74u,
    SYNC_IO_TRACE_BISS_TAP_ARM      = 80u,
    SYNC_IO_TRACE_BISS_TAP_DISARM   = 81u,
    SYNC_IO_TRACE_BISS_TAP_FAIL     = 82u,
    SYNC_IO_TRACE_BISS_TAP_FORWARD  = 83u,
} sync_io_trace_event_t;

void sync_io_core_trace(sync_io_trace_event_t event_id,
                        uint8_t severity,
                        uint32_t arg0,
                        uint32_t arg1);
bool sync_io_core_initialized(void);
bool sync_io_core_sm_is_enabled(PIO pio, uint sm);
uint32_t sync_io_core_pack_runtime_flags(bool running,
                                         bool pio_enabled,
                                         bool dma_busy,
                                         bool dma_irq_enabled,
                                         bool tx_fifo_empty,
                                         bool tx_fifo_full);
uint32_t sync_io_core_pack_pio_state(uint sm,
                                     uint32_t offset,
                                     bool pio_enabled,
                                     bool tx_fifo_empty,
                                     bool tx_fifo_full);
void sync_io_core_dma_irq_handler(void);
bool sync_io_seq_step_dma_irq_service(uint32_t ints);
bool sync_io_enc_count_dma_irq_service(uint32_t ints);
uint sync_io_core_biss_tap_offset(void);
uint sync_io_core_aux_passthrough_offset(void);
void sync_io_core_restore_aux_channel_input(uint pin);
void sync_io_core_mark_aux_channel_input(uint pin);
void sync_io_core_set_aux_mode(sync_io_aux_channel_t channel,
                               sync_io_aux_mode_t mode);

#endif
