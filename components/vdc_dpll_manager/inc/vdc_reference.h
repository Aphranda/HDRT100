#ifndef VDC_REFERENCE_H
#define VDC_REFERENCE_H
#include "sync_io_reference.h"

typedef struct {
    uint32_t config_generation;
    uint32_t enabled;
    uint32_t resource_held;
    sync_io_reference_snapshot_t monitor;
} vdc_reference_status_t;

/* Core0 control plane. Config and enable require TDMA STOP. Disable is an
 * asynchronous cancellation intent; the Core1 owner retires before release.
 * STORe persists signal parameters only; boot always leaves capture disabled.
 * This monitor does not authorize or apply a DCO frequency correction. */
bool vdc_dpll_manager_get_reference_config(sync_io_reference_config_t *config);
bool vdc_dpll_manager_set_reference_config(const sync_io_reference_config_t *config);
bool vdc_dpll_manager_set_reference_enabled(bool enabled);
bool vdc_dpll_manager_get_reference_status(vdc_reference_status_t *status);
bool vdc_dpll_manager_default_reference(void);
bool vdc_dpll_manager_recall_reference(void);
bool vdc_dpll_manager_store_reference(void);
#endif
