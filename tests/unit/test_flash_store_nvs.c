#include "flash_store_nvs.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    const uint8_t payload[] = {1u, 2u, 3u};
    uint8_t sector[256];
    uint8_t scratch[32];
    uint8_t program[128];
    memset(sector, 0xFF, sizeof(sector));
    size_t size = 0u;
    assert(flash_store_nvs_plan_append(1u, 9u, 1u, 1u, 0u, payload,
                                       sizeof(payload), 0u, sizeof(sector),
                                       16u, program, sizeof(program), &size) ==
           FLASH_STORE_NVS_OK);
    assert(size == 48u);
    memcpy(sector, program, size);
    flash_store_nvs_scan_t scan;
    assert(flash_store_nvs_scan(sector, sizeof(sector), 1u, 9u, 0u, 16u,
                                scratch, sizeof(scratch), &scan) ==
           FLASH_STORE_NVS_OK);
    assert(scan.valid_count == 1u && scan.has_latest && scan.append_offset == 48u);
    assert(scan.latest.generation == 1u && scan.latest.sequence == 1u);

    assert(flash_store_nvs_plan_append(1u, 9u, 1u, 2u, 0u, payload,
                                       sizeof(payload), scan.append_offset,
                                       sizeof(sector), 16u, program,
                                       sizeof(program), &size) == FLASH_STORE_NVS_OK);
    memcpy(&sector[scan.append_offset], program, size);
    assert(flash_store_nvs_scan(sector, sizeof(sector), 1u, 9u, 0u, 16u,
                                scratch, sizeof(scratch), &scan) ==
           FLASH_STORE_NVS_OK);
    assert(scan.valid_count == 2u && scan.latest.sequence == 2u);

    sector[48u + 3u] ^= 1u;
    assert(flash_store_nvs_scan(sector, sizeof(sector), 1u, 9u, 0u, 16u,
                                scratch, sizeof(scratch), &scan) ==
           FLASH_STORE_NVS_OK);
    assert(scan.valid_count == 1u && scan.saw_torn_tail && scan.append_offset == 48u);

    assert(flash_store_nvs_scan(sector, sizeof(sector), 2u, 9u, 0u, 16u,
                                scratch, sizeof(scratch), &scan) ==
           FLASH_STORE_NVS_UNKNOWN_SCHEMA);
    return 0;
}
