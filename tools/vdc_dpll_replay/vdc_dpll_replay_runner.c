#include "vdc_domain.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed = 0ul;
    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_i32(const char *text, int32_t *value)
{
    char *end = NULL;
    long parsed = 0l;
    if (text == NULL || value == NULL || text[0] == '\0') {
        return false;
    }
    errno = 0;
    parsed = strtol(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < INT32_MIN || parsed > INT32_MAX) {
        return false;
    }
    *value = (int32_t)parsed;
    return true;
}

static bool parse_sample(char *line,
                         uint32_t *sample_seq,
                         int32_t *phase_error_ns,
                         uint32_t *jitter_ns,
                         uint32_t *delay_ns,
                         uint32_t *source_node,
                         uint32_t *original_flags,
                         uint32_t *resolution_ns)
{
    char *fields[7] = {0};
    char *cursor = line;
    for (size_t i = 0u; i < 7u; i++) {
        fields[i] = cursor;
        char *comma = strchr(cursor, ',');
        if (i < 6u) {
            if (comma == NULL) {
                return false;
            }
            *comma = '\0';
            cursor = comma + 1;
        } else if (comma != NULL) {
            return false;
        }
    }
    char *newline = strpbrk(fields[6], "\r\n");
    if (newline != NULL) {
        *newline = '\0';
    }
    return parse_u32(fields[0], sample_seq) &&
           parse_i32(fields[1], phase_error_ns) &&
           parse_u32(fields[2], jitter_ns) &&
           parse_u32(fields[3], delay_ns) &&
           parse_u32(fields[4], source_node) &&
           parse_u32(fields[5], original_flags) &&
           parse_u32(fields[6], resolution_ns);
}

int main(int argc, char **argv)
{
    uint32_t node_count = 0u;
    uint32_t local_node = 0u;
    uint32_t reference_node = 0u;
    uint32_t period_ns = 0u;
    uint32_t lock_threshold_ns = 0u;
    uint32_t lock_samples = 0u;
    int32_t kp_q16 = 0;
    int32_t ki_q16 = 0;
    uint32_t outlier_threshold_ns = 0u;
    if (argc != 10 ||
        !parse_u32(argv[1], &node_count) ||
        !parse_u32(argv[2], &local_node) ||
        !parse_u32(argv[3], &reference_node) ||
        !parse_u32(argv[4], &period_ns) ||
        !parse_u32(argv[5], &lock_threshold_ns) ||
        !parse_u32(argv[6], &lock_samples) ||
        !parse_i32(argv[7], &kp_q16) ||
        !parse_i32(argv[8], &ki_q16) ||
        !parse_u32(argv[9], &outlier_threshold_ns)) {
        (void)fprintf(stderr,
                      "usage: runner node_count local_node reference_node "
                      "period_ns lock_threshold_ns lock_samples kp_q16 "
                      "ki_q16 outlier_threshold_ns\n");
        return 2;
    }

    vdc_domain_context_t context;
    if (!vdc_domain_init(&context) ||
        !vdc_domain_set_schedule_ring_topology(&context,
                                               local_node,
                                               reference_node,
                                               node_count)) {
        (void)fprintf(stderr, "failed to initialize VDC replay context\n");
        return 3;
    }
    context.schedule.period_ns = period_ns;
    context.schedule.observation_window_offset_ns = 0u;
    context.schedule.schedule_crc32 =
        vdc_domain_schedule_crc32(&context.schedule);
    context.servo.lock_acceptance_threshold_ns = lock_threshold_ns;
    context.servo.lock_sample_count = lock_samples;
    context.servo.kp_q16 = kp_q16;
    context.servo.ki_q16 = ki_q16;
    context.servo.update_period_us = period_ns / 1000u;
    if (context.servo.update_period_us == 0u) {
        context.servo.update_period_us = 1u;
    }
    context.servo.outlier_threshold_ns = outlier_threshold_ns;
    context.clock.tdma_schedule_crc32 = context.schedule.schedule_crc32;
    context.dpll.schedule_crc32 = context.schedule.schedule_crc32;
    context.dco.tdma_schedule_crc32 = context.schedule.schedule_crc32;
    vdc_domain_set_ready(&context, true);

    (void)printf("sample_seq,accepted,original_flags,replay_flags,gate," 
                 "lock_state,quality_tier,input_phase_error_ns," 
                 "residual_ns,phase_offset_ns,period_adjust_ppb," 
                 "accepted_count,rejected_count,rms_offset_ns," 
                 "max_abs_offset_ns\n");

    char line[512];
    uint32_t line_number = 0u;
    while (fgets(line, sizeof(line), stdin) != NULL) {
        line_number++;
        if (line[0] == '\0' || line[0] == '\r' || line[0] == '\n' ||
            line[0] == '#') {
            continue;
        }
        uint32_t sample_seq = 0u;
        int32_t phase_error_ns = 0;
        uint32_t jitter_ns = 0u;
        uint32_t delay_ns = 0u;
        uint32_t source_node = 0u;
        uint32_t original_flags = 0u;
        uint32_t resolution_ns = 0u;
        if (!parse_sample(line,
                          &sample_seq,
                          &phase_error_ns,
                          &jitter_ns,
                          &delay_ns,
                          &source_node,
                          &original_flags,
                          &resolution_ns)) {
            (void)fprintf(stderr, "invalid sample line %" PRIu32 "\n",
                          line_number);
            return 4;
        }
        if (sample_seq == 0u || source_node >= node_count) {
            (void)fprintf(stderr, "sample identity out of range on line %" PRIu32
                          "\n", line_number);
            return 5;
        }

        const uint64_t expected_ns =
            (uint64_t)(sample_seq - 1u) * (uint64_t)period_ns;
        int64_t observed_signed = (int64_t)expected_ns + phase_error_ns;
        if (observed_signed < 0) {
            (void)fprintf(stderr, "negative observed time on line %" PRIu32
                          "\n", line_number);
            return 6;
        }
        vdc_tdma_timestamp_evidence_t evidence;
        (void)memset(&evidence, 0, sizeof(evidence));
        evidence.sample_seq = sample_seq;
        evidence.schedule_epoch = context.schedule.schedule_epoch;
        evidence.slot_index = source_node;
        evidence.source_slot_id = source_node;
        evidence.reference_slot_id = reference_node;
        evidence.payload_class = VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE;
        evidence.expected_window_start_ns = expected_ns;
        evidence.arm_time_ns = expected_ns;
        evidence.start_time_ns = expected_ns;
        evidence.observed_time_ns = (uint64_t)observed_signed;
        evidence.done_time_ns = evidence.observed_time_ns + resolution_ns;
        evidence.apply_time_ns = evidence.done_time_ns + resolution_ns;
        evidence.late_ns = phase_error_ns > 0 ? (uint32_t)phase_error_ns : 0u;
        evidence.jitter_ns = jitter_ns;
        evidence.delay_ns = delay_ns;
        evidence.phase_error_ns = phase_error_ns;
        evidence.timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
        evidence.timestamp_resolution_ns = resolution_ns;
        /* This promotion exists only inside the host replay process. The
         * original flags remain in the output and can never reach firmware. */
        evidence.timestamp_flags = VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
        evidence.schedule_crc32 = context.schedule.schedule_crc32;
        evidence.frame_crc32 = 0xD0000000u ^ sample_seq;
        evidence.sample_crc32 = 0xA0000000u ^ sample_seq;

        const bool accepted =
            vdc_domain_submit_tdma_evidence(&context, &evidence);
        vdc_domain_snapshot_t snapshot;
        if (!vdc_domain_get_snapshot(&context, &snapshot)) {
            (void)fprintf(stderr, "snapshot failed on line %" PRIu32 "\n",
                          line_number);
            return 7;
        }
        (void)printf("%" PRIu32 ",%u,%" PRIu32 ",%" PRIu32 ",%" PRIu32
                     ",%" PRIu32 ",%" PRIu32 ",%" PRId32 ",%" PRId32
                     ",%" PRId32 ",%" PRId32 ",%" PRIu32 ",%" PRIu32
                     ",%" PRIu32 ",%" PRIu32 "\n",
                     sample_seq,
                     accepted ? 1u : 0u,
                     original_flags,
                     evidence.timestamp_flags,
                     snapshot.gate.reject_code,
                     snapshot.dpll.state,
                     snapshot.quality.lock_quality_tier,
                     phase_error_ns,
                     snapshot.dpll.last_phase_error_ns,
                     snapshot.clock.phase_offset_ns,
                     snapshot.clock.period_adjust_ppb,
                     snapshot.dpll.accepted_sample_count,
                     snapshot.dpll.rejected_sample_count,
                     snapshot.dpll.rms_offset_ns,
                     snapshot.dpll.max_abs_offset_ns);
    }
    return 0;
}
