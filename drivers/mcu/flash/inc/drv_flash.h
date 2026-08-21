#ifndef DRV_FLASH_H
#define DRV_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drv_flash_lockout.h"
#include "flash_map_gen/flash_map_v2.h"

#define DRV_FLASH_TOTAL_SIZE_BYTES FLASH_GEOMETRY_TOTAL_SIZE_BYTES
#define DRV_FLASH_SECTOR_SIZE      FLASH_GEOMETRY_ERASE_SIZE_BYTES
#define DRV_FLASH_PAGE_SIZE        FLASH_GEOMETRY_PROGRAM_SIZE_BYTES
#define DRV_FLASH_XIP_BASE         FLASH_GEOMETRY_XIP_BASE

bool drv_flash_is_range_valid(uint32_t flash_offset, size_t length);
bool drv_flash_is_erased(uint32_t flash_offset, size_t length);
bool drv_flash_erase(uint32_t flash_offset, size_t length);
bool drv_flash_program(uint32_t flash_offset, const uint8_t *data, size_t length);
bool drv_flash_read(uint32_t flash_offset, void *data, size_t length);
const uint8_t *drv_flash_xip_ptr(uint32_t flash_offset);
void drv_flash_core1_lockout_poll(void);
void drv_flash_get_lockout_status(drv_flash_lockout_status_t *status);
void drv_flash_set_lockout_fault_injection(uint32_t flags);
void drv_flash_clear_lockout_fault_injection(void);

#endif
