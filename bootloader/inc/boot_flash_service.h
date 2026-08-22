#ifndef BOOT_FLASH_SERVICE_H
#define BOOT_FLASH_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Boot-only write owner. Callers provide physical offsets for deployed v1
 * boot-writable partitions (the two image slots and Boot Control). */
bool boot_flash_service_erase(uint32_t flash_offset, size_t length);
bool boot_flash_service_program(uint32_t flash_offset,
                                const uint8_t *data,
                                size_t length);

#endif
