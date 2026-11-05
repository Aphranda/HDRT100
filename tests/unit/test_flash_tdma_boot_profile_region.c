#include <assert.h>
#include <stdio.h>

#include "flash_deployment_map.h"

int main(void)
{
#if FLASH_DEPLOYMENT_MAP_HAS_TDMA_BOOT_PROFILE
    /* v2 map: the boot profile lives in the calibration NVS partition, after
     * the calibration store (2 sectors) and the seat mount store (2 sectors),
     * and must not overlap either. */
    assert(FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_PARTITION_ID ==
           FLASH_MAP_CALIBRATION_NVS_ID);
    assert(FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_OFFSET ==
           FLASH_MAP_CALIBRATION_NVS_OFFSET);
    assert(FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_RELATIVE_OFFSET ==
           4u * FLASH_GEOMETRY_ERASE_SIZE_BYTES);
    assert(FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_SIZE ==
           2u * FLASH_GEOMETRY_ERASE_SIZE_BYTES);
    assert(FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_RELATIVE_OFFSET +
               FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_SIZE <=
           FLASH_MAP_CALIBRATION_NVS_SIZE);
    assert(FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_RELATIVE_OFFSET >=
           FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_RELATIVE_OFFSET +
               FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_SIZE);
    printf("v2 boot profile region ok\n");
#else
    /* v1 compatibility map: no calibration partition, therefore no boot
     * profile. A board on v1 keeps the multi-port bring-up. */
    assert(FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_SIZE == 0u);
    assert(FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_RELATIVE_OFFSET == 0u);
    printf("v1 compat: no boot profile region\n");
#endif
    return 0;
}
