#include "calibration_bidirectional.h"

#include <stdbool.h>
#include <stdio.h>

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %llu got %llu\n",
                     name,
                     (unsigned long long)expected,
                     (unsigned long long)actual);
        return 1;
    }
    return 0;
}

static int expect_i64(const char *name, int64_t actual, int64_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lld got %lld\n",
                     name,
                     (long long)expected,
                     (long long)actual);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static calibration_bidirectional_sample_t make_loopback_sample(void)
{
    calibration_bidirectional_sample_t sample = {0};
    sample.t1_clk_tx = 1000u;
    sample.t2_clk_rx = 1020u;
    sample.t3_data_tx = 1070u;
    sample.t4_data_rx = 1100u;
    sample.train_epoch = 7u;
    sample.train_sequence = 11u;
    sample.persona_generation = 3u;
    sample.sample_flags =
        CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH |
        CALIBRATION_BIDIRECTIONAL_FLAG_DMA_COMPLETE |
        CALIBRATION_BIDIRECTIONAL_FLAG_BIAS_VALID |
        CALIBRATION_BIDIRECTIONAL_FLAG_TOPOLOGY_FRESH |
        CALIBRATION_BIDIRECTIONAL_FLAG_REFERENCE_LOOPBACK |
        CALIBRATION_BIDIRECTIONAL_FLAG_DIAGNOSTIC_ONLY;
    sample.edge_mask = CALIBRATION_BIDIRECTIONAL_EDGE_ALL;
    sample.bias_generation = 4u;
    sample.topology_generation = 5u;
    sample.endpoint_bias_ns = 10;
    sample.clock_rate_error_bound_ns = 2u;
    sample.reference_loopback = true;
    return sample;
}

static calibration_bidirectional_gate_t make_loopback_gate(void)
{
    calibration_bidirectional_gate_t gate = {0};
    gate.required_sample_flags =
        CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH |
        CALIBRATION_BIDIRECTIONAL_FLAG_DMA_COMPLETE;
    gate.required_edge_mask = CALIBRATION_BIDIRECTIONAL_EDGE_ALL;
    gate.expected_persona_generation = 3u;
    gate.expected_topology_generation = 5u;
    gate.max_clock_rate_error_bound_ns = 4u;
    gate.require_bias_generation = true;
    gate.require_fresh_topology = true;
    gate.allow_reference_loopback = true;
    return gate;
}

static int test_loopback_formula(void)
{
    const calibration_bidirectional_sample_t sample = make_loopback_sample();
    const calibration_bidirectional_gate_t gate = make_loopback_gate();
    calibration_bidirectional_result_t result;
    int failed = 0;

    failed += expect_bool("loopback evaluate",
                          calibration_bidirectional_evaluate(&sample,
                                                             &gate,
                                                             &result),
                          true);
    failed += expect_u64("residence", result.residence_ns, 50u);
    failed += expect_u64("raw path sum", result.raw_path_sum_ns, 50u);
    failed += expect_i64("corrected path sum", result.corrected_path_sum_ns, 40);
    failed += expect_i64("delay estimate", result.delay_estimate_ns, 20);
    failed += expect_bool("reference accepted", result.reference_accepted, true);
    failed += expect_bool("loopback active eligible",
                          result.active_eligible,
                          false);
    return failed;
}

static int test_rejects_bad_order(void)
{
    calibration_bidirectional_sample_t sample = make_loopback_sample();
    const calibration_bidirectional_gate_t gate = make_loopback_gate();
    calibration_bidirectional_result_t result;
    sample.t4_data_rx = 990u;
    int failed = 0;

    failed += expect_bool("bad order evaluate",
                          calibration_bidirectional_evaluate(&sample,
                                                             &gate,
                                                             &result),
                          false);
    failed += expect_u32("bad order reason",
                         result.reject_reason,
                         CALIBRATION_BIDIRECTIONAL_REJECT_EDGE_ORDER);
    return failed;
}

static int test_rejects_missing_evidence(void)
{
    calibration_bidirectional_sample_t sample = make_loopback_sample();
    const calibration_bidirectional_gate_t gate = make_loopback_gate();
    calibration_bidirectional_result_t result;
    int failed = 0;
    sample.sample_flags &= ~CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH;
    failed += expect_bool("missing evidence evaluate",
                          calibration_bidirectional_evaluate(&sample,
                                                             &gate,
                                                             &result),
                          false);
    failed += expect_u32("missing evidence reason",
                         result.reject_reason,
                         CALIBRATION_BIDIRECTIONAL_REJECT_SAMPLE_FLAGS);

    sample = make_loopback_sample();
    sample.dma_status = 1u;
    failed += expect_bool("dma fault evaluate",
                          calibration_bidirectional_evaluate(&sample,
                                                             &gate,
                                                             &result),
                          false);
    failed += expect_u32("dma fault reason",
                         result.reject_reason,
                         CALIBRATION_BIDIRECTIONAL_REJECT_DMA);
    return failed;
}

static int test_hardware_sample_can_be_active_eligible(void)
{
    calibration_bidirectional_sample_t sample = make_loopback_sample();
    const calibration_bidirectional_gate_t gate = make_loopback_gate();
    calibration_bidirectional_result_t result;
    int failed = 0;
    sample.reference_loopback = false;
    sample.sample_flags &= ~(CALIBRATION_BIDIRECTIONAL_FLAG_REFERENCE_LOOPBACK |
                             CALIBRATION_BIDIRECTIONAL_FLAG_DIAGNOSTIC_ONLY);
    sample.sample_flags |= CALIBRATION_BIDIRECTIONAL_FLAG_HARDWARE_LATCHED;
    failed += expect_bool("hardware sample evaluate",
                          calibration_bidirectional_evaluate(&sample,
                                                             &gate,
                                                             &result),
                          true);
    failed += expect_bool("hardware sample active eligible",
                          result.active_eligible,
                          true);
    return failed;
}

static int expect_reject(const char *name,
                         calibration_bidirectional_sample_t sample,
                         calibration_bidirectional_gate_t gate,
                         uint32_t expected_reason)
{
    calibration_bidirectional_result_t result;
    int failed = 0;
    failed += expect_bool(name,
                          calibration_bidirectional_evaluate(&sample,
                                                             &gate,
                                                             &result),
                          false);
    failed += expect_u32(name, result.reject_reason, expected_reason);
    failed += expect_bool("rejected sample active eligible",
                          result.active_eligible,
                          false);
    return failed;
}

static int test_reject_gate_matrix(void)
{
    calibration_bidirectional_sample_t sample = make_loopback_sample();
    calibration_bidirectional_gate_t gate = make_loopback_gate();
    int failed = 0;

    sample.edge_mask &= ~CALIBRATION_BIDIRECTIONAL_EDGE_DATA_RX;
    failed += expect_reject("missing edge", sample, gate,
                            CALIBRATION_BIDIRECTIONAL_REJECT_MISSING_EDGE);

    sample = make_loopback_sample();
    sample.persona_generation++;
    failed += expect_reject("persona mismatch", sample, gate,
                            CALIBRATION_BIDIRECTIONAL_REJECT_PERSONA);

    sample = make_loopback_sample();
    sample.topology_generation++;
    failed += expect_reject("topology mismatch", sample, gate,
                            CALIBRATION_BIDIRECTIONAL_REJECT_TOPOLOGY);

    sample = make_loopback_sample();
    sample.sample_flags &= ~CALIBRATION_BIDIRECTIONAL_FLAG_BIAS_VALID;
    failed += expect_reject("bias missing", sample, gate,
                            CALIBRATION_BIDIRECTIONAL_REJECT_BIAS);

    sample = make_loopback_sample();
    sample.clock_rate_error_bound_ns = gate.max_clock_rate_error_bound_ns + 1u;
    failed += expect_reject("clock rate", sample, gate,
                            CALIBRATION_BIDIRECTIONAL_REJECT_CLOCK_RATE);

    sample = make_loopback_sample();
    gate.allow_reference_loopback = false;
    failed += expect_reject("reference policy", sample, gate,
                            CALIBRATION_BIDIRECTIONAL_REJECT_LOOPBACK_POLICY);

    gate = make_loopback_gate();
    sample = make_loopback_sample();
    sample.t3_data_tx = sample.t2_clk_rx + 100u;
    sample.t4_data_rx = sample.t1_clk_tx + 90u;
    failed += expect_reject("negative path", sample, gate,
                            CALIBRATION_BIDIRECTIONAL_REJECT_NEGATIVE_PATH);

    sample = make_loopback_sample();
    sample.endpoint_bias_ns = 60;
    failed += expect_reject("bias exceeds path", sample, gate,
                            CALIBRATION_BIDIRECTIONAL_REJECT_NEGATIVE_PATH);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_loopback_formula();
    failed += test_rejects_bad_order();
    failed += test_rejects_missing_evidence();
    failed += test_hardware_sample_can_be_active_eligible();
    failed += test_reject_gate_matrix();
    if (failed != 0) {
        return 1;
    }
    puts("calibration_bidirectional tests passed");
    return 0;
}
