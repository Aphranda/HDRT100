#ifndef SYNC_IO_PERSONA_RESOURCES_H
#define SYNC_IO_PERSONA_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYNC_IO_PERSONA_RESOURCE_CONTRACT_VERSION 1u
#define SYNC_IO_PERSONA_PIO_INSTRUCTION_CAPACITY 32u
#define SYNC_IO_PERSONA_PIO_SM_COUNT 4u
#define SYNC_IO_PERSONA_PIO_SM_MASK 0x0Fu
#define SYNC_IO_PERSONA_GPIO_COUNT 30u
#define SYNC_IO_PERSONA_GPIO_MASK 0x3FFFFFFFu
#define SYNC_IO_PERSONA_DMA_CHANNEL_COUNT 16u
#define SYNC_IO_PERSONA_DMA_CHANNEL_MASK 0x0000FFFFu

#define SYNC_IO_SEQ_STEP_DMA_CH 0u
#define SYNC_IO_ENC_COUNT_DMA_CH 1u
#define SYNC_IO_MODEL_PULSE_DMA_CH 2u
#define SYNC_IO_CAPTURE_DMA_CH 3u
#define SYNC_IO_SHARED_WORKSPACE_WORDS 8192u

#define SYNC_IO_INPUT_CAPTURE_INSTRUCTION_WORDS 1u
#define SYNC_IO_SCHEDULED_TRIGGER_INSTRUCTION_WORDS 8u
#define SYNC_IO_WAVE_OUTPUT_INSTRUCTION_WORDS \
    SYNC_IO_SCHEDULED_TRIGGER_INSTRUCTION_WORDS
#define SYNC_IO_LOGIC_ANALYZER_INSTRUCTION_WORDS 1u
#define SYNC_IO_SMA_PERSONA_MAX_INSTRUCTION_WORDS 9u

typedef enum {
    SYNC_IO_PERSONA_ID_NONE = 0,
    SYNC_IO_PERSONA_ID_INPUT_CAPTURE,
    SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER,
    SYNC_IO_PERSONA_ID_WAVE_OUTPUT,
    SYNC_IO_PERSONA_ID_LOGIC_ANALYZER,
    SYNC_IO_PERSONA_ID_SMA_MAINTENANCE,
    SYNC_IO_PERSONA_ID_SMA_CALIBRATION,
    SYNC_IO_PERSONA_ID_COUNT,
} sync_io_persona_id_t;

typedef enum {
    SYNC_IO_PERSONA_IMPLEMENTATION_INVALID = 0,
    SYNC_IO_PERSONA_IMPLEMENTATION_CURRENT,
    SYNC_IO_PERSONA_IMPLEMENTATION_MIGRATION_TARGET,
    SYNC_IO_PERSONA_IMPLEMENTATION_COMPATIBILITY,
} sync_io_persona_implementation_t;

typedef enum {
    SYNC_IO_PERSONA_FLAG_NONE = 0u,
    SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD = 1u << 0,
    SYNC_IO_PERSONA_FLAG_EXCLUSIVE_PIO = 1u << 1,
    SYNC_IO_PERSONA_FLAG_RESTORE_GPIO = 1u << 2,
} sync_io_persona_flag_t;

typedef enum {
    SYNC_IO_PERSONA_IRQ_NONE = 0u,
    SYNC_IO_PERSONA_IRQ_DMA_0 = 1u << 0,
    SYNC_IO_PERSONA_IRQ_DMA_1 = 1u << 1,
    SYNC_IO_PERSONA_IRQ_PIO_0 = 1u << 2,
    SYNC_IO_PERSONA_IRQ_PIO_1 = 1u << 3,
} sync_io_persona_irq_t;

typedef enum {
    SYNC_IO_PERSONA_WORKSPACE_NONE = 0u,
    SYNC_IO_PERSONA_WORKSPACE_CAPTURE_SCHEDULE = 1u << 0,
} sync_io_persona_workspace_t;

typedef enum {
    SYNC_IO_PERSONA_CONFLICT_NONE = 0u,
    SYNC_IO_PERSONA_CONFLICT_INVALID_DESCRIPTOR = 1u << 0,
    SYNC_IO_PERSONA_CONFLICT_EXCLUSIVE_PIO = 1u << 1,
    SYNC_IO_PERSONA_CONFLICT_SM = 1u << 2,
    SYNC_IO_PERSONA_CONFLICT_INSTRUCTION_SPACE = 1u << 3,
    SYNC_IO_PERSONA_CONFLICT_GPIO = 1u << 4,
    SYNC_IO_PERSONA_CONFLICT_FIFO = 1u << 5,
    SYNC_IO_PERSONA_CONFLICT_DMA = 1u << 6,
    SYNC_IO_PERSONA_CONFLICT_DREQ = 1u << 7,
    SYNC_IO_PERSONA_CONFLICT_IRQ = 1u << 8,
    SYNC_IO_PERSONA_CONFLICT_WORKSPACE = 1u << 9,
} sync_io_persona_conflict_t;

typedef struct {
    sync_io_persona_id_t id;
    const char *name;
    const char *program_name;
    sync_io_persona_implementation_t implementation;
    uint32_t flags;
    uint8_t pio_block_id;
    uint8_t instruction_words;
    /* Number of channels needed. dma_channel_mask is the eligible channel
     * pool; a dynamic persona may claim any dma_channel_count members. */
    uint8_t dma_channel_count;
    uint8_t reserved;
    uint32_t sm_mask;
    uint32_t gpio_read_mask;
    uint32_t gpio_write_mask;
    uint32_t rx_fifo_sm_mask;
    uint32_t tx_fifo_sm_mask;
    uint32_t dma_channel_mask;
    uint32_t rx_dreq_sm_mask;
    uint32_t tx_dreq_sm_mask;
    uint32_t irq_mask;
    uint32_t workspace_mask;
    uint32_t safe_low_gpio_mask;
    uint32_t restore_gpio_mask;
} sync_io_persona_descriptor_t;

typedef struct {
    bool compatible;
    uint32_t conflict_mask;
    uint32_t sm_conflict_mask;
    uint32_t gpio_conflict_mask;
    uint32_t fifo_conflict_mask;
    uint32_t dma_conflict_mask;
    uint32_t dreq_conflict_mask;
    uint32_t irq_conflict_mask;
    uint32_t workspace_conflict_mask;
    uint32_t instruction_words_total;
} sync_io_persona_compatibility_t;

const sync_io_persona_descriptor_t *sync_io_persona_descriptor(
    sync_io_persona_id_t id);
const sync_io_persona_descriptor_t *sync_io_persona_descriptor_by_index(
    size_t index);
size_t sync_io_persona_descriptor_count(void);
bool sync_io_persona_descriptor_valid(
    const sync_io_persona_descriptor_t *descriptor);
bool sync_io_persona_catalog_valid(void);
bool sync_io_persona_compatible(
    const sync_io_persona_descriptor_t *first,
    const sync_io_persona_descriptor_t *second,
    sync_io_persona_compatibility_t *result);

#endif
