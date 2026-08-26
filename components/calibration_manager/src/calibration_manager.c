#include "calibration_manager.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_config.h"
#include "board_identity.h"
#include "calibration_training_phase.h"
#include "ota_crc32.h"
#include "osal.h"
#include "project_config.h"
#include "resource_arbiter.h"
#include "storage_manager.h"
#include "tdma_runtime_owner.h"

#define CALIBRATION_MANAGER_DEFAULT_CRC32 0x10000003u

static calibration_manager_status_t s_status;
static bool s_ready;
static calibration_manager_loopback_snapshot_t s_loopback_snapshot;
static uint32_t s_loopback_processed_epoch;
static calibration_bias_accumulator_t s_bias_accumulator;
static calibration_bias_snapshot_t s_bias_snapshot;
static bool s_bias_active;
static uint32_t s_bias_next_epoch;
static calibration_clk_coded_store_t s_clk_coded_store;
static calibration_clk_coded_workspace_t s_clk_coded_workspace;
static uint32_t s_clk_coded_capture[TDMA_PIO_SPI_CODED_BUFFER_WORDS];
static volatile bool s_clk_coded_active;
static uint32_t s_clk_coded_request_sequence;
static calibration_manager_p3_snapshot_t s_p3_snapshot;
static calibration_training_marker_store_t s_marker_store;
static calibration_clk_coded_workspace_t s_marker_workspace;
static uint32_t s_marker_raw_capture[TDMA_PIO_SPI_MARKER_BUFFER_WORDS];
static uint32_t s_marker_bit_capture[TDMA_PIO_SPI_MARKER_BUFFER_WORDS];
static volatile uint32_t s_marker_raw_word_count;
static volatile uint32_t s_marker_raw_sample_count;
static char s_marker_capture_payload[8192];
static volatile bool s_marker_active;
static calibration_training_data_store_t s_data_store;
static calibration_clk_coded_workspace_t s_data_workspace;
static uint32_t s_data_capture[TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS];
static volatile uint32_t s_data_capture_word_count;
static volatile uint32_t s_data_capture_sample_count;
static char s_data_capture_payload[8192];
static volatile bool s_data_active;
static calibration_training_sck_store_t s_sck_store;
static calibration_clk_coded_workspace_t s_sck_workspace;
static uint32_t s_sck_capture[TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS];
static volatile uint32_t s_sck_capture_word_count;
static volatile uint32_t s_sck_capture_sample_count;
static char s_sck_capture_payload[8192];
static volatile bool s_sck_active;
static uint32_t s_ring_capture_rx[TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES];
static uint32_t s_ring_capture_tx[TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES];
static char s_ring_capture_payload[8192];

typedef struct {
    volatile uint32_t guard;
    uint32_t sequence;
    uint32_t calibration_generation;
    uint32_t capture_epoch;
} calibration_ring_capture_intent_t;

static calibration_ring_capture_intent_t s_ring_capture_intent;
static volatile uint32_t s_ring_capture_consumed_sequence;
static uint32_t s_ring_capture_next_sequence;
static volatile uint32_t s_ring_capture_snapshot_guard;
static calibration_ring_capture_snapshot_t s_ring_capture_snapshot;
static volatile uint32_t s_ring_capture_core1_service_count;
static volatile uint32_t s_ring_capture_intent_read_fail_count;
static volatile uint32_t s_ring_capture_last_seen_sequence;
static volatile uint32_t s_ring_capture_copy_attempt_count;
static volatile uint32_t s_ring_capture_copy_fail_count;

typedef enum {
    CALIBRATION_DATA_INTENT_NONE = 0u,
    CALIBRATION_DATA_INTENT_ARM = 1u,
    CALIBRATION_DATA_INTENT_INJECT = 2u,
    CALIBRATION_DATA_INTENT_STOP = 3u,
} calibration_data_intent_opcode_t;

typedef struct {
    volatile uint32_t guard;
    uint32_t sequence;
    uint32_t opcode;
    calibration_training_data_request_t request;
} calibration_data_intent_t;

static calibration_data_intent_t s_data_intent;
static uint32_t s_data_intent_next_sequence;
static volatile uint32_t s_data_intent_consumed_sequence;
static calibration_training_data_request_t s_data_active_request;

typedef enum {
    CALIBRATION_SCK_INTENT_NONE = 0u,
    CALIBRATION_SCK_INTENT_ARM = 1u,
    CALIBRATION_SCK_INTENT_INJECT = 2u,
    CALIBRATION_SCK_INTENT_STOP = 3u,
} calibration_sck_intent_opcode_t;

typedef struct {
    volatile uint32_t guard;
    uint32_t sequence;
    uint32_t opcode;
    calibration_training_sck_request_t request;
} calibration_sck_intent_t;

static calibration_sck_intent_t s_sck_intent;
static uint32_t s_sck_intent_next_sequence;
static volatile uint32_t s_sck_intent_consumed_sequence;
static calibration_training_sck_request_t s_sck_active_request;

static void calibration_manager_sck_publish(
    calibration_sck_intent_opcode_t opcode,
    const calibration_training_sck_request_t *request)
{
    (void)__atomic_add_fetch(&s_sck_intent.guard, 1u, __ATOMIC_ACQ_REL);
    s_sck_intent.sequence = ++s_sck_intent_next_sequence;
    s_sck_intent.opcode = (uint32_t)opcode;
    if (request != NULL) s_sck_intent.request = *request;
    (void)__atomic_add_fetch(&s_sck_intent.guard, 1u, __ATOMIC_RELEASE);
}

static bool calibration_manager_sck_read(calibration_sck_intent_t *intent)
{
    if (intent == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&s_sck_intent.guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *intent = s_sck_intent;
        const uint32_t end =
            __atomic_load_n(&s_sck_intent.guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

static void calibration_manager_ring_capture_publish(
    const calibration_ring_capture_snapshot_t *snapshot)
{
    (void)__atomic_add_fetch(&s_ring_capture_snapshot_guard,
                             1u, __ATOMIC_ACQ_REL);
    s_ring_capture_snapshot = *snapshot;
    (void)__atomic_add_fetch(&s_ring_capture_snapshot_guard,
                             1u, __ATOMIC_RELEASE);
}

static bool calibration_manager_ring_capture_intent_read(
    calibration_ring_capture_intent_t *intent)
{
    if (intent == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_ring_capture_intent.guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *intent = s_ring_capture_intent;
        const uint32_t end = __atomic_load_n(
            &s_ring_capture_intent.guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

static void calibration_manager_data_publish(
    calibration_data_intent_opcode_t opcode,
    const calibration_training_data_request_t *request)
{
    (void)__atomic_add_fetch(&s_data_intent.guard, 1u, __ATOMIC_ACQ_REL);
    s_data_intent.sequence = ++s_data_intent_next_sequence;
    s_data_intent.opcode = (uint32_t)opcode;
    if (request != NULL) s_data_intent.request = *request;
    (void)__atomic_add_fetch(&s_data_intent.guard, 1u, __ATOMIC_RELEASE);
}

static bool calibration_manager_data_read(calibration_data_intent_t *intent)
{
    if (intent == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&s_data_intent.guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *intent = s_data_intent;
        const uint32_t end =
            __atomic_load_n(&s_data_intent.guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

typedef enum {
    CALIBRATION_MARKER_INTENT_NONE = 0u,
    CALIBRATION_MARKER_INTENT_ARM = 1u,
    CALIBRATION_MARKER_INTENT_INJECT = 2u,
    CALIBRATION_MARKER_INTENT_STOP = 3u,
} calibration_marker_intent_opcode_t;

typedef struct {
    volatile uint32_t guard;
    uint32_t sequence;
    uint32_t opcode;
    calibration_training_marker_request_t request;
} calibration_marker_intent_t;

static calibration_marker_intent_t s_marker_intent;
static uint32_t s_marker_intent_next_sequence;
static volatile uint32_t s_marker_intent_consumed_sequence;
static calibration_training_marker_request_t s_marker_active_request;

static void calibration_manager_marker_publish(
    calibration_marker_intent_opcode_t opcode,
    const calibration_training_marker_request_t *request)
{
    (void)__atomic_add_fetch(&s_marker_intent.guard, 1u, __ATOMIC_ACQ_REL);
    s_marker_intent.sequence = ++s_marker_intent_next_sequence;
    s_marker_intent.opcode = (uint32_t)opcode;
    if (request != NULL) s_marker_intent.request = *request;
    (void)__atomic_add_fetch(&s_marker_intent.guard, 1u, __ATOMIC_RELEASE);
}

static bool calibration_manager_marker_read(
    calibration_marker_intent_t *intent)
{
    if (intent == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&s_marker_intent.guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *intent = s_marker_intent;
        const uint32_t end =
            __atomic_load_n(&s_marker_intent.guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

static void calibration_manager_publish_training_activity(void)
{
    calibration_pio_loopback_snapshot_t training_loopback;
    const bool loopback_active =
        calibration_pio_loopback_get_snapshot(&training_loopback) &&
        training_loopback.armed != 0u;
    const bool calibration_active =
        loopback_active ||
        __atomic_load_n(&s_data_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_sck_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_marker_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_clk_coded_active, __ATOMIC_ACQUIRE) ||
        s_p3_snapshot.raw.state == TDMA_PIO_SPI_P3_ARMED;
    resource_arbiter_publish_calibration_training(calibration_active);
}

typedef enum {
    CALIBRATION_P3_INTENT_NONE = 0u,
    CALIBRATION_P3_INTENT_START = 1u,
    CALIBRATION_P3_INTENT_STOP = 2u,
} calibration_p3_intent_opcode_t;

typedef struct {
    volatile uint32_t guard;
    uint32_t sequence;
    uint32_t opcode;
    tdma_pio_spi_p3_request_t request;
} calibration_p3_intent_t;

static calibration_p3_intent_t s_p3_intent;
static uint32_t s_p3_intent_next_sequence;
static volatile uint32_t s_p3_intent_consumed_sequence;

static void calibration_manager_p3_publish(
    calibration_p3_intent_opcode_t opcode,
    const tdma_pio_spi_p3_request_t *request)
{
    (void)__atomic_add_fetch(&s_p3_intent.guard, 1u, __ATOMIC_ACQ_REL);
    s_p3_intent.sequence = ++s_p3_intent_next_sequence;
    s_p3_intent.opcode = (uint32_t)opcode;
    if (request != NULL) s_p3_intent.request = *request;
    (void)__atomic_add_fetch(&s_p3_intent.guard, 1u, __ATOMIC_RELEASE);
}

static bool calibration_manager_p3_read(calibration_p3_intent_t *intent)
{
    if (intent == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&s_p3_intent.guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *intent = s_p3_intent;
        const uint32_t end =
            __atomic_load_n(&s_p3_intent.guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

typedef enum {
    CALIBRATION_CLK_CODED_INTENT_NONE = 0u,
    CALIBRATION_CLK_CODED_INTENT_START = 1u,
    CALIBRATION_CLK_CODED_INTENT_STOP = 2u,
} calibration_clk_coded_intent_opcode_t;

typedef struct {
    volatile uint32_t guard;
    uint32_t sequence;
    uint32_t opcode;
    calibration_clk_coded_request_t request;
    calibration_clk_correlation_gate_t gate;
} calibration_clk_coded_intent_t;

static calibration_clk_coded_intent_t s_clk_coded_intent;
static uint32_t s_clk_coded_intent_next_sequence;
static volatile uint32_t s_clk_coded_intent_consumed_sequence;
static calibration_clk_coded_request_t s_clk_coded_active_request;
static calibration_clk_correlation_gate_t s_clk_coded_active_gate;

static bool calibration_manager_parse_hex_u64(const char *text,
                                              uint64_t *value)
{
    uint64_t parsed = 0u;
    uint32_t digits = 0u;
    if (text == NULL || value == NULL) return false;
    while (*text != '\0') {
        uint32_t nibble;
        if (*text >= '0' && *text <= '9') {
            nibble = (uint32_t)(*text - '0');
        } else if (*text >= 'A' && *text <= 'F') {
            nibble = (uint32_t)(*text - 'A') + 10u;
        } else if (*text >= 'a' && *text <= 'f') {
            nibble = (uint32_t)(*text - 'a') + 10u;
        } else {
            return false;
        }
        if (digits >= 16u) return false;
        parsed = (parsed << 4u) | nibble;
        digits++;
        text++;
    }
    if (digits == 0u) return false;
    *value = parsed;
    return true;
}

static uint64_t calibration_manager_build_id_value(const char *text)
{
    uint64_t parsed = 0u;
    bool decimal = text != NULL && *text != '\0';
    for (const char *cursor = text; decimal && *cursor != '\0'; cursor++) {
        if (*cursor < '0' || *cursor > '9') {
            decimal = false;
            break;
        }
        const uint32_t digit = (uint32_t)(*cursor - '0');
        if (parsed > (UINT64_MAX - digit) / 10u) {
            decimal = false;
            break;
        }
        parsed = parsed * 10u + digit;
    }
    if (decimal) return parsed;

    /* Development build labels are represented by a stable FNV-1a value;
     * generated numeric release IDs retain their exact decimal value. */
    uint64_t hash = UINT64_C(14695981039346656037);
    while (text != NULL && *text != '\0') {
        hash ^= (uint8_t)*text++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void calibration_manager_clk_coded_intent_write_begin(void)
{
    (void)__atomic_add_fetch(&s_clk_coded_intent.guard,
                             1u, __ATOMIC_ACQ_REL);
}

static void calibration_manager_clk_coded_intent_write_end(void)
{
    (void)__atomic_add_fetch(&s_clk_coded_intent.guard,
                             1u, __ATOMIC_RELEASE);
}

static bool calibration_manager_clk_coded_intent_read(
    calibration_clk_coded_intent_t *intent)
{
    if (intent == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_clk_coded_intent.guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *intent = s_clk_coded_intent;
        const uint32_t end = __atomic_load_n(
            &s_clk_coded_intent.guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

static void calibration_manager_clk_coded_intent_publish(
    calibration_clk_coded_intent_opcode_t opcode,
    const calibration_clk_coded_request_t *request,
    const calibration_clk_correlation_gate_t *gate)
{
    calibration_manager_clk_coded_intent_write_begin();
    s_clk_coded_intent.sequence = ++s_clk_coded_intent_next_sequence;
    s_clk_coded_intent.opcode = (uint32_t)opcode;
    if (request != NULL) s_clk_coded_intent.request = *request;
    if (gate != NULL) s_clk_coded_intent.gate = *gate;
    calibration_manager_clk_coded_intent_write_end();
}

bool calibration_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    memset(&s_status, 0, sizeof(s_status));
    memset(&s_loopback_snapshot, 0, sizeof(s_loopback_snapshot));
    memset(&s_bias_accumulator, 0, sizeof(s_bias_accumulator));
    memset(&s_bias_snapshot, 0, sizeof(s_bias_snapshot));
    calibration_clk_coded_store_init(&s_clk_coded_store);
    memset(&s_clk_coded_workspace, 0, sizeof(s_clk_coded_workspace));
    memset(s_clk_coded_capture, 0, sizeof(s_clk_coded_capture));
    memset(&s_clk_coded_intent, 0, sizeof(s_clk_coded_intent));
    memset(&s_p3_snapshot, 0, sizeof(s_p3_snapshot));
    memset(&s_p3_intent, 0, sizeof(s_p3_intent));
    calibration_training_marker_store_init(&s_marker_store);
    memset(&s_marker_workspace, 0, sizeof(s_marker_workspace));
    memset(s_marker_raw_capture, 0, sizeof(s_marker_raw_capture));
    memset(s_marker_bit_capture, 0, sizeof(s_marker_bit_capture));
    __atomic_store_n(&s_marker_raw_word_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_marker_raw_sample_count, 0u, __ATOMIC_RELEASE);
    memset(&s_marker_intent, 0, sizeof(s_marker_intent));
    memset(&s_marker_active_request, 0, sizeof(s_marker_active_request));
    __atomic_store_n(&s_marker_active, false, __ATOMIC_RELEASE);
    s_marker_intent_next_sequence = 0u;
    s_marker_intent_consumed_sequence = 0u;
    calibration_training_data_store_init(&s_data_store);
    memset(&s_data_workspace, 0, sizeof(s_data_workspace));
    memset(s_data_capture, 0, sizeof(s_data_capture));
    __atomic_store_n(&s_data_capture_word_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_data_capture_sample_count, 0u, __ATOMIC_RELEASE);
    memset(&s_data_intent, 0, sizeof(s_data_intent));
    memset(&s_data_active_request, 0, sizeof(s_data_active_request));
    __atomic_store_n(&s_data_active, false, __ATOMIC_RELEASE);
    s_data_intent_next_sequence = 0u;
    s_data_intent_consumed_sequence = 0u;
    calibration_training_sck_store_init(&s_sck_store);
    memset(&s_sck_workspace, 0, sizeof(s_sck_workspace));
    memset(s_sck_capture, 0, sizeof(s_sck_capture));
    __atomic_store_n(&s_sck_capture_word_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_sck_capture_sample_count, 0u, __ATOMIC_RELEASE);
    memset(&s_sck_intent, 0, sizeof(s_sck_intent));
    memset(&s_sck_active_request, 0, sizeof(s_sck_active_request));
    __atomic_store_n(&s_sck_active, false, __ATOMIC_RELEASE);
    s_sck_intent_next_sequence = 0u;
    s_sck_intent_consumed_sequence = 0u;
    memset(s_ring_capture_rx, 0, sizeof(s_ring_capture_rx));
    memset(s_ring_capture_tx, 0, sizeof(s_ring_capture_tx));
    memset(&s_ring_capture_intent, 0, sizeof(s_ring_capture_intent));
    memset(&s_ring_capture_snapshot, 0, sizeof(s_ring_capture_snapshot));
    s_ring_capture_next_sequence = 0u;
    __atomic_store_n(&s_ring_capture_consumed_sequence,
                     0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ring_capture_snapshot_guard,
                     0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ring_capture_core1_service_count,
                     0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ring_capture_intent_read_fail_count,
                     0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ring_capture_last_seen_sequence,
                     0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ring_capture_copy_attempt_count,
                     0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_ring_capture_copy_fail_count,
                     0u, __ATOMIC_RELEASE);
    memset(&s_clk_coded_active_request, 0,
           sizeof(s_clk_coded_active_request));
    memset(&s_clk_coded_active_gate, 0, sizeof(s_clk_coded_active_gate));
    __atomic_store_n(&s_clk_coded_active, false, __ATOMIC_RELEASE);
    s_clk_coded_intent_next_sequence = 0u;
    s_clk_coded_intent_consumed_sequence = 0u;
    s_clk_coded_request_sequence = 0u;
    s_p3_intent_next_sequence = 0u;
    s_p3_intent_consumed_sequence = 0u;
    s_loopback_processed_epoch = 0u;
    s_bias_active = false;
    s_bias_next_epoch = 1u;
    s_status.last_service_ms = now_ms;
    s_status.command_seq = 1u;
    s_status.link_count = 1u;
    s_status.delay_count = 1u;
    s_status.active_crc32 = CALIBRATION_MANAGER_DEFAULT_CRC32;
    s_ready = false;
    return calibration_pio_loopback_init();
}

void calibration_manager_set_ready(bool ready)
{
    osal_critical_enter();
    s_ready = ready;
    s_status.ready = ready;
    osal_critical_exit();
}

void calibration_manager_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_status.service_count == 0u) {
        s_status.first_service_ms = now_ms;
    }
    s_status.service_count++;
    s_status.last_service_ms = now_ms;
    s_status.ready = s_ready;
    s_status.state = 0u;
    osal_critical_exit();

    calibration_pio_loopback_snapshot_t raw;
    if (calibration_pio_loopback_get_snapshot(&raw) && raw.complete != 0u &&
        raw.epoch != s_loopback_processed_epoch) {
        calibration_bidirectional_sample_t sample = {
            .t1_clk_tx = raw.t1_clk_tx,
            .t2_clk_rx = raw.t2_clk_rx,
            .t3_data_tx = raw.t3_data_tx,
            .t4_data_rx = raw.t4_data_rx,
            .train_epoch = raw.epoch,
            .train_sequence = raw.epoch,
            .persona_generation = 1u,
            .sample_flags = CALIBRATION_BIDIRECTIONAL_FLAG_HARDWARE_LATCHED |
                            CALIBRATION_BIDIRECTIONAL_FLAG_DIAGNOSTIC_ONLY |
                            CALIBRATION_BIDIRECTIONAL_FLAG_DMA_COMPLETE |
                            CALIBRATION_BIDIRECTIONAL_FLAG_REFERENCE_LOOPBACK,
            .edge_mask = raw.edge_mask,
            .clock_rate_error_bound_ns = raw.sample_period_ns,
            .reference_loopback = true,
        };
        if ((raw.flags & (1u << 2u)) != 0u) {
            sample.sample_flags |= CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH;
        }
        const calibration_bidirectional_gate_t gate = {
            .required_sample_flags = CALIBRATION_BIDIRECTIONAL_FLAG_HARDWARE_LATCHED |
                                     CALIBRATION_BIDIRECTIONAL_FLAG_DMA_COMPLETE |
                                     CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH,
            .required_edge_mask = CALIBRATION_BIDIRECTIONAL_EDGE_ALL,
            .expected_persona_generation = 1u,
            .max_clock_rate_error_bound_ns = raw.sample_period_ns,
            .allow_reference_loopback = true,
        };
        calibration_manager_loopback_snapshot_t next = { .raw = raw };
        next.result_valid = calibration_bidirectional_evaluate(
            &sample, &gate, &next.result) ? 1u : 0u;
        osal_critical_enter();
        s_loopback_snapshot = next;
        s_loopback_processed_epoch = raw.epoch;
        s_status.state = next.result_valid != 0u ? 2u : 3u;
        s_status.last_error = next.result.reject_reason;
        osal_critical_exit();

        if (s_bias_active) {
            const calibration_bias_sample_t bias_sample = {
                .raw_path_sum_ns = next.result.raw_path_sum_ns,
                .clock_error_bound_ns = raw.sample_period_ns,
                .persona_generation = 1u,
                .profile_crc32 = 0u,
                .topology_generation = 0u,
                .sample_flags = sample.sample_flags,
                .epoch = raw.epoch,
                .reference_loopback = true,
            };
            (void)calibration_bias_add(&s_bias_accumulator, &bias_sample);
            const bool reached_limit = s_bias_accumulator.sample_count >=
                                        s_bias_accumulator.gate.maximum_samples;
            const bool reached_target = s_bias_accumulator.accepted_count >=
                                         s_bias_accumulator.gate.minimum_samples;
            if (reached_target || reached_limit) {
                calibration_bias_snapshot_t snapshot;
                const bool finalized = calibration_bias_finalize(
                    &s_bias_accumulator, &snapshot);
                osal_critical_enter();
                s_bias_snapshot = snapshot;
                s_bias_active = false;
                osal_critical_exit();
                calibration_pio_loopback_request_stop();
                if (!finalized) s_status.last_error = snapshot.reject_reason;
            } else {
                s_bias_next_epoch = raw.epoch + 1u;
                if (s_bias_next_epoch == 0u) s_bias_next_epoch = 1u;
                (void)calibration_pio_loopback_request_start(
                    &(calibration_pio_loopback_config_t){
                        .sample_hz = TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_HZ,
                        .sample_words = TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_WORDS,
                        .epoch = s_bias_next_epoch,
                    });
            }
        }
    }
}

bool calibration_manager_start_loopback(uint32_t sample_words)
{
    calibration_pio_loopback_config_t config = {
        .sample_hz = TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_HZ,
        .sample_words = sample_words == 0u ?
                             TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_WORDS :
                             sample_words,
        .epoch = s_status.command_seq + 1u,
    };
    const bool accepted = calibration_pio_loopback_request_start(&config);
    if (accepted) {
        resource_arbiter_publish_calibration_training(true);
        osal_critical_enter();
        s_status.command_seq++;
        s_status.state = 1u;
        s_status.last_error = 0u;
        osal_critical_exit();
    }
    return accepted;
}

void calibration_manager_stop_loopback(void)
{
    s_bias_active = false;
    calibration_pio_loopback_request_stop();
}

bool calibration_manager_start_bias(uint32_t expected_path_sum_ns,
                                    uint32_t minimum_samples,
                                    uint32_t maximum_samples,
                                    uint32_t maximum_spread_ns,
                                    uint32_t maximum_clock_error_ns)
{
    if (minimum_samples == 0u || maximum_samples < minimum_samples ||
        maximum_samples > 1024u || s_bias_active) {
        return false;
    }
    calibration_bias_gate_t gate = {
        .expected_path_sum_ns = expected_path_sum_ns,
        .expected_persona_generation = 1u,
        .minimum_samples = minimum_samples,
        .maximum_samples = maximum_samples,
        .maximum_spread_ns = maximum_spread_ns,
        .maximum_clock_error_ns = maximum_clock_error_ns,
        .require_hardware_latched = true,
        .require_sync_match = true,
    };
    const uint32_t generation = s_bias_snapshot.generation + 1u;
    calibration_bias_begin(&s_bias_accumulator, &gate,
                           generation == 0u ? 1u : generation);
    memset(&s_bias_snapshot, 0, sizeof(s_bias_snapshot));
    s_bias_active = true;
    s_bias_next_epoch = s_loopback_processed_epoch + 1u;
    if (s_bias_next_epoch == 0u) s_bias_next_epoch = 1u;
    if (!calibration_pio_loopback_request_start(
            &(calibration_pio_loopback_config_t){
                .sample_hz = TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_HZ,
                .sample_words = TDMA_PIO_SPI_CAL_LOOPBACK_DEFAULT_WORDS,
                .epoch = s_bias_next_epoch,
            })) {
        s_bias_active = false;
        return false;
    }
    resource_arbiter_publish_calibration_training(true);
    return true;
}

void calibration_manager_stop_bias(void)
{
    if (!s_bias_active) {
        calibration_pio_loopback_request_stop();
        return;
    }
    calibration_bias_snapshot_t snapshot;
    (void)calibration_bias_finalize(&s_bias_accumulator, &snapshot);
    osal_critical_enter();
    s_bias_snapshot = snapshot;
    s_bias_active = false;
    osal_critical_exit();
    calibration_pio_loopback_request_stop();
}

bool calibration_manager_get_bias_snapshot(
    calibration_bias_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    osal_critical_enter();
    *snapshot = s_bias_snapshot;
    osal_critical_exit();
    return true;
}

bool calibration_manager_save_bias_snapshot(uint32_t *job_id)
{
    calibration_bias_snapshot_t snapshot;
    if (job_id != NULL) *job_id = 0u;
    if (job_id == NULL || !calibration_manager_get_bias_snapshot(&snapshot) ||
        !calibration_bias_snapshot_validate(&snapshot)) {
        return false;
    }

    char path[96];
    (void)snprintf(path, sizeof(path),
                   "/cal/accepted_%s_g%lu.json",
                   board_identity_serial(),
                   (unsigned long)snapshot.generation);

    char payload[2048];
    const int written = snprintf(
        payload, sizeof(payload),
        "{\n"
        "  \"magic\": \"HAOFV_CALIBRATION_EVIDENCE\",\n"
        "  \"schema\": 1,\n"
        "  \"state\": \"accepted\",\n"
        "  \"identity\": \"%s\",\n"
        "  \"build_id\": \"%s\",\n"
        "  \"generation\": %lu,\n"
        "  \"sample_count\": %lu,\n"
        "  \"accepted_count\": %lu,\n"
        "  \"rejected_count\": %lu,\n"
        "  \"persona_generation\": %lu,\n"
        "  \"profile_crc32\": %lu,\n"
        "  \"topology_generation\": %lu,\n"
        "  \"first_epoch\": %lu,\n"
        "  \"last_epoch\": %lu,\n"
        "  \"mean_bias_ns\": %lld,\n"
        "  \"spread_ns\": %lu,\n"
        "  \"table_crc32\": %lu,\n"
        "  \"source\": \"calibration_bias_snapshot\",\n"
        "  \"active\": false\n"
        "}\n",
        board_identity_serial(), g_project_build_id,
        (unsigned long)snapshot.generation,
        (unsigned long)snapshot.sample_count,
        (unsigned long)snapshot.accepted_count,
        (unsigned long)snapshot.rejected_count,
        (unsigned long)snapshot.persona_generation,
        (unsigned long)snapshot.profile_crc32,
        (unsigned long)snapshot.topology_generation,
        (unsigned long)snapshot.first_epoch,
        (unsigned long)snapshot.last_epoch,
        (long long)snapshot.mean_bias_ns,
        (unsigned long)snapshot.spread_ns,
        (unsigned long)snapshot.table_crc32);
    if (written <= 0 || (size_t)written >= sizeof(payload)) return false;

    const uint32_t crc32 = ota_crc32_compute(
        (const uint8_t *)payload, (size_t)written);
    uint32_t transaction_id = 0u;
    if (!storage_manager_begin_file_write(path, (uint32_t)written, crc32,
                                          &transaction_id) ||
        !storage_manager_write_file_chunk(transaction_id, 0u,
                                           (const uint8_t *)payload,
                                           (size_t)written) ||
        !storage_manager_commit_file_write(transaction_id, job_id)) {
        (void)storage_manager_abort_file_write(transaction_id);
        *job_id = 0u;
        return false;
    }
    return true;
}

static void calibration_manager_marker_set_bit(uint32_t *words,
                                               uint32_t index,
                                               uint32_t value)
{
    if (value != 0u) words[index >> 5u] |= 1u << (index & 31u);
}

static uint32_t calibration_manager_marker_extract_rx(
    const tdma_pio_spi_marker_snapshot_t *raw,
    const uint32_t *interleaved,
    uint32_t *packed,
    int32_t offset_sample_count)
{
    const uint32_t high_prefix =
        s_marker_workspace.marker.half_chip_samples;
    uint32_t phase_delay_cycles = 0u;
    if (!calibration_training_marker_capture_delay_cycles(
            s_marker_active_request.link_base_delay_ns,
            s_marker_active_request.tick_resolution_ns,
            offset_sample_count, &phase_delay_cycles)) {
        return 0u;
    }
    const uint32_t prefix = high_prefix + 1u + phase_delay_cycles;
    const uint32_t sample_count = raw->capture_sample_count + prefix;
    memset(packed, 0,
           ((sample_count + 31u) / 32u) * sizeof(packed[0]));
    /* CS is idle high, so the first observable marker event is the first
     * Manchester falling edge on every node. Rebuild the known leading high
     * half-chip; WAIT 0 GPIO and its phase delay supply the following low. */
    for (uint32_t index = 0u; index < high_prefix; index++) {
        calibration_manager_marker_set_bit(packed, index, 1u);
    }
    for (uint32_t index = 0u; index < raw->capture_sample_count; index++) {
        const uint32_t pair =
            (interleaved[index / 16u] >> ((index & 15u) * 2u)) & 0x3u;
        calibration_manager_marker_set_bit(
            packed, index + prefix, (pair >> 1u) & 1u);
    }
    return sample_count;
}

static void calibration_manager_marker_finish_core1(
    const tdma_pio_spi_marker_snapshot_t *raw)
{
    size_t raw_words = 0u;
    memset(s_marker_raw_capture, 0, sizeof(s_marker_raw_capture));
    memset(s_marker_bit_capture, 0, sizeof(s_marker_bit_capture));
    const bool copied =
        (raw->state == TDMA_PIO_SPI_MARKER_COMPLETE ||
         raw->state == TDMA_PIO_SPI_MARKER_ERROR) &&
        tdma_runtime_owner_copy_marker_capture_core1(
            s_marker_raw_capture, TDMA_PIO_SPI_MARKER_BUFFER_WORDS,
            &raw_words);
    uint32_t marker_flags = 0u;
    calibration_clk_correlation_result_t correlation;
    memset(&correlation, 0, sizeof(correlation));
    bool correlated = false;
    if (copied) {
        const uint32_t sample_count = calibration_manager_marker_extract_rx(
            raw, s_marker_raw_capture, s_marker_bit_capture,
            s_marker_active_request.offset_sample_count);
        const uint32_t available_lag =
            sample_count >= s_marker_workspace.marker.raw_samples
                ? sample_count - s_marker_workspace.marker.raw_samples
                : 0u;
        const calibration_clk_correlation_gate_t gate = {
            .min_lag_sample = 0u,
            .max_lag_sample = available_lag <
                                      CALIBRATION_CLK_CORRELATION_MAX_LAGS
                                  ? available_lag
                                  : CALIBRATION_CLK_CORRELATION_MAX_LAGS - 1u,
            .max_best_distance = 512u,
            .min_margin = 0u,
        };
        correlated = gate.max_lag_sample > gate.min_lag_sample &&
            calibration_clk_marker_correlate(
                &(calibration_clk_marker_config_t){
                    .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
                    .codebook_id = (uint8_t)
                        s_marker_active_request.marker_codebook_id,
                    .epoch = (uint8_t)s_marker_active_request.train_epoch,
                    .source_node =
                        (uint8_t)s_marker_active_request.reference_node,
                    .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
                },
                s_marker_workspace.expected_words,
                s_marker_workspace.marker.raw_samples,
                s_marker_bit_capture, sample_count, &gate, &correlation) &&
            correlation.accepted != 0u;
        marker_flags = correlation.marker_flags;
    }

    uint32_t flags = CALIBRATION_TRAINING_MARKER_FLAG_DIAGNOSTIC_ONLY;
    if ((raw->flags & TDMA_PIO_SPI_MARKER_FLAG_HARDWARE_CAPTURE) != 0u) {
        flags |= CALIBRATION_TRAINING_MARKER_FLAG_HARDWARE_LATCHED;
    }
    if ((raw->flags & TDMA_PIO_SPI_MARKER_FLAG_OUTPUT_EDGE) != 0u) {
        flags |= CALIBRATION_TRAINING_MARKER_FLAG_FORWARD_VALID;
    }
    if ((raw->flags & TDMA_PIO_SPI_MARKER_FLAG_RX_DMA_COMPLETE) != 0u &&
        copied) {
        flags |= CALIBRATION_TRAINING_MARKER_FLAG_DMA_COMPLETE;
    }
    if (correlated && marker_flags == CALIBRATION_CLK_MARKER_FLAG_ALL) {
        flags |= CALIBRATION_TRAINING_MARKER_FLAG_CAPTURE_VALID |
                 CALIBRATION_TRAINING_MARKER_FLAG_CRC_VALID |
                 CALIBRATION_TRAINING_MARKER_FLAG_EPOCH_VALID |
                 CALIBRATION_TRAINING_MARKER_FLAG_SEQUENCE_VALID |
                 CALIBRATION_TRAINING_MARKER_FLAG_POLARITY_VALID;
    }
    const calibration_training_marker_evidence_t evidence = {
        .train_epoch = s_marker_active_request.train_epoch,
        .train_sequence = s_marker_active_request.train_sequence,
        .marker_id = s_marker_active_request.marker_id,
        .observed_crc32 = correlated
                              ? s_marker_active_request.marker_crc32
                              : 0u,
        .polarity = correlation.detected_polarity,
        .marker_flags = marker_flags,
        .correlation_reject_reason = correlation.reject_reason,
        .best_lag_sample = correlation.best_lag_sample,
        .best_distance = correlation.best_distance,
        .calibration_generation =
            s_marker_active_request.calibration_generation,
        .topology_generation = s_marker_active_request.topology_generation,
        .topology_crc32 = s_marker_active_request.topology_crc32,
        .profile_crc32 = s_marker_active_request.profile_crc32,
        .schedule_crc32 = s_marker_active_request.schedule_crc32,
        .flags = flags,
        .marker_capture_tick = raw->marker_capture_tick,
        .marker_forward_tick = raw->marker_forward_tick,
        .marker_return_tick = raw->marker_return_tick,
        .dma_capture_count = copied ? (uint32_t)raw_words : 0u,
        .dma_overrun_count = raw->dma_overrun_count,
        .pio_stall_count = raw->pio_stall_count,
        .timeout_count = raw->timeout_count,
        .offset_sample_count = s_marker_active_request.offset_sample_count,
    };
    (void)calibration_training_marker_evaluate_core1(
        &s_marker_store, &s_marker_active_request, &evidence);
    __atomic_store_n(&s_marker_raw_word_count,
                     copied ? (uint32_t)raw_words : 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_marker_raw_sample_count,
                     copied ? raw->capture_sample_count : 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_marker_active, false, __ATOMIC_RELEASE);
}

static void calibration_manager_data_finish_core1(
    const tdma_pio_spi_data_train_snapshot_t *raw)
{
    const bool initiator =
        raw->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR;
    if (!initiator) {
        __atomic_store_n(&s_data_capture_word_count, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&s_data_capture_sample_count, 0u, __ATOMIC_RELEASE);
        calibration_training_data_snapshot_t snapshot;
        if (calibration_training_data_get_snapshot(&s_data_store, &snapshot)) {
            snapshot.state = raw->state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE
                                 ? CALIBRATION_TRAINING_DATA_ACCEPTED
                                 : CALIBRATION_TRAINING_DATA_REJECTED;
            snapshot.reject_reason =
                raw->state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE
                    ? CALIBRATION_TRAINING_DATA_REJECT_NONE
                    : CALIBRATION_TRAINING_DATA_REJECT_DMA;
            snapshot.flags |=
                CALIBRATION_TRAINING_DATA_FLAG_HARDWARE_MARKER;
            if ((raw->flags &
                 TDMA_PIO_SPI_DATA_TRAIN_FLAG_DATA_DMA_COMPLETE) != 0u &&
                (raw->flags &
                 TDMA_PIO_SPI_DATA_TRAIN_FLAG_MARKER_DMA_COMPLETE) != 0u) {
                snapshot.flags |= CALIBRATION_TRAINING_DATA_FLAG_DMA_COMPLETE;
            }
            snapshot.dma_overrun_count = raw->dma_overrun_count;
            snapshot.pio_stall_count = raw->pio_stall_count;
            snapshot.timeout_count = raw->timeout_count;
            snapshot.marker_capture_tick = raw->marker_capture_tick;
            snapshot.data_capture_tick = raw->data_capture_tick;
            (void)calibration_training_data_publish_core1(
                &s_data_store, &snapshot);
        }
        __atomic_store_n(&s_data_active, false, __ATOMIC_RELEASE);
        return;
    }

    size_t capture_words = 0u;
    memset(s_data_capture, 0, sizeof(s_data_capture));
    const bool copied = raw->state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE &&
        tdma_runtime_owner_copy_data_train_capture_core1(
            s_data_capture, TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS,
            &capture_words);
    const uint32_t search_samples = (uint32_t)(
        s_data_active_request.search_end_offset_sample -
        s_data_active_request.search_start_offset_sample + 1);
    const calibration_clk_marker_config_t config = {
        .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        .codebook_id = (uint8_t)s_data_active_request.data_codebook_id,
        .epoch = (uint8_t)s_data_active_request.train_epoch,
        .source_node = (uint8_t)s_data_active_request.source_node,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    const calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = 0u,
        .max_lag_sample = search_samples - 1u,
        .max_best_distance = s_data_active_request.max_best_distance,
        .min_margin = s_data_active_request.min_margin,
    };
    calibration_clk_correlation_result_t correlation;
    memset(&correlation, 0, sizeof(correlation));
    const bool correlated = copied && calibration_clk_marker_correlate(
        &config, s_data_workspace.expected_words,
        s_data_workspace.marker.raw_samples,
        s_data_capture, raw->capture_sample_count, &gate, &correlation);

    uint32_t flags = CALIBRATION_TRAINING_DATA_FLAG_DIAGNOSTIC_ONLY;
    if ((raw->flags & TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_MARKER) != 0u) {
        flags |= CALIBRATION_TRAINING_DATA_FLAG_HARDWARE_MARKER;
    }
    if ((raw->flags & TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_DATA) != 0u) {
        flags |= CALIBRATION_TRAINING_DATA_FLAG_HARDWARE_DATA_CAPTURE;
    }
    if (copied &&
        (raw->flags & TDMA_PIO_SPI_DATA_TRAIN_FLAG_DATA_DMA_COMPLETE) != 0u) {
        flags |= CALIBRATION_TRAINING_DATA_FLAG_DMA_COMPLETE;
    }
    if (correlated && correlation.accepted != 0u &&
        correlation.marker_flags == CALIBRATION_CLK_MARKER_FLAG_ALL) {
        flags |= CALIBRATION_TRAINING_DATA_FLAG_CRC_VALID |
                 CALIBRATION_TRAINING_DATA_FLAG_EPOCH_VALID |
                 CALIBRATION_TRAINING_DATA_FLAG_SEQUENCE_VALID;
        if (correlation.detected_polarity ==
            s_data_active_request.expected_polarity) {
            flags |= CALIBRATION_TRAINING_DATA_FLAG_POLARITY_VALID;
        }
    }
    const calibration_training_data_evidence_t evidence = {
        .train_epoch = s_data_active_request.train_epoch,
        .train_sequence = s_data_active_request.train_sequence,
        .observed_crc32 = correlated && correlation.accepted != 0u
                              ? s_data_active_request.data_crc32
                              : 0u,
        .calibration_generation =
            s_data_active_request.calibration_generation,
        .topology_generation = s_data_active_request.topology_generation,
        .topology_crc32 = s_data_active_request.topology_crc32,
        .profile_crc32 = s_data_active_request.profile_crc32,
        .schedule_crc32 = s_data_active_request.schedule_crc32,
        .flags = flags,
        .polarity = correlation.detected_polarity,
        .correlation_reject_reason = correlated
                                         ? correlation.reject_reason
                                         : CALIBRATION_CLK_CORRELATION_REJECT_BAD_ARGUMENT,
        .best_lag_sample = correlation.best_lag_sample,
        .best_distance = correlation.best_distance,
        .second_lag_sample = correlation.second_lag_sample,
        .second_distance = correlation.second_distance,
        .margin = correlation.margin,
        .captured_sample_count = copied ? raw->capture_sample_count : 0u,
        .expected_sample_count = s_data_workspace.marker.raw_samples,
        .dma_overrun_count = raw->dma_overrun_count,
        .pio_stall_count = raw->pio_stall_count,
        .timeout_count = raw->timeout_count,
        .marker_capture_tick = raw->marker_capture_tick,
        .data_capture_tick = raw->data_capture_tick +
                             correlation.best_lag_sample,
    };
    (void)calibration_training_data_evaluate_core1(
        &s_data_store, &s_data_active_request, &evidence);
    __atomic_store_n(&s_data_capture_word_count,
                     copied ? (uint32_t)capture_words : 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_data_capture_sample_count,
                     copied ? raw->capture_sample_count : 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_data_active, false, __ATOMIC_RELEASE);
}

static void calibration_manager_sck_finish_core1(
    const tdma_pio_spi_data_train_snapshot_t *raw)
{
    const bool destination = raw->role ==
        TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION;
    if (!destination) {
        __atomic_store_n(&s_sck_capture_word_count, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&s_sck_capture_sample_count, 0u, __ATOMIC_RELEASE);
        calibration_training_sck_snapshot_t snapshot;
        if (calibration_training_sck_get_snapshot(&s_sck_store, &snapshot)) {
            snapshot.state = raw->state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE
                                 ? CALIBRATION_TRAINING_SCK_ACCEPTED
                                 : CALIBRATION_TRAINING_SCK_REJECTED;
            snapshot.reject_reason =
                raw->state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE
                    ? CALIBRATION_TRAINING_SCK_REJECT_NONE
                    : CALIBRATION_TRAINING_SCK_REJECT_DMA;
            snapshot.flags |=
                CALIBRATION_TRAINING_SCK_FLAG_HARDWARE_SCK_ORIGIN |
                CALIBRATION_TRAINING_SCK_FLAG_HARDWARE_SCK_CAPTURE;
            if ((raw->flags &
                 TDMA_PIO_SPI_DATA_TRAIN_FLAG_DATA_DMA_COMPLETE) != 0u &&
                (raw->flags &
                 TDMA_PIO_SPI_DATA_TRAIN_FLAG_MARKER_DMA_COMPLETE) != 0u) {
                snapshot.flags |= CALIBRATION_TRAINING_SCK_FLAG_DMA_COMPLETE;
            }
            snapshot.dma_overrun_count = raw->dma_overrun_count;
            snapshot.pio_stall_count = raw->pio_stall_count;
            snapshot.timeout_count = raw->timeout_count;
            snapshot.sck_capture_origin_tick = raw->marker_capture_tick;
            snapshot.sck_code_capture_tick = raw->data_capture_tick;
            (void)calibration_training_sck_publish_core1(
                &s_sck_store, &snapshot);
        }
        __atomic_store_n(&s_sck_active, false, __ATOMIC_RELEASE);
        return;
    }

    size_t capture_words = 0u;
    memset(s_sck_capture, 0, sizeof(s_sck_capture));
    const bool copied = raw->state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE &&
        tdma_runtime_owner_copy_sck_train_capture_core1(
            s_sck_capture, TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS,
            &capture_words);
    uint32_t link_base_delay_samples = 0u;
    if (!calibration_training_phase_delay_samples(
            s_sck_active_request.link_base_delay_ns,
            s_sck_active_request.sample_period_ns, 0, UINT32_MAX,
            &link_base_delay_samples)) {
        __atomic_store_n(&s_sck_active, false, __ATOMIC_RELEASE);
        return;
    }
    const int32_t nominal_lag =
        (int32_t)s_sck_active_request.sck_launch_guard_sample_count -
        (int32_t)link_base_delay_samples;
    const uint32_t min_lag = (uint32_t)(
        nominal_lag +
        s_sck_active_request.search_start_offset_sample);
    const uint32_t max_lag = (uint32_t)(
        nominal_lag +
        s_sck_active_request.search_end_offset_sample + 4);
    const calibration_clk_marker_config_t config = {
        .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        .codebook_id = (uint8_t)s_sck_active_request.sck_codebook_id,
        .epoch = (uint8_t)s_sck_active_request.train_epoch,
        .source_node = (uint8_t)s_sck_active_request.source_node,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    const calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = min_lag,
        .max_lag_sample = max_lag,
        .max_best_distance = s_sck_active_request.max_best_distance,
        .min_margin = s_sck_active_request.min_margin,
    };
    calibration_clk_correlation_result_t correlation;
    memset(&correlation, 0, sizeof(correlation));
    const bool correlated = copied && calibration_clk_marker_correlate(
        &config, s_sck_workspace.expected_words,
        s_sck_workspace.marker.raw_samples,
        s_sck_capture, raw->capture_sample_count, &gate, &correlation);

    uint32_t flags = CALIBRATION_TRAINING_SCK_FLAG_DIAGNOSTIC_ONLY;
    if ((raw->flags & TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_ORIGIN) != 0u) {
        flags |= CALIBRATION_TRAINING_SCK_FLAG_HARDWARE_SCK_ORIGIN;
    }
    if ((raw->flags & TDMA_PIO_SPI_DATA_TRAIN_FLAG_HARDWARE_DATA) != 0u) {
        flags |= CALIBRATION_TRAINING_SCK_FLAG_HARDWARE_SCK_CAPTURE;
    }
    if (copied &&
        (raw->flags & TDMA_PIO_SPI_DATA_TRAIN_FLAG_DATA_DMA_COMPLETE) != 0u) {
        flags |= CALIBRATION_TRAINING_SCK_FLAG_DMA_COMPLETE;
    }
    if (correlated && correlation.accepted != 0u &&
        correlation.marker_flags == CALIBRATION_CLK_MARKER_FLAG_ALL) {
        flags |= CALIBRATION_TRAINING_SCK_FLAG_CRC_VALID |
                 CALIBRATION_TRAINING_SCK_FLAG_EPOCH_VALID |
                 CALIBRATION_TRAINING_SCK_FLAG_SEQUENCE_VALID;
        if (correlation.detected_polarity ==
            s_sck_active_request.expected_polarity) {
            flags |= CALIBRATION_TRAINING_SCK_FLAG_POLARITY_VALID;
        }
    }
    const calibration_training_sck_evidence_t evidence = {
        .train_epoch = s_sck_active_request.train_epoch,
        .train_sequence = s_sck_active_request.train_sequence,
        .observed_crc32 = correlated && correlation.accepted != 0u
                              ? s_sck_active_request.sck_crc32
                              : 0u,
        .calibration_generation =
            s_sck_active_request.calibration_generation,
        .topology_generation = s_sck_active_request.topology_generation,
        .topology_crc32 = s_sck_active_request.topology_crc32,
        .profile_crc32 = s_sck_active_request.profile_crc32,
        .schedule_crc32 = s_sck_active_request.schedule_crc32,
        .flags = flags,
        .polarity = correlation.detected_polarity,
        .correlation_reject_reason = correlated
            ? correlation.reject_reason
            : CALIBRATION_CLK_CORRELATION_REJECT_BAD_ARGUMENT,
        .best_lag_sample = correlation.best_lag_sample,
        .best_distance = correlation.best_distance,
        .second_lag_sample = correlation.second_lag_sample,
        .second_distance = correlation.second_distance,
        .margin = correlation.margin,
        .captured_sample_count = copied ? raw->capture_sample_count : 0u,
        .expected_sample_count = s_sck_workspace.marker.raw_samples,
        .dma_overrun_count = raw->dma_overrun_count,
        .pio_stall_count = raw->pio_stall_count,
        .timeout_count = raw->timeout_count,
        .sck_capture_origin_tick = raw->marker_capture_tick,
        .sck_code_capture_tick = raw->data_capture_tick +
                                 correlation.best_lag_sample,
    };
    (void)calibration_training_sck_evaluate_core1(
        &s_sck_store, &s_sck_active_request, &evidence);
    __atomic_store_n(&s_sck_capture_word_count,
                     copied ? (uint32_t)capture_words : 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_sck_capture_sample_count,
                     copied ? raw->capture_sample_count : 0u,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_sck_active, false, __ATOMIC_RELEASE);
}

bool calibration_manager_request_ring_capture(
    uint32_t calibration_generation,
    uint32_t capture_epoch)
{
    if (calibration_generation == 0u || capture_epoch == 0u) {
        return false;
    }
    /* Latest request wins. A diagnostic capture must not become permanently
     * wedged when a previous core0 publication was not consumed (for example
     * while core1 was parked). The sequence lets core1 distinguish retries;
     * generation/epoch still protect SAVE from accepting stale evidence. */
    (void)__atomic_add_fetch(&s_ring_capture_intent.guard,
                             1u, __ATOMIC_ACQ_REL);
    s_ring_capture_intent.sequence = ++s_ring_capture_next_sequence;
    s_ring_capture_intent.calibration_generation = calibration_generation;
    s_ring_capture_intent.capture_epoch = capture_epoch;
    (void)__atomic_add_fetch(&s_ring_capture_intent.guard,
                             1u, __ATOMIC_RELEASE);

    calibration_ring_capture_snapshot_t pending;
    memset(&pending, 0, sizeof(pending));
    pending.state = CALIBRATION_RING_CAPTURE_PENDING;
    pending.sequence = s_ring_capture_next_sequence;
    pending.calibration_generation = calibration_generation;
    pending.capture_epoch = capture_epoch;
    calibration_manager_ring_capture_publish(&pending);
    return true;
}

bool calibration_manager_get_ring_capture_snapshot(
    calibration_ring_capture_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin = __atomic_load_n(
            &s_ring_capture_snapshot_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = s_ring_capture_snapshot;
        const uint32_t end = __atomic_load_n(
            &s_ring_capture_snapshot_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

void calibration_manager_get_ring_capture_debug(
    calibration_ring_capture_debug_t *debug)
{
    if (debug == NULL) return;
    debug->core1_service_count = __atomic_load_n(
        &s_ring_capture_core1_service_count, __ATOMIC_ACQUIRE);
    debug->intent_read_fail_count = __atomic_load_n(
        &s_ring_capture_intent_read_fail_count, __ATOMIC_ACQUIRE);
    debug->last_seen_sequence = __atomic_load_n(
        &s_ring_capture_last_seen_sequence, __ATOMIC_ACQUIRE);
    debug->copy_attempt_count = __atomic_load_n(
        &s_ring_capture_copy_attempt_count, __ATOMIC_ACQUIRE);
    debug->copy_fail_count = __atomic_load_n(
        &s_ring_capture_copy_fail_count, __ATOMIC_ACQUIRE);
    debug->consumed_sequence = __atomic_load_n(
        &s_ring_capture_consumed_sequence, __ATOMIC_ACQUIRE);
}

void calibration_manager_service_core1(void)
{
    (void)__atomic_add_fetch(&s_ring_capture_core1_service_count,
                             1u, __ATOMIC_RELAXED);
    tdma_ring_runtime_snapshot_t ring;
    const bool stopped = tdma_runtime_owner_get_ring_snapshot(&ring) &&
                         ring.enabled == 0u;
    calibration_ring_capture_intent_t ring_capture_intent;
    const bool ring_capture_intent_valid =
        calibration_manager_ring_capture_intent_read(&ring_capture_intent);
    if (!ring_capture_intent_valid) {
        (void)__atomic_add_fetch(&s_ring_capture_intent_read_fail_count,
                                 1u, __ATOMIC_RELAXED);
    } else {
        __atomic_store_n(&s_ring_capture_last_seen_sequence,
                         ring_capture_intent.sequence, __ATOMIC_RELEASE);
    }
    if (ring_capture_intent_valid &&
        ring_capture_intent.sequence != __atomic_load_n(
            &s_ring_capture_consumed_sequence, __ATOMIC_ACQUIRE)) {
        calibration_ring_capture_snapshot_t captured;
        memset(&captured, 0, sizeof(captured));
        captured.state = CALIBRATION_RING_CAPTURE_REJECTED;
        captured.sequence = ring_capture_intent.sequence;
        captured.calibration_generation =
            ring_capture_intent.calibration_generation;
        captured.capture_epoch = ring_capture_intent.capture_epoch;
        tdma_ring_calibration_stage_t stage;
        bool complete = false;
        const bool capture_eligible = !stopped && ring.node_count >= 2u &&
            ring.node_count <= TDMA_RING_CALIBRATION_LINK_MAX &&
            ring.local_slot_id < ring.node_count &&
            tdma_runtime_owner_get_calibration_stage(&stage, &complete) &&
            complete && stage.calibration_generation ==
                ring_capture_intent.calibration_generation;
        bool copied = false;
        if (capture_eligible) {
            (void)__atomic_add_fetch(&s_ring_capture_copy_attempt_count,
                                     1u, __ATOMIC_RELAXED);
            copied = tdma_runtime_owner_copy_normal_capture_core1(
                s_ring_capture_rx, TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES,
                s_ring_capture_tx, TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES,
                &captured.physical);
            if (!copied) {
                (void)__atomic_add_fetch(&s_ring_capture_copy_fail_count,
                                         1u, __ATOMIC_RELAXED);
            }
        }
        if (capture_eligible && copied) {
            captured.node = ring.local_slot_id;
            captured.node_count = ring.node_count;
            captured.state = CALIBRATION_RING_CAPTURE_READY;
        }
        calibration_manager_ring_capture_publish(&captured);
        __atomic_store_n(&s_ring_capture_consumed_sequence,
                         ring_capture_intent.sequence, __ATOMIC_RELEASE);
    }
    calibration_pio_loopback_service_core1(stopped);
    tdma_runtime_owner_coded_service_core1();
    tdma_runtime_owner_p3_service_core1();
    tdma_runtime_owner_marker_service_core1();
    if (__atomic_load_n(&s_sck_active, __ATOMIC_ACQUIRE)) {
        tdma_runtime_owner_sck_train_service_core1();
    } else {
        tdma_runtime_owner_data_train_service_core1();
    }
    calibration_manager_publish_training_activity();

    calibration_data_intent_t data_intent;
    if (calibration_manager_data_read(&data_intent) &&
        data_intent.sequence != __atomic_load_n(
            &s_data_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        if (data_intent.opcode == CALIBRATION_DATA_INTENT_STOP) {
            tdma_runtime_owner_data_train_stop_core1();
            __atomic_store_n(&s_data_active, false, __ATOMIC_RELEASE);
            calibration_training_data_snapshot_t idle;
            memset(&idle, 0, sizeof(idle));
            idle.version = CALIBRATION_TRAINING_DATA_SNAPSHOT_VERSION;
            idle.state = CALIBRATION_TRAINING_DATA_IDLE;
            idle.flags = CALIBRATION_TRAINING_DATA_FLAG_DIAGNOSTIC_ONLY;
            (void)calibration_training_data_publish_core1(
                &s_data_store, &idle);
        } else if (data_intent.opcode == CALIBRATION_DATA_INTENT_ARM &&
                   stopped &&
                   !__atomic_load_n(&s_data_active, __ATOMIC_ACQUIRE)) {
            const calibration_clk_marker_config_t config = {
                .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
                .codebook_id =
                    (uint8_t)data_intent.request.data_codebook_id,
                .epoch = (uint8_t)data_intent.request.train_epoch,
                .source_node = (uint8_t)data_intent.request.source_node,
                .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
            };
            calibration_clk_marker_descriptor_t descriptor;
            if (calibration_clk_marker_build(
                    &config, s_data_workspace.expected_words,
                    CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &descriptor)) {
                tdma_service_ring_runtime_config_t staged;
                if (tdma_runtime_owner_get_staged_ring_config(&staged)) {
                    const bool local_initiator = staged.local_slot_id ==
                                                 data_intent.request.source_node;
                    uint32_t base_samples = 0u;
                    const bool base_valid =
                        calibration_training_phase_delay_samples(
                            data_intent.request.link_base_delay_ns,
                            data_intent.request.sample_period_ns, 0,
                            UINT32_MAX, &base_samples);
                    /* The DATA response traverses two physical links after
                     * the locally generated marker: CS source->responder,
                     * then DATA responder->initiator.  base_delay is one
                     * half-link baseline, so the complete round trip is
                     * 4 * base_samples.  Keep the dynamic phase within the
                     * PIO instruction's 5-bit delay field and place the
                     * additional 2 * base_samples in the initiator's PIO
                     * long-wait counter. */
                    const uint32_t initiator_wait_samples =
                        data_intent.request.marker_to_data_samples +
                        2u * base_samples;
                    uint32_t marker_phase = 0u;
                    uint32_t data_phase = 0u;
                    const bool phase_valid = base_valid &&
                        calibration_training_phase_delay_samples(
                            data_intent.request.link_base_delay_ns,
                            data_intent.request.sample_period_ns,
                            data_intent.request.marker_offset_sample_count,
                            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES,
                            &marker_phase) &&
                        calibration_training_phase_delay_samples(
                            data_intent.request.link_base_delay_ns,
                            data_intent.request.sample_period_ns,
                            data_intent.request.
                                configured_data_offset_sample_count,
                            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES,
                            &data_phase);
                    const int32_t initiator_capture_phase =
                        (int32_t)marker_phase + (int32_t)base_samples +
                        (int32_t)data_phase +
                        data_intent.request.search_start_offset_sample;
                    const uint32_t search_samples = (uint32_t)(
                        data_intent.request.search_end_offset_sample -
                        data_intent.request.search_start_offset_sample + 1);
                    const tdma_pio_spi_data_train_request_t raw_request = {
                        .role = local_initiator
                                    ? TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR
                                    : TDMA_PIO_SPI_DATA_TRAIN_ROLE_RESPONDER,
                        .marker_words = local_initiator
                                            ? s_data_workspace.expected_words
                                            : NULL,
                        .marker_word_count = local_initiator
                                                 ? descriptor.raw_words : 0u,
                        .data_words = local_initiator
                                          ? NULL
                                          : s_data_workspace.expected_words,
                        .data_word_count = local_initiator
                                               ? 0u : descriptor.raw_words,
                        .data_sample_count = descriptor.raw_samples,
                        .capture_sample_count = local_initiator
                                                    ? descriptor.raw_samples +
                                                          search_samples - 1u
                                                    : 0u,
                        .marker_to_data_delay_cycles =
                            local_initiator
                                ? initiator_wait_samples
                                : data_intent.request.marker_to_data_samples,
                        .source_phase_delay_cycles =
                            marker_phase,
                        .phase_delay_cycles = local_initiator
                                                  ? (uint32_t)
                                                        initiator_capture_phase
                                                  : (uint32_t)marker_phase,
                        .epoch = data_intent.request.train_epoch,
                    };
                    s_data_workspace.marker = descriptor;
                    if (phase_valid && initiator_capture_phase >= 0 &&
                        initiator_capture_phase <=
                            (int32_t)TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES &&
                        calibration_training_data_prepare_core1(
                            &s_data_store, &data_intent.request) &&
                        tdma_runtime_owner_data_train_arm_core1(
                            &raw_request)) {
                        s_data_active_request = data_intent.request;
                        __atomic_store_n(&s_data_active, true,
                                         __ATOMIC_RELEASE);
                    }
                }
            }
        } else if (data_intent.opcode == CALIBRATION_DATA_INTENT_INJECT &&
                   stopped &&
                   __atomic_load_n(&s_data_active, __ATOMIC_ACQUIRE) &&
                   tdma_runtime_owner_data_train_inject_core1()) {
            calibration_training_data_snapshot_t running;
            if (calibration_training_data_get_snapshot(
                    &s_data_store, &running)) {
                running.state = CALIBRATION_TRAINING_DATA_RUNNING;
                (void)calibration_training_data_publish_core1(
                    &s_data_store, &running);
            }
        }
        __atomic_store_n(&s_data_intent_consumed_sequence,
                         data_intent.sequence, __ATOMIC_RELEASE);
    }

    if (__atomic_load_n(&s_data_active, __ATOMIC_ACQUIRE)) {
        tdma_pio_spi_data_train_snapshot_t raw;
        if (tdma_runtime_owner_get_data_train_snapshot(&raw) &&
            (raw.state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE ||
             raw.state == TDMA_PIO_SPI_DATA_TRAIN_ERROR)) {
            calibration_manager_data_finish_core1(&raw);
        }
    }

    calibration_sck_intent_t sck_intent;
    if (calibration_manager_sck_read(&sck_intent) &&
        sck_intent.sequence != __atomic_load_n(
            &s_sck_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        if (sck_intent.opcode == CALIBRATION_SCK_INTENT_STOP) {
            tdma_runtime_owner_sck_train_stop_core1();
            __atomic_store_n(&s_sck_active, false, __ATOMIC_RELEASE);
            calibration_training_sck_snapshot_t idle;
            memset(&idle, 0, sizeof(idle));
            idle.version = CALIBRATION_TRAINING_SCK_SNAPSHOT_VERSION;
            idle.state = CALIBRATION_TRAINING_SCK_IDLE;
            idle.flags = CALIBRATION_TRAINING_SCK_FLAG_DIAGNOSTIC_ONLY;
            (void)calibration_training_sck_publish_core1(
                &s_sck_store, &idle);
        } else if (sck_intent.opcode == CALIBRATION_SCK_INTENT_ARM &&
                   stopped &&
                   !__atomic_load_n(&s_sck_active, __ATOMIC_ACQUIRE) &&
                   !__atomic_load_n(&s_data_active, __ATOMIC_ACQUIRE)) {
            const calibration_clk_marker_config_t config = {
                .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
                .codebook_id =
                    (uint8_t)sck_intent.request.sck_codebook_id,
                .epoch = (uint8_t)sck_intent.request.train_epoch,
                .source_node = (uint8_t)sck_intent.request.source_node,
                .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
            };
            calibration_clk_marker_descriptor_t descriptor;
            if (calibration_clk_marker_build(
                    &config, s_sck_workspace.expected_words,
                    CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &descriptor)) {
                tdma_service_ring_runtime_config_t staged;
                if (tdma_runtime_owner_get_staged_ring_config(&staged)) {
                    const bool local_source = staged.local_slot_id ==
                                              sck_intent.request.source_node;
                    uint32_t source_phase = 0u;
                    uint32_t destination_phase = 0u;
                    uint32_t link_base_delay_samples = 0u;
                    const bool base_valid =
                        calibration_training_phase_delay_samples(
                            sck_intent.request.link_base_delay_ns,
                            sck_intent.request.sample_period_ns, 0,
                            UINT32_MAX, &link_base_delay_samples);
                    const int32_t nominal_lag =
                        (int32_t)sck_intent.request.
                            sck_launch_guard_sample_count -
                        (int32_t)link_base_delay_samples;
                    const uint32_t max_capture_lag = (uint32_t)(
                        nominal_lag +
                        sck_intent.request.search_end_offset_sample + 4);
                    if (base_valid && nominal_lag >= 0 &&
                        calibration_training_sck_map_offset_to_phase_cycles(
                            sck_intent.request.link_base_delay_ns,
                            sck_intent.request.sample_period_ns,
                            sck_intent.request.
                                configured_sck_offset_sample_count,
                            &source_phase, &destination_phase) &&
                        source_phase <=
                            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES &&
                        destination_phase <=
                            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES) {
                        const tdma_pio_spi_data_train_request_t raw_request = {
                            .role = local_source
                                ? TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE
                                : TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION,
                            .marker_words = NULL,
                            .marker_word_count = 0u,
                            .data_words = local_source
                                ? s_sck_workspace.expected_words : NULL,
                            .data_word_count = local_source
                                ? descriptor.raw_words : 0u,
                            .data_sample_count = descriptor.raw_samples,
                            .capture_sample_count = local_source
                                ? 0u
                                : descriptor.raw_samples +
                                      max_capture_lag,
                            .marker_to_data_delay_cycles =
                                sck_intent.request.
                                    sck_launch_guard_sample_count,
                            .source_phase_delay_cycles =
                                source_phase,
                            .phase_delay_cycles = local_source
                                ? source_phase
                                : destination_phase,
                            .epoch = sck_intent.request.train_epoch,
                        };
                        s_sck_workspace.marker = descriptor;
                        if (tdma_runtime_owner_sck_train_arm_core1(
                                &raw_request) &&
                            calibration_training_sck_prepare_core1(
                                &s_sck_store, &sck_intent.request)) {
                            s_sck_active_request = sck_intent.request;
                            __atomic_store_n(&s_sck_active, true,
                                             __ATOMIC_RELEASE);
                        }
                    }
                }
            }
        } else if (sck_intent.opcode == CALIBRATION_SCK_INTENT_INJECT) {
            const bool active =
                __atomic_load_n(&s_sck_active, __ATOMIC_ACQUIRE);
            const bool injected = stopped && active &&
                tdma_runtime_owner_sck_train_inject_core1();
            calibration_training_sck_snapshot_t running;
            if (!calibration_training_sck_get_snapshot(
                    &s_sck_store, &running)) {
                (void)calibration_training_sck_prepare_core1(
                    &s_sck_store, &s_sck_active_request);
                (void)calibration_training_sck_get_snapshot(
                    &s_sck_store, &running);
            }
            if (injected) {
                running.state = CALIBRATION_TRAINING_SCK_RUNNING;
            } else {
                running.state = CALIBRATION_TRAINING_SCK_REJECTED;
                running.reject_reason =
                    CALIBRATION_TRAINING_SCK_REJECT_BAD_ARGUMENT;
                running.timeout_count = active ? 0u : 1u;
                __atomic_store_n(&s_sck_active, false, __ATOMIC_RELEASE);
            }
            (void)calibration_training_sck_publish_core1(
                &s_sck_store, &running);
        }
        __atomic_store_n(&s_sck_intent_consumed_sequence,
                         sck_intent.sequence, __ATOMIC_RELEASE);
    }

    if (__atomic_load_n(&s_sck_active, __ATOMIC_ACQUIRE)) {
        tdma_pio_spi_data_train_snapshot_t raw;
        if (tdma_runtime_owner_get_sck_train_snapshot(&raw) &&
            (raw.state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE ||
             raw.state == TDMA_PIO_SPI_DATA_TRAIN_ERROR)) {
            calibration_manager_sck_finish_core1(&raw);
        }
    }

    calibration_marker_intent_t marker_intent;
    if (calibration_manager_marker_read(&marker_intent) &&
        marker_intent.sequence != __atomic_load_n(
            &s_marker_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        if (marker_intent.opcode == CALIBRATION_MARKER_INTENT_STOP) {
            tdma_runtime_owner_marker_stop_core1();
            __atomic_store_n(&s_marker_active, false, __ATOMIC_RELEASE);
            calibration_training_marker_snapshot_t idle;
            memset(&idle, 0, sizeof(idle));
            idle.version = CALIBRATION_TRAINING_MARKER_SNAPSHOT_VERSION;
            idle.state = CALIBRATION_TRAINING_MARKER_IDLE;
            idle.flags = CALIBRATION_TRAINING_MARKER_FLAG_DIAGNOSTIC_ONLY;
            (void)calibration_training_marker_publish_core1(
                &s_marker_store, &idle);
        } else if (marker_intent.opcode == CALIBRATION_MARKER_INTENT_ARM &&
                   stopped &&
                   !__atomic_load_n(&s_marker_active, __ATOMIC_ACQUIRE)) {
            const calibration_clk_marker_config_t marker_config = {
                .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
                .codebook_id = (uint8_t)
                    marker_intent.request.marker_codebook_id,
                .epoch = (uint8_t)marker_intent.request.train_epoch,
                .source_node = (uint8_t)marker_intent.request.reference_node,
                .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
            };
            calibration_clk_marker_descriptor_t descriptor;
            uint32_t capture_phase_delay_cycles = 0u;
            if (calibration_clk_marker_build(
                    &marker_config, s_marker_workspace.expected_words,
                    CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &descriptor) &&
                calibration_training_marker_capture_delay_cycles(
                    marker_intent.request.link_base_delay_ns,
                    marker_intent.request.tick_resolution_ns,
                    marker_intent.request.offset_sample_count,
                    &capture_phase_delay_cycles) &&
                calibration_training_marker_prepare_core1(
                    &s_marker_store, &marker_intent.request)) {
                s_marker_workspace.marker = descriptor;
                const tdma_pio_spi_marker_request_t raw_request = {
                    .role = marker_intent.request.role ==
                                    CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR
                                ? TDMA_PIO_SPI_MARKER_ROLE_ORIGINATOR
                                : TDMA_PIO_SPI_MARKER_ROLE_FOLLOWER,
                    .tx_words = s_marker_workspace.expected_words,
                    .tx_word_count = descriptor.raw_words,
                    .marker_sample_count = descriptor.raw_samples,
                    .capture_sample_count = descriptor.raw_samples +
                        (marker_intent.request.role ==
                                 CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR
                             ? TDMA_PIO_SPI_MARKER_RETURN_GUARD_SAMPLES
                             : 8u),
                    .epoch = marker_intent.request.train_epoch,
                    .offset_sample_count =
                        marker_intent.request.offset_sample_count,
                    .capture_phase_delay_cycles = capture_phase_delay_cycles,
                };
                if (tdma_runtime_owner_marker_arm_core1(&raw_request)) {
                    s_marker_active_request = marker_intent.request;
                    __atomic_store_n(&s_marker_active, true,
                                     __ATOMIC_RELEASE);
                    calibration_training_marker_snapshot_t prepared;
                    if (calibration_training_marker_get_snapshot(
                            &s_marker_store, &prepared)) {
                        prepared.state = CALIBRATION_TRAINING_MARKER_PREPARED;
                        (void)calibration_training_marker_publish_core1(
                            &s_marker_store, &prepared);
                    }
                }
            }
        } else if (marker_intent.opcode ==
                       CALIBRATION_MARKER_INTENT_INJECT &&
                   stopped &&
                   __atomic_load_n(&s_marker_active, __ATOMIC_ACQUIRE) &&
                   s_marker_active_request.role ==
                       CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR &&
                   tdma_runtime_owner_marker_inject_core1()) {
            calibration_training_marker_snapshot_t running;
            if (calibration_training_marker_get_snapshot(
                    &s_marker_store, &running)) {
                running.state = CALIBRATION_TRAINING_MARKER_RUNNING;
                (void)calibration_training_marker_publish_core1(
                    &s_marker_store, &running);
            }
        }
        __atomic_store_n(&s_marker_intent_consumed_sequence,
                         marker_intent.sequence, __ATOMIC_RELEASE);
    }

    if (__atomic_load_n(&s_marker_active, __ATOMIC_ACQUIRE)) {
        tdma_pio_spi_marker_snapshot_t raw;
        if (tdma_runtime_owner_get_marker_snapshot(&raw) &&
            (raw.state == TDMA_PIO_SPI_MARKER_COMPLETE ||
             raw.state == TDMA_PIO_SPI_MARKER_ERROR)) {
            calibration_manager_marker_finish_core1(&raw);
        }
    }

    calibration_p3_intent_t p3_intent;
    if (calibration_manager_p3_read(&p3_intent) &&
        p3_intent.sequence != __atomic_load_n(
            &s_p3_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        if (p3_intent.opcode == CALIBRATION_P3_INTENT_STOP) {
            tdma_runtime_owner_p3_stop_core1();
        } else if (p3_intent.opcode == CALIBRATION_P3_INTENT_START &&
                   stopped) {
            (void)tdma_runtime_owner_p3_start_core1(&p3_intent.request);
        }
        __atomic_store_n(&s_p3_intent_consumed_sequence,
                         p3_intent.sequence, __ATOMIC_RELEASE);
    }

    calibration_clk_coded_intent_t intent;
    if (calibration_manager_clk_coded_intent_read(&intent) &&
        intent.sequence != __atomic_load_n(
            &s_clk_coded_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        if (intent.opcode == CALIBRATION_CLK_CODED_INTENT_STOP) {
            tdma_runtime_owner_coded_stop_core1();
            __atomic_store_n(&s_clk_coded_active, false, __ATOMIC_RELEASE);
            (void)calibration_clk_coded_stop_core1(&s_clk_coded_store);
        } else if (intent.opcode == CALIBRATION_CLK_CODED_INTENT_START &&
                   !stopped) {
            (void)calibration_clk_coded_reject_request_core1(
                &s_clk_coded_store, &intent.request,
                CALIBRATION_CLK_CODED_REJECT_BAD_STATE);
        } else if (intent.opcode == CALIBRATION_CLK_CODED_INTENT_START &&
                   !__atomic_load_n(&s_clk_coded_active, __ATOMIC_ACQUIRE) &&
                   calibration_clk_coded_begin_coarse_core1(
                       &s_clk_coded_store, &intent.request)) {
            const calibration_clk_marker_config_t marker_config = {
                .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
                .codebook_id = (uint8_t)intent.request.codebook_id,
                .epoch = (uint8_t)intent.request.train_epoch,
                .source_node = (uint8_t)intent.request.local_node,
                .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
            };
            calibration_clk_marker_descriptor_t marker;
            if (calibration_clk_marker_build(
                    &marker_config, s_clk_coded_workspace.expected_words,
                    CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &marker) &&
                marker.raw_samples <=
                    UINT32_MAX - intent.request.coarse_max_sample) {
                const uint32_t capture_samples =
                    marker.raw_samples + intent.request.coarse_max_sample;
                const tdma_pio_spi_coded_request_t raw_request = {
                    .tx_words = s_clk_coded_workspace.expected_words,
                    .tx_word_count = marker.raw_words,
                    .tx_sample_count = marker.raw_samples,
                    .capture_sample_count = capture_samples,
                    .timing_field_tx_origin_sample =
                        marker.timing_origin_sample,
                    .epoch = intent.request.train_epoch,
                };
                if (tdma_runtime_owner_coded_start_core1(&raw_request)) {
                    s_clk_coded_workspace.marker = marker;
                    s_clk_coded_active_request = intent.request;
                    s_clk_coded_active_gate = intent.gate;
                    __atomic_store_n(&s_clk_coded_active, true,
                                     __ATOMIC_RELEASE);
                } else {
                    (void)calibration_clk_coded_reject_request_core1(
                        &s_clk_coded_store, &intent.request,
                        CALIBRATION_CLK_CODED_REJECT_DMA);
                }
            } else {
                (void)calibration_clk_coded_reject_request_core1(
                    &s_clk_coded_store, &intent.request,
                    CALIBRATION_CLK_CODED_REJECT_BAD_ARGUMENT);
            }
        }
        __atomic_store_n(&s_clk_coded_intent_consumed_sequence,
                         intent.sequence, __ATOMIC_RELEASE);
    }

    if (__atomic_load_n(&s_clk_coded_active, __ATOMIC_ACQUIRE)) {
        tdma_pio_spi_coded_snapshot_t raw;
        if (tdma_runtime_owner_get_coded_snapshot(&raw) &&
            (raw.state == TDMA_PIO_SPI_CODED_COMPLETE ||
             raw.state == TDMA_PIO_SPI_CODED_ERROR)) {
            size_t capture_words = 0u;
            memset(s_clk_coded_capture, 0, sizeof(s_clk_coded_capture));
            const bool copied = raw.state == TDMA_PIO_SPI_CODED_COMPLETE &&
                tdma_runtime_owner_copy_coded_capture_core1(
                    s_clk_coded_capture, TDMA_PIO_SPI_CODED_BUFFER_WORDS,
                    &capture_words);
            uint32_t flags = 0u;
            if ((raw.flags & TDMA_PIO_SPI_CODED_FLAG_TX_DMA_COMPLETE) != 0u) {
                flags |= CALIBRATION_CLK_CODED_FLAG_TX_DMA_COMPLETE;
            }
            if (copied &&
                (raw.flags & TDMA_PIO_SPI_CODED_FLAG_RX_DMA_COMPLETE) != 0u) {
                flags |= CALIBRATION_CLK_CODED_FLAG_RX_DMA_COMPLETE;
            }
            const calibration_clk_coded_evidence_t evidence = {
                .capture_words = s_clk_coded_capture,
                .capture_sample_count = raw.capture_sample_count,
                .capture_origin_tick = raw.capture_origin_tick,
                .timing_field_tx_origin_sample =
                    raw.timing_field_tx_origin_sample,
                .tx_dma_count = raw.tx_word_count - raw.tx_dma_remaining,
                .rx_dma_count = copied ? (uint32_t)capture_words : 0u,
                .dma_overrun_count = raw.dma_overrun_count,
                .pio_stall_count = raw.pio_stall_count,
                .train_epoch = s_clk_coded_active_request.train_epoch,
                .train_sequence = s_clk_coded_active_request.train_sequence,
                .topology_generation =
                    s_clk_coded_active_request.topology_generation,
                .topology_crc32 =
                    s_clk_coded_active_request.topology_crc32,
                .profile_crc32 = s_clk_coded_active_request.profile_crc32,
                .schedule_crc32 = s_clk_coded_active_request.schedule_crc32,
                .flags = flags,
            };
            (void)calibration_clk_coded_process_core1(
                &s_clk_coded_store, &s_clk_coded_workspace, &evidence,
                &s_clk_coded_active_gate);
            __atomic_store_n(&s_clk_coded_active, false, __ATOMIC_RELEASE);
        }
    }
    tdma_pio_spi_p3_snapshot_t p3;
    if (tdma_runtime_owner_get_p3_snapshot(&p3)) {
        osal_critical_enter();
        s_p3_snapshot.raw = p3;
        s_p3_snapshot.result_valid =
            p3.state == TDMA_PIO_SPI_P3_COMPLETE ? 1u : 0u;
        osal_critical_exit();
    }
    calibration_manager_publish_training_activity();
}

bool calibration_manager_get_loopback_snapshot(
    calibration_manager_loopback_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    calibration_pio_loopback_snapshot_t raw;
    if (!calibration_pio_loopback_get_snapshot(&raw)) return false;
    osal_critical_enter();
    *snapshot = s_loopback_snapshot;
    osal_critical_exit();
    snapshot->raw = raw;
    return true;
}

bool calibration_manager_get_clk_coded_snapshot(
    calibration_clk_coded_snapshot_t *snapshot)
{
    return calibration_clk_coded_get_snapshot(&s_clk_coded_store, snapshot);
}

bool calibration_manager_begin_training_stage(
    uint32_t node_count,
    uint32_t evidence_flags,
    uint32_t calibration_generation,
    uint32_t topology_generation,
    uint32_t topology_crc32,
    uint32_t profile_crc32,
    uint32_t schedule_crc32)
{
    const tdma_ring_calibration_stage_t header = {
        .enabled = 1u,
        .node_count = node_count,
        .evidence_flags = evidence_flags,
        .calibration_generation = calibration_generation,
        .topology_generation = topology_generation,
        .topology_crc32 = topology_crc32,
        .profile_crc32 = profile_crc32,
        .schedule_crc32 = schedule_crc32,
    };
    return tdma_runtime_owner_begin_calibration_stage(&header);
}

bool calibration_manager_stage_training_link(
    uint32_t link_index,
    uint32_t evidence_flags,
    uint32_t pio_persona,
    uint32_t clkdiv_q16,
    uint32_t clk_sys_hz,
    uint32_t instruction_period_ns,
    uint32_t bit_cycles,
    uint32_t marker_to_data_cycles,
    uint32_t forward_residence_cycles,
    uint32_t rx_arm_lead_cycles,
    uint32_t codeword_cycles,
    uint32_t guard_cycles,
    uint32_t link_budget_cycles,
    uint32_t loop_delay_cycles,
    int32_t marker_offset_sample_count,
    int32_t sck_offset_sample_count,
    int32_t data_offset_sample_count,
    uint32_t sample_period_ns,
    uint32_t link_base_delay_ns,
    uint32_t marker_phase_delay_cycles,
    uint32_t sck_phase_delay_cycles,
    uint32_t data_phase_delay_cycles)
{
    tdma_ring_calibration_stage_t stage;
    bool complete = false;
    if (!tdma_runtime_owner_get_calibration_stage(&stage, &complete) ||
        link_index >= stage.node_count) {
        return false;
    }
    (void)complete;
    const tdma_ring_calibration_link_t link = {
        .valid = 1u,
        .link_index = link_index,
        .evidence_flags = evidence_flags,
        .calibration_generation = stage.calibration_generation,
        .topology_generation = stage.topology_generation,
        .topology_crc32 = stage.topology_crc32,
        .profile_crc32 = stage.profile_crc32,
        .schedule_crc32 = stage.schedule_crc32,
        .pio_persona = pio_persona,
        .clkdiv_q16 = clkdiv_q16,
        .clk_sys_hz = clk_sys_hz,
        .instruction_period_ns = instruction_period_ns,
        .bit_cycles = bit_cycles,
        .marker_to_data_cycles = marker_to_data_cycles,
        .forward_residence_cycles = forward_residence_cycles,
        .rx_arm_lead_cycles = rx_arm_lead_cycles,
        .codeword_cycles = codeword_cycles,
        .guard_cycles = guard_cycles,
        .link_budget_cycles = link_budget_cycles,
        .loop_delay_cycles = loop_delay_cycles,
        .marker_offset_sample_count = marker_offset_sample_count,
        .sck_offset_sample_count = sck_offset_sample_count,
        .data_offset_sample_count = data_offset_sample_count,
        .sample_period_ns = sample_period_ns,
        .link_base_delay_ns = link_base_delay_ns,
        .marker_phase_delay_cycles = marker_phase_delay_cycles,
        .sck_phase_delay_cycles = sck_phase_delay_cycles,
        .data_phase_delay_cycles = data_phase_delay_cycles,
    };
    return tdma_runtime_owner_stage_calibration_link(&link);
}

bool calibration_manager_get_training_stage(
    tdma_ring_calibration_stage_t *stage,
    bool *complete)
{
    return tdma_runtime_owner_get_calibration_stage(stage, complete);
}

bool calibration_manager_clear_training_stage(void)
{
    return tdma_runtime_owner_clear_calibration_stage();
}

bool calibration_manager_request_p3(
    uint32_t role, uint32_t baud_hz, uint32_t pulse_count,
    uint32_t capture_words, uint32_t epoch, uint32_t signal_group)
{
    calibration_p3_intent_t pending;
    tdma_pio_spi_p3_snapshot_t raw;
    if ((role != TDMA_PIO_SPI_P3_ROLE_INITIATOR &&
         role != TDMA_PIO_SPI_P3_ROLE_RESPONDER) ||
        signal_group > TDMA_PIO_SPI_P3_GROUP_CS_DATA ||
        !calibration_manager_p3_read(&pending) ||
        pending.sequence != __atomic_load_n(
            &s_p3_intent_consumed_sequence, __ATOMIC_ACQUIRE) ||
        !tdma_runtime_owner_get_p3_snapshot(&raw) ||
        raw.state == TDMA_PIO_SPI_P3_ARMED) {
        return false;
    }
    const tdma_pio_spi_p3_request_t request = {
        .role = role,
        .baud_hz = baud_hz,
        .pulse_count = pulse_count,
        .capture_words = capture_words,
        .epoch = epoch,
        .signal_group = signal_group,
    };
    calibration_manager_p3_publish(CALIBRATION_P3_INTENT_START, &request);
    resource_arbiter_publish_calibration_training(true);
    return true;
}

void calibration_manager_stop_p3(void)
{
    calibration_manager_p3_publish(CALIBRATION_P3_INTENT_STOP, NULL);
}

bool calibration_manager_get_p3_snapshot(
    calibration_manager_p3_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    osal_critical_enter();
    *snapshot = s_p3_snapshot;
    osal_critical_exit();

    (void)tdma_runtime_owner_get_p3_snapshot(&snapshot->raw);
    snapshot->result_valid =
        snapshot->raw.state == TDMA_PIO_SPI_P3_COMPLETE ? 1u : 0u;
    return true;
}

bool calibration_manager_request_marker_training(
    uint32_t codebook_id,
    uint32_t train_epoch,
    uint32_t train_sequence,
    uint32_t marker_id,
    uint32_t calibration_generation,
    uint32_t link_base_delay_ns,
    int32_t offset_sample_count,
    uint32_t origin_node)
{
    tdma_ring_runtime_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    calibration_marker_intent_t pending;
    uint64_t board_unique_id = 0u;
    if (codebook_id > CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32 ||
        train_epoch == 0u || train_epoch > UINT8_MAX ||
        train_sequence != train_epoch || marker_id != train_epoch ||
        offset_sample_count <
            CALIBRATION_TRAINING_MARKER_MIN_OFFSET_SAMPLES ||
        offset_sample_count >
            CALIBRATION_TRAINING_MARKER_MAX_OFFSET_SAMPLES ||
        calibration_generation == 0u || link_base_delay_ns == 0u ||
        BOARD_SYS_CLOCK_HZ == 0u ||
        (UINT32_C(1000000000) % BOARD_SYS_CLOCK_HZ) != 0u ||
        !calibration_manager_parse_hex_u64(board_identity_serial(),
                                           &board_unique_id) ||
        !tdma_runtime_owner_get_ring_snapshot(&ring) ||
        ring.enabled != 0u ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        staged.node_count < 2u ||
        staged.node_count > CALIBRATION_TRAINING_MARKER_MAX_NODES ||
        staged.local_slot_id >= staged.node_count ||
        staged.reference_slot_id >= staged.node_count ||
        staged.ring_profile_crc32 == 0u ||
        staged.operating_profile_crc32 == 0u ||
        staged.schedule_crc32 == 0u ||
        __atomic_load_n(&s_marker_active, __ATOMIC_ACQUIRE) ||
        !calibration_manager_marker_read(&pending) ||
        pending.sequence != __atomic_load_n(
            &s_marker_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        return false;
    }

    if (origin_node == UINT32_MAX) {
        origin_node = staged.reference_slot_id;
    }
    if (origin_node >= staged.node_count) {
        return false;
    }

    const uint32_t sample_period_ns =
        UINT32_C(1000000000) / BOARD_SYS_CLOCK_HZ;
    uint32_t capture_delay_cycles = 0u;
    if (!calibration_training_marker_capture_delay_cycles(
            link_base_delay_ns, sample_period_ns, offset_sample_count,
            &capture_delay_cycles)) {
        return false;
    }

    uint32_t marker_words[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    calibration_clk_marker_descriptor_t marker;
    const calibration_clk_marker_config_t marker_config = {
        .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        .codebook_id = (uint8_t)codebook_id,
        .epoch = (uint8_t)train_epoch,
        .source_node = (uint8_t)origin_node,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    if (!calibration_clk_marker_build(
            &marker_config, marker_words,
            CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &marker)) {
        return false;
    }
    const calibration_training_marker_request_t request = {
        .board_unique_id = board_unique_id,
        .build_id = calibration_manager_build_id_value(g_project_build_id),
        .role = staged.local_slot_id == origin_node
                    ? CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR
                    : CALIBRATION_TRAINING_MARKER_ROLE_FOLLOWER,
        /* Explicit TDMA slot -> training node boundary mapping. */
        .local_node = staged.local_slot_id,
        /* Marker origin is independent from the TDMA topology reference.
         * This permits every physical node's follower residence to be
         * measured without changing the bound topology identity. */
        .reference_node = origin_node,
        .predecessor_node = (staged.local_slot_id + staged.node_count - 1u) %
                            staged.node_count,
        .successor_node = (staged.local_slot_id + 1u) % staged.node_count,
        .train_epoch = train_epoch,
        .train_sequence = train_sequence,
        .marker_id = marker_id,
        .marker_codebook_id = codebook_id,
        .marker_crc32 = ota_crc32_compute(
            (const uint8_t *)marker_words,
            marker.raw_words * sizeof(marker_words[0])),
        .calibration_generation = calibration_generation,
        .topology_generation = ring.config_seq,
        .topology_crc32 = staged.ring_profile_crc32,
        .profile_crc32 = staged.operating_profile_crc32,
        .schedule_crc32 = staged.schedule_crc32,
        .tick_resolution_ns = sample_period_ns,
        .link_base_delay_ns = link_base_delay_ns,
        .offset_sample_count = offset_sample_count,
    };
    calibration_manager_marker_publish(CALIBRATION_MARKER_INTENT_ARM,
                                       &request);
    resource_arbiter_publish_calibration_training(true);
    return true;
}

bool calibration_manager_inject_marker_training(void)
{
    calibration_marker_intent_t pending;
    if (!__atomic_load_n(&s_marker_active, __ATOMIC_ACQUIRE) ||
        s_marker_active_request.role !=
            CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR ||
        !calibration_manager_marker_read(&pending) ||
        pending.sequence != __atomic_load_n(
            &s_marker_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        return false;
    }
    calibration_manager_marker_publish(CALIBRATION_MARKER_INTENT_INJECT,
                                       NULL);
    return true;
}

void calibration_manager_stop_marker_training(void)
{
    calibration_manager_marker_publish(CALIBRATION_MARKER_INTENT_STOP, NULL);
}

bool calibration_manager_get_marker_training_snapshot(
    calibration_training_marker_snapshot_t *snapshot)
{
    return calibration_training_marker_get_snapshot(&s_marker_store, snapshot);
}

bool calibration_manager_request_data_training(
    uint32_t source_node,
    uint32_t destination_node,
    uint32_t codebook_id,
    uint32_t train_epoch,
    uint32_t train_sequence,
    uint32_t calibration_generation,
    uint32_t marker_to_data_samples,
    uint32_t link_base_delay_ns,
    int32_t marker_offset_sample_count,
    int32_t configured_data_offset_sample_count,
    int32_t search_start_offset_sample,
    int32_t search_end_offset_sample,
    uint32_t guard_sample_count,
    uint32_t max_best_distance,
    uint32_t min_margin)
{
    tdma_ring_runtime_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    calibration_data_intent_t pending;
    uint64_t board_unique_id = 0u;
    if (codebook_id > CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32 ||
        train_epoch == 0u || train_epoch > UINT8_MAX ||
        train_sequence == 0u || calibration_generation == 0u ||
        marker_to_data_samples == 0u ||
        marker_to_data_samples > TDMA_PIO_SPI_DATA_TRAIN_MAX_DELAY_CYCLES ||
        link_base_delay_ns == 0u || BOARD_SYS_CLOCK_HZ == 0u ||
        (UINT32_C(1000000000) % BOARD_SYS_CLOCK_HZ) != 0u ||
        search_start_offset_sample <
            CALIBRATION_TRAINING_DATA_MIN_OFFSET_SAMPLES ||
        search_end_offset_sample >
            CALIBRATION_TRAINING_DATA_MAX_OFFSET_SAMPLES ||
        configured_data_offset_sample_count <
            CALIBRATION_TRAINING_DATA_MIN_OFFSET_SAMPLES ||
        configured_data_offset_sample_count >
            CALIBRATION_TRAINING_DATA_MAX_OFFSET_SAMPLES ||
        search_start_offset_sample > search_end_offset_sample ||
        guard_sample_count > CALIBRATION_TRAINING_DATA_MAX_GUARD_SAMPLES ||
        !calibration_manager_parse_hex_u64(board_identity_serial(),
                                           &board_unique_id) ||
        !tdma_runtime_owner_get_ring_snapshot(&ring) || ring.enabled != 0u ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        staged.node_count < 2u ||
        staged.node_count > CALIBRATION_TRAINING_DATA_MAX_NODES ||
        source_node >= staged.node_count ||
        destination_node >= staged.node_count ||
        (destination_node != (source_node + 1u) % staged.node_count &&
         source_node != (destination_node + 1u) % staged.node_count) ||
        (staged.local_slot_id != source_node &&
         staged.local_slot_id != destination_node) ||
        staged.ring_profile_crc32 == 0u ||
        staged.operating_profile_crc32 == 0u ||
        staged.schedule_crc32 == 0u ||
        __atomic_load_n(&s_data_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_sck_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_marker_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_clk_coded_active, __ATOMIC_ACQUIRE) ||
        !calibration_manager_data_read(&pending) ||
        pending.sequence != __atomic_load_n(
            &s_data_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        return false;
    }
    const uint32_t sample_period_ns =
        UINT32_C(1000000000) / BOARD_SYS_CLOCK_HZ;
    uint32_t base_samples = 0u;
    if (!calibration_training_phase_delay_samples(
            link_base_delay_ns, sample_period_ns, 0, UINT32_MAX,
            &base_samples)) {
        return false;
    }
    if ((uint64_t)marker_to_data_samples + 2ull * base_samples >
        TDMA_PIO_SPI_DATA_TRAIN_MAX_DELAY_CYCLES) {
        return false;
    }

    uint32_t data_words[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    calibration_clk_marker_descriptor_t descriptor;
    const calibration_clk_marker_config_t config = {
        .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        .codebook_id = (uint8_t)codebook_id,
        .epoch = (uint8_t)train_epoch,
        .source_node = (uint8_t)source_node,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    if (!calibration_clk_marker_build(
            &config, data_words, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS,
            &descriptor)) {
        return false;
    }
    uint32_t marker_phase = 0u;
    uint32_t data_phase = 0u;
    if (!calibration_training_phase_delay_samples(
            link_base_delay_ns, sample_period_ns,
            marker_offset_sample_count,
            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES, &marker_phase) ||
        !calibration_training_phase_delay_samples(
            link_base_delay_ns, sample_period_ns,
            configured_data_offset_sample_count,
            TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES, &data_phase)) {
        return false;
    }
    const int32_t initiator_capture_phase =
        (int32_t)marker_phase + (int32_t)base_samples +
        (int32_t)data_phase + search_start_offset_sample;
    if (marker_offset_sample_count <
            CALIBRATION_TRAINING_DATA_MIN_OFFSET_SAMPLES ||
        marker_offset_sample_count >
            CALIBRATION_TRAINING_DATA_MAX_OFFSET_SAMPLES ||
        marker_phase == 0u ||
        initiator_capture_phase < (int32_t)marker_phase ||
        initiator_capture_phase <= 0 ||
        initiator_capture_phase >
            (int32_t)TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES) {
        return false;
    }

    const calibration_training_data_request_t request = {
        .board_unique_id = board_unique_id,
        .build_id = calibration_manager_build_id_value(g_project_build_id),
        .source_node = source_node,
        .destination_node = destination_node,
        .train_epoch = train_epoch,
        .train_sequence = train_sequence,
        .data_codebook_id = codebook_id,
        .data_crc32 = ota_crc32_compute(
            (const uint8_t *)data_words,
            descriptor.raw_words * sizeof(data_words[0])),
        .calibration_generation = calibration_generation,
        .topology_generation = ring.config_seq,
        .topology_crc32 = staged.ring_profile_crc32,
        .profile_crc32 = staged.operating_profile_crc32,
        .schedule_crc32 = staged.schedule_crc32,
        .sample_period_ns = sample_period_ns,
        .marker_to_data_samples = marker_to_data_samples,
        .link_base_delay_ns = link_base_delay_ns,
        .marker_offset_sample_count = marker_offset_sample_count,
        .configured_data_offset_sample_count =
            configured_data_offset_sample_count,
        .search_start_offset_sample = search_start_offset_sample,
        .search_end_offset_sample = search_end_offset_sample,
        .guard_sample_count = guard_sample_count,
        .expected_polarity = CALIBRATION_CLK_POLARITY_NORMAL,
        .max_best_distance = max_best_distance,
        .min_margin = min_margin,
    };
    calibration_training_data_store_t validation_store;
    calibration_training_data_store_init(&validation_store);
    if (!calibration_training_data_prepare_core1(
            &validation_store, &request)) {
        return false;
    }
    calibration_manager_data_publish(CALIBRATION_DATA_INTENT_ARM, &request);
    resource_arbiter_publish_calibration_training(true);
    return true;
}

bool calibration_manager_inject_data_training(void)
{
    calibration_data_intent_t pending;
    tdma_service_ring_runtime_config_t staged;
    if (!__atomic_load_n(&s_data_active, __ATOMIC_ACQUIRE) ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        staged.local_slot_id != s_data_active_request.source_node ||
        !calibration_manager_data_read(&pending) ||
        pending.sequence != __atomic_load_n(
            &s_data_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        return false;
    }
    calibration_manager_data_publish(CALIBRATION_DATA_INTENT_INJECT, NULL);
    return true;
}

void calibration_manager_stop_data_training(void)
{
    calibration_manager_data_publish(CALIBRATION_DATA_INTENT_STOP, NULL);
}

bool calibration_manager_get_data_training_snapshot(
    calibration_training_data_snapshot_t *snapshot)
{
    return calibration_training_data_get_snapshot(&s_data_store, snapshot);
}

bool calibration_manager_request_sck_training(
    uint32_t source_node,
    uint32_t destination_node,
    uint32_t codebook_id,
    uint32_t train_epoch,
    uint32_t train_sequence,
    uint32_t calibration_generation,
    uint32_t sck_launch_guard_sample_count,
    uint32_t link_base_delay_ns,
    int32_t configured_sck_offset_sample_count,
    int32_t search_start_offset_sample,
    int32_t search_end_offset_sample,
    uint32_t guard_sample_count,
    uint32_t max_best_distance,
    uint32_t min_margin)
{
    tdma_ring_runtime_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    calibration_sck_intent_t pending;
    uint64_t board_unique_id = 0u;
    if (codebook_id > CALIBRATION_CLK_CODEBOOK_M255_MANCHESTER_32 ||
        train_epoch == 0u || train_epoch > UINT8_MAX ||
        train_sequence == 0u || calibration_generation == 0u ||
        sck_launch_guard_sample_count == 0u ||
        sck_launch_guard_sample_count >
            TDMA_PIO_SPI_DATA_TRAIN_MAX_DELAY_CYCLES ||
        link_base_delay_ns == 0u ||
        BOARD_SYS_CLOCK_HZ == 0u ||
        (UINT32_C(1000000000) % BOARD_SYS_CLOCK_HZ) != 0u ||
        configured_sck_offset_sample_count <
            CALIBRATION_TRAINING_SCK_MIN_OFFSET_SAMPLES ||
        configured_sck_offset_sample_count >
            CALIBRATION_TRAINING_SCK_MAX_OFFSET_SAMPLES ||
        search_start_offset_sample <
            CALIBRATION_TRAINING_SCK_MIN_OFFSET_SAMPLES ||
        search_end_offset_sample >
            CALIBRATION_TRAINING_SCK_MAX_OFFSET_SAMPLES ||
        search_start_offset_sample > search_end_offset_sample ||
        guard_sample_count > CALIBRATION_TRAINING_SCK_MAX_GUARD_SAMPLES ||
        !calibration_manager_parse_hex_u64(board_identity_serial(),
                                           &board_unique_id) ||
        !tdma_runtime_owner_get_ring_snapshot(&ring) || ring.enabled != 0u ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        staged.node_count < 2u ||
        staged.node_count > CALIBRATION_TRAINING_SCK_MAX_NODES ||
        source_node >= staged.node_count ||
        destination_node != (source_node + 1u) % staged.node_count ||
        (staged.local_slot_id != source_node &&
         staged.local_slot_id != destination_node) ||
        staged.ring_profile_crc32 == 0u ||
        staged.operating_profile_crc32 == 0u ||
        staged.schedule_crc32 == 0u ||
        __atomic_load_n(&s_sck_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_data_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_marker_active, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_clk_coded_active, __ATOMIC_ACQUIRE) ||
        !calibration_manager_sck_read(&pending) ||
        pending.sequence != __atomic_load_n(
            &s_sck_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        return false;
    }

    const uint32_t sample_period_ns =
        UINT32_C(1000000000) / BOARD_SYS_CLOCK_HZ;
    uint32_t link_base_delay_samples = 0u;
    if (!calibration_training_phase_delay_samples(
            link_base_delay_ns, sample_period_ns, 0, UINT32_MAX,
            &link_base_delay_samples) ||
        (int32_t)sck_launch_guard_sample_count -
                (int32_t)link_base_delay_samples +
                search_start_offset_sample < 0) {
        return false;
    }
    uint32_t sck_words[CALIBRATION_CLK_MARKER_MAX_RAW_WORDS];
    calibration_clk_marker_descriptor_t descriptor;
    const calibration_clk_marker_config_t config = {
        .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        .codebook_id = (uint8_t)codebook_id,
        .epoch = (uint8_t)train_epoch,
        .source_node = (uint8_t)source_node,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    if (!calibration_clk_marker_build(
            &config, sck_words, CALIBRATION_CLK_MARKER_MAX_RAW_WORDS,
            &descriptor)) {
        return false;
    }
    uint32_t source_phase = 0u;
    uint32_t destination_phase = 0u;
    if (!calibration_training_sck_map_offset_to_phase_cycles(
            link_base_delay_ns, sample_period_ns,
            configured_sck_offset_sample_count,
            &source_phase, &destination_phase) ||
        source_phase > TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES ||
        destination_phase > TDMA_PIO_SPI_DATA_TRAIN_MAX_PHASE_CYCLES) {
        return false;
    }

    const calibration_training_sck_request_t request = {
        .board_unique_id = board_unique_id,
        .build_id = calibration_manager_build_id_value(g_project_build_id),
        .source_node = source_node,
        .destination_node = destination_node,
        .train_epoch = train_epoch,
        .train_sequence = train_sequence,
        .sck_codebook_id = codebook_id,
        .sck_crc32 = ota_crc32_compute(
            (const uint8_t *)sck_words,
            descriptor.raw_words * sizeof(sck_words[0])),
        .calibration_generation = calibration_generation,
        .topology_generation = ring.config_seq,
        .topology_crc32 = staged.ring_profile_crc32,
        .profile_crc32 = staged.operating_profile_crc32,
        .schedule_crc32 = staged.schedule_crc32,
        .sample_period_ns = sample_period_ns,
        .sck_launch_guard_sample_count = sck_launch_guard_sample_count,
        .link_base_delay_ns = link_base_delay_ns,
        .configured_sck_offset_sample_count =
            configured_sck_offset_sample_count,
        .search_start_offset_sample = search_start_offset_sample,
        .search_end_offset_sample = search_end_offset_sample,
        .guard_sample_count = guard_sample_count,
        .expected_polarity = CALIBRATION_CLK_POLARITY_NORMAL,
        .max_best_distance = max_best_distance,
        .min_margin = min_margin,
    };
    calibration_training_sck_store_t validation_store;
    calibration_training_sck_store_init(&validation_store);
    if (!calibration_training_sck_prepare_core1(
            &validation_store, &request)) {
        return false;
    }
    calibration_manager_sck_publish(CALIBRATION_SCK_INTENT_ARM, &request);
    resource_arbiter_publish_calibration_training(true);
    return true;
}

bool calibration_manager_inject_sck_training(void)
{
    calibration_sck_intent_t pending;
    tdma_service_ring_runtime_config_t staged;
    if (!__atomic_load_n(&s_sck_active, __ATOMIC_ACQUIRE) ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        staged.local_slot_id != s_sck_active_request.source_node ||
        !calibration_manager_sck_read(&pending) ||
        pending.sequence != __atomic_load_n(
            &s_sck_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        return false;
    }
    calibration_manager_sck_publish(CALIBRATION_SCK_INTENT_INJECT, NULL);
    return true;
}

void calibration_manager_stop_sck_training(void)
{
    calibration_manager_sck_publish(CALIBRATION_SCK_INTENT_STOP, NULL);
}

bool calibration_manager_get_sck_training_snapshot(
    calibration_training_sck_snapshot_t *snapshot)
{
    return calibration_training_sck_get_snapshot(&s_sck_store, snapshot);
}

bool calibration_manager_save_sck_capture(
    uint32_t *job_id, char *path, size_t path_size)
{
    if (job_id == NULL || path == NULL || path_size == 0u ||
        __atomic_load_n(&s_sck_active, __ATOMIC_ACQUIRE)) {
        return false;
    }
    *job_id = 0u;
    path[0] = '\0';
    calibration_training_sck_snapshot_t snapshot;
    const uint32_t word_count = __atomic_load_n(
        &s_sck_capture_word_count, __ATOMIC_ACQUIRE);
    const uint32_t sample_count = __atomic_load_n(
        &s_sck_capture_sample_count, __ATOMIC_ACQUIRE);
    if (!calibration_training_sck_get_snapshot(&s_sck_store, &snapshot) ||
        (snapshot.state != CALIBRATION_TRAINING_SCK_ACCEPTED &&
         snapshot.state != CALIBRATION_TRAINING_SCK_REJECTED) ||
        word_count == 0u ||
        word_count > TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS ||
        sample_count == 0u || sample_count > word_count * 32u) {
        return false;
    }
    const uint32_t link_index = snapshot.source_node;
    const int path_written = snprintf(
        path, path_size, "/cal/sck_node%lu_link%lu_g%lu_e%lu.json",
        (unsigned long)snapshot.destination_node,
        (unsigned long)link_index,
        (unsigned long)snapshot.calibration_generation,
        (unsigned long)snapshot.train_epoch);
    if (path_written <= 0 || (size_t)path_written >= path_size) return false;

    int written = snprintf(
        s_sck_capture_payload, sizeof(s_sck_capture_payload),
        "{\n"
        "  \"schema\": \"HAOFV_SCK_TRAIN_CAPTURE_V3\",\n"
        "  \"source_node\": %lu,\n"
        "  \"destination_node\": %lu,\n"
        "  \"link_index\": %lu,\n"
        "  \"build_id\": %llu,\n"
        "  \"calibration_generation\": %lu,\n"
        "  \"epoch\": %lu,\n"
        "  \"sck_codebook_id\": %lu,\n"
        "  \"sample_period_ns\": %lu,\n"
        "  \"sck_launch_guard_sample_count\": %lu,\n"
        "  \"link_base_delay_ns\": %lu,\n"
        "  \"configured_sck_offset_sample_count\": %ld,\n"
        "  \"search_start_offset_sample\": %ld,\n"
        "  \"search_end_offset_sample\": %ld,\n"
        "  \"resolved_offset_sample_count\": %ld,\n"
        "  \"resolved_offset_ns\": %ld,\n"
        "  \"capture_anchor\": \"physical_rx_sck_origin_falling_edge\",\n"
        "  \"sck_input\": \"physical_rx_sck\",\n"
        "  \"raw_word_count\": %lu,\n"
        "  \"raw_sample_count\": %lu,\n"
        "  \"raw_words\": [",
        (unsigned long)snapshot.source_node,
        (unsigned long)snapshot.destination_node,
        (unsigned long)link_index,
        (unsigned long long)snapshot.build_id,
        (unsigned long)snapshot.calibration_generation,
        (unsigned long)snapshot.train_epoch,
        (unsigned long)snapshot.sck_codebook_id,
        (unsigned long)snapshot.sample_period_ns,
        (unsigned long)snapshot.sck_launch_guard_sample_count,
        (unsigned long)snapshot.link_base_delay_ns,
        (long)snapshot.configured_sck_offset_sample_count,
        (long)snapshot.search_start_offset_sample,
        (long)snapshot.search_end_offset_sample,
        (long)snapshot.resolved_offset_sample_count,
        (long)snapshot.resolved_offset_ns,
        (unsigned long)word_count,
        (unsigned long)sample_count);
    if (written <= 0 ||
        (size_t)written >= sizeof(s_sck_capture_payload)) return false;
    size_t used = (size_t)written;
    for (uint32_t index = 0u; index < word_count; index++) {
        written = snprintf(s_sck_capture_payload + used,
                           sizeof(s_sck_capture_payload) - used,
                           "%s%lu", index == 0u ? "" : ",",
                           (unsigned long)s_sck_capture[index]);
        if (written <= 0 ||
            (size_t)written >= sizeof(s_sck_capture_payload) - used) {
            return false;
        }
        used += (size_t)written;
    }
    written = snprintf(s_sck_capture_payload + used,
                       sizeof(s_sck_capture_payload) - used, "]\n}\n");
    if (written <= 0 ||
        (size_t)written >= sizeof(s_sck_capture_payload) - used) {
        return false;
    }
    used += (size_t)written;
    const uint32_t crc32 = ota_crc32_compute(
        (const uint8_t *)s_sck_capture_payload, used);
    uint32_t transaction_id = 0u;
    if (!storage_manager_begin_file_write(path, (uint32_t)used, crc32,
                                          &transaction_id) ||
        !storage_manager_write_file_chunk(
            transaction_id, 0u,
            (const uint8_t *)s_sck_capture_payload, used) ||
        !storage_manager_commit_file_write(transaction_id, job_id)) {
        (void)storage_manager_abort_file_write(transaction_id);
        *job_id = 0u;
        return false;
    }
    return true;
}

bool calibration_manager_save_data_capture(
    uint32_t *job_id, char *path, size_t path_size)
{
    if (job_id == NULL || path == NULL || path_size == 0u ||
        __atomic_load_n(&s_data_active, __ATOMIC_ACQUIRE)) {
        return false;
    }
    *job_id = 0u;
    path[0] = '\0';
    calibration_training_data_snapshot_t snapshot;
    const uint32_t word_count = __atomic_load_n(
        &s_data_capture_word_count, __ATOMIC_ACQUIRE);
    const uint32_t sample_count = __atomic_load_n(
        &s_data_capture_sample_count, __ATOMIC_ACQUIRE);
    if (!calibration_training_data_get_snapshot(&s_data_store, &snapshot) ||
        (snapshot.state != CALIBRATION_TRAINING_DATA_ACCEPTED &&
         snapshot.state != CALIBRATION_TRAINING_DATA_REJECTED) ||
        word_count == 0u ||
        word_count > TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS ||
        sample_count == 0u || sample_count > word_count * 32u) {
        return false;
    }
    const int path_written = snprintf(
        path, path_size, "/cal/data_node%lu_link%lu_g%lu_e%lu.json",
        (unsigned long)snapshot.source_node,
        (unsigned long)snapshot.source_node,
        (unsigned long)snapshot.calibration_generation,
        (unsigned long)snapshot.train_epoch);
    if (path_written <= 0 || (size_t)path_written >= path_size) return false;

    int written = snprintf(
        s_data_capture_payload, sizeof(s_data_capture_payload),
        "{\n"
        "  \"schema\": \"HAOFV_DATA_TRAIN_CAPTURE_V2\",\n"
        "  \"source_node\": %lu,\n"
        "  \"destination_node\": %lu,\n"
        "  \"marker_source_node\": %lu,\n"
        "  \"marker_destination_node\": %lu,\n"
        "  \"data_source_node\": %lu,\n"
        "  \"data_destination_node\": %lu,\n"
        "  \"measurement_direction\": \"marker_forward_data_return\",\n"
        "  \"build_id\": %llu,\n"
        "  \"calibration_generation\": %lu,\n"
        "  \"epoch\": %lu,\n"
        "  \"data_codebook_id\": %lu,\n"
        "  \"sample_period_ns\": %lu,\n"
        "  \"marker_to_data_samples\": %lu,\n"
        "  \"link_base_delay_ns\": %lu,\n"
        "  \"marker_offset_sample_count\": %ld,\n"
        "  \"configured_data_offset_sample_count\": %ld,\n"
        "  \"search_start_offset_sample\": %ld,\n"
        "  \"search_end_offset_sample\": %ld,\n"
        "  \"resolved_offset_sample_count\": %ld,\n"
        "  \"resolved_offset_ns\": %ld,\n"
        "  \"capture_anchor\": \"physical_rx_csn_falling_edge_pio_delay\",\n"
        "  \"data_input\": \"physical_rx_data\",\n"
        "  \"raw_word_count\": %lu,\n"
        "  \"raw_sample_count\": %lu,\n"
        "  \"raw_words\": [",
        (unsigned long)snapshot.source_node,
        (unsigned long)snapshot.destination_node,
        (unsigned long)snapshot.source_node,
        (unsigned long)snapshot.destination_node,
        (unsigned long)snapshot.destination_node,
        (unsigned long)snapshot.source_node,
        (unsigned long long)snapshot.build_id,
        (unsigned long)snapshot.calibration_generation,
        (unsigned long)snapshot.train_epoch,
        (unsigned long)snapshot.data_codebook_id,
        (unsigned long)snapshot.sample_period_ns,
        (unsigned long)snapshot.marker_to_data_samples,
        (unsigned long)snapshot.link_base_delay_ns,
        (long)snapshot.marker_offset_sample_count,
        (long)snapshot.configured_data_offset_sample_count,
        (long)snapshot.search_start_offset_sample,
        (long)snapshot.search_end_offset_sample,
        (long)snapshot.resolved_offset_sample_count,
        (long)snapshot.resolved_offset_ns,
        (unsigned long)word_count,
        (unsigned long)sample_count);
    if (written <= 0 ||
        (size_t)written >= sizeof(s_data_capture_payload)) return false;
    size_t used = (size_t)written;
    for (uint32_t index = 0u; index < word_count; index++) {
        written = snprintf(s_data_capture_payload + used,
                           sizeof(s_data_capture_payload) - used,
                           "%s%lu", index == 0u ? "" : ",",
                           (unsigned long)s_data_capture[index]);
        if (written <= 0 ||
            (size_t)written >= sizeof(s_data_capture_payload) - used) {
            return false;
        }
        used += (size_t)written;
    }
    written = snprintf(s_data_capture_payload + used,
                       sizeof(s_data_capture_payload) - used, "]\n}\n");
    if (written <= 0 ||
        (size_t)written >= sizeof(s_data_capture_payload) - used) {
        return false;
    }
    used += (size_t)written;
    const uint32_t crc32 = ota_crc32_compute(
        (const uint8_t *)s_data_capture_payload, used);
    uint32_t transaction_id = 0u;
    if (!storage_manager_begin_file_write(path, (uint32_t)used, crc32,
                                          &transaction_id) ||
        !storage_manager_write_file_chunk(
            transaction_id, 0u,
            (const uint8_t *)s_data_capture_payload, used) ||
        !storage_manager_commit_file_write(transaction_id, job_id)) {
        (void)storage_manager_abort_file_write(transaction_id);
        *job_id = 0u;
        return false;
    }
    return true;
}

bool calibration_manager_save_ring_capture(
    uint32_t calibration_generation,
    uint32_t capture_epoch,
    uint32_t *job_id,
    char *path,
    size_t path_size)
{
    if (calibration_generation == 0u || capture_epoch == 0u ||
        job_id == NULL || path == NULL || path_size == 0u) {
        return false;
    }
    *job_id = 0u;
    path[0] = '\0';

    calibration_ring_capture_snapshot_t snapshot;
    if (!calibration_manager_get_ring_capture_snapshot(&snapshot) ||
        snapshot.state != CALIBRATION_RING_CAPTURE_READY ||
        snapshot.calibration_generation != calibration_generation ||
        snapshot.capture_epoch != capture_epoch) {
        return false;
    }
    const tdma_pio_spi_normal_capture_snapshot_t capture = snapshot.physical;

    const int path_written = snprintf(
        path, path_size, "/cal/trn03b_node%lu_g%lu_e%lu.json",
        (unsigned long)snapshot.node,
        (unsigned long)calibration_generation,
        (unsigned long)capture_epoch);
    if (path_written <= 0 || (size_t)path_written >= path_size) {
        return false;
    }

    int written = snprintf(
        s_ring_capture_payload, sizeof(s_ring_capture_payload),
        "{\n"
        "  \"schema\": \"HAOFV_TRN03_RING_CAPTURE_V3\",\n"
        "  \"node\": %lu,\n"
        "  \"node_count\": %lu,\n"
        "  \"build_id\": \"%s\",\n"
        "  \"calibration_generation\": %lu,\n"
        "  \"capture_epoch\": %lu,\n"
        "  \"capture_version\": %lu,\n"
        "  \"baud_hz\": %lu,\n"
        "  \"bit_period_ns\": %lu,\n"
        "  \"capture_anchor\": \"pio_rx_sck_rising_edge\",\n"
        "  \"capture_scope\": \"raw_physical_loop_input_window\",\n"
        "  \"rx_data_semantics\": \"unparsed_physical_rx_data_sampled_by_rx_sck\",\n"
        "  \"tx_data_semantics\": \"newest_complete_frame_accepted_by_local_tx_fifo\",\n"
        "  \"rx_produced_bytes\": %lu,\n"
        "  \"tx_produced_bytes\": %lu,\n"
        "  \"tx_complete_frame_count\": %lu,\n"
        "  \"sck_capture_anchor\": \"physical_rx_sck_first_rising_edge\",\n"
        "  \"sck_input\": \"physical_rx_sck\",\n"
        "  \"sck_sample_period_ns\": %lu,\n"
        "  \"sck_sample_count\": %lu,\n"
        "  \"sck_word_count\": %lu,\n"
        "  \"rx_byte_count\": %lu,\n"
        "  \"tx_byte_count\": %lu,\n"
        "  \"rx_bytes\": [",
        (unsigned long)snapshot.node,
        (unsigned long)snapshot.node_count,
        g_project_build_id,
        (unsigned long)calibration_generation,
        (unsigned long)capture_epoch,
        (unsigned long)capture.version,
        (unsigned long)capture.baud_hz,
        (unsigned long)capture.bit_period_ns,
        (unsigned long)capture.rx_produced_bytes,
        (unsigned long)capture.tx_produced_bytes,
        (unsigned long)capture.tx_complete_frame_count,
        (unsigned long)capture.sck_sample_period_ns,
        (unsigned long)capture.sck_sample_count,
        (unsigned long)capture.sck_word_count,
        (unsigned long)capture.rx_byte_count,
        (unsigned long)capture.tx_byte_count);
    if (written <= 0 ||
        (size_t)written >= sizeof(s_ring_capture_payload)) {
        return false;
    }
    size_t used = (size_t)written;
    for (uint32_t index = 0u; index < capture.rx_byte_count; index++) {
        written = snprintf(
            s_ring_capture_payload + used,
            sizeof(s_ring_capture_payload) - used,
            "%s%lu", index == 0u ? "" : ",",
            (unsigned long)s_ring_capture_rx[index]);
        if (written <= 0 ||
            (size_t)written >= sizeof(s_ring_capture_payload) - used) {
            return false;
        }
        used += (size_t)written;
    }
    written = snprintf(
        s_ring_capture_payload + used,
        sizeof(s_ring_capture_payload) - used,
        "],\n  \"tx_bytes\": [");
    if (written <= 0 ||
        (size_t)written >= sizeof(s_ring_capture_payload) - used) {
        return false;
    }
    used += (size_t)written;
    for (uint32_t index = 0u; index < capture.tx_byte_count; index++) {
        written = snprintf(
            s_ring_capture_payload + used,
            sizeof(s_ring_capture_payload) - used,
            "%s%lu", index == 0u ? "" : ",",
            (unsigned long)s_ring_capture_tx[index]);
        if (written <= 0 ||
            (size_t)written >= sizeof(s_ring_capture_payload) - used) {
            return false;
        }
        used += (size_t)written;
    }
    written = snprintf(
        s_ring_capture_payload + used,
        sizeof(s_ring_capture_payload) - used,
        "],\n  \"rx_sck_words\": [");
    if (written <= 0 ||
        (size_t)written >= sizeof(s_ring_capture_payload) - used) {
        return false;
    }
    used += (size_t)written;
    for (uint32_t index = 0u; index < capture.sck_word_count; index++) {
        written = snprintf(
            s_ring_capture_payload + used,
            sizeof(s_ring_capture_payload) - used,
            "%s%lu", index == 0u ? "" : ",",
            (unsigned long)capture.sck_words[index]);
        if (written <= 0 ||
            (size_t)written >= sizeof(s_ring_capture_payload) - used) {
            return false;
        }
        used += (size_t)written;
    }
    written = snprintf(
        s_ring_capture_payload + used,
        sizeof(s_ring_capture_payload) - used, "]\n}\n");
    if (written <= 0 ||
        (size_t)written >= sizeof(s_ring_capture_payload) - used) {
        return false;
    }
    used += (size_t)written;

    const uint32_t crc32 = ota_crc32_compute(
        (const uint8_t *)s_ring_capture_payload, used);
    uint32_t transaction_id = 0u;
    if (!storage_manager_begin_file_write(path, (uint32_t)used, crc32,
                                          &transaction_id) ||
        !storage_manager_write_file_chunk(
            transaction_id, 0u,
            (const uint8_t *)s_ring_capture_payload, used) ||
        !storage_manager_commit_file_write(transaction_id, job_id)) {
        (void)storage_manager_abort_file_write(transaction_id);
        *job_id = 0u;
        return false;
    }
    return true;
}

bool calibration_manager_save_marker_capture(
    uint32_t *job_id, char *path, size_t path_size)
{
    if (job_id == NULL || path == NULL || path_size == 0u ||
        __atomic_load_n(&s_marker_active, __ATOMIC_ACQUIRE)) {
        return false;
    }
    *job_id = 0u;
    path[0] = '\0';
    calibration_training_marker_snapshot_t snapshot;
    const uint32_t raw_word_count = __atomic_load_n(
        &s_marker_raw_word_count, __ATOMIC_ACQUIRE);
    const uint32_t raw_sample_count = __atomic_load_n(
        &s_marker_raw_sample_count, __ATOMIC_ACQUIRE);
    if (!calibration_training_marker_get_snapshot(&s_marker_store, &snapshot) ||
        (snapshot.state != CALIBRATION_TRAINING_MARKER_ACCEPTED &&
         snapshot.state != CALIBRATION_TRAINING_MARKER_REJECTED) ||
        raw_word_count == 0u ||
        raw_word_count > TDMA_PIO_SPI_MARKER_BUFFER_WORDS ||
        raw_sample_count == 0u ||
        raw_sample_count > raw_word_count * 16u ||
        raw_word_count != snapshot.dma_capture_count) {
        return false;
    }
    const uint32_t incoming_link = snapshot.predecessor_node;
    const int path_written = snapshot.role ==
            CALIBRATION_TRAINING_MARKER_ROLE_ORIGINATOR
        ? snprintf(path, path_size,
                   "/cal/marker_node%lu_loop_g%lu_e%lu.json",
                   (unsigned long)snapshot.local_node,
                   (unsigned long)snapshot.calibration_generation,
                   (unsigned long)snapshot.train_epoch)
        : snprintf(path, path_size,
                   "/cal/marker_node%lu_link%lu_g%lu_e%lu.json",
                   (unsigned long)snapshot.local_node,
                   (unsigned long)incoming_link,
                   (unsigned long)snapshot.calibration_generation,
                   (unsigned long)snapshot.train_epoch);
    if (path_written <= 0 || (size_t)path_written >= path_size) return false;

    int written = snprintf(
        s_marker_capture_payload, sizeof(s_marker_capture_payload),
        "{\n"
        "  \"schema\": \"HAOFV_MARKER_CAPTURE_V2\",\n"
        "  \"node\": %lu,\n"
        "  \"incoming_link\": %lu,\n"
        "  \"incoming_link_source_node\": %lu,\n"
        "  \"incoming_link_destination_node\": %lu,\n"
        "  \"build_id\": %llu,\n"
        "  \"calibration_generation\": %lu,\n"
        "  \"epoch\": %lu,\n"
        "  \"link_base_delay_ns\": %lu,\n"
        "  \"offset_sample_count\": %ld,\n"
        "  \"marker_start_model\": \"tx_fifo_virtual_rx_csn\",\n"
        "  \"capture_anchor\": \"physical_rx_csn_falling_edge\",\n"
        "  \"tick_resolution_ns\": %lu,\n"
        "  \"raw_interleaved_word_count\": %lu,\n"
        "  \"raw_interleaved_sample_count\": %lu,\n"
        "  \"raw_interleaved_sample_capacity\": %lu,\n"
        "  \"channel_0\": \"forward_output\",\n"
        "  \"channel_1\": \"incoming_link\",\n"
        "  \"link_delay_model\": \"source_node_driver_delay + link_path_delay + destination_node_receiver_delay + link_capture_quantization\",\n"
        "  \"raw_interleaved_words\": [",
        (unsigned long)snapshot.local_node,
        (unsigned long)incoming_link,
        (unsigned long)snapshot.predecessor_node,
        (unsigned long)snapshot.local_node,
        (unsigned long long)snapshot.build_id,
        (unsigned long)snapshot.calibration_generation,
        (unsigned long)snapshot.train_epoch,
        (unsigned long)snapshot.link_base_delay_ns,
        (long)snapshot.offset_sample_count,
        (unsigned long)snapshot.tick_resolution_ns,
        (unsigned long)raw_word_count,
        (unsigned long)raw_sample_count,
        (unsigned long)(raw_word_count * 16u));
    if (written <= 0 || (size_t)written >= sizeof(s_marker_capture_payload)) {
        return false;
    }
    size_t used = (size_t)written;
    for (uint32_t index = 0u; index < raw_word_count; index++) {
        written = snprintf(s_marker_capture_payload + used,
                           sizeof(s_marker_capture_payload) - used,
                           "%s%lu", index == 0u ? "" : ",",
                           (unsigned long)s_marker_raw_capture[index]);
        if (written <= 0 || (size_t)written >=
                sizeof(s_marker_capture_payload) - used) {
            return false;
        }
        used += (size_t)written;
    }
    written = snprintf(s_marker_capture_payload + used,
                       sizeof(s_marker_capture_payload) - used, "]\n}\n");
    if (written <= 0 || (size_t)written >=
            sizeof(s_marker_capture_payload) - used) {
        return false;
    }
    used += (size_t)written;
    const uint32_t crc32 = ota_crc32_compute(
        (const uint8_t *)s_marker_capture_payload, used);
    uint32_t transaction_id = 0u;
    if (!storage_manager_begin_file_write(path, (uint32_t)used, crc32,
                                          &transaction_id) ||
        !storage_manager_write_file_chunk(
            transaction_id, 0u,
            (const uint8_t *)s_marker_capture_payload, used) ||
        !storage_manager_commit_file_write(transaction_id, job_id)) {
        (void)storage_manager_abort_file_write(transaction_id);
        *job_id = 0u;
        return false;
    }
    return true;
}

bool calibration_manager_start_clk_coded(
    const calibration_clk_coded_request_t *request,
    const calibration_clk_correlation_gate_t *gate)
{
    calibration_clk_coded_intent_t pending;
    if (request == NULL || gate == NULL) return false;
    const calibration_clk_marker_config_t marker_config = {
        .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        .codebook_id = (uint8_t)request->codebook_id,
        .epoch = (uint8_t)request->train_epoch,
        .source_node = (uint8_t)request->local_node,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    if (request->local_node > 7u ||
        request->train_epoch > UINT8_MAX || request->codebook_id > UINT8_MAX ||
        request->sample_period_ns == 0u ||
        request->coarse_min_sample >= request->coarse_max_sample ||
        request->coarse_max_sample - request->coarse_min_sample + 1u >
            CALIBRATION_CLK_CORRELATION_MAX_LAGS ||
        gate->min_lag_sample != request->coarse_min_sample ||
        gate->max_lag_sample != request->coarse_max_sample ||
        !calibration_clk_marker_config_valid(&marker_config) ||
        __atomic_load_n(&s_clk_coded_active, __ATOMIC_ACQUIRE) ||
        !calibration_manager_clk_coded_intent_read(&pending) ||
        pending.sequence != __atomic_load_n(
            &s_clk_coded_intent_consumed_sequence, __ATOMIC_ACQUIRE)) {
        return false;
    }
    calibration_manager_clk_coded_intent_publish(
        CALIBRATION_CLK_CODED_INTENT_START, request, gate);
    resource_arbiter_publish_calibration_training(true);
    return true;
}

bool calibration_manager_request_clk_coded(
    uint32_t codebook_id,
    uint32_t min_lag_sample,
    uint32_t max_lag_sample,
    uint32_t max_best_distance,
    uint32_t min_margin)
{
    tdma_ring_runtime_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    uint64_t board_unique_id = 0u;
    if (BOARD_SYS_CLOCK_HZ == 0u ||
        (UINT32_C(1000000000) % BOARD_SYS_CLOCK_HZ) != 0u ||
        !calibration_manager_parse_hex_u64(board_identity_serial(),
                                           &board_unique_id) ||
        !tdma_runtime_owner_get_ring_snapshot(&ring) ||
        ring.enabled != 0u ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        staged.node_count < 2u ||
        staged.local_slot_id >= staged.node_count ||
        staged.ring_profile_crc32 == 0u ||
        staged.operating_profile_crc32 == 0u ||
        staged.schedule_crc32 == 0u || staged.baud_hz == 0u) {
        return false;
    }

    uint32_t sequence = s_clk_coded_request_sequence + 1u;
    if (sequence == 0u) sequence = 1u;
    const calibration_clk_coded_request_t request = {
        .board_unique_id = board_unique_id,
        .build_id = calibration_manager_build_id_value(g_project_build_id),
        .local_node = staged.local_slot_id,
        .train_epoch = sequence & UINT8_MAX,
        .train_sequence = sequence,
        .calibration_generation = sequence,
        .topology_generation = ring.config_seq,
        .topology_crc32 = staged.ring_profile_crc32,
        .profile_crc32 = staged.operating_profile_crc32,
        .schedule_crc32 = staged.schedule_crc32,
        .baud_hz = staged.baud_hz,
        .codebook_id = codebook_id,
        .sample_period_ns = UINT32_C(1000000000) / BOARD_SYS_CLOCK_HZ,
        .coarse_min_sample = min_lag_sample,
        .coarse_max_sample = max_lag_sample,
    };
    const calibration_clk_correlation_gate_t gate = {
        .min_lag_sample = min_lag_sample,
        .max_lag_sample = max_lag_sample,
        .max_best_distance = max_best_distance,
        .min_margin = min_margin,
    };
    if (!calibration_manager_start_clk_coded(&request, &gate)) {
        return false;
    }
    s_clk_coded_request_sequence = sequence;
    return true;
}

void calibration_manager_stop_clk_coded(void)
{
    calibration_manager_clk_coded_intent_publish(
        CALIBRATION_CLK_CODED_INTENT_STOP, NULL, NULL);
}

void calibration_manager_get_status(calibration_manager_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_status;
    status->ready = s_ready;
    osal_critical_exit();
}
