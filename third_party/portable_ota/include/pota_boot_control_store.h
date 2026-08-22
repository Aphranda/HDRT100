#ifndef POTA_BOOT_CONTROL_STORE_H
#define POTA_BOOT_CONTROL_STORE_H

#include <stdbool.h>
#include <stdint.h>

/* One body page and one commit page form a record.  The final page of each
 * lane is reserved for the lane seal.  The values are page geometry, not a
 * board-specific Flash offset. */
#define POTA_BCB_PAGE_SIZE 256u
#define POTA_BCB_LANE_COUNT 2u
#define POTA_BCB_BODY_PAYLOAD_SIZE 220u
#define POTA_BCB_BODY_MAGIC 0x42434242u
#define POTA_BCB_COMMIT_MAGIC 0x42434243u
#define POTA_BCB_SEAL_MAGIC 0x42434253u
#define POTA_BCB_COMMIT_MARKER 0xC04D4D49u
#define POTA_BCB_SEAL_MARKER 0x5345414Cu

typedef enum {
    POTA_BCB_RESULT_OK = 0,
    POTA_BCB_RESULT_BAD_ARGUMENT,
    POTA_BCB_RESULT_NO_VALID,
    POTA_BCB_RESULT_BUSY,
    POTA_BCB_RESULT_RANGE,
    POTA_BCB_RESULT_POLICY,
    POTA_BCB_RESULT_IO,
    POTA_BCB_RESULT_VERIFY,
    POTA_BCB_RESULT_REPLAY,
} pota_bcb_result_t;

typedef struct {
    void *context;
    bool (*read_page)(void *context, uint32_t lane, uint32_t page,
                      uint8_t *data, uint32_t length);
    bool (*program_page)(void *context, uint32_t lane, uint32_t page,
                         const uint8_t *data, uint32_t length);
    bool (*erase_lane)(void *context, uint32_t lane);
} pota_bcb_platform_t;

typedef struct {
    uint32_t sequence;
    uint32_t boot_generation;
    uint32_t security_counter;
    uint32_t payload_length;
    uint8_t payload[POTA_BCB_BODY_PAYLOAD_SIZE];
} pota_bcb_update_t;

typedef struct {
    uint32_t lane;
    uint32_t record_page;
    uint32_t lane_generation;
    pota_bcb_update_t update;
} pota_bcb_view_t;

typedef struct {
    pota_bcb_platform_t platform;
    uint32_t schema_version;
    uint32_t map_version;
    uint32_t lane_page_count;
} pota_bcb_store_t;

pota_bcb_result_t pota_bcb_store_init(pota_bcb_store_t *store,
                                       const pota_bcb_platform_t *platform,
                                       uint32_t schema_version,
                                       uint32_t map_version,
                                       uint32_t lane_page_count);
pota_bcb_result_t pota_bcb_store_select_newest(const pota_bcb_store_t *store,
                                                pota_bcb_view_t *view);
pota_bcb_result_t pota_bcb_store_append(pota_bcb_store_t *store,
                                         const pota_bcb_update_t *update,
                                         pota_bcb_view_t *view);

#endif
