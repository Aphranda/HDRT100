#ifndef SYNC_IO_MODE_H
#define SYNC_IO_MODE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SYNC_IO_MODE_ID_NONE = 0,
    SYNC_IO_MODE_ID_SEQ_STEP,
    SYNC_IO_MODE_ID_ENC_COUNT,
    SYNC_IO_MODE_ID_BISS_TAP,
    SYNC_IO_MODE_ID_AUX_DIFF_TRIGGER,
    SYNC_IO_MODE_ID_SELF_CAL,
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
    sync_io_mode_id_t id;
    const char *name;
    uint32_t resources;
    bool (*validate)(const void *config);
    bool (*arm)(const void *config);
    void (*disarm)(void);
    bool (*is_running)(void);
} sync_io_mode_ops_t;

#endif

