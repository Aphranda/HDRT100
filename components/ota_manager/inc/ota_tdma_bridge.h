#ifndef OTA_TDMA_BRIDGE_H
#define OTA_TDMA_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

/* OTA-TDMA bridge (architecture v2): the TDMA maintenance stream only carries
 * OTA data; control and the flash/BCB/commit/switch flow stay with the existing
 * AB pipeline.  This adapter stages stream frames into flash-writable blocks
 * and forwards one ordered OTA operation per block through a sink, so the
 * target reuses the proven ota_ao/ota_fb -> portable_ota_port_core_* path
 * instead of a second stream session/ingress/checkpoint stack.
 *
 * Block sizes mirror the source pump's ACK granularity: the first block is
 * 512 B (package header), later blocks are 256 B.  One block is in flight at a
 * time; the OTA AO calls release() when it has consumed the block, and only
 * then may the next block be forwarded and acknowledged. */

typedef enum {
    OTA_TDMA_BRIDGE_IDLE = 0,
    OTA_TDMA_BRIDGE_RECEIVING,
    OTA_TDMA_BRIDGE_DONE,
    OTA_TDMA_BRIDGE_FAILED,
} ota_tdma_bridge_state_t;

typedef enum {
    OTA_TDMA_BRIDGE_OP_BEGIN = 0,
    OTA_TDMA_BRIDGE_OP_DATA,
    OTA_TDMA_BRIDGE_OP_END,
    OTA_TDMA_BRIDGE_OP_ABORT,
} ota_tdma_bridge_op_t;

typedef struct {
    uint32_t size;
    uint32_t crc32;
    bool package_mode;
} ota_tdma_bridge_begin_t;

/* The sink owns the existing OTA pipeline.  Returning false is fail-closed:
 * the bridge stops and reports FAILED, mirroring the OTA AO's own errors. */
typedef bool (*ota_tdma_bridge_sink_fn)(void *context,
                                        ota_tdma_bridge_op_t operation,
                                        const ota_tdma_bridge_begin_t *begin,
                                        const uint8_t *data,
                                        uint32_t length);

#define OTA_TDMA_BRIDGE_FIRST_BLOCK 512u
#define OTA_TDMA_BRIDGE_BLOCK 256u
#define OTA_TDMA_BRIDGE_BLOCK_MAX OTA_TDMA_BRIDGE_FIRST_BLOCK

typedef struct {
    ota_tdma_bridge_sink_fn sink;
    void *context;
    uint32_t expected_size;
    uint32_t received_size;
    ota_tdma_bridge_state_t state;
    uint32_t stage_offset;
    uint32_t stage_length;
    uint32_t block_target;
    uint32_t block_end;
    bool block_in_flight;
    uint8_t stage[OTA_TDMA_BRIDGE_BLOCK_MAX];
} ota_tdma_bridge_t;

bool ota_tdma_bridge_init(ota_tdma_bridge_t *bridge,
                          ota_tdma_bridge_sink_fn sink, void *context);

bool ota_tdma_bridge_begin(ota_tdma_bridge_t *bridge, uint32_t size,
                           uint32_t crc32, bool package_mode);

/* Appends one frame payload.  `offset` must be the block-contiguous stream
 * offset; a block is forwarded to the sink as soon as it is complete.  Returns
 * false when the offset is not contiguous, the frame overflows the staging
 * block, or the sink rejects the block (state becomes FAILED); returns true
 * while a previous block is in flight only when it is also backpressure-safe. */
bool ota_tdma_bridge_data(ota_tdma_bridge_t *bridge, uint32_t offset,
                          const uint8_t *data, uint32_t length);

/* True while a staged block waits for the OTA AO (new data is backpressure). */
bool ota_tdma_bridge_block_in_flight(const ota_tdma_bridge_t *bridge);

/* Releases the in-flight block once the OTA AO consumed it; returns the block
 * end offset that may now be acknowledged, or 0 when nothing was in flight. */
uint32_t ota_tdma_bridge_release(ota_tdma_bridge_t *bridge);

bool ota_tdma_bridge_end(ota_tdma_bridge_t *bridge);

bool ota_tdma_bridge_abort(ota_tdma_bridge_t *bridge);

ota_tdma_bridge_state_t ota_tdma_bridge_state(const ota_tdma_bridge_t *bridge);

uint32_t ota_tdma_bridge_received(const ota_tdma_bridge_t *bridge);

#endif
