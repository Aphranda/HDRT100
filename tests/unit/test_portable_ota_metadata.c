#include "pota_metadata.h"

#include <stdio.h>
#include <string.h>

static pota_metadata_t make_metadata(uint32_t sequence)
{
    pota_metadata_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    metadata.magic = POTA_METADATA_MAGIC;
    metadata.version = POTA_METADATA_VERSION;
    metadata.sequence = sequence;
    metadata.active_slot = (uint32_t)POTA_SLOT_A;
    metadata.confirmed_slot = (uint32_t)POTA_SLOT_A;
    metadata.boot_mode = (uint32_t)POTA_BOOT_MODE_COPY_TO_ACTIVE;
    metadata.metadata_crc32 = pota_metadata_crc32(&metadata);
    return metadata;
}

static int expect_true(const char *name, bool value)
{
    if (!value) {
        (void)printf("%s: expected true\n", name);
        return 1;
    }
    return 0;
}

static int expect_false(const char *name, bool value)
{
    if (value) {
        (void)printf("%s: expected false\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;

    pota_metadata_t copies[3];
    copies[0] = make_metadata(1u);
    copies[1] = make_metadata(3u);
    copies[2] = make_metadata(2u);

    failed += expect_true("valid metadata", pota_metadata_is_valid(&copies[0]));

    pota_metadata_t corrupted = copies[1];
    corrupted.active_slot = (uint32_t)POTA_SLOT_B;
    failed += expect_false("corrupted metadata", pota_metadata_is_valid(&corrupted));

    const pota_metadata_t *selected = pota_metadata_select_newest(copies, 3u);
    if (selected != &copies[1]) {
        (void)printf("select newest: expected sequence 3\n");
        failed++;
    }

    copies[1].metadata_crc32 ^= 0x1u;
    selected = pota_metadata_select_newest(copies, 3u);
    if (selected != &copies[2]) {
        (void)printf("select newest after corruption: expected sequence 2\n");
        failed++;
    }

    copies[0].metadata_crc32 ^= 0x1u;
    copies[2].metadata_crc32 ^= 0x1u;
    selected = pota_metadata_select_newest(copies, 3u);
    if (selected != NULL) {
        (void)printf("select newest all invalid: expected NULL\n");
        failed++;
    }

    return failed == 0 ? 0 : 1;
}
