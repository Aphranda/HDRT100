#ifndef DRV_FLASH_WRITE_H
#define DRV_FLASH_WRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drv_flash.h"

/* Raw erase/program is visible only to FlashTransactionAO and BootFlashService. */
bool drv_flash_erase(uint32_t flash_offset, size_t length);
bool drv_flash_program(uint32_t flash_offset, const uint8_t *data, size_t length);

#endif
