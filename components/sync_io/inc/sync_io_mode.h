#ifndef SYNC_IO_MODE_H
#define SYNC_IO_MODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SYNC_IO_MODE_ID_NONE = 0,
    SYNC_IO_MODE_ID_SEQ_STEP,
    SYNC_IO_MODE_ID_ENC_COUNT,
    SYNC_IO_MODE_ID_BISS_TAP,
    SYNC_IO_MODE_ID_AUX_DIFF_TRIGGER,  /* reserved: get_ops() returns NULL */
    SYNC_IO_MODE_ID_SELF_CAL,          /* reserved: get_ops() returns NULL */
} sync_io_mode_id_t;

typedef enum {
    SYNC_IO_MODE_RESOURCE_NONE        = 0u,
    SYNC_IO_MODE_RESOURCE_MAIN_INPUT  = 1u << 0,
    SYNC_IO_MODE_RESOURCE_MAIN_OUTPUT = 1u << 1,
    SYNC_IO_MODE_RESOURCE_AUX_RX      = 1u << 2,
    SYNC_IO_MODE_RESOURCE_AUX_TX      = 1u << 3,
    SYNC_IO_MODE_RESOURCE_PIO_WAVE    = 1u << 4,
    SYNC_IO_MODE_RESOURCE_PIO_AUX     = 1u << 5,
    SYNC_IO_MODE_RESOURCE_DMA         = 1u << 6,
    SYNC_IO_MODE_RESOURCE_IRQ         = 1u << 7,
} sync_io_mode_resource_t;

typedef struct {
    uint32_t pio_mask;
    uint32_t pio0_sm_mask;
    uint32_t pio1_sm_mask;
    uint32_t pio2_sm_mask;
    uint32_t dma_channel_mask;
    uint32_t irq_mask;
} sync_io_mode_hw_resources_t;

enum {
    SYNC_IO_MODE_HW_PIO0 = 1u << 0,
    SYNC_IO_MODE_HW_PIO1 = 1u << 1,
    SYNC_IO_MODE_HW_PIO2 = 1u << 2,
};

enum {
    SYNC_IO_MODE_HW_IRQ_DMA0 = 1u << 0,
    SYNC_IO_MODE_HW_IRQ_DMA1 = 1u << 1,
};

typedef struct {
    sync_io_mode_id_t id;
    const char *name;
    uint32_t resources;
    sync_io_mode_hw_resources_t hw;
    bool (*validate)(const void *config);
    bool (*arm)(const void *config);
    void (*disarm)(void);
    bool (*is_running)(void);
} sync_io_mode_ops_t;

#define SYNC_IO_MODE_VOID_DISPATCH(prefix, config_type)                 \
    static bool prefix##_validate_void(const void *config)              \
    {                                                                   \
        return prefix##_validate((const config_type *)config);          \
    }                                                                   \
    static bool prefix##_arm_void(const void *config)                   \
    {                                                                   \
        return prefix##_arm((const config_type *)config);               \
    }

const sync_io_mode_ops_t *sync_io_mode_get_ops(sync_io_mode_id_t id);

/* Enumerates implemented modes only; reserved IDs with NULL ops are filtered. */
const sync_io_mode_ops_t *sync_io_mode_get_by_index(size_t index);
size_t sync_io_mode_count(void);

#endif
