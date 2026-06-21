#ifndef OTA_PARTITION_H
#define OTA_PARTITION_H

#include <stdint.h>

#define OTA_FLASH_SIZE_BYTES             (4u * 1024u * 1024u)
#define OTA_BOOTLOADER_OFFSET            0x000000u
#define OTA_BOOTLOADER_SIZE              0x040000u
#define OTA_SLOT_A_OFFSET                0x040000u
#define OTA_SLOT_A_SIZE                  0x180000u
#define OTA_SLOT_B_OFFSET                0x1C0000u
#define OTA_SLOT_B_SIZE                  0x180000u
#define OTA_METADATA_OFFSET              0x340000u
#define OTA_METADATA_SIZE                0x010000u
#define OTA_PRODUCT_CONFIG_OFFSET        0x350000u
#define OTA_PRODUCT_CONFIG_SIZE          0x010000u
#define OTA_SCRATCH_OFFSET               0x360000u
#define OTA_SCRATCH_SIZE                 0x0A0000u
#define OTA_APP_SIZE_WARN_THRESHOLD      (1200u * 1024u)
#define OTA_APP_SIZE_FAIL_THRESHOLD      (1400u * 1024u)
#define OTA_DEFAULT_TARGET_SLOT_OFFSET   OTA_SLOT_B_OFFSET
#define OTA_DEFAULT_TARGET_SLOT_SIZE     OTA_SLOT_B_SIZE

typedef enum {
    OTA_SLOT_NONE = 0,
    OTA_SLOT_A,
    OTA_SLOT_B,
} ota_slot_t;

static inline uint32_t ota_partition_slot_offset(ota_slot_t slot)
{
    return (slot == OTA_SLOT_A) ? OTA_SLOT_A_OFFSET :
           (slot == OTA_SLOT_B) ? OTA_SLOT_B_OFFSET :
                                  0u;
}

static inline uint32_t ota_partition_slot_size(ota_slot_t slot)
{
    return (slot == OTA_SLOT_A) ? OTA_SLOT_A_SIZE :
           (slot == OTA_SLOT_B) ? OTA_SLOT_B_SIZE :
                                  0u;
}

#endif
