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
    /* Last commit's sector scan (flash_store_nvs_result_t, 0 = OK): diagnostic
     * only, so a RECORD refusal can be attributed to the scan or the profile. */
    uint32_t scan_result[2];
    uint32_t scan_usable[2];
} tdma_boot_profile_store_status_t;

enum {
    TDMA_BOOT_PROFILE_STORE_REJECT_NONE = 0u,
    TDMA_BOOT_PROFILE_STORE_REJECT_UNSUPPORTED = 1u,
    TDMA_BOOT_PROFILE_STORE_REJECT_EMPTY = 2u,
    TDMA_BOOT_PROFILE_STORE_REJECT_RECORD = 3u,
    TDMA_BOOT_PROFILE_STORE_REJECT_FLASH = 4u,
    TDMA_BOOT_PROFILE_STORE_REJECT_VERIFY = 5u,
    TDMA_BOOT_PROFILE_STORE_REJECT_PROFILE = 6u,
};

/* Scan both sectors and cache the newest valid profile. Always returns true;
 * inspect the status for EMPTY/UNSUPPORTED. */
bool tdma_boot_profile_store_init(void);

/* Cached profile, false when nothing valid is loaded. */
bool tdma_boot_profile_store_get(tdma_boot_profile_t *out);

/* Persist one profile. The previous record stays intact until the new one is
 * verified by re-scanning the target sector. A sector whose record cannot be
 * parsed (old schema/object) is erased before the append instead of refusing
 * forever, so one stale sector cannot block provisioning.
 *
 * `nonce` is folded into the durable request identity. The flash completion
 * journal deduplicates by identity, so a constant (generation only) makes a
 * re-provision after a clear look like a COMPLETED replay and skip the physical
 * program (measured 2026-11-08). Callers pass a per-call value that is unique
 * within the session (the SCPI layer uses the boot uptime). */
bool tdma_boot_profile_store_commit(const tdma_boot_profile_t *profile,
                                    uint32_t nonce);

/* Generation the next commit should carry: newest readable record + 1, or 1 on
 * an empty store. Rescans both sectors (not the boot-time cache), so a write
 * started after init still gets a strictly newer generation. Returns false on
 * an unsupported map, an unreadable region, or generation 0xFFFFFFFF. */
bool tdma_boot_profile_store_next_generation(uint32_t *out);

/* Drop the profile: erase both sectors and report "empty". Needed to get back
 * to the "no boot profile" state (for example before a P3 run that wants a cold
 * ring) and to recover from a corrupt record. `nonce` has the same
 * journal-identity role as in commit. Readback verifies both sectors are empty,
 * so a deduplicated erase can never report CLEARED. */
bool tdma_boot_profile_store_erase(uint32_t nonce);

void tdma_boot_profile_store_status(tdma_boot_profile_store_status_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TDMA_BOOT_PROFILE_STORE_H */
