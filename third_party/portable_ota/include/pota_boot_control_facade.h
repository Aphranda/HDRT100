#ifndef POTA_BOOT_CONTROL_FACADE_H
#define POTA_BOOT_CONTROL_FACADE_H

#include <stdbool.h>
#include <stdint.h>

#include "pota_boot_control_store.h"

/* Boot-only owner boundary around the portable BCB primitive.  Callers do not
 * receive Flash offsets or lane geometry; all physical access remains behind
 * the platform callbacks supplied at initialization. */
typedef struct {
    pota_bcb_store_t store;
    bool initialized;
} pota_boot_control_facade_t;

pota_bcb_result_t pota_boot_control_facade_init(
    pota_boot_control_facade_t *facade,
    const pota_bcb_platform_t *platform,
    uint32_t schema_version,
    uint32_t map_version,
    uint32_t lane_page_count);
pota_bcb_result_t pota_boot_control_facade_select_newest(
    const pota_boot_control_facade_t *facade,
    pota_bcb_view_t *view);
pota_bcb_result_t pota_boot_control_facade_append(
    pota_boot_control_facade_t *facade,
    const pota_bcb_update_t *update,
    pota_bcb_view_t *view);
bool pota_boot_control_facade_get_wear_snapshot(
    const pota_boot_control_facade_t *facade,
    pota_bcb_wear_snapshot_t *snapshot);

#endif
