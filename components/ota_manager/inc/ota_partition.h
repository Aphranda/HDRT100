#ifndef OTA_PARTITION_H
#define OTA_PARTITION_H

#include <stdint.h>

#include "flash_deployment_map.h"

#define OTA_FLASH_SIZE_BYTES             FLASH_DEPLOYMENT_GEOMETRY_TOTAL_SIZE
#define OTA_BOOTLOADER_OFFSET            FLASH_DEPLOYMENT_MAP_BOOTLOADER_OFFSET
#define OTA_BOOTLOADER_SIZE              FLASH_DEPLOYMENT_MAP_BOOTLOADER_SIZE
#define OTA_SLOT_A_OFFSET                FLASH_DEPLOYMENT_MAP_APP_A_OFFSET
#define OTA_SLOT_A_SIZE                  FLASH_DEPLOYMENT_MAP_APP_A_SIZE
#define OTA_SLOT_B_OFFSET                FLASH_DEPLOYMENT_MAP_APP_B_OFFSET
#define OTA_SLOT_B_SIZE                  FLASH_DEPLOYMENT_MAP_APP_B_SIZE
#define OTA_METADATA_OFFSET              FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_OFFSET
#define OTA_METADATA_SIZE                FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_SIZE
#define OTA_PRODUCT_CONFIG_OFFSET        FLASH_DEPLOYMENT_MAP_PRODUCT_NVS_OFFSET
#define OTA_PRODUCT_CONFIG_SIZE          FLASH_DEPLOYMENT_MAP_PRODUCT_NVS_SIZE
#define OTA_SCRATCH_OFFSET               FLASH_DEPLOYMENT_MAP_SCRATCH_OFFSET
#define OTA_SCRATCH_SIZE                 FLASH_DEPLOYMENT_MAP_SCRATCH_SIZE
#define OTA_DEPLOYMENT_LAYOUT_SIZE       FLASH_DEPLOYMENT_MAP_FUTURE_POOL_OFFSET
#define OTA_APP_SIZE_WARN_THRESHOLD      (1200u * 1024u)
#define OTA_APP_SIZE_FAIL_THRESHOLD      (1400u * 1024u)
#define OTA_DEFAULT_TARGET_SLOT_OFFSET   OTA_SLOT_B_OFFSET
#define OTA_DEFAULT_TARGET_SLOT_SIZE     OTA_SLOT_B_SIZE
#define OTA_DEFAULT_APP_RUN_OFFSET       OTA_SLOT_A_OFFSET

_Static_assert((OTA_SCRATCH_OFFSET + OTA_SCRATCH_SIZE) == OTA_DEPLOYMENT_LAYOUT_SIZE,
               "OTA deployment layout must end at Future Pool");
_Static_assert(OTA_DEPLOYMENT_LAYOUT_SIZE <= OTA_FLASH_SIZE_BYTES,
               "OTA layout exceeds physical flash");

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
