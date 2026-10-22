#ifndef REFMEM_CMD_CODEC_H
#define REFMEM_CMD_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_command.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical REFMEM_CMD wire frame.
 *
 * The frame carries command intent only. It never carries a large payload
 * array: payload_kind/payload_ref/payload_size/payload_crc32 refer to the
 * actual payload (inline small, staging, or TLV). All multi-byte fields are
 * little-endian on the wire.
 */
#define REFMEM_CMD_MAGIC 0x52434D44u /* "RCMD" */
#define REFMEM_CMD_VERSION 1u
#define REFMEM_CMD_WIRE_SIZE 56u
#define REFMEM_CMD_CRC_OFFSET 52u

typedef enum {
    REFMEM_CMD_CODEC_OK = 0u,
    REFMEM_CMD_CODEC_BAD_ARGUMENT = 1u,
    REFMEM_CMD_CODEC_BAD_MAGIC = 2u,
    REFMEM_CMD_CODEC_BAD_VERSION = 3u,
    REFMEM_CMD_CODEC_BAD_CRC = 4u,
    REFMEM_CMD_CODEC_BAD_RESERVED = 5u,
    REFMEM_CMD_CODEC_RANGE = 6u,
} refmem_cmd_codec_result_t;

/*
 * Encode a command request into a canonical wire frame.
 * Returns REFMEM_CMD_CODEC_OK on success.
 */
refmem_cmd_codec_result_t refmem_cmd_codec_encode(
    const refmem_command_request_t *request,
    uint8_t wire[REFMEM_CMD_WIRE_SIZE]);

/*
 * Decode a canonical wire frame into a command request.
 * The frame CRC covers bytes [0, REFMEM_CMD_CRC_OFFSET).
 */
refmem_cmd_codec_result_t refmem_cmd_codec_decode(
    const uint8_t wire[REFMEM_CMD_WIRE_SIZE],
    refmem_command_request_t *request);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_CMD_CODEC_H */
