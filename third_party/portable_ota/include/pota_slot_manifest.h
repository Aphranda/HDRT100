#ifndef POTA_SLOT_MANIFEST_H
#define POTA_SLOT_MANIFEST_H

#include <stdbool.h>
#include <stdint.h>

#include "pota_package.h"

#define POTA_SLOT_MANIFEST_BODY_MAGIC       0x534D4244u
#define POTA_SLOT_MANIFEST_COMMIT_MAGIC     0x534D434Du
#define POTA_SLOT_MANIFEST_COMMIT_MARKER    0x51A7C04Du
#define POTA_SLOT_MANIFEST_SCHEMA_VERSION   1u
#define POTA_SLOT_MANIFEST_LANE_COUNT       2u
#define POTA_SLOT_MANIFEST_BODY_SIZE        768u
#define POTA_SLOT_MANIFEST_COMMIT_SIZE      256u
#define POTA_SLOT_MANIFEST_RECORD_SIZE      1024u

typedef enum {
    POTA_SLOT_MANIFEST_OK = 0,
    POTA_SLOT_MANIFEST_BAD_ARGUMENT,
    POTA_SLOT_MANIFEST_NO_VALID,
    POTA_SLOT_MANIFEST_IO,
    POTA_SLOT_MANIFEST_CORRUPT,
    POTA_SLOT_MANIFEST_ROLLBACK,
} pota_slot_manifest_result_t;

typedef struct {
    void *context;
    bool (*read)(void *context, uint32_t offset, void *data, uint32_t length);
    bool (*program)(void *context, uint32_t offset,
                    const void *data, uint32_t length);
    bool (*erase)(void *context, uint32_t offset, uint32_t length);
    uint32_t base_offset;
    uint32_t lane_size;
    uint32_t page_size;
    uint32_t erase_size;
    uint32_t map_version;
    pota_slot_t slot;
} pota_slot_manifest_config_t;

typedef struct {
    pota_slot_manifest_config_t config;
    bool initialized;
} pota_slot_manifest_store_t;

typedef struct {
    uint32_t sequence;
    uint32_t lane;
    pota_slot_t slot;
    uint8_t header[POTA_PACKAGE_HEADER_SIZE];
} pota_slot_manifest_t;

typedef enum {
    POTA_SLOT_MANIFEST_STEP_FAILED = 0,
    POTA_SLOT_MANIFEST_STEP_PENDING,
    POTA_SLOT_MANIFEST_STEP_DONE,
} pota_slot_manifest_step_result_t;

/* AO-owned append context.  The byte arrays intentionally keep the wire
 * layout private to the implementation while allowing one bounded physical
 * operation per service call. */
typedef struct {
    pota_slot_manifest_config_t config;
    pota_slot_manifest_step_result_t terminal;
    uint8_t header[POTA_PACKAGE_HEADER_SIZE];
    uint8_t body[POTA_SLOT_MANIFEST_BODY_SIZE];
    uint8_t commit[POTA_SLOT_MANIFEST_COMMIT_SIZE];
    uint8_t readback[POTA_SLOT_MANIFEST_BODY_SIZE];
    uint32_t sequence;
    uint32_t target_lane;
    uint32_t target_offset;
    uint32_t erase_cursor;
    uint32_t program_cursor;
    uint32_t state;
    bool active;
} pota_slot_manifest_txn_t;

pota_slot_manifest_result_t pota_slot_manifest_init(
    pota_slot_manifest_store_t *store,
    const pota_slot_manifest_config_t *config);
pota_slot_manifest_result_t pota_slot_manifest_load(
    const pota_slot_manifest_store_t *store,
    pota_slot_manifest_t *manifest);
pota_slot_manifest_result_t pota_slot_manifest_append(
    pota_slot_manifest_store_t *store,
    const uint8_t header[POTA_PACKAGE_HEADER_SIZE],
    pota_slot_manifest_t *committed);

pota_slot_manifest_result_t pota_slot_manifest_txn_begin(
    pota_slot_manifest_txn_t *txn,
    const pota_slot_manifest_store_t *store,
    const uint8_t header[POTA_PACKAGE_HEADER_SIZE]);
pota_slot_manifest_step_result_t pota_slot_manifest_txn_step(
    pota_slot_manifest_txn_t *txn);

#endif
