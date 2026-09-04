#ifndef POTA_TYPES_H
#define POTA_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POTA_PACKAGE_MAGIC           0x474B5054u
#define POTA_PACKAGE_VERSION         2u
#define POTA_PACKAGE_HEADER_SIZE     512u
#define POTA_PACKAGE_MAX_IMAGES      2u
#ifndef POTA_MAX_DATA_BLOCK_SIZE
#define POTA_MAX_DATA_BLOCK_SIZE     512u
#endif
#define POTA_MAX_FLASH_PAGE_SIZE     512u
#define POTA_TEXT_FIELD_SIZE         32u
#define POTA_SHA256_SIZE             32u
#define POTA_IMAGE_ENTRY_SIZE        32u
#define POTA_IMAGE_TABLE_OFFSET      192u
#define POTA_MANIFEST_EXTENSION_OFFSET 256u
#define POTA_MANIFEST_EXTENSION_MAGIC 0x4D465458u
#define POTA_MANIFEST_EXTENSION_VERSION 1u
#define POTA_MANIFEST_EXTENSION_VERSION_SLOT_HASHES 2u
#define POTA_MANIFEST_SIGNATURE_MAX_SIZE 64u
#define POTA_MANIFEST_SIGNING_TRANSCRIPT_SIZE POTA_PACKAGE_HEADER_SIZE
#define POTA_MANIFEST_REQUIRED_SIGNATURE (1u << 0)
#define POTA_MANIFEST_REQUIRED_IMAGE_HASHES (1u << 1)

#define POTA_PACK_VERSION(major, minor, patch) \
    ((((uint32_t)(major) & 0xFFu) << 16u) | \
     (((uint32_t)(minor) & 0xFFu) << 8u) | \
     ((uint32_t)(patch) & 0xFFu))

typedef enum {
    POTA_SLOT_NONE = 0,
    POTA_SLOT_A = 1,
    POTA_SLOT_B = 2,
} pota_slot_t;

typedef enum {
    POTA_BOOT_MODE_COPY_TO_ACTIVE = 0,
    POTA_BOOT_MODE_DIRECT_AB = 1,
} pota_boot_mode_t;

typedef enum {
    POTA_STATE_IDLE = 0,
    POTA_STATE_CHECK_PERMISSION,
    POTA_STATE_ERASE_SLOT,
    POTA_STATE_RECEIVING,
    POTA_STATE_VERIFYING,
    POTA_STATE_MARK_PENDING,
    POTA_STATE_READY_TO_REBOOT,
    POTA_STATE_PENDING_CONFIRM,
    POTA_STATE_COMMITTED,
    POTA_STATE_FAILED,
    POTA_STATE_ABORTED,
} pota_state_t;

typedef enum {
    POTA_ERR_NONE = 0,
    POTA_ERR_BUSY,
    POTA_ERR_INVALID_STATE,
    POTA_ERR_IMAGE_TOO_LARGE,
    POTA_ERR_BAD_HEADER,
    POTA_ERR_PRODUCT_MISMATCH,
    POTA_ERR_HARDWARE_MISMATCH,
    POTA_ERR_BOOTLOADER_TOO_OLD,
    POTA_ERR_FLASH_ERASE,
    POTA_ERR_FLASH_PROGRAM,
    POTA_ERR_READBACK,
    POTA_ERR_CRC,
    POTA_ERR_VECTOR,
    POTA_ERR_METADATA,
    POTA_ERR_ABORTED,
    POTA_ERR_BAD_ARGUMENT,
    POTA_ERR_SIGNATURE_INVALID,
    POTA_ERR_SECURITY_COUNTER_ROLLBACK,
} pota_error_t;

typedef enum {
    POTA_RESULT_NONE = 0,
    POTA_RESULT_ACCEPTED,
    POTA_RESULT_IMAGE_STAGED,
    POTA_RESULT_COMMITTED,
    POTA_RESULT_FAILED,
    POTA_RESULT_ABORTED,
} pota_result_t;

typedef struct {
    uint32_t state;
    uint32_t target_slot;
    uint32_t expected_size;
    uint32_t received_size;
    uint32_t programmed_size;
    uint32_t progress_permille;
    uint32_t crc32_expected;
    uint32_t crc32_running;
    uint32_t error_code;
    uint32_t last_result;
} pota_status_t;

uint32_t pota_crc32_update(uint32_t crc, const void *data, size_t size);
uint32_t pota_crc32_compute(const void *data, size_t size);

#endif
