#ifndef TEST_HARDWARE_FLASH_H
#define TEST_HARDWARE_FLASH_H

#include <stddef.h>
#include <stdint.h>

void flash_range_erase(uint32_t flash_offset, size_t length);
void flash_range_program(uint32_t flash_offset, const uint8_t *data, size_t length);
void flash_do_cmd(const uint8_t *txbuf, uint8_t *rxbuf, size_t count);

#endif
