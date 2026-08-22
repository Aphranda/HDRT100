#include "calibration_manager.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "board_config.h"
#include "board_identity.h"
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

static void calibration_manager_publish_training_activity(void)
{
    calibration_pio_loopback_snapshot_t training_loopback;
    const bool loopback_active =
        calibration_pio_loopback_get_snapshot(&training_loopback) &&
        training_loopback.armed != 0u;
    const bool calibration_active =
        loopback_active ||
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

void calibration_manager_service_core1(void)
{
    tdma_ring_runtime_snapshot_t ring;
    const bool stopped = tdma_runtime_owner_get_ring_snapshot(&ring) &&
                         ring.enabled == 0u;
    calibration_pio_loopback_service_core1(stopped);
    tdma_runtime_owner_coded_service_core1();
    tdma_runtime_owner_p3_service_core1();
    calibration_manager_publish_training_activity();

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
                .master_slot = (uint8_t)intent.request.logical_slot,
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
        .master_slot = (uint8_t)request->logical_slot,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    if (request->logical_slot > 7u ||
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
        .logical_slot = staged.local_slot_id,
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
