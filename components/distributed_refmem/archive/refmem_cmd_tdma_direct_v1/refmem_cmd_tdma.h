#ifndef REFMEM_CMD_TDMA_ARCHIVE_V1_H
#define REFMEM_CMD_TDMA_ARCHIVE_V1_H

/* ARCHIVED v1 - not built. See ARCHIVE_NOTE.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_cmd_codec.h"

bool refmem_cmd_tdma_submit(const uint8_t wire[REFMEM_CMD_WIRE_SIZE]);
bool refmem_cmd_tdma_take(uint8_t wire[REFMEM_CMD_WIRE_SIZE]);
void refmem_cmd_tdma_rx(const uint8_t *payload, size_t payload_size);

#endif /* REFMEM_CMD_TDMA_ARCHIVE_V1_H */
