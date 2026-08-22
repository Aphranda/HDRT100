#include "pota_boot_control_facade.h"

#include <string.h>

static bool facade_valid(const pota_boot_control_facade_t *facade)
{
    return facade != NULL && facade->initialized;
}

pota_bcb_result_t pota_boot_control_facade_init(
    pota_boot_control_facade_t *facade,
    const pota_bcb_platform_t *platform,
    uint32_t schema_version,
    uint32_t map_version,
    uint32_t lane_page_count)
{
    if (facade == NULL) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    memset(facade, 0, sizeof(*facade));
    const pota_bcb_result_t result =
        pota_bcb_store_init(&facade->store, platform, schema_version,
                            map_version, lane_page_count);
    facade->initialized = result == POTA_BCB_RESULT_OK;
    return result;
}

pota_bcb_result_t pota_boot_control_facade_select_newest(
    const pota_boot_control_facade_t *facade,
    pota_bcb_view_t *view)
{
    if (!facade_valid(facade) || view == NULL) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    return pota_bcb_store_select_newest(&facade->store, view);
}

pota_bcb_result_t pota_boot_control_facade_append(
    pota_boot_control_facade_t *facade,
    const pota_bcb_update_t *update,
    pota_bcb_view_t *view)
{
    if (!facade_valid(facade) || update == NULL || view == NULL) {
        return POTA_BCB_RESULT_BAD_ARGUMENT;
    }
    return pota_bcb_store_append(&facade->store, update, view);
}

bool pota_boot_control_facade_get_wear_snapshot(
    const pota_boot_control_facade_t *facade,
    pota_bcb_wear_snapshot_t *snapshot)
{
    if (!facade_valid(facade) || snapshot == NULL) {
        return false;
    }
    return pota_bcb_store_get_wear_snapshot(&facade->store, snapshot);
}

bool pota_boot_control_facade_get_health_snapshot(
    const pota_boot_control_facade_t *facade,
    pota_bcb_health_snapshot_t *snapshot)
{
    if (facade == NULL || !facade->initialized) {
        return false;
    }
    return pota_bcb_store_get_health_snapshot(&facade->store, snapshot);
}
