#include "sync_io_logic_analyzer.h"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "board_config.h"
#include "sync_io.pio.h"
#include "sync_io_core_internal.h"
#endif

#define SYNC_IO_LOGIC_ANALYZER_DMA_RING_BITS 15u
#define SYNC_IO_LOGIC_ANALYZER_EDGE_SAMPLE_PERIOD_NS 1000u

_Static_assert(SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS > 0u,
               "logic analyzer workspace must hold at least one record");

static sync_io_logic_analyzer_persona_t *s_active_persona;

typedef struct {
    volatile uint32_t request_sequence;
    volatile uint32_t handled_sequence;
    volatile uint32_t command;
    volatile uint32_t result;
    sync_io_logic_analyzer_config_t config;
    sync_io_logic_analyzer_persona_t persona;
    sync_io_logic_analyzer_raw_capture_t capture;
    sync_io_logic_analyzer_raw_capture_t shadow_capture;
    volatile uint32_t shadow_sequence;
    volatile uint32_t shadow_ready;
    sync_io_logic_analyzer_record_t records[
        SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS];
} sync_io_logic_analyzer_control_t;

static sync_io_logic_analyzer_control_t s_control;

#if !defined(PICO_ON_DEVICE) || !PICO_ON_DEVICE
static sync_io_logic_analyzer_record_t s_host_shadow_records[
    SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS];
#endif

static sync_io_logic_analyzer_record_t *
sync_io_logic_analyzer_active_records(void)
{
    return s_control.records;
}

static sync_io_logic_analyzer_record_t *
sync_io_logic_analyzer_shadow_records(void)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    return (sync_io_logic_analyzer_record_t *)sync_io_shared_workspace;
#else
    return s_host_shadow_records;
#endif
}

static void sync_io_logic_analyzer_publish_shadow(void)
{
    if (!s_control.capture.initialized) {
        return;
    }
    const uint32_t retained = s_control.capture.produced_records -
                              s_control.capture.consumed_records;
    sync_io_logic_analyzer_record_t *shadow_records =
        sync_io_logic_analyzer_shadow_records();
    __atomic_fetch_add(&s_control.shadow_sequence, 1u, __ATOMIC_ACQ_REL);
    s_control.shadow_capture = s_control.capture;
    uint32_t source_index = s_control.capture.read_index;
    for (uint32_t index = 0u; index < retained; ++index) {
        shadow_records[index] = s_control.capture.records[source_index];
        source_index = (source_index + 1u) % s_control.capture.capacity;
    }
    s_control.shadow_capture.records = shadow_records;
    s_control.shadow_capture.read_index = 0u;
    s_control.shadow_capture.write_index =
        retained % s_control.shadow_capture.capacity;
    s_control.shadow_ready = 1u;
    memset(&s_control.capture, 0, sizeof(s_control.capture));
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_fetch_add(&s_control.shadow_sequence, 1u, __ATOMIC_RELEASE);
}

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
typedef struct {
    sync_io_logic_analyzer_raw_capture_t *capture;
    uint offset;
    uint32_t produced_raw;
    uint32_t consumed_raw;
    uint32_t sequence;
    uint32_t previous_level;
    bool previous_level_valid;
    bool sm_claimed;
    bool dma_claimed;
    bool program_loaded;
    bool running;
} sync_io_logic_analyzer_hw_t;

static sync_io_logic_analyzer_hw_t s_hw;

static uint32_t sync_io_logic_analyzer_hw_raw_produced(void)
{
    if (!s_hw.running) {
        return s_hw.produced_raw;
    }
    const uintptr_t base = (uintptr_t)sync_io_shared_workspace;
    const uintptr_t addr =
        (uintptr_t)dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].write_addr;
    const uint32_t index = (uint32_t)(((addr - base) &
        (SYNC_IO_SHARED_WORKSPACE_WORDS * sizeof(uint32_t) - 1u)) /
        sizeof(uint32_t));
    const uint32_t transferred =
        UINT32_MAX - dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].transfer_count;
    if (transferred > s_hw.produced_raw) {
        s_hw.produced_raw = transferred;
    } else {
        /* The transfer counter is modulo 2^32; the write index still lets us
         * account for a ring wrap during the bounded capture window. */
        const uint32_t previous =
            s_hw.produced_raw % SYNC_IO_SHARED_WORKSPACE_WORDS;
        const uint32_t delta = index >= previous
            ? index - previous
            : SYNC_IO_SHARED_WORKSPACE_WORDS - previous + index;
        s_hw.produced_raw += delta;
    }
    return s_hw.produced_raw;
}

bool sync_io_logic_analyzer_hw_arm(
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *records,
    uint32_t capacity,
    const sync_io_logic_analyzer_config_t *config)
{
    if (capture == NULL || records == NULL || config == NULL ||
        (config->mode != SYNC_IO_LOGIC_ANALYZER_MODE_RAW_SAMPLE &&
         config->mode != SYNC_IO_LOGIC_ANALYZER_MODE_EDGE_TIMESTAMP) ||
        !sync_io_logic_analyzer_config_valid(config) ||
        !sync_io_core_initialized() || sync_io_core_capture_is_running() ||
        sync_io_core_wave_output_persona_active() || s_hw.running) {
        return false;
    }
    if (!sync_io_logic_analyzer_raw_capture_init(capture, records,
                                                  capacity, config)) {
        return false;
    }
    if (!pio_can_add_program(BOARD_SYNC_PIO_FAST,
                             &logic_analyzer_raw_sample_program)) {
        return false;
    }

    s_hw.offset = (uint)pio_add_program(
        BOARD_SYNC_PIO_FAST, &logic_analyzer_raw_sample_program);
    s_hw.program_loaded = true;
    s_hw.capture = capture;
    s_hw.produced_raw = 0u;
    s_hw.consumed_raw = 0u;
    s_hw.sequence = 0u;
    s_hw.previous_level = 0u;
    s_hw.previous_level_valid = false;

    PIO pio = BOARD_SYNC_PIO_FAST;
    const uint sm = BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM;
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_config pio_config =
        logic_analyzer_raw_sample_program_get_default_config(s_hw.offset);
    /* The analyzer observes the pad bus without changing GPIO function or
     * direction.  The validated source mask is applied after DMA sampling. */
    sm_config_set_in_pins(&pio_config, 0u);
    sm_config_set_in_shift(&pio_config, false, true, 32);
    sm_config_set_fifo_join(&pio_config, PIO_FIFO_JOIN_RX);
    const uint32_t sample_period_ns = config->sample_period_ns != 0u
        ? config->sample_period_ns
        : SYNC_IO_LOGIC_ANALYZER_EDGE_SAMPLE_PERIOD_NS;
    sm_config_set_clkdiv(&pio_config,
                         (float)clock_get_hz(clk_sys) /
                         (float)(1000000000u / sample_period_ns));
    pio_sm_init(pio, sm, s_hw.offset, &pio_config);
    pio_sm_set_pins(pio, sm, 0u);

    dma_channel_abort(SYNC_IO_CAPTURE_DMA_CH);
    dma_channel_config dma_config =
        dma_channel_get_default_config(SYNC_IO_CAPTURE_DMA_CH);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_config, false);
    channel_config_set_write_increment(&dma_config, true);
    channel_config_set_ring(&dma_config, true,
                            SYNC_IO_LOGIC_ANALYZER_DMA_RING_BITS);
    channel_config_set_dreq(&dma_config,
        pio_get_dreq(pio, sm, false));
    dma_channel_configure(SYNC_IO_CAPTURE_DMA_CH,
                          &dma_config,
                          sync_io_shared_workspace,
                          &pio->rxf[sm],
                          UINT32_MAX,
                          false);
    s_hw.sm_claimed = true; /* shared SYNC_IO static reservation */
    s_hw.dma_claimed = true;
    capture->state = SYNC_IO_LOGIC_ANALYZER_STATE_ARMED;
    return true;
}

bool sync_io_logic_analyzer_hw_start(void)
{
    if (s_hw.capture == NULL || !s_hw.program_loaded || !s_hw.sm_claimed ||
        s_hw.running || s_hw.capture->state !=
            SYNC_IO_LOGIC_ANALYZER_STATE_ARMED) {
        return false;
    }
    s_hw.produced_raw = 0u;
    s_hw.consumed_raw = 0u;
    s_hw.running = true;
    dma_start_channel_mask(1u << SYNC_IO_CAPTURE_DMA_CH);
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST,
                       BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM, true);
    s_hw.capture->state = SYNC_IO_LOGIC_ANALYZER_STATE_RUNNING;
    return true;
}

void sync_io_logic_analyzer_hw_stop(void)
{
    if (s_hw.capture == NULL) {
        return;
    }
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST,
                       BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM, false);
    dma_channel_abort(SYNC_IO_CAPTURE_DMA_CH);
    if (s_hw.capture->state == SYNC_IO_LOGIC_ANALYZER_STATE_RUNNING ||
        s_hw.capture->state == SYNC_IO_LOGIC_ANALYZER_STATE_ARMED) {
        sync_io_logic_analyzer_raw_capture_finish(
            s_hw.capture, SYNC_IO_LOGIC_ANALYZER_END_STOP_REQUEST);
    }
    if (s_hw.program_loaded) {
        pio_remove_program(BOARD_SYNC_PIO_FAST,
                           &logic_analyzer_raw_sample_program,
                           s_hw.offset);
    }
    memset(&s_hw, 0, sizeof(s_hw));
}

size_t sync_io_logic_analyzer_hw_service(uint32_t max_records)
{
    if (!s_hw.running || s_hw.capture == NULL || max_records == 0u) {
        return 0u;
    }
    const uint32_t produced = sync_io_logic_analyzer_hw_raw_produced();
    uint32_t available = produced - s_hw.consumed_raw;
    if (available > SYNC_IO_SHARED_WORKSPACE_WORDS) {
        const uint32_t dropped =
            available - SYNC_IO_SHARED_WORKSPACE_WORDS;
        s_hw.consumed_raw = produced - SYNC_IO_SHARED_WORKSPACE_WORDS;
        available = SYNC_IO_SHARED_WORKSPACE_WORDS;
        s_hw.capture->dropped_records += dropped;
        s_hw.capture->overrun_count++;
    }
    if (available > max_records) {
        available = max_records;
    }
    size_t pushed = 0u;
    uint32_t inspected = 0u;
    while (inspected < available && pushed < max_records) {
        const uint32_t raw = sync_io_shared_workspace[
            s_hw.consumed_raw % SYNC_IO_SHARED_WORKSPACE_WORDS];
        const uint32_t level = raw & s_hw.capture->config.source_mask;
        const bool edge_mode = s_hw.capture->config.mode ==
            SYNC_IO_LOGIC_ANALYZER_MODE_EDGE_TIMESTAMP;
        if (edge_mode) {
            if (!s_hw.previous_level_valid) {
                s_hw.previous_level = level;
                s_hw.previous_level_valid = true;
                ++s_hw.consumed_raw;
                ++inspected;
                continue;
            }
            const uint32_t edge = s_hw.previous_level ^ level;
            s_hw.previous_level = level;
            ++s_hw.consumed_raw;
            ++inspected;
            if (edge == 0u) {
                continue;
            }
            sync_io_logic_analyzer_record_t record = {
                .hardware_tick = 0u,
                .capture_sequence = 1u,
                .record_sequence = s_hw.sequence++,
                .level_mask = level,
                .edge_mask = edge,
                .flags = SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_NONE,
                .reserved = raw,
            };
            (void)sync_io_capture_time_now_ns(&record.hardware_tick);
            if (sync_io_logic_analyzer_raw_capture_push(
                    s_hw.capture, &record)) {
                ++pushed;
            }
            continue;
        }
        sync_io_logic_analyzer_record_t record = {
            .hardware_tick = 0u,
            .capture_sequence = 1u,
            .record_sequence = s_hw.sequence++,
            .level_mask = level,
            .edge_mask = 0u,
            .flags = SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_NONE,
            .reserved = raw,
        };
        (void)sync_io_capture_time_now_ns(&record.hardware_tick);
        if (!sync_io_logic_analyzer_raw_capture_push(s_hw.capture, &record)) {
            break;
        }
        ++s_hw.consumed_raw;
        ++inspected;
        ++pushed;
    }
    if (s_hw.capture->complete) {
        sync_io_logic_analyzer_hw_stop();
    }
    return pushed;
}

bool sync_io_logic_analyzer_hw_active(void)
{
    return s_hw.running;
}

static bool sync_io_logic_analyzer_persona_load(
    void *context, const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    (void)context;
    return descriptor != NULL &&
           descriptor->id == SYNC_IO_PERSONA_ID_LOGIC_ANALYZER &&
           dma_channel_mask == (1u << SYNC_IO_CAPTURE_DMA_CH);
}

static bool sync_io_logic_analyzer_persona_arm(
    void *context, const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    sync_io_logic_analyzer_persona_t *persona =
        (sync_io_logic_analyzer_persona_t *)context;
    return persona != NULL && persona->capture != NULL &&
           sync_io_logic_analyzer_hw_arm(
               persona->capture, persona->capture->records,
               persona->capture->capacity, &persona->capture->config) &&
           descriptor != NULL &&
           descriptor->id == SYNC_IO_PERSONA_ID_LOGIC_ANALYZER &&
           dma_channel_mask == (1u << SYNC_IO_CAPTURE_DMA_CH);
}

static bool sync_io_logic_analyzer_persona_start(
    void *context, const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    (void)descriptor;
    (void)dma_channel_mask;
    return context != NULL && sync_io_logic_analyzer_hw_start();
}

static void sync_io_logic_analyzer_persona_stop(
    void *context, const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    (void)context;
    (void)descriptor;
    (void)dma_channel_mask;
    sync_io_logic_analyzer_hw_stop();
}

static void sync_io_logic_analyzer_persona_cleanup(
    void *context, const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    (void)context;
    (void)descriptor;
    (void)dma_channel_mask;
    if (s_hw.capture != NULL) {
        sync_io_logic_analyzer_hw_stop();
    }
}

bool sync_io_logic_analyzer_persona_begin(
    sync_io_logic_analyzer_persona_t *persona,
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *records,
    uint32_t capacity,
    const sync_io_logic_analyzer_config_t *config)
{
    if (persona == NULL || capture == NULL || records == NULL ||
        config == NULL) {
        return false;
    }
    memset(persona, 0, sizeof(*persona));
    persona->capture = capture;
    const sync_io_persona_manager_hooks_t hooks = {
        .load = sync_io_logic_analyzer_persona_load,
        .arm = sync_io_logic_analyzer_persona_arm,
        .start = sync_io_logic_analyzer_persona_start,
        .stop = sync_io_logic_analyzer_persona_stop,
        .cleanup = sync_io_logic_analyzer_persona_cleanup,
    };
    sync_io_persona_manager_init(&persona->manager, &hooks, persona);
    persona->initialized = true;
    if (!sync_io_logic_analyzer_raw_capture_init(
            capture, records, capacity, config) ||
        !sync_io_persona_manager_claim(
            &persona->manager, SYNC_IO_PERSONA_ID_LOGIC_ANALYZER,
            &persona->handle, NULL) ||
        !sync_io_persona_manager_load(&persona->manager, &persona->handle) ||
        !sync_io_persona_manager_arm(&persona->manager, &persona->handle) ||
        !sync_io_persona_manager_start(&persona->manager, &persona->handle)) {
        if (sync_io_persona_manager_handle_valid(
                &persona->manager, &persona->handle)) {
            (void)sync_io_persona_manager_release(
                &persona->manager, &persona->handle);
        }
        sync_io_persona_manager_deinit(&persona->manager);
        persona->initialized = false;
        persona->capture = NULL;
        return false;
    }
    persona->active = true;
    s_active_persona = persona;
    return true;
}

void sync_io_logic_analyzer_persona_end(
    sync_io_logic_analyzer_persona_t *persona)
{
    if (persona == NULL || !persona->initialized) {
        return;
    }
    if (s_active_persona == persona) {
        s_active_persona = NULL;
    }
    if (sync_io_persona_manager_handle_valid(
            &persona->manager, &persona->handle)) {
        (void)sync_io_persona_manager_release(
            &persona->manager, &persona->handle);
    }
    (void)sync_io_persona_manager_deinit(&persona->manager);
    memset(persona, 0, sizeof(*persona));
}

bool sync_io_logic_analyzer_persona_active(
    const sync_io_logic_analyzer_persona_t *persona)
{
    return persona != NULL && persona->active &&
           sync_io_logic_analyzer_hw_active();
}

void sync_io_logic_analyzer_persona_get_snapshot(
    const sync_io_logic_analyzer_persona_t *persona,
    sync_io_persona_manager_snapshot_t *snapshot)
{
    if (persona == NULL || snapshot == NULL || !persona->initialized) {
        if (snapshot != NULL) {
            memset(snapshot, 0, sizeof(*snapshot));
        }
        return;
    }
    sync_io_persona_manager_get_snapshot(&persona->manager, snapshot);
}
#else
bool sync_io_logic_analyzer_hw_arm(
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *records,
    uint32_t capacity,
    const sync_io_logic_analyzer_config_t *config)
{
    (void)capture;
    (void)records;
    (void)capacity;
    (void)config;
    return false;
}

bool sync_io_logic_analyzer_hw_start(void)
{
    return false;
}

void sync_io_logic_analyzer_hw_stop(void)
{
}

size_t sync_io_logic_analyzer_hw_service(uint32_t max_records)
{
    (void)max_records;
    return 0u;
}

bool sync_io_logic_analyzer_hw_active(void)
{
    return false;
}

bool sync_io_logic_analyzer_persona_begin(
    sync_io_logic_analyzer_persona_t *persona,
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *records,
    uint32_t capacity,
    const sync_io_logic_analyzer_config_t *config)
{
    (void)persona; (void)capture; (void)records; (void)capacity; (void)config;
    return false;
}

void sync_io_logic_analyzer_persona_end(
    sync_io_logic_analyzer_persona_t *persona)
{
    (void)persona;
}

bool sync_io_logic_analyzer_persona_active(
    const sync_io_logic_analyzer_persona_t *persona)
{
    (void)persona;
    return false;
}

void sync_io_logic_analyzer_persona_get_snapshot(
    const sync_io_logic_analyzer_persona_t *persona,
    sync_io_persona_manager_snapshot_t *snapshot)
{
    (void)persona;
    if (snapshot != NULL) {
        memset(snapshot, 0, sizeof(*snapshot));
    }
}
#endif

bool sync_io_logic_analyzer_request_arm(
    const sync_io_logic_analyzer_config_t *config)
{
    if (!sync_io_logic_analyzer_config_valid(config)) {
        return false;
    }
    const uint32_t request =
        __atomic_load_n(&s_control.request_sequence, __ATOMIC_ACQUIRE);
    const uint32_t handled =
        __atomic_load_n(&s_control.handled_sequence, __ATOMIC_ACQUIRE);
    if (request != handled ||
        __atomic_load_n(&s_control.shadow_ready, __ATOMIC_ACQUIRE) != 0u ||
        (s_active_persona != NULL && s_active_persona->initialized)) {
        __atomic_store_n(&s_control.result,
                         SYNC_IO_LOGIC_ANALYZER_COMMAND_RESULT_BUSY,
                         __ATOMIC_RELEASE);
        return false;
    }

    s_control.config = *config;
    __atomic_store_n(&s_control.command,
                     SYNC_IO_LOGIC_ANALYZER_COMMAND_ARM,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&s_control.result,
                     SYNC_IO_LOGIC_ANALYZER_COMMAND_RESULT_NONE,
                     __ATOMIC_RELAXED);
    uint32_t next = request + 1u;
    if (next == 0u) {
        next = 1u;
    }
    __atomic_store_n(&s_control.request_sequence, next, __ATOMIC_RELEASE);
    return true;
}

bool sync_io_logic_analyzer_request_stop(void)
{
    const uint32_t request =
        __atomic_load_n(&s_control.request_sequence, __ATOMIC_ACQUIRE);
    const uint32_t handled =
        __atomic_load_n(&s_control.handled_sequence, __ATOMIC_ACQUIRE);
    if (request != handled) {
        __atomic_store_n(&s_control.result,
                         SYNC_IO_LOGIC_ANALYZER_COMMAND_RESULT_BUSY,
                         __ATOMIC_RELEASE);
        return false;
    }

    __atomic_store_n(&s_control.command,
                     SYNC_IO_LOGIC_ANALYZER_COMMAND_STOP,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&s_control.result,
                     SYNC_IO_LOGIC_ANALYZER_COMMAND_RESULT_NONE,
                     __ATOMIC_RELAXED);
    uint32_t next = request + 1u;
    if (next == 0u) {
        next = 1u;
    }
    __atomic_store_n(&s_control.request_sequence, next, __ATOMIC_RELEASE);
    return true;
}

void sync_io_logic_analyzer_service_core1(uint32_t max_records)
{
    const uint32_t request =
        __atomic_load_n(&s_control.request_sequence, __ATOMIC_ACQUIRE);
    const uint32_t handled =
        __atomic_load_n(&s_control.handled_sequence, __ATOMIC_ACQUIRE);
    if (request != handled) {
        const sync_io_logic_analyzer_command_t command =
            (sync_io_logic_analyzer_command_t)__atomic_load_n(
                &s_control.command, __ATOMIC_RELAXED);
        bool accepted = false;
        if (command == SYNC_IO_LOGIC_ANALYZER_COMMAND_ARM) {
            accepted = sync_io_logic_analyzer_persona_begin(
                &s_control.persona, &s_control.capture,
                sync_io_logic_analyzer_active_records(),
                s_control.config.max_records, &s_control.config);
        } else if (command == SYNC_IO_LOGIC_ANALYZER_COMMAND_STOP) {
            if (s_control.persona.initialized) {
                sync_io_logic_analyzer_persona_end(&s_control.persona);
            }
            sync_io_logic_analyzer_publish_shadow();
            accepted = true;
        }
        __atomic_store_n(
            &s_control.result,
            accepted ? SYNC_IO_LOGIC_ANALYZER_COMMAND_RESULT_ACCEPTED
                     : SYNC_IO_LOGIC_ANALYZER_COMMAND_RESULT_REJECTED,
            __ATOMIC_RELEASE);
        __atomic_store_n(&s_control.handled_sequence, request,
                         __ATOMIC_RELEASE);
    }

    if (s_active_persona != NULL && s_active_persona->initialized &&
        s_active_persona->active) {
        (void)sync_io_logic_analyzer_hw_service(max_records);
    }
}

void sync_io_logic_analyzer_get_control_status(
    uint32_t *request_sequence,
    uint32_t *handled_sequence,
    sync_io_logic_analyzer_command_t *command,
    sync_io_logic_analyzer_command_result_t *result)
{
    if (request_sequence != NULL) {
        *request_sequence = __atomic_load_n(
            &s_control.request_sequence, __ATOMIC_ACQUIRE);
    }
    if (handled_sequence != NULL) {
        *handled_sequence = __atomic_load_n(
            &s_control.handled_sequence, __ATOMIC_ACQUIRE);
    }
    if (command != NULL) {
        *command = (sync_io_logic_analyzer_command_t)__atomic_load_n(
            &s_control.command, __ATOMIC_ACQUIRE);
    }
    if (result != NULL) {
        *result =
            (sync_io_logic_analyzer_command_result_t)__atomic_load_n(
                &s_control.result, __ATOMIC_ACQUIRE);
    }
}

void sync_io_logic_analyzer_get_status(
    sync_io_logic_analyzer_status_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    sync_io_logic_analyzer_get_control_status(
        &status->command_sequence, &status->command_handled_sequence,
        &status->command, &status->command_result);
    const sync_io_logic_analyzer_persona_t *persona = s_active_persona;
    const sync_io_logic_analyzer_raw_capture_t *capture = NULL;
    if (persona != NULL && persona->initialized && persona->capture != NULL) {
        capture = persona->capture;
    } else if (__atomic_load_n(&s_control.shadow_ready, __ATOMIC_ACQUIRE) != 0u) {
        capture = &s_control.shadow_capture;
    } else if (s_control.shadow_capture.initialized) {
        capture = &s_control.shadow_capture;
    } else if (s_control.capture.initialized) {
        /* Keep the last Core1-owned capture snapshot readable after STOP and
         * persona release.  This is a shadow read only: it never reclaims
         * the lease, consumes records, or touches hardware. */
        capture = &s_control.capture;
    }
    if (capture == NULL) {
        return;
    }

    sync_io_logic_analyzer_snapshot_payload_t capture_snapshot;
    if (!sync_io_logic_analyzer_raw_capture_snapshot(
            capture, &capture_snapshot)) {
        return;
    }
    sync_io_persona_manager_snapshot_t manager_snapshot;
    sync_io_logic_analyzer_persona_get_snapshot(persona, &manager_snapshot);
    status->initialized = true;
    status->active = persona != NULL && persona->active;
    status->state = capture_snapshot.state;
    status->mode = capture_snapshot.mode;
    status->end_reason = capture_snapshot.end_reason;
    status->capture_sequence = capture_snapshot.capture_sequence;
    status->source_mask = capture_snapshot.source_mask;
    status->profile_generation = capture_snapshot.profile_generation;
    status->persona_generation = capture_snapshot.persona_generation;
    status->hardware_tick_hz = capture_snapshot.hardware_tick_hz;
    status->timestamp_resolution_ns = capture_snapshot.timestamp_resolution_ns;
    status->produced_records = capture_snapshot.produced_records;
    status->consumed_records = capture_snapshot.consumed_records;
    status->dropped_records = capture_snapshot.dropped_records;
    status->overrun_count = capture_snapshot.overrun_count;
    status->data_crc32 = capture_snapshot.data_crc32;
    status->manager_active_count = manager_snapshot.active_count;
    status->manager_used_sm_mask = manager_snapshot.used_sm_mask;
    status->manager_used_dma_channel_mask =
        manager_snapshot.used_dma_channel_mask;
    status->manager_last_error = manager_snapshot.last_error;
    status->manager_last_conflict_mask = manager_snapshot.last_conflict_mask;
}

size_t sync_io_logic_analyzer_drain_core0(
    sync_io_logic_analyzer_record_t *records,
    uint32_t capacity)
{
    if (records == NULL || capacity == 0u ||
        (s_active_persona != NULL &&
         sync_io_logic_analyzer_persona_active(s_active_persona)) ||
        sync_io_logic_analyzer_hw_active() ||
        (!s_control.capture.initialized &&
         !s_control.shadow_capture.initialized)) {
        return 0u;
    }

    if (__atomic_load_n(&s_control.shadow_sequence, __ATOMIC_ACQUIRE) & 1u) {
        return 0u;
    }
    sync_io_logic_analyzer_raw_capture_t *source =
        __atomic_load_n(&s_control.shadow_ready, __ATOMIC_ACQUIRE) != 0u
        ? &s_control.shadow_capture : &s_control.capture;
    size_t drained = 0u;
    while (drained < capacity &&
           sync_io_logic_analyzer_raw_capture_pop(
               source, &records[drained])) {
        ++drained;
    }
    if (source == &s_control.shadow_capture &&
        source->consumed_records == source->produced_records) {
        __atomic_store_n(&s_control.shadow_ready, 0u, __ATOMIC_RELEASE);
    }
    return drained;
}

static bool sync_io_logic_analyzer_source_mask_valid(uint32_t source_mask)
{
    const sync_io_persona_descriptor_t *descriptor =
        sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_LOGIC_ANALYZER);
    return sync_io_persona_descriptor_valid(descriptor) &&
           (descriptor->flags & SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD) != 0u &&
           descriptor->gpio_write_mask == 0u &&
           source_mask != 0u &&
           (source_mask & ~descriptor->gpio_read_mask) == 0u;
}

uint32_t sync_io_logic_analyzer_default_source_mask(void)
{
    const sync_io_persona_descriptor_t *descriptor =
        sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_LOGIC_ANALYZER);
    return descriptor != NULL ? descriptor->gpio_read_mask : 0u;
}

static uint32_t sync_io_logic_analyzer_crc32_update(uint32_t crc,
                                                    const uint8_t *data,
                                                    size_t size)
{
    for (size_t index = 0u; index < size; ++index) {
        crc ^= data[index];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u
                ? (crc >> 1u) ^ 0xEDB88320u
                : crc >> 1u;
        }
    }
    return crc;
}

static bool sync_io_logic_analyzer_mask_valid(uint32_t mask,
                                              uint32_t source_mask)
{
    return (mask & ~source_mask) == 0u;
}

bool sync_io_logic_analyzer_config_valid(
    const sync_io_logic_analyzer_config_t *config)
{
    if (config == NULL ||
        config->contract_version != SYNC_IO_LOGIC_ANALYZER_CONTRACT_VERSION ||
        config->mode <= SYNC_IO_LOGIC_ANALYZER_MODE_INVALID ||
        config->mode >= SYNC_IO_LOGIC_ANALYZER_MODE_COUNT ||
        !sync_io_logic_analyzer_source_mask_valid(config->source_mask) ||
        config->max_records == 0u ||
        config->max_records > SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS ||
        config->timeout_us == 0u ||
        config->overwrite_oldest > 1u ||
        config->expected_profile_generation == 0u ||
        config->expected_persona_generation == 0u ||
        config->debug_continue_budget >
            SYNC_IO_LOGIC_ANALYZER_DEBUG_CONTINUE_LIMIT) {
        return false;
    }

    if (config->mode == SYNC_IO_LOGIC_ANALYZER_MODE_EDGE_TIMESTAMP) {
        if (config->sample_period_ns != 0u) {
            return false;
        }
    } else if (config->sample_period_ns == 0u) {
        return false;
    }

    const bool triggered =
        config->mode == SYNC_IO_LOGIC_ANALYZER_MODE_TRIGGERED_CAPTURE;
    if (!triggered) {
        return config->trigger.type == SYNC_IO_LOGIC_ANALYZER_TRIGGER_NONE &&
               config->pre_trigger_records == 0u &&
               config->post_trigger_records == 0u;
    }

    if (config->trigger.type <= SYNC_IO_LOGIC_ANALYZER_TRIGGER_NONE ||
        config->trigger.type >= SYNC_IO_LOGIC_ANALYZER_TRIGGER_COUNT ||
        config->pre_trigger_records > config->max_records ||
        config->post_trigger_records >
            config->max_records - config->pre_trigger_records ||
        config->trigger.source_mask == 0u ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.source_mask,
                                           config->source_mask) ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.level_mask,
                                           config->trigger.source_mask) ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.edge_mask,
                                           config->trigger.source_mask) ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.pattern_mask,
                                           config->trigger.source_mask) ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.pattern_value,
                                           config->trigger.pattern_mask)) {
        return false;
    }

    if (config->trigger.type == SYNC_IO_LOGIC_ANALYZER_TRIGGER_LEVEL) {
        return config->trigger.level_mask != 0u;
    }
    if (config->trigger.type == SYNC_IO_LOGIC_ANALYZER_TRIGGER_EDGE) {
        return config->trigger.edge_mask != 0u;
    }
    return config->trigger.pattern_mask != 0u;
}

sync_io_logic_analyzer_gate_action_t sync_io_logic_analyzer_gate_action(
    sync_io_logic_analyzer_gate_reason_t reason,
    bool product_mode,
    uint32_t debug_continue_count,
    uint32_t debug_continue_budget)
{
    if (reason <= SYNC_IO_LOGIC_ANALYZER_GATE_NONE ||
        reason >= SYNC_IO_LOGIC_ANALYZER_GATE_COUNT) {
        return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_REJECT;
    }
    const bool hard_stop =
        reason == SYNC_IO_LOGIC_ANALYZER_GATE_DMA_BOUNDS ||
        reason == SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_MEMORY ||
        reason == SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_FLASH ||
        reason == SYNC_IO_LOGIC_ANALYZER_GATE_UNCONTROLLED_GPIO;
    if (hard_stop) {
        return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_HARD_STOP;
    }
    if (product_mode) {
        return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_REJECT;
    }
    if (debug_continue_count >= debug_continue_budget) {
        return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_ROUND_END;
    }
    return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_CONTINUE;
}

bool sync_io_logic_analyzer_raw_capture_init(
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *records,
    uint32_t capacity,
    const sync_io_logic_analyzer_config_t *config)
{
    if (capture == NULL || records == NULL || config == NULL ||
        !sync_io_logic_analyzer_config_valid(config) || capacity == 0u ||
        capacity > SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS ||
        capacity > config->max_records) {
        return false;
    }
    /* Hardware ARM may reinitialize an already prepared capture with
     * config == &capture->config.  Preserve the accepted value before
     * clearing runtime counters so that self-aliasing cannot silently turn
     * RAW_SAMPLE/overwrite settings into zero. */
    const sync_io_logic_analyzer_config_t accepted_config = *config;
    memset(capture, 0, sizeof(*capture));
    capture->records = records;
    capture->config = accepted_config;
    capture->capacity = capacity;
    capture->state = SYNC_IO_LOGIC_ANALYZER_STATE_ARMED;
    capture->initialized = true;
    return true;
}

bool sync_io_logic_analyzer_raw_capture_push(
    sync_io_logic_analyzer_raw_capture_t *capture,
    const sync_io_logic_analyzer_record_t *record)
{
    if (capture == NULL || record == NULL || !capture->initialized ||
        capture->complete) {
        return false;
    }
    capture->state = SYNC_IO_LOGIC_ANALYZER_STATE_RUNNING;
    const uint32_t retained = capture->produced_records -
                              capture->consumed_records;
    sync_io_logic_analyzer_record_t next = *record;
    if (retained >= capture->capacity) {
        capture->dropped_records++;
        capture->overrun_count++;
        if (capture->config.overwrite_oldest == 0u) {
            capture->end_reason = SYNC_IO_LOGIC_ANALYZER_END_OVERFLOW;
            capture->complete = true;
            capture->state = SYNC_IO_LOGIC_ANALYZER_STATE_COMPLETE;
            return false;
        }
        capture->consumed_records++;
        capture->read_index = (capture->read_index + 1u) % capture->capacity;
        next.flags |= SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_DISCONTINUITY;
    }
    capture->records[capture->write_index] = next;
    capture->write_index = (capture->write_index + 1u) % capture->capacity;
    capture->produced_records++;
    return true;
}

bool sync_io_logic_analyzer_raw_capture_pop(
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *record)
{
    if (capture == NULL || record == NULL || !capture->initialized ||
        capture->consumed_records == capture->produced_records) {
        return false;
    }
    *record = capture->records[capture->read_index];
    capture->read_index = (capture->read_index + 1u) % capture->capacity;
    capture->consumed_records++;
    return true;
}

void sync_io_logic_analyzer_raw_capture_finish(
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_end_reason_t reason)
{
    if (capture == NULL || !capture->initialized || capture->complete) {
        return;
    }
    capture->end_reason = reason;
    capture->complete = true;
    capture->state = reason == SYNC_IO_LOGIC_ANALYZER_END_DMA_FAULT
        ? SYNC_IO_LOGIC_ANALYZER_STATE_FAULT
        : SYNC_IO_LOGIC_ANALYZER_STATE_COMPLETE;
}

uint32_t sync_io_logic_analyzer_raw_capture_crc32(
    const sync_io_logic_analyzer_raw_capture_t *capture)
{
    if (capture == NULL || !capture->initialized || capture->records == NULL) {
        return 0u;
    }
    uint32_t crc = 0xFFFFFFFFu;
    const uint32_t retained = capture->produced_records -
                              capture->consumed_records;
    uint32_t index = capture->read_index;
    for (uint32_t count = 0u; count < retained; ++count) {
        crc = sync_io_logic_analyzer_crc32_update(
            crc, (const uint8_t *)&capture->records[index],
            sizeof(capture->records[index]));
        index = (index + 1u) % capture->capacity;
    }
    return crc ^ 0xFFFFFFFFu;
}

bool sync_io_logic_analyzer_raw_capture_snapshot(
    const sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_snapshot_payload_t *snapshot)
{
    if (capture == NULL || snapshot == NULL || !capture->initialized) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->contract_version = capture->config.contract_version;
    snapshot->state = capture->state;
    snapshot->mode = capture->config.mode;
    snapshot->end_reason = capture->end_reason;
    snapshot->capture_sequence = 1u;
    snapshot->source_mask = capture->config.source_mask;
    snapshot->profile_generation = capture->config.expected_profile_generation;
    snapshot->persona_generation = capture->config.expected_persona_generation;
    snapshot->hardware_tick_hz = 1000000000u;
    snapshot->timestamp_resolution_ns = capture->config.sample_period_ns != 0u
        ? capture->config.sample_period_ns : 1u;
    snapshot->produced_records = capture->produced_records;
    snapshot->consumed_records = capture->consumed_records;
    snapshot->dropped_records = capture->dropped_records;
    snapshot->overrun_count = capture->overrun_count;
    snapshot->data_crc32 = sync_io_logic_analyzer_raw_capture_crc32(capture);
    snapshot->capture_complete = capture->complete ? 1u : 0u;
    return true;
}

bool sync_io_logic_analyzer_snapshot_read(
    const sync_io_logic_analyzer_snapshot_t *source,
    sync_io_logic_analyzer_snapshot_payload_t *snapshot)
{
    if (source == NULL || snapshot == NULL) {
        return false;
    }

    for (uint32_t attempt = 0u;
         attempt < SYNC_IO_LOGIC_ANALYZER_SNAPSHOT_READ_ATTEMPTS;
         attempt++) {
        const uint32_t before =
            __atomic_load_n(&source->sequence_lock, __ATOMIC_ACQUIRE);
        if ((before & 1u) != 0u) {
            continue;
        }
        memcpy(snapshot, &source->payload, sizeof(*snapshot));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t after =
            __atomic_load_n(&source->sequence_lock, __ATOMIC_RELAXED);
        if (before == after && (after & 1u) == 0u) {
            return true;
        }
    }
    return false;
}
