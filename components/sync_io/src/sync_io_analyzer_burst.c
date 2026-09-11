#include "sync_io_analyzer_burst.h"

#include <string.h>
#include "board_config.h"

#define BURST_PIN_BASE BOARD_TDMA_TX_DATA_IN_PIN
#define BURST_SOURCE_MASK ((1u << BOARD_TDMA_TX_CLK_OUT_PIN) | \
    (1u << BOARD_TDMA_TX_SYNC_OUT_PIN) | (1u << BOARD_TDMA_TX_DATA_IN_PIN) | \
    (1u << BOARD_TDMA_RX_CLK_IN_PIN) | (1u << BOARD_TDMA_RX_SYNC_IN_PIN) | \
    (1u << BOARD_TDMA_RX_DATA_OUT_PIN))

_Static_assert(BURST_SOURCE_MASK == (0x3fu << BURST_PIN_BASE),
               "finite analyzer requires the board's contiguous six-pad group");
_Static_assert(SYNC_IO_ANALYZER_BURST_MAX_WORDS *
               SYNC_IO_ANALYZER_BURST_SAMPLES_PER_WORD <= (1u << 20),
               "finite sample counter must fit the fixed register preload");

static volatile uint32_t s_publish_sequence;
static volatile uint32_t s_busy;
static sync_io_analyzer_burst_snapshot_t s_published;
static volatile uint32_t s_export_publish_sequence;
static volatile uint32_t s_export_retry_sequence;
static sync_io_analyzer_burst_export_snapshot_t s_export_facts;

bool sync_io_analyzer_burst_get_export_snapshot(
    sync_io_analyzer_burst_export_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        const uint32_t before = __atomic_load_n(&s_export_publish_sequence, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        *snapshot = s_export_facts;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (before == __atomic_load_n(&s_export_publish_sequence, __ATOMIC_RELAXED)) return true;
    }
    return false;
}

void sync_io_analyzer_burst_begin_export_core0(uint32_t sequence)
{
    __atomic_fetch_add(&s_export_publish_sequence, 1u, __ATOMIC_ACQ_REL);
    s_export_facts = (sync_io_analyzer_burst_export_snapshot_t){.capture_sequence = sequence};
    __atomic_fetch_add(&s_export_publish_sequence, 1u, __ATOMIC_RELEASE);
}

void sync_io_analyzer_burst_export_failed_core0(uint32_t job, uint32_t error)
{
    __atomic_fetch_add(&s_export_publish_sequence, 1u, __ATOMIC_ACQ_REL);
    ++s_export_facts.failure_count;
    s_export_facts.last_failed_job = job;
    s_export_facts.last_error = error;
    __atomic_fetch_add(&s_export_publish_sequence, 1u, __ATOMIC_RELEASE);
}

bool sync_io_analyzer_burst_take_export_retry_core0(uint32_t sequence)
{
    if (sequence == 0u || !__atomic_compare_exchange_n(&s_export_retry_sequence,
            &sequence, 0u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    __atomic_fetch_add(&s_export_publish_sequence, 1u, __ATOMIC_ACQ_REL);
    ++s_export_facts.retry_count;
    __atomic_fetch_add(&s_export_publish_sequence, 1u, __ATOMIC_RELEASE);
    return true;
}

bool sync_io_analyzer_burst_retry_export_core1(uint32_t sequence)
{
    sync_io_analyzer_burst_snapshot_t snapshot;
    if (sequence == 0u || !sync_io_analyzer_burst_busy() ||
        !sync_io_analyzer_burst_get_snapshot(&snapshot) ||
        snapshot.state != SYNC_IO_ANALYZER_BURST_FROZEN ||
        snapshot.capture_sequence != sequence) return false;
    uint32_t expected = 0u;
    return __atomic_compare_exchange_n(&s_export_retry_sequence, &expected,
        sequence, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

bool sync_io_analyzer_burst_config_valid(
    const sync_io_analyzer_burst_config_t *config)
{
    return config != NULL && config->word_count != 0u &&
        config->word_count <= SYNC_IO_ANALYZER_BURST_MAX_WORDS &&
        config->clkdiv != 0u &&
        config->clkdiv <= SYNC_IO_ANALYZER_BURST_MAX_DIVIDER &&
        config->timeout_us != 0u &&
        config->timeout_us <= SYNC_IO_ANALYZER_BURST_MAX_TIMEOUT_US &&
        config->trigger_rx <= 1u && config->capture_tag != 0u;
}

bool sync_io_analyzer_burst_busy(void)
{
    return __atomic_load_n(&s_busy, __ATOMIC_ACQUIRE) != 0u;
}

bool sync_io_analyzer_burst_get_snapshot(
    sync_io_analyzer_burst_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 8u; ++attempt) {
        const uint32_t before = __atomic_load_n(
            &s_publish_sequence, __ATOMIC_ACQUIRE);
        if ((before & 1u) != 0u) continue;
        *snapshot = s_published;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (before == __atomic_load_n(&s_publish_sequence, __ATOMIC_RELAXED)) {
            return snapshot->schema == SYNC_IO_ANALYZER_BURST_SCHEMA;
        }
    }
    return false;
}

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "sync_io_core_internal.h"
#include "sync_io_persona_manager.h"
#include "sync_io.pio.h"

typedef struct {
    sync_io_persona_manager_t manager;
    sync_io_persona_manager_handle_t handle;
    sync_io_analyzer_burst_config_t config;
    sync_io_analyzer_burst_snapshot_t facts;
    uint offset;
    bool loaded;
    bool running;
    uint64_t started_ns;
} burst_owner_t;

static burst_owner_t s_burst;
static uint32_t s_capture_sequence;

static void burst_publish(void)
{
    __atomic_fetch_add(&s_publish_sequence, 1u, __ATOMIC_ACQ_REL);
    s_published = s_burst.facts;
    __atomic_fetch_add(&s_publish_sequence, 1u, __ATOMIC_RELEASE);
}

static void burst_stop_hw(void)
{
    if (!s_burst.loaded) return;
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST,
                       BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM, false);
    dma_channel_abort(SYNC_IO_CAPTURE_DMA_CH);
    s_burst.running = false;
}

static bool burst_load(void *context,
                       const sync_io_persona_descriptor_t *descriptor,
                       uint32_t dma_mask)
{
    (void)context;
    if (descriptor == NULL || descriptor->gpio_write_mask != 0u ||
        descriptor->instruction_words < logic_analyzer_finite_sample_program.length ||
        dma_mask != (1u << SYNC_IO_CAPTURE_DMA_CH) ||
        !pio_can_add_program(BOARD_SYNC_PIO_FAST,
                             &logic_analyzer_finite_sample_program)) return false;
    s_burst.offset = pio_add_program(BOARD_SYNC_PIO_FAST,
                                     &logic_analyzer_finite_sample_program);
    s_burst.loaded = true;
    return true;
}

static bool burst_arm(void *context,
                      const sync_io_persona_descriptor_t *descriptor,
                      uint32_t dma_mask)
{
    (void)context;
    (void)descriptor;
    (void)dma_mask;
    PIO pio = BOARD_SYNC_PIO_FAST;
    const uint sm = BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM;
    const uint pin = s_burst.facts.trigger_pin;
    pio_sm_set_enabled(pio, sm, false);
    pio->instr_mem[s_burst.offset +
        logic_analyzer_finite_sample_offset_trigger_idle] =
            pio_encode_wait_gpio(true, pin);
    pio->instr_mem[s_burst.offset +
        logic_analyzer_finite_sample_offset_trigger_active] =
            pio_encode_wait_gpio(false, pin);
    pio_sm_config config =
        logic_analyzer_finite_sample_program_get_default_config(s_burst.offset);
    sm_config_set_in_pins(&config, BURST_PIN_BASE);
    sm_config_set_in_shift(&config, false, true,
        SYNC_IO_ANALYZER_BURST_SAMPLE_BITS * SYNC_IO_ANALYZER_BURST_SAMPLES_PER_WORD);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv_int_frac(&config, (uint16_t)s_burst.config.clkdiv, 0u);
    pio_sm_init(pio, sm, s_burst.offset, &config);
    pio_sm_clear_fifos(pio, sm);
    /* No GPIO mux, direction, pull or output-latch write: pad reads only. */
    const uint32_t count = s_burst.config.word_count *
        SYNC_IO_ANALYZER_BURST_SAMPLES_PER_WORD - 1u;
    pio_sm_exec(pio, sm, pio_encode_mov(pio_isr, pio_null));
    /* Four fixed five-bit groups build the bounded counter in ISR, then X.
     * Autopush is at 30 bits, above this 20-bit preload. SM restart clears
     * ISR/shift counters while retaining X and the configured entry PC. */
    for (int shift = 15; shift >= 0; shift -= 5) {
        pio_sm_exec(pio, sm, pio_encode_set(pio_x, (count >> shift) & 31u));
        pio_sm_exec(pio, sm, pio_encode_in(pio_x, 5u));
    }
    pio_sm_exec(pio, sm, pio_encode_mov(pio_x, pio_isr));
    pio_sm_restart(pio, sm);
    pio->fdebug = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm);
    dma_channel_abort(SYNC_IO_CAPTURE_DMA_CH);
    dma_channel_set_irq0_enabled(SYNC_IO_CAPTURE_DMA_CH, false);
    dma_channel_set_irq1_enabled(SYNC_IO_CAPTURE_DMA_CH, false);
    /* Reset only this lease's stale sticky DMA errors before acquisition. */
    dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].ctrl_trig =
        DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS;
    dma_channel_config dma = dma_channel_get_default_config(SYNC_IO_CAPTURE_DMA_CH);
    channel_config_set_transfer_data_size(&dma, DMA_SIZE_32);
    channel_config_set_read_increment(&dma, false);
    channel_config_set_write_increment(&dma, true);
    channel_config_set_dreq(&dma, pio_get_dreq(pio, sm, false));
    dma_channel_configure(SYNC_IO_CAPTURE_DMA_CH, &dma,
        sync_io_shared_workspace, &pio->rxf[sm], s_burst.config.word_count, false);
    s_burst.facts.clkdiv_256 = (pio->sm[sm].clkdiv >> 8u) & 0x00ffffffu;
    return true;
}

static bool burst_start(void *context,
                        const sync_io_persona_descriptor_t *descriptor,
                        uint32_t dma_mask)
{
    (void)context;
    (void)descriptor;
    (void)dma_mask;
    if (!sync_io_capture_time_now_ns(&s_burst.started_ns)) return false;
    dma_start_channel_mask(1u << SYNC_IO_CAPTURE_DMA_CH);
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST,
                       BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM, true);
    s_burst.running = true;
    return true;
}

static void burst_stop(void *context,
                       const sync_io_persona_descriptor_t *descriptor,
                       uint32_t dma_mask)
{
    (void)context;
    (void)descriptor;
    (void)dma_mask;
    burst_stop_hw();
}

static void burst_cleanup(void *context,
                          const sync_io_persona_descriptor_t *descriptor,
                          uint32_t dma_mask)
{
    burst_stop(context, descriptor, dma_mask);
    if (s_burst.loaded) {
        pio_remove_program(BOARD_SYNC_PIO_FAST,
                            &logic_analyzer_finite_sample_program, s_burst.offset);
        s_burst.loaded = false;
    }
}

bool sync_io_analyzer_burst_begin_core1(
    const sync_io_analyzer_burst_config_t *config)
{
    if (!sync_io_analyzer_burst_config_valid(config) ||
        sync_io_analyzer_burst_busy()) return false;
    __atomic_store_n(&s_busy, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_export_retry_sequence, 0u, __ATOMIC_RELEASE);
    memset(&s_burst, 0, sizeof(s_burst));
    s_burst.config = *config;
    if (++s_capture_sequence == 0u) ++s_capture_sequence;
    s_burst.facts = (sync_io_analyzer_burst_snapshot_t){
        .schema = SYNC_IO_ANALYZER_BURST_SCHEMA,
        .capture_sequence = s_capture_sequence,
        .capture_tag = config->capture_tag,
        .requested_words = config->word_count,
        .clk_sys_hz = clock_get_hz(clk_sys),
        .sample_cycles = SYNC_IO_ANALYZER_BURST_SAMPLE_CYCLES,
        .samples_per_word = SYNC_IO_ANALYZER_BURST_SAMPLES_PER_WORD,
        .sample_bits = SYNC_IO_ANALYZER_BURST_SAMPLE_BITS,
        .pin_base = BURST_PIN_BASE,
        .source_mask = BURST_SOURCE_MASK,
        .trigger_pin = config->trigger_rx ? BOARD_TDMA_RX_SYNC_IN_PIN
                                         : BOARD_TDMA_TX_SYNC_OUT_PIN,
        .trigger_level = 0u,
        /* Lossless packed board pin mapping, in TX CLK/SYNC/DATA then RX
         * CLK/SYNC/DATA order. TDMA active matrix is bound by the HIL report. */
        .profile_identity = BOARD_TDMA_TX_CLK_OUT_PIN |
            (BOARD_TDMA_TX_SYNC_OUT_PIN << 5u) | (BOARD_TDMA_TX_DATA_IN_PIN << 10u) |
            (BOARD_TDMA_RX_CLK_IN_PIN << 15u) | (BOARD_TDMA_RX_SYNC_IN_PIN << 20u) |
            (BOARD_TDMA_RX_DATA_OUT_PIN << 25u),
    };
    const sync_io_persona_manager_hooks_t hooks = {
        .load = burst_load, .arm = burst_arm, .start = burst_start,
        .stop = burst_stop, .cleanup = burst_cleanup,
    };
    sync_io_persona_manager_init(&s_burst.manager, &hooks, NULL);
    if (!sync_io_core_initialized() || sync_io_core_capture_is_running() ||
        sync_io_core_wave_output_persona_active() ||
        !sync_io_persona_manager_claim(&s_burst.manager,
            SYNC_IO_PERSONA_ID_LOGIC_ANALYZER, &s_burst.handle, NULL) ||
        !sync_io_persona_manager_load(&s_burst.manager, &s_burst.handle) ||
        !sync_io_persona_manager_arm(&s_burst.manager, &s_burst.handle) ||
        !sync_io_persona_manager_start(&s_burst.manager, &s_burst.handle)) {
        s_burst.facts.state = SYNC_IO_ANALYZER_BURST_REJECTED;
        s_burst.facts.end_reason = SYNC_IO_ANALYZER_BURST_END_RESOURCE;
        s_burst.facts.manager_error = s_burst.manager.last_error;
        s_burst.facts.conflict_mask = s_burst.manager.last_conflict_mask;
        if (sync_io_persona_manager_handle_valid(&s_burst.manager, &s_burst.handle))
            (void)sync_io_persona_manager_release(&s_burst.manager, &s_burst.handle);
        (void)sync_io_persona_manager_deinit(&s_burst.manager);
        burst_publish();
        __atomic_store_n(&s_busy, 0u, __ATOMIC_RELEASE);
        return false;
    }
    s_burst.facts.persona_generation = s_burst.handle.generation;
    s_burst.facts.state = SYNC_IO_ANALYZER_BURST_CAPTURING;
    burst_publish();
    return true;
}

static void burst_freeze(sync_io_analyzer_burst_end_t reason)
{
    burst_stop_hw();
    const uint32_t remaining = dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].transfer_count;
    const uintptr_t base = (uintptr_t)sync_io_shared_workspace;
    const uintptr_t address = dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].write_addr;
    const bool address_valid = address >= base &&
        address - base <= s_burst.config.word_count * sizeof(uint32_t) &&
        (address - base) % sizeof(uint32_t) == 0u;
    s_burst.facts.captured_words = address_valid
        ? (uint32_t)((address - base) / sizeof(uint32_t)) : 0u;
    s_burst.facts.dma_remaining = remaining;
    s_burst.facts.pio_fdebug = BOARD_SYNC_PIO_FAST->fdebug;
    s_burst.facts.dma_ctrl = dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].ctrl_trig;
    if (!address_valid || (s_burst.facts.dma_ctrl &
            (DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS)))
        reason = SYNC_IO_ANALYZER_BURST_END_DMA_ERROR;
    else if (s_burst.facts.pio_fdebug &
             (1u << (PIO_FDEBUG_RXSTALL_LSB + BOARD_SYNC_PIO0_LOGIC_ANALYZER_SM)))
        reason = SYNC_IO_ANALYZER_BURST_END_RX_STALL;
    s_burst.facts.end_reason = reason;
    s_burst.facts.timing_valid = reason == SYNC_IO_ANALYZER_BURST_END_COMPLETE &&
        remaining == 0u && s_burst.facts.captured_words == s_burst.config.word_count &&
        clock_get_hz(clk_sys) == s_burst.facts.clk_sys_hz;
    s_burst.facts.state = SYNC_IO_ANALYZER_BURST_FROZEN;
    /* DMA is stopped. Publish immutable words without copying the workspace
     * or releasing the lease. Core0's final export acknowledgement releases it. */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    burst_publish();
}

void sync_io_analyzer_burst_service_core1(void)
{
    if (!s_burst.running) return;
    if (!dma_channel_is_busy(SYNC_IO_CAPTURE_DMA_CH)) {
        burst_freeze(SYNC_IO_ANALYZER_BURST_END_COMPLETE);
        return;
    }
    uint64_t now_ns = 0u;
    (void)sync_io_capture_time_now_ns(&now_ns);
    if (now_ns - s_burst.started_ns >= (uint64_t)s_burst.config.timeout_us * 1000u)
        burst_freeze(SYNC_IO_ANALYZER_BURST_END_TIMEOUT);
}

void sync_io_analyzer_burst_stop_core1(void)
{
    if (s_burst.running) burst_freeze(SYNC_IO_ANALYZER_BURST_END_STOP);
}

bool sync_io_analyzer_burst_release_core1(uint32_t sequence)
{
    if (!sync_io_analyzer_burst_busy() ||
        s_burst.facts.state != SYNC_IO_ANALYZER_BURST_FROZEN ||
        s_burst.facts.capture_sequence != sequence) return false;
    if (!sync_io_persona_manager_release(&s_burst.manager, &s_burst.handle)) return false;
    (void)sync_io_persona_manager_deinit(&s_burst.manager);
    s_burst.facts.state = SYNC_IO_ANALYZER_BURST_RELEASED;
    burst_publish();
    __atomic_store_n(&s_busy, 0u, __ATOMIC_RELEASE);
    return true;
}

size_t sync_io_analyzer_burst_copy_core0(
    uint32_t sequence, uint32_t first_word, uint32_t *words, uint32_t capacity)
{
    sync_io_analyzer_burst_snapshot_t snapshot;
    if (words == NULL || capacity == 0u ||
        capacity > SYNC_IO_ANALYZER_BURST_COPY_WORDS ||
        !sync_io_analyzer_burst_get_snapshot(&snapshot) ||
        snapshot.state != SYNC_IO_ANALYZER_BURST_FROZEN ||
        snapshot.capture_sequence != sequence || first_word >= snapshot.captured_words)
        return 0u;
    uint32_t count = snapshot.captured_words - first_word;
    if (count > capacity) count = capacity;
    memcpy(words, &sync_io_shared_workspace[first_word], count * sizeof(uint32_t));
    return count;
}

#else
bool sync_io_analyzer_burst_begin_core1(const sync_io_analyzer_burst_config_t *config)
{ (void)config; return false; }
void sync_io_analyzer_burst_service_core1(void) {}
void sync_io_analyzer_burst_stop_core1(void) {}
bool sync_io_analyzer_burst_release_core1(uint32_t sequence)
{ (void)sequence; return false; }
size_t sync_io_analyzer_burst_copy_core0(
    uint32_t sequence, uint32_t first_word, uint32_t *words, uint32_t capacity)
{ (void)sequence; (void)first_word; (void)words; (void)capacity; return 0u; }
#endif
