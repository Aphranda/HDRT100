#include "calibration_bias.h"

#include <stdio.h>

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n", name,
                     expected ? 1 : 0, actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static int expect_i64(const char *name, int64_t actual, int64_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lld got %lld\n", name,
                     (long long)expected, (long long)actual);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n", name,
                     (unsigned long)expected, (unsigned long)actual);
        return 1;
    }
    return 0;
}

static calibration_bias_gate_t make_gate(void)
{
    calibration_bias_gate_t gate = {0};
    gate.expected_path_sum_ns = 100u;
    gate.expected_persona_generation = 3u;
    gate.expected_profile_crc32 = 0x1234u;
    gate.expected_topology_generation = 7u;
    gate.minimum_samples = 3u;
    gate.maximum_samples = 6u;
    gate.maximum_spread_ns = 4u;
    gate.maximum_clock_error_ns = 4u;
    gate.require_hardware_latched = true;
    gate.require_sync_match = true;
    return gate;
}

static calibration_bias_sample_t make_sample(uint64_t raw, uint32_t epoch)
{
    calibration_bias_sample_t sample = {0};
    sample.raw_path_sum_ns = raw;
    sample.clock_error_bound_ns = 2u;
    sample.persona_generation = 3u;
    sample.profile_crc32 = 0x1234u;
    sample.topology_generation = 7u;
    sample.sample_flags = (1u << 0u) | (1u << 2u);
    sample.epoch = epoch;
    sample.reference_loopback = true;
    return sample;
}

static int test_valid_bias(void)
{
    calibration_bias_accumulator_t accumulator;
    calibration_bias_snapshot_t snapshot;
    const calibration_bias_gate_t gate = make_gate();
    calibration_bias_sample_t sample;
    calibration_bias_begin(&accumulator, &gate, 11u);
    int failed = 0;
    sample = make_sample(110u, 1u);
    failed += expect_bool("bias sample 1", calibration_bias_add(&accumulator, &sample), true);
    sample = make_sample(112u, 2u);
    failed += expect_bool("bias sample 2", calibration_bias_add(&accumulator, &sample), true);
    sample = make_sample(111u, 3u);
    failed += expect_bool("bias sample 3", calibration_bias_add(&accumulator, &sample), true);
    failed += expect_bool("bias finalize", calibration_bias_finalize(&accumulator,
                                                                      &snapshot), true);
    failed += expect_i64("bias mean", snapshot.mean_bias_ns, 11);
    failed += expect_u32("bias spread", snapshot.spread_ns, 2u);
    failed += expect_u32("bias generation", snapshot.generation, 11u);
    failed += expect_bool("bias validate", calibration_bias_snapshot_validate(&snapshot), true);
    return failed;
}

static int test_rejects_mismatch_and_spread(void)
{
    calibration_bias_accumulator_t accumulator;
    calibration_bias_snapshot_t snapshot;
    const calibration_bias_gate_t gate = make_gate();
    int failed = 0;
    calibration_bias_begin(&accumulator, &gate, 12u);
    calibration_bias_sample_t sample = make_sample(110u, 1u);
    sample.sample_flags &= ~(1u << 0u);
    failed += expect_bool("missing latch", calibration_bias_add(&accumulator, &sample), false);
    sample = make_sample(110u, 2u);
    sample.persona_generation++;
    failed += expect_bool("persona mismatch", calibration_bias_add(&accumulator, &sample), false);

    calibration_bias_begin(&accumulator, &gate, 13u);
    sample = make_sample(100u, 1u);
    failed += expect_bool("spread sample 1", calibration_bias_add(&accumulator, &sample), true);
    sample = make_sample(110u, 2u);
    failed += expect_bool("spread sample 2", calibration_bias_add(&accumulator, &sample), true);
    sample = make_sample(100u, 3u);
    failed += expect_bool("spread sample 3", calibration_bias_add(&accumulator, &sample), true);
    failed += expect_bool("spread rejected", calibration_bias_finalize(&accumulator,
                                                                         &snapshot), false);
    failed += expect_u32("spread reason", snapshot.reject_reason,
                         CALIBRATION_BIAS_REJECT_SPREAD);
    return failed;
}

int main(void)
{
    const int failed = test_valid_bias() + test_rejects_mismatch_and_spread();
    if (failed != 0) return 1;
    puts("calibration_bias tests passed");
    return 0;
}
