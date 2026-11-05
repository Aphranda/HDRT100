#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drv_flash.h"
#include "flash_deployment_map.h"
#include "flash_store_nvs.h"
#include "flash_store_record.h"
#include "flash_transaction.h"
#include "tdma_boot_profile_store.h"

#if FLASH_DEPLOYMENT_MAP_HAS_TDMA_BOOT_PROFILE

#define SECTOR_SIZE FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE
#define REGION_BASE FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_OFFSET
#define REGION_RELATIVE \
    FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_RELATIVE_OFFSET
#define REGION_SIZE FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_SIZE

static uint8_t s_flash[REGION_SIZE];

static uint8_t *flash_at_absolute(uint32_t offset)
{
    if (offset < REGION_BASE || offset >= REGION_BASE + REGION_SIZE) {
        return NULL;
    }
    return &s_flash[offset - REGION_BASE];
}

static uint8_t *flash_at_relative(uint32_t offset)
{
    if (offset < REGION_RELATIVE ||
        offset >= REGION_RELATIVE + REGION_SIZE) {
        return NULL;
    }
    return &s_flash[offset - REGION_RELATIVE];
}

const uint8_t *drv_flash_xip_ptr(uint32_t offset)
{
    return flash_at_absolute(offset);
}

bool drv_flash_is_erased(uint32_t offset, size_t length)
{
    const uint8_t *p = flash_at_absolute(offset);
    if (p == NULL || length > REGION_SIZE) {
        return false;
    }
    for (size_t i = 0u; i < length; i++) {
        if (p[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}

bool flash_transaction_ao_execute(
    const flash_transaction_request_t *request,
    flash_transaction_completion_t *completion)
{
    (void)completion;
    if (request == NULL) {
        return false;
    }
    uint8_t *p = flash_at_relative(request->relative_offset);
    if (p == NULL || request->length > REGION_SIZE) {
        return false;
    }
    if (request->operation == FLASH_TRANSACTION_OPERATION_ERASE) {
        memset(p, 0xFF, request->length);
        return true;
    }
    if (request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        request->data != NULL) {
        memcpy(p, request->data, request->length);
        return true;
    }
    return false;
}

const flash_transaction_completion_lease_t *
flash_transaction_ao_get_completion_lease(void)
{
    return NULL;
}

static tdma_boot_profile_t profile_for(uint32_t slot, uint32_t generation)
{
    tdma_boot_profile_t profile = {0};
    profile.version = TDMA_BOOT_PROFILE_VERSION;
    profile.profile_level = 23u;
    profile.node_count = 4u;
    profile.local_slot = slot;
    profile.flags = TDMA_BOOT_PROFILE_FLAG_BRINGUP_NO_CAL;
    profile.topology_crc32 = 0x1234u;
    profile.generation = generation;
    return profile;
}

int main(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));

    tdma_boot_profile_store_status_t status;
    tdma_boot_profile_t cached;

    assert(tdma_boot_profile_store_init());
    tdma_boot_profile_store_status(&status);
    assert(status.supported);
    assert(!status.loaded);
    assert(status.reject_reason == TDMA_BOOT_PROFILE_STORE_REJECT_EMPTY);

    const tdma_boot_profile_t first = profile_for(2u, 1u);
    assert(tdma_boot_profile_store_commit(&first));
    assert(tdma_boot_profile_store_get(&cached));
    assert(memcmp(&cached, &first, sizeof(cached)) == 0);

    /* Same generation appends a new sequence; the cached value follows. */
    const tdma_boot_profile_t second = profile_for(3u, 1u);
    assert(tdma_boot_profile_store_commit(&second));
    assert(tdma_boot_profile_store_get(&cached));
    assert(cached.local_slot == 3u);
    tdma_boot_profile_store_status(&status);
    assert(status.sequence >= 2u);

    /* A higher generation supersedes the previous one. */
    const tdma_boot_profile_t third = profile_for(1u, 2u);
    assert(tdma_boot_profile_store_commit(&third));
    assert(tdma_boot_profile_store_get(&cached));
    assert(cached.local_slot == 1u && cached.generation == 2u);

    /* Replay of an older generation is rejected. */
    const tdma_boot_profile_t older = profile_for(0u, 1u);
    assert(!tdma_boot_profile_store_commit(&older));
    assert(tdma_boot_profile_store_get(&cached));
    assert(cached.generation == 2u);

    /* Enough appends force a sector rotation; the newest profile survives. */
    tdma_boot_profile_t rotating = profile_for(0u, 3u);
    for (uint32_t i = 0u; i < 20u; i++) {
        rotating.local_slot = i % 4u;
        assert(tdma_boot_profile_store_commit(&rotating));
    }
    assert(tdma_boot_profile_store_get(&cached));
    assert(cached.generation == 3u);
    assert(cached.local_slot == 19u % 4u);

    /* Re-init reads the newest record back from the "flash". */
    assert(tdma_boot_profile_store_init());
    assert(tdma_boot_profile_store_get(&cached));
    assert(cached.generation == 3u && cached.local_slot == 3u);

    printf("tdma boot profile store host tests passed\n");
    return 0;
}

#else

int main(void)
{
    tdma_boot_profile_store_status_t status;
    tdma_boot_profile_t cached;
    tdma_boot_profile_t profile = {0};
    profile.version = TDMA_BOOT_PROFILE_VERSION;
    profile.profile_level = 23u;
    profile.node_count = 4u;
    profile.local_slot = 0u;
    profile.flags = TDMA_BOOT_PROFILE_FLAG_BRINGUP_NO_CAL;
    profile.topology_crc32 = 1u;
    profile.generation = 1u;

    assert(tdma_boot_profile_store_init());
    tdma_boot_profile_store_status(&status);
    assert(!status.supported);
    assert(status.reject_reason == TDMA_BOOT_PROFILE_STORE_REJECT_UNSUPPORTED);
    assert(!tdma_boot_profile_store_get(&cached));
    assert(!tdma_boot_profile_store_commit(&profile));
    printf("v1 compat: boot profile store unsupported\n");
    return 0;
}

#endif
