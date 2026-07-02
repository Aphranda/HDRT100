#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sd_card.h"

typedef enum {
    STORAGE_MANAGER_STATE_UNINITIALIZED = 0,
    STORAGE_MANAGER_STATE_IDLE,
    STORAGE_MANAGER_STATE_CARD_READY,
    STORAGE_MANAGER_STATE_NO_CARD,
    STORAGE_MANAGER_STATE_NO_FILESYSTEM,
    STORAGE_MANAGER_STATE_PATH_ERROR,
    STORAGE_MANAGER_STATE_FAILED,
} storage_manager_state_t;

typedef struct {
    storage_manager_state_t state;
    bool card_present;
    bool fs_mounted;
    bool fatfs_available;
    sd_card_type_t card_type;
    bool high_capacity;
    uint32_t block_count;
    uint32_t capacity_kib;
    uint32_t probe_count;
    uint32_t last_probe_ms;
    sd_card_status_t card_status;
    uint32_t storage_error;
} storage_manager_vector_t;

bool storage_manager_init(void);
bool storage_manager_probe(void);
bool storage_manager_catalog(const char *path, char *buffer, size_t buffer_size);
void storage_manager_service(uint32_t budget_us);
void storage_manager_get_vector(storage_manager_vector_t *vector);
const char *storage_manager_state_string(storage_manager_state_t state);

#endif
