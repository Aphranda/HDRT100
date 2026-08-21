#include "calibration_clk_coded.h"

#include <string.h>

void calibration_clk_coded_store_init(calibration_clk_coded_store_t *store)
{
    if (store == NULL) return;
    memset(store, 0, sizeof(*store));
    store->snapshot.version = CALIBRATION_CLK_CODED_SNAPSHOT_VERSION;
    store->snapshot.state = CALIBRATION_CLK_CODED_IDLE;
}

bool calibration_clk_coded_publish_core1(
    calibration_clk_coded_store_t *store,
    const calibration_clk_coded_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL ||
        snapshot->version != CALIBRATION_CLK_CODED_SNAPSHOT_VERSION ||
        snapshot->state > CALIBRATION_CLK_CODED_REJECTED) {
        return false;
    }
    (void)__atomic_add_fetch(&store->guard, 1u, __ATOMIC_ACQ_REL);
    store->snapshot = *snapshot;
    (void)__atomic_add_fetch(&store->guard, 1u, __ATOMIC_RELEASE);
    return true;
}

bool calibration_clk_coded_get_snapshot(
    const calibration_clk_coded_store_t *store,
    calibration_clk_coded_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u;
         attempt < CALIBRATION_CLK_CODED_SNAPSHOT_READ_ATTEMPTS; attempt++) {
        const uint32_t begin = __atomic_load_n(&store->guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = store->snapshot;
        const uint32_t end = __atomic_load_n(&store->guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}
