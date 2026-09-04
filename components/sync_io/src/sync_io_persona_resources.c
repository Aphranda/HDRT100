#include "sync_io_persona_resources.h"

#include <string.h>

#include "board_config.h"

#define SYNC_IO_SM_BIT(sm) (1u << (sm))
#define SYNC_IO_GPIO_BIT(pin) (1u << (pin))
#define SYNC_IO_GPIO_RANGE_MASK(base, count) \
    (((1u << (count)) - 1u) << (base))
#define SYNC_IO_DMA_BIT(channel) (1u << (channel))

#define SYNC_IO_MAIN_INPUT_GPIO_MASK \
    SYNC_IO_GPIO_RANGE_MASK(BOARD_SYNC_INPUT_BASE_PIN, \
                            BOARD_SYNC_INPUT_PIN_COUNT)
#define SYNC_IO_MAIN_OUTPUT_GPIO_MASK \
    SYNC_IO_GPIO_RANGE_MASK(BOARD_SYNC_OUTPUT_BASE_PIN, \
                            BOARD_SYNC_OUTPUT_PIN_COUNT)
#define SYNC_IO_TDMA_PAD_GPIO_MASK \
    (SYNC_IO_GPIO_BIT(BOARD_TDMA_TX_CLK_OUT_PIN) | \
     SYNC_IO_GPIO_BIT(BOARD_TDMA_TX_SYNC_OUT_PIN) | \
     SYNC_IO_GPIO_BIT(BOARD_TDMA_TX_DATA_IN_PIN) | \
     SYNC_IO_GPIO_BIT(BOARD_TDMA_RX_CLK_IN_PIN) | \
     SYNC_IO_GPIO_BIT(BOARD_TDMA_RX_SYNC_IN_PIN) | \
     SYNC_IO_GPIO_BIT(BOARD_TDMA_RX_DATA_OUT_PIN))

static const sync_io_persona_descriptor_t s_sync_io_personas[] = {
    {
        .id = SYNC_IO_PERSONA_ID_INPUT_CAPTURE,
        .name = "input_capture",
        .program_name = "sync_capture_4bit",
        .implementation = SYNC_IO_PERSONA_IMPLEMENTATION_CURRENT,
        .pio_block_id = BOARD_TDMA_SMA_PIO_BLOCK_ID,
        .instruction_words = SYNC_IO_INPUT_CAPTURE_INSTRUCTION_WORDS,
        .dma_channel_count = 1u,
        .sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_INPUT_CAPTURE_SM),
        .gpio_read_mask = SYNC_IO_MAIN_INPUT_GPIO_MASK,
        .rx_fifo_sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_INPUT_CAPTURE_SM),
        .dma_channel_mask = SYNC_IO_DMA_BIT(SYNC_IO_CAPTURE_DMA_CH),
        .rx_dreq_sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_INPUT_CAPTURE_SM),
        .workspace_mask = SYNC_IO_PERSONA_WORKSPACE_CAPTURE_SCHEDULE,
    },
    {
        .id = SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER,
        .name = "scheduled_trigger",
        .program_name = "sync_model_sched_pulse_high_or_low",
        .implementation = SYNC_IO_PERSONA_IMPLEMENTATION_MIGRATION_TARGET,
        .flags = SYNC_IO_PERSONA_FLAG_RESTORE_GPIO,
        .pio_block_id = BOARD_TDMA_SMA_PIO_BLOCK_ID,
        .instruction_words = SYNC_IO_SCHEDULED_TRIGGER_INSTRUCTION_WORDS,
        .dma_channel_count = 1u,
        .sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM),
        .gpio_write_mask = SYNC_IO_GPIO_BIT(BOARD_SYNC_TRIG_OUT_PIN),
        .tx_fifo_sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM),
        .dma_channel_mask = SYNC_IO_DMA_BIT(SYNC_IO_MODEL_PULSE_DMA_CH),
        .tx_dreq_sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM),
        .workspace_mask = SYNC_IO_PERSONA_WORKSPACE_CAPTURE_SCHEDULE,
        .safe_low_gpio_mask = SYNC_IO_GPIO_BIT(BOARD_SYNC_TRIG_OUT_PIN),
        .restore_gpio_mask = SYNC_IO_GPIO_BIT(BOARD_SYNC_TRIG_OUT_PIN),
    },
    {
        .id = SYNC_IO_PERSONA_ID_WAVE_OUTPUT,
        .name = "wave_output",
        .program_name = "sync_model_sched_pulse_high_or_low",
        .implementation = SYNC_IO_PERSONA_IMPLEMENTATION_MIGRATION_TARGET,
        .flags = SYNC_IO_PERSONA_FLAG_RESTORE_GPIO,
        .pio_block_id = BOARD_TDMA_SMA_PIO_BLOCK_ID,
        .instruction_words = SYNC_IO_WAVE_OUTPUT_INSTRUCTION_WORDS,
        .dma_channel_count = 1u,
        .sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_WAVE_OUTPUT_SM),
        .gpio_write_mask = SYNC_IO_MAIN_OUTPUT_GPIO_MASK,
        .tx_fifo_sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_WAVE_OUTPUT_SM),
        .dma_channel_mask = SYNC_IO_DMA_BIT(SYNC_IO_MODEL_PULSE_DMA_CH),
        .tx_dreq_sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_WAVE_OUTPUT_SM),
        .workspace_mask = SYNC_IO_PERSONA_WORKSPACE_CAPTURE_SCHEDULE,
        .safe_low_gpio_mask = SYNC_IO_MAIN_OUTPUT_GPIO_MASK,
        .restore_gpio_mask = SYNC_IO_MAIN_OUTPUT_GPIO_MASK,
    },
    {
        .id = SYNC_IO_PERSONA_ID_LOGIC_ANALYZER,
        .name = "logic_analyzer",
        .program_name = "logic_analyzer_raw_sample",
        .implementation = SYNC_IO_PERSONA_IMPLEMENTATION_MIGRATION_TARGET,
        .flags = SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD,
        .pio_block_id = BOARD_TDMA_SMA_PIO_BLOCK_ID,
        .instruction_words = SYNC_IO_LOGIC_ANALYZER_INSTRUCTION_WORDS,
        .dma_channel_count = 1u,
        .sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM),
        .gpio_read_mask = SYNC_IO_MAIN_INPUT_GPIO_MASK |
                          SYNC_IO_MAIN_OUTPUT_GPIO_MASK |
                          SYNC_IO_TDMA_PAD_GPIO_MASK,
        .rx_fifo_sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM),
        .dma_channel_mask = SYNC_IO_DMA_BIT(SYNC_IO_CAPTURE_DMA_CH),
        .rx_dreq_sm_mask = SYNC_IO_SM_BIT(BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM),
        .workspace_mask = SYNC_IO_PERSONA_WORKSPACE_CAPTURE_SCHEDULE,
    },
    {
        .id = SYNC_IO_PERSONA_ID_SMA_MAINTENANCE,
        .name = "sma_maintenance",
        .program_name = "sma_cable_delay_role_set",
        .implementation = SYNC_IO_PERSONA_IMPLEMENTATION_COMPATIBILITY,
        .flags = SYNC_IO_PERSONA_FLAG_EXCLUSIVE_PIO |
                 SYNC_IO_PERSONA_FLAG_RESTORE_GPIO,
        .pio_block_id = BOARD_TDMA_SMA_PIO_BLOCK_ID,
        .instruction_words = SYNC_IO_SMA_PERSONA_MAX_INSTRUCTION_WORDS,
        .dma_channel_count = 1u,
        .sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .gpio_read_mask = SYNC_IO_MAIN_INPUT_GPIO_MASK |
                          SYNC_IO_TDMA_PAD_GPIO_MASK,
        .gpio_write_mask = SYNC_IO_MAIN_OUTPUT_GPIO_MASK,
        .rx_fifo_sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .tx_fifo_sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .dma_channel_mask = SYNC_IO_PERSONA_DMA_CHANNEL_MASK,
        .rx_dreq_sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .tx_dreq_sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .irq_mask = SYNC_IO_PERSONA_IRQ_PIO_0,
        .safe_low_gpio_mask = SYNC_IO_MAIN_OUTPUT_GPIO_MASK,
        .restore_gpio_mask = SYNC_IO_MAIN_INPUT_GPIO_MASK |
                             SYNC_IO_MAIN_OUTPUT_GPIO_MASK |
                             SYNC_IO_TDMA_PAD_GPIO_MASK,
    },
    {
        .id = SYNC_IO_PERSONA_ID_SMA_CALIBRATION,
        .name = "sma_calibration",
        .program_name = "sma_cable_delay_role_set",
        .implementation = SYNC_IO_PERSONA_IMPLEMENTATION_COMPATIBILITY,
        .flags = SYNC_IO_PERSONA_FLAG_EXCLUSIVE_PIO |
                 SYNC_IO_PERSONA_FLAG_RESTORE_GPIO,
        .pio_block_id = BOARD_TDMA_SMA_PIO_BLOCK_ID,
        .instruction_words = SYNC_IO_SMA_PERSONA_MAX_INSTRUCTION_WORDS,
        .dma_channel_count = 1u,
        .sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .gpio_read_mask = SYNC_IO_MAIN_INPUT_GPIO_MASK |
                          SYNC_IO_TDMA_PAD_GPIO_MASK,
        .gpio_write_mask = SYNC_IO_MAIN_OUTPUT_GPIO_MASK,
        .rx_fifo_sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .tx_fifo_sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .dma_channel_mask = SYNC_IO_PERSONA_DMA_CHANNEL_MASK,
        .rx_dreq_sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .tx_dreq_sm_mask = SYNC_IO_PERSONA_PIO_SM_MASK,
        .irq_mask = SYNC_IO_PERSONA_IRQ_PIO_0,
        .safe_low_gpio_mask = SYNC_IO_MAIN_OUTPUT_GPIO_MASK,
        .restore_gpio_mask = SYNC_IO_MAIN_INPUT_GPIO_MASK |
                             SYNC_IO_MAIN_OUTPUT_GPIO_MASK |
                             SYNC_IO_TDMA_PAD_GPIO_MASK,
    },
};

static uint32_t sync_io_persona_popcount(uint32_t value)
{
    uint32_t count = 0u;
    while (value != 0u) {
        count += value & 1u;
        value >>= 1u;
    }
    return count;
}

const sync_io_persona_descriptor_t *sync_io_persona_descriptor(
    sync_io_persona_id_t id)
{
    if (id <= SYNC_IO_PERSONA_ID_NONE || id >= SYNC_IO_PERSONA_ID_COUNT) {
        return NULL;
    }
    return &s_sync_io_personas[(size_t)id - 1u];
}

const sync_io_persona_descriptor_t *sync_io_persona_descriptor_by_index(
    size_t index)
{
    if (index >= sync_io_persona_descriptor_count()) {
        return NULL;
    }
    return &s_sync_io_personas[index];
}

size_t sync_io_persona_descriptor_count(void)
{
    return sizeof(s_sync_io_personas) / sizeof(s_sync_io_personas[0]);
}

bool sync_io_persona_descriptor_valid(
    const sync_io_persona_descriptor_t *descriptor)
{
    if (descriptor == NULL ||
        descriptor->id <= SYNC_IO_PERSONA_ID_NONE ||
        descriptor->id >= SYNC_IO_PERSONA_ID_COUNT ||
        descriptor->name == NULL || descriptor->name[0] == '\0' ||
        descriptor->program_name == NULL ||
        descriptor->program_name[0] == '\0' ||
        descriptor->implementation <= SYNC_IO_PERSONA_IMPLEMENTATION_INVALID ||
        descriptor->implementation >
            SYNC_IO_PERSONA_IMPLEMENTATION_COMPATIBILITY ||
        descriptor->pio_block_id != BOARD_TDMA_SMA_PIO_BLOCK_ID ||
        descriptor->sm_mask == 0u ||
        (descriptor->sm_mask & ~SYNC_IO_PERSONA_PIO_SM_MASK) != 0u ||
        descriptor->instruction_words == 0u ||
        descriptor->instruction_words >
            SYNC_IO_PERSONA_PIO_INSTRUCTION_CAPACITY ||
        ((descriptor->gpio_read_mask | descriptor->gpio_write_mask |
          descriptor->safe_low_gpio_mask | descriptor->restore_gpio_mask) &
         ~SYNC_IO_PERSONA_GPIO_MASK) != 0u ||
        (descriptor->gpio_read_mask & descriptor->gpio_write_mask) != 0u ||
        ((descriptor->rx_fifo_sm_mask | descriptor->tx_fifo_sm_mask |
          descriptor->rx_dreq_sm_mask | descriptor->tx_dreq_sm_mask) &
         ~descriptor->sm_mask) != 0u ||
        (descriptor->rx_dreq_sm_mask & ~descriptor->rx_fifo_sm_mask) != 0u ||
        (descriptor->tx_dreq_sm_mask & ~descriptor->tx_fifo_sm_mask) != 0u ||
        (descriptor->dma_channel_mask &
         ~SYNC_IO_PERSONA_DMA_CHANNEL_MASK) != 0u ||
        descriptor->dma_channel_count >
            sync_io_persona_popcount(descriptor->dma_channel_mask) ||
        (descriptor->safe_low_gpio_mask &
         ~descriptor->gpio_write_mask) != 0u ||
        (descriptor->restore_gpio_mask &
         ~(descriptor->gpio_read_mask | descriptor->gpio_write_mask)) != 0u) {
        return false;
    }

    const bool uses_dreq = descriptor->rx_dreq_sm_mask != 0u ||
                           descriptor->tx_dreq_sm_mask != 0u;
    if ((uses_dreq && descriptor->dma_channel_count == 0u) ||
        (descriptor->dma_channel_count == 0u &&
         descriptor->dma_channel_mask != 0u) ||
        (descriptor->dma_channel_count != 0u &&
         descriptor->dma_channel_mask == 0u)) {
        return false;
    }
    if ((descriptor->flags & SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD) != 0u &&
        (descriptor->gpio_write_mask != 0u ||
         descriptor->tx_fifo_sm_mask != 0u ||
         descriptor->tx_dreq_sm_mask != 0u ||
         descriptor->safe_low_gpio_mask != 0u)) {
        return false;
    }
    if ((descriptor->flags & SYNC_IO_PERSONA_FLAG_EXCLUSIVE_PIO) != 0u &&
        descriptor->sm_mask != SYNC_IO_PERSONA_PIO_SM_MASK) {
        return false;
    }
    if ((descriptor->flags & SYNC_IO_PERSONA_FLAG_RESTORE_GPIO) == 0u &&
        descriptor->restore_gpio_mask != 0u) {
        return false;
    }
    return true;
}

bool sync_io_persona_catalog_valid(void)
{
    if (sync_io_persona_descriptor_count() !=
        (size_t)SYNC_IO_PERSONA_ID_COUNT - 1u) {
        return false;
    }
    for (size_t index = 0u;
         index < sync_io_persona_descriptor_count();
         ++index) {
        const sync_io_persona_descriptor_t *descriptor =
            &s_sync_io_personas[index];
        if (!sync_io_persona_descriptor_valid(descriptor) ||
            descriptor->id != (sync_io_persona_id_t)(index + 1u)) {
            return false;
        }
        for (size_t other = index + 1u;
             other < sync_io_persona_descriptor_count();
             ++other) {
            if (descriptor->id == s_sync_io_personas[other].id ||
                strcmp(descriptor->name,
                       s_sync_io_personas[other].name) == 0) {
                return false;
            }
        }
    }
    return true;
}

bool sync_io_persona_compatible(
    const sync_io_persona_descriptor_t *first,
    const sync_io_persona_descriptor_t *second,
    sync_io_persona_compatibility_t *result)
{
    sync_io_persona_compatibility_t local = {0};
    if (!sync_io_persona_descriptor_valid(first) ||
        !sync_io_persona_descriptor_valid(second)) {
        local.conflict_mask = SYNC_IO_PERSONA_CONFLICT_INVALID_DESCRIPTOR;
        if (result != NULL) {
            *result = local;
        }
        return false;
    }

    const bool same_pio = first->pio_block_id == second->pio_block_id;
    if (same_pio &&
        (((first->flags | second->flags) &
          SYNC_IO_PERSONA_FLAG_EXCLUSIVE_PIO) != 0u)) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_EXCLUSIVE_PIO;
    }
    if (same_pio) {
        local.sm_conflict_mask = first->sm_mask & second->sm_mask;
        if (local.sm_conflict_mask != 0u) {
            local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_SM;
        }
        local.instruction_words_total =
            (uint32_t)first->instruction_words + second->instruction_words;
        if (local.instruction_words_total >
            SYNC_IO_PERSONA_PIO_INSTRUCTION_CAPACITY) {
            local.conflict_mask |=
                SYNC_IO_PERSONA_CONFLICT_INSTRUCTION_SPACE;
        }
        local.fifo_conflict_mask =
            (first->rx_fifo_sm_mask | first->tx_fifo_sm_mask) &
            (second->rx_fifo_sm_mask | second->tx_fifo_sm_mask);
        if (local.fifo_conflict_mask != 0u) {
            local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_FIFO;
        }
        local.dreq_conflict_mask =
            (first->rx_dreq_sm_mask | first->tx_dreq_sm_mask) &
            (second->rx_dreq_sm_mask | second->tx_dreq_sm_mask);
        if (local.dreq_conflict_mask != 0u) {
            local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_DREQ;
        }
    }

    /* Reading a pad is observational and may coexist with its producer. Only
     * two writers constitute a GPIO ownership conflict. */
    local.gpio_conflict_mask =
        first->gpio_write_mask & second->gpio_write_mask;
    if (local.gpio_conflict_mask != 0u) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_GPIO;
    }

    local.dma_conflict_mask =
        first->dma_channel_mask & second->dma_channel_mask;
    if (first->dma_channel_count != 0u &&
        second->dma_channel_count != 0u &&
        sync_io_persona_popcount(first->dma_channel_mask |
                                 second->dma_channel_mask) <
            (uint32_t)first->dma_channel_count +
            (uint32_t)second->dma_channel_count) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_DMA;
    }
    local.irq_conflict_mask = first->irq_mask & second->irq_mask;
    if (local.irq_conflict_mask != 0u) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_IRQ;
    }
    local.workspace_conflict_mask =
        first->workspace_mask & second->workspace_mask;
    if (local.workspace_conflict_mask != 0u) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_WORKSPACE;
    }

    local.compatible = local.conflict_mask == SYNC_IO_PERSONA_CONFLICT_NONE;
    if (result != NULL) {
        *result = local;
    }
    return local.compatible;
}
