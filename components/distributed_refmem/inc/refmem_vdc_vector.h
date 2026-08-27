#ifndef REFMEM_VDC_VECTOR_H
#define REFMEM_VDC_VECTOR_H

#include <stdint.h>

/*
 * VDC/DPLL runtime vectors are deliberately compact.  The complete
 * vdc_domain_snapshot_t remains private to the VDC owner; only the fields
 * needed by the realtime consumer and by read-only diagnostics cross the
 * DistributedVectorTable boundary.
 *
 * The first word of each region is a seqlock owned by core1.  The payload
 * sequence is the corresponding stable (even) seqlock value, while
 * source_update_seq identifies the VDC/DPLL model update that was mirrored.
 */
#define REFMEM_VDC_VECTOR_LAYOUT_VERSION  1u
#define REFMEM_DPLL_VECTOR_LAYOUT_VERSION 1u
#define REFMEM_VECTOR_WRITER_CORE1        1u

#define REFMEM_VECTOR_FLAG_VALID              (1u << 0u)
#define REFMEM_VECTOR_FLAG_STALE              (1u << 1u)
#define REFMEM_VECTOR_FLAG_SCHEDULE_VALID     (1u << 2u)
#define REFMEM_VECTOR_FLAG_CALIBRATION_VALID  (1u << 3u)
#define REFMEM_VECTOR_FLAG_HARDWARE_EVIDENCE  (1u << 4u)
#define REFMEM_VECTOR_FLAG_LOCKED             (1u << 5u)

typedef struct {
    uint32_t layout_version;
    uint32_t writer;
    uint32_t flags;
    uint32_t stable_sequence;
    uint32_t publish_sequence;
    uint32_t source_update_seq;
    uint32_t source_service_count;
    uint32_t schedule_epoch;
    uint32_t local_node_id;
    uint32_t reference_node_id;
    uint32_t node_count;
    uint32_t schedule_crc32;
    uint32_t servo_profile_crc32;
    uint32_t path_delay_table_crc32;
    uint32_t path_delay_generation;
    uint32_t path_delay_freshness_us;

    uint32_t dpll_state;
    uint32_t dpll_update_seq;
    uint32_t dpll_accepted_sample_count;
    uint32_t dpll_rejected_sample_count;
    uint32_t dpll_last_sample_seq;
    int32_t dpll_last_phase_error_ns;
    int32_t dpll_last_frequency_error_ppb;
    int32_t dpll_last_offset_ns;
    uint32_t dpll_rms_offset_ns;
    uint32_t dpll_max_abs_offset_ns;
    uint32_t dpll_jitter_pk_ns;
    uint32_t dpll_holdover_age_us;

    uint32_t quality_health_state;
    uint32_t quality_lock_quality_tier;
    uint32_t quality_flags;
    uint32_t quality_last_reject_code;
    uint32_t quality_last_timestamp_source;
    uint32_t quality_last_timestamp_resolution_ns;
    uint32_t quality_last_timestamp_flags;
    uint32_t quality_last_sample_age_us;
    uint32_t quality_freshness_limit_us;

    uint32_t gate_passed;
    uint32_t gate_reject_code;
    uint32_t gate_reject_slot;
    uint32_t gate_reject_evidence;

    uint64_t clock_base_local_tick64;
    uint64_t clock_base_vdc_time64_ns;
    int32_t clock_phase_offset_ns;
    int32_t clock_period_adjust_ppb;
    uint32_t clock_nominal_period_ns;
    uint32_t clock_model_seq;
    uint32_t clock_slew_limit_ppb;
    uint64_t last_sample_time_ns;

    uint32_t payload_crc32;
} refmem_vdc_vector_payload_t;

typedef struct {
    uint32_t layout_version;
    uint32_t writer;
    uint32_t flags;
    uint32_t stable_sequence;
    uint32_t publish_sequence;
    uint32_t source_update_seq;
    uint32_t source_service_count;
    uint32_t ready;
    uint32_t schedule_epoch;
    uint32_t local_node_id;
    uint32_t reference_node_id;
    uint32_t node_count;
    uint32_t schedule_crc32;
    uint32_t servo_profile_crc32;

    uint32_t state;
    uint32_t dpll_update_seq;
    uint32_t dpll_accepted_sample_count;
    uint32_t dpll_rejected_sample_count;
    uint32_t last_sample_seq;
    int32_t last_phase_error_ns;
    int32_t last_frequency_error_ppb;
    int32_t last_offset_ns;
    uint32_t rms_offset_ns;
    uint32_t max_abs_offset_ns;
    uint32_t jitter_pk_ns;
    uint32_t holdover_age_us;

    uint32_t dco_valid;
    uint32_t dco_update_seq;
    uint32_t dco_source_model_seq;
    uint32_t dco_lock_state;
    int32_t dco_phase_offset_ns;
    int32_t dco_period_adjust_ppb;
    uint64_t dco_base_local_tick64;
    uint64_t dco_base_vdc_time64_ns;
    uint32_t dco_nominal_period_ns;
    uint32_t dco_slew_limit_ppb;

    uint32_t quality_health_state;
    uint32_t quality_lock_quality_tier;
    uint32_t quality_last_reject_code;
    uint32_t quality_last_timestamp_source;
    uint32_t quality_last_timestamp_resolution_ns;
    uint32_t quality_last_timestamp_flags;
    uint32_t quality_last_sample_age_us;
    uint32_t quality_freshness_limit_us;
    uint32_t gate_passed;
    uint32_t gate_reject_code;
    uint32_t gate_reject_slot;
    uint32_t gate_reject_evidence;
    uint32_t path_delay_table_crc32;
    uint32_t path_delay_generation;
    uint32_t path_delay_freshness_us;
    uint32_t payload_crc32;
} refmem_dpll_vector_payload_t;

uint32_t refmem_vdc_vector_payload_crc(const refmem_vdc_vector_payload_t *payload);
uint32_t refmem_dpll_vector_payload_crc(const refmem_dpll_vector_payload_t *payload);
int refmem_vdc_vector_payload_validate(const refmem_vdc_vector_payload_t *payload);
int refmem_dpll_vector_payload_validate(const refmem_dpll_vector_payload_t *payload);

#endif
