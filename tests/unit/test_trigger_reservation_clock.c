#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "trigger_reservation_clock.h"

static vdc_dpll_manager_vector_snapshot_t current;
static bool readable = true;

bool vdc_dpll_manager_get_vector_snapshot(vdc_dpll_manager_vector_snapshot_t *out)
{
    if (!readable || out == NULL) return false;
    *out = current;
    return true;
}

static vdc_dpll_manager_vector_snapshot_t valid_vector(void)
{
    vdc_dpll_manager_vector_snapshot_t v;
    memset(&v, 0, sizeof(v));
    v.ready = 1u;
    v.publication_revision = 2u;
    v.schedule.enabled = 1u;
    v.schedule.schedule_crc32 = 0x111u;
    v.servo.servo_profile_crc32 = 0x222u;
    v.clock.valid = 1u;
    v.clock.model_seq = 3u;
    v.clock.epoch_id = 4u;
    v.clock.run_id = 5u;
    v.clock.tdma_schedule_crc32 = 0x111u;
    v.dco.valid = 1u;
    v.dco.dco_update_seq = 6u;
    v.dco.source_model_seq = 3u;
    v.dco.epoch_id = 4u;
    v.dco.run_id = 5u;
    v.dco.base_local_tick64 = 1000000000ull;
    v.dco.base_vdc_time64_ns = 2000000000ull;
    v.dco.nominal_period_ns = 1000000u;
    v.dco.lock_state = VDC_DOMAIN_LOCK_LOCKED;
    v.dco.tdma_schedule_crc32 = 0x111u;
    v.dco.servo_profile_crc32 = 0x222u;
    v.dpll.state = VDC_DOMAIN_LOCK_LOCKED;
    v.dpll.schedule_crc32 = 0x111u;
    v.dpll.servo_profile_crc32 = 0x222u;
    v.quality.valid = 1u;
    v.quality.update_seq = 7u;
    v.quality.health_state = VDC_DOMAIN_HEALTH_HEALTHY;
    v.quality.lock_state = VDC_DOMAIN_LOCK_LOCKED;
    v.quality.lock_quality_tier = VDC_DOMAIN_LOCK_QUALITY_FINE_100NS;
    v.quality.last_timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    v.quality.last_timestamp_resolution_ns = 4u;
    v.quality.last_sample_time_ns = 1599980000ull;
    v.quality.last_timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    v.quality.last_sample_age_us = 20u;
    v.quality.freshness_limit_us = 100u;
    v.path_delay.valid = 1u;
    v.path_delay.table_crc32 = 0x333u;
    v.path_delay.schedule_crc32 = v.schedule.schedule_crc32;
    v.path_delay.calibration_generation = 8u;
    v.path_delay.flags = VDC_PATH_DELAY_FLAG_ACCEPTED |
        VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED | VDC_PATH_DELAY_FLAG_BIAS_VALID |
        VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH |
        VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID;
    v.gate.passed = 1u;
    v.gate.reject_code = VDC_DOMAIN_GATE_PASS;
    return v;
}

static const trigger_reservation_identity_t identity = {9u, 10u, 11u};

static void test_admission_and_fixed_deadline(void)
{
    current = valid_vector();
    trigger_reservation_clock_snapshot_t snapshot;
    assert(trigger_reservation_clock_capture(&identity, 50u, &snapshot) ==
           TRIGGER_RESERVATION_CLOCK_OK);
    assert(snapshot.publication_revision == 2u);
    assert(snapshot.valid_from_local_ns == 1000000000ull);
    trigger_reservation_clock_prepared_t prepared;
    assert(trigger_reservation_clock_prepare(&snapshot, 3000000001ull,
        250000000u, 400000000u, 100u, &prepared) == TRIGGER_RESERVATION_CLOCK_OK);
    assert(prepared.model_local_ns == 2000000001ull);
    assert(prepared.timer1_deadline_ticks == 500000001ull);
    const uint64_t committed_deadline = prepared.timer1_deadline_ticks;

    current.publication_revision += 2u;
    current.quality.last_sample_age_us++;
    trigger_reservation_clock_snapshot_t refreshed;
    assert(trigger_reservation_clock_capture(&identity, 50u, &refreshed) ==
           TRIGGER_RESERVATION_CLOCK_OK);
    assert(trigger_reservation_clock_identity_matches(&prepared, &refreshed));
    assert(prepared.timer1_deadline_ticks == committed_deadline);
    refreshed.dco_update_seq++;
    assert(!trigger_reservation_clock_identity_matches(&prepared, &refreshed));
    assert(prepared.timer1_deadline_ticks == committed_deadline);
}

static void test_fail_closed_reasons(void)
{
    trigger_reservation_clock_snapshot_t snapshot;
#define REJECT(statement, reason) do { current = valid_vector(); statement; \
    assert(trigger_reservation_clock_capture(&identity, 50u, &snapshot) == reason); } while (0)
    REJECT(current.ready = 0u, TRIGGER_RESERVATION_CLOCK_NOT_READY);
    REJECT(current.publication_revision = 3u, TRIGGER_RESERVATION_CLOCK_NOT_READY);
    REJECT(current.dco.source_model_seq++, TRIGGER_RESERVATION_CLOCK_MODEL);
    REJECT(current.dpll.debug_continue_enabled = 1u,
           TRIGGER_RESERVATION_CLOCK_QUALITY);
    REJECT(current.quality.health_state = VDC_DOMAIN_HEALTH_DEGRADED,
           TRIGGER_RESERVATION_CLOCK_QUALITY);
    REJECT(current.quality.last_sample_age_us = 51u,
           TRIGGER_RESERVATION_CLOCK_QUALITY);
    REJECT(current.quality.last_timestamp_resolution_ns = 1000u,
           TRIGGER_RESERVATION_CLOCK_QUALITY);
    REJECT(current.quality.last_sample_time_ns = 0u,
           TRIGGER_RESERVATION_CLOCK_QUALITY);
    REJECT(current.quality.last_timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY,
           TRIGGER_RESERVATION_CLOCK_QUALITY);
    REJECT(current.path_delay.flags |= VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY,
           TRIGGER_RESERVATION_CLOCK_CALIBRATION);
    REJECT(current.path_delay.calibration_generation = 0u,
           TRIGGER_RESERVATION_CLOCK_CALIBRATION);
    REJECT(current.path_delay.schedule_crc32++, TRIGGER_RESERVATION_CLOCK_CALIBRATION);
#undef REJECT
    current = valid_vector();
    current.quality.last_sample_time_ns = 1000000000ull;
    assert(trigger_reservation_clock_capture(&identity, 50u, &snapshot) ==
           TRIGGER_RESERVATION_CLOCK_OK);
    trigger_reservation_clock_prepared_t prepared;
    assert(trigger_reservation_clock_prepare(&snapshot, 2000000001ull,
        250000000u, 250000000u, 2u, &prepared) == TRIGGER_RESERVATION_CLOCK_LATE);
    assert(trigger_reservation_clock_prepare(&snapshot, 3000000000ull,
        250000000u, 250012500u, 2u, &prepared) == TRIGGER_RESERVATION_CLOCK_OK);
    const trigger_reservation_clock_prepared_t saved = prepared;
    assert(trigger_reservation_clock_prepare(&snapshot, 3000000000ull,
        250000000u, 250012501u, 2u, &prepared) == TRIGGER_RESERVATION_CLOCK_QUALITY);
    assert(memcmp(&saved, &prepared, sizeof(saved)) == 0);
    assert(trigger_reservation_clock_prepare(&snapshot, 3000000000ull,
        250000000u, 249999999u, 2u, &prepared) == TRIGGER_RESERVATION_CLOCK_QUALITY);
}

static void test_real_dco_mapping_and_hardware_quantization(void)
{
    const int32_t rates[] = {-500000, -1, 0, 1, 500000};
    const int32_t phases[] = {-127, 0, 129};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        for (unsigned p = 0; p < sizeof(phases) / sizeof(phases[0]); ++p) {
            current = valid_vector();
            current.dco.period_adjust_ppb = rates[r];
            current.dco.phase_offset_ns = phases[p];
            trigger_reservation_clock_snapshot_t snapshot;
            assert(trigger_reservation_clock_capture(&identity, 50u, &snapshot) ==
                   TRIGGER_RESERVATION_CLOCK_OK);
            trigger_reservation_clock_prepared_t prepared;
            const uint64_t target = 3000000001ull;
            assert(trigger_reservation_clock_prepare(&snapshot, target,
                250000000u, 400000000u, 100u, &prepared) ==
                TRIGGER_RESERVATION_CLOCK_OK);
            /* At the actual 4 ns hardware tick, the mapped VDC time must
             * reach the target. The preceding tick must still be early. */
            uint64_t output, previous;
            assert(vdc_domain_dco_local_to_output_ns(&snapshot.dco,
                prepared.timer1_deadline_ticks * 4u, &output));
            assert(vdc_domain_dco_local_to_output_ns(&snapshot.dco,
                (prepared.timer1_deadline_ticks - 1u) * 4u, &previous));
            assert(output >= target && previous < target);
            assert(prepared.timer1_deadline_ticks * 4u >= prepared.model_local_ns);
            assert(prepared.timer1_deadline_ticks * 4u - prepared.model_local_ns < 4u);
        }
    }
}

int main(void)
{
    test_admission_and_fixed_deadline();
    test_fail_closed_reasons();
    test_real_dco_mapping_and_hardware_quantization();
    puts("reservation clock passed");
    return 0;
}
