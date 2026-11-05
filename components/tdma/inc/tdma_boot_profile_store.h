#ifndef TDMA_BOOT_PROFILE_STORE_H
#define TDMA_BOOT_PROFILE_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_boot_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Durable boot-profile store: two erase sectors with append + generation
 * selection, so a power loss during program cannot lose the previous profile.
 * Only the v2 map has the sub-region; the v1 compatibility map reports
 * "unsupported" and the guide ring keeps its multi-port bring-up. */

typedef struct {
    bool supported;
    bool loaded;
    uint32_t generation;
    uint32_t sequence;
    uint32_t payload_crc32;
    uint32_t reject_reason;
} tdma_boot_profile_store_status_t;

enum {
    TDMA_BOOT_PROFILE_STORE_REJECT_NONE = 0u,
    TDMA_BOOT_PROFILE_STORE_REJECT_UNSUPPORTED = 1u,
    TDMA_BOOT_PROFILE_STORE_REJECT_EMPTY = 2u,
    TDMA_BOOT_PROFILE_STORE_REJECT_RECORD = 3u,
    TDMA_BOOT_PROFILE_STORE_REJECT_FLASH = 4u,
    TDMA_BOOT_PROFILE_STORE_REJECT_VERIFY = 5u,
};

/* Scan both sectors and cache the newest valid profile. Always returns true;
 * inspect the status for EMPTY/UNSUPPORTED. */
bool tdma_boot_profile_store_init(void);

/* Cached profile, false when nothing valid is loaded. */
bool tdma_boot_profile_store_get(tdma_boot_profile_t *out);

/* Persist one profile. The previous record stays intact until the new one is
 * verified by re-scanning the target sector. */
bool tdma_boot_profile_store_commit(const tdma_boot_profile_t *profile);

void tdma_boot_profile_store_status(tdma_boot_profile_store_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TDMA_BOOT_PROFILE_STORE_H */
