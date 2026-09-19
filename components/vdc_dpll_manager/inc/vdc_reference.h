#ifndef VDC_REFERENCE_H
#define VDC_REFERENCE_H
#include "sync_io_reference.h"

typedef struct {
    uint32_t config_generation;
    uint32_t enabled;
    uint32_t resource_held;
    sync_io_reference_snapshot_t monitor;
} vdc_reference_status_t;

/* Slow software-DCO discipline, explicitly armed separately from MONITOR.
 * The capture profile remains the Flash-persisted signal configuration.
 * Discipline enable is volatile, STOP-only, and binds once to a RUN. */
#define VDC_REFERENCE_DISCIPLINE_SCHEMA 1u
typedef struct {
    uint32_t slew_ppb_per_s;
    uint32_t filter_divisor; /* 0 and 1 bypass filtering; larger values use IIR 1/N. */
    uint32_t max_ppb;
} vdc_reference_discipline_config_t;
typedef enum { VDC_REFERENCE_DISCIPLINE_OFF, VDC_REFERENCE_DISCIPLINE_WAIT,
    VDC_REFERENCE_DISCIPLINE_TRACK, VDC_REFERENCE_DISCIPLINE_HOLD,
    VDC_REFERENCE_DISCIPLINE_RETIRED } vdc_reference_discipline_state_t;
typedef enum { VDC_REFERENCE_DISCIPLINE_OK, VDC_REFERENCE_DISCIPLINE_NO_INPUT,
    VDC_REFERENCE_DISCIPLINE_STOP, VDC_REFERENCE_DISCIPLINE_BINDING,
    VDC_REFERENCE_DISCIPLINE_STALE, VDC_REFERENCE_DISCIPLINE_RANGE,
    VDC_REFERENCE_DISCIPLINE_MODEL } vdc_reference_discipline_reason_t;
typedef struct {
    uint32_t schema, request, enabled, state, reason;
    uint32_t reference_generation, sample_seq, accepted, rejected, applied;
    int32_t measured_ppb, filtered_ppb, baseline_ppb;
    uint32_t session, role_generation, clock_epoch, clock_run, origin_epoch, origin_sequence;
    uint32_t dco_update_seq;
    uint32_t config_generation, config_crc32;
    vdc_reference_discipline_config_t config;
} vdc_reference_discipline_status_t;
bool vdc_dpll_manager_set_reference_discipline(bool enabled);
bool vdc_dpll_manager_get_reference_discipline(vdc_reference_discipline_status_t *status);
bool vdc_dpll_manager_get_reference_discipline_config(vdc_reference_discipline_config_t *config);
bool vdc_dpll_manager_set_reference_discipline_config(const vdc_reference_discipline_config_t *config);
bool vdc_dpll_manager_default_reference_discipline(void);
bool vdc_dpll_manager_recall_reference_discipline(void);
bool vdc_dpll_manager_store_reference_discipline(void);

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
