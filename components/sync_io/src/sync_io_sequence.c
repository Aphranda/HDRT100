#include "sync_io_sequence.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "board_config.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico/time.h"
#include "resource_arbiter.h"
#include "sync_io.h"
#include "sync_io_persona_manager.h"
#include "sync_io_sequence.pio.h"

#define SEQUENCE_OWNER "sync_io.sequence"
#define LEGACY_OWNER "sync_io.sma_mutator"
#define SNAPSHOT_WORDS ((sizeof(sync_io_sequence_snapshot_t) + 3u) / 4u)
#define SMA_MASK ((1u << BOARD_SYNC_OUTPUT_PIN_COUNT) - 1u)
#define INGRESS_SM 1u
#define EXECUTOR_SM 2u
#define COUNTER_SM 3u
#define SM_MASK ((1u << INGRESS_SM) | (1u << EXECUTOR_SM) | (1u << COUNTER_SM))
#define INPUT_SM_MASK ((1u << INGRESS_SM) | (1u << COUNTER_SM))
#define READY_IRQ 4u
#define REQUEST_IRQ 5u
#define IRQ_MASK ((1u << READY_IRQ) | (1u << REQUEST_IRQ))
#define RECEIPT_BITS 10u
#define RECEIPT_WORDS ((1u << RECEIPT_BITS) / sizeof(uint32_t))
#define RX_TRANSFERS 0x0FFFFFFFu
#define TICK_HZ (1000000000u / SYNC_IO_SEQUENCE_TICK_NS)
#define NO_INDEX UINT32_MAX
#define PLAN_INDEX_SHIFT 4u
#define ACTIVE_VALUE_SHIFT 12u

_Static_assert(BOARD_SYNC_OUTPUT_PIN_COUNT <= PLAN_INDEX_SHIFT,
               "sequence receipt reserves four output bits");
_Static_assert(SYNC_IO_SEQUENCE_PLAN_MAX <=
               (1u << (ACTIVE_VALUE_SHIFT - PLAN_INDEX_SHIFT)),
               "sequence receipt reserves eight index bits");
_Static_assert(sequence_finite_ingress_wrap + 1u + sequence_executor_wrap + 1u +
               sequence_counter_wrap + 1u == SYNC_IO_SEQUENCE_INSTRUCTION_WORDS,
               "sequence PIO instruction budget changed");
_Static_assert(SYNC_IO_SEQUENCE_INSTRUCTION_WORDS + SYNC_IO_INPUT_CAPTURE_INSTRUCTION_WORDS <=
               SYNC_IO_PERSONA_PIO_INSTRUCTION_CAPACITY, "sequence must preserve capture");
_Static_assert(sequence_gateway_wrap <= sequence_ingress_wrap,
               "gateway replaces ingress within the existing PIO0 budget");

extern bool sync_io_core_initialized(void);
extern bool sync_io_core_wave_output_persona_active(void);
extern bool sync_io_core_model_output_active(void);

static struct {
    sync_io_sequence_config_t config;
    sync_io_sequence_snapshot_t status;
    uint offset[3];
    int dma[4];
    uint32_t dma_mask;
    uint32_t sm_claimed;
    uint32_t loaded;
    uint32_t rx_consumed;
    uint32_t rx_stopped_produced;
    uint32_t last_edges;
    uint32_t paused_edges;
    uint32_t pause_started;
    uint32_t read_address;
    uint32_t input_pin;
    uint32_t initial_output;
    uint64_t prime_ready_at_us;
    gpio_function_t saved_function[BOARD_SYNC_OUTPUT_PIN_COUNT];
    bool saved_direction[BOARD_SYNC_OUTPUT_PIN_COUNT];
    bool pins_saved;
    bool paused;
    bool manager_claimed;
    bool rx_stopped;
    bool priming;
} s_sequence;
static sync_io_persona_manager_t s_manager;
static sync_io_persona_manager_handle_t s_handle;
static uint32_t s_plan[SYNC_IO_SEQUENCE_PLAN_MAX * SYNC_IO_SEQUENCE_PLAN_WORDS];
static uint32_t s_receipts[RECEIPT_WORDS] __attribute__((aligned(1u << RECEIPT_BITS)));
static volatile uint32_t s_edge_latest;

static uint32_t s_reserved;
static uint32_t s_switch1 = 1u;
static uint32_t s_switch2;
static uint32_t s_armed;
static uint32_t s_owned_mask;
static uint32_t s_publish_seq;
static uint32_t s_published[SNAPSHOT_WORDS];
static uint32_t s_legacy_depth[2];

static uint32_t stop_counter(void);
static void gateway_service(void);
static void gateway_cancel(void);

static void publish(void)
{
    uint32_t words[SNAPSHOT_WORDS] = {0};
    memcpy(words, &s_sequence.status, sizeof(s_sequence.status));
    __atomic_add_fetch(&s_publish_seq, 1u, __ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < SNAPSHOT_WORDS; ++i) {
        __atomic_store_n(&s_published[i], words[i], __ATOMIC_RELAXED);
    }
    __atomic_add_fetch(&s_publish_seq, 1u, __ATOMIC_RELEASE);
}

void sync_io_sequence_get_snapshot(sync_io_sequence_snapshot_t *snapshot)
{
    if (snapshot == NULL) return;
    uint32_t words[SNAPSHOT_WORDS];
    uint32_t before;
    do {
        before = __atomic_load_n(&s_publish_seq, __ATOMIC_ACQUIRE);
        if ((before & 1u) != 0u) continue;
        for (size_t i = 0u; i < SNAPSHOT_WORDS; ++i) {
            words[i] = __atomic_load_n(&s_published[i], __ATOMIC_RELAXED);
        }
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (before == __atomic_load_n(&s_publish_seq, __ATOMIC_ACQUIRE)) break;
    } while (true);
    memcpy(snapshot, words, sizeof(*snapshot));
}

bool sync_io_sequence_is_armed(void)
{
    return __atomic_load_n(&s_armed, __ATOMIC_ACQUIRE) != 0u;
}

uint32_t sync_io_sequence_owned_mask(void)
{
    return __atomic_load_n(&s_owned_mask, __ATOMIC_ACQUIRE);
}

bool sync_io_sequence_set_switch(uint32_t switch_number, uint32_t value)
{
    if (switch_number < 1u || switch_number > 2u ||
        (switch_number == 1u && (value < 1u || value > 8u)) ||
        (switch_number == 2u && value > 1u)) return false;
    if (!sync_io_sequence_legacy_begin()) return false;
    const uint32_t encoded = switch_number == 1u ? value - 1u : value << 3u;
    const uint32_t mask = switch_number == 1u ? 0x7u : 0x8u;
    for (uint i = 0u; i < BOARD_SYNC_OUTPUT_PIN_COUNT; ++i) {
        gpio_set_function(BOARD_SYNC_OUTPUT_BASE_PIN + i, GPIO_FUNC_SIO);
        gpio_set_dir(BOARD_SYNC_OUTPUT_BASE_PIN + i, GPIO_OUT);
    }
    gpio_put_masked(mask << BOARD_SYNC_OUTPUT_BASE_PIN,
                   encoded << BOARD_SYNC_OUTPUT_BASE_PIN);
    if (switch_number == 1u) s_switch1 = value;
    else s_switch2 = value;
    sync_io_sequence_legacy_end();
    return true;
}

uint32_t sync_io_sequence_get_switch(uint32_t switch_number)
{
    if (switch_number == 1u) {
        /* Report the physical SP8T position, not only the last command. */
        return (sync_io_sequence_read_outputs() & 0x7u) + 1u;
    }
    if (switch_number == 2u) return s_switch2;
    return 0u;
}

uint32_t sync_io_sequence_read_inputs(void)
{
    const uint32_t raw = (gpio_get_all() >> BOARD_SYNC_INPUT_BASE_PIN) &
                         ((1u << BOARD_SYNC_INPUT_PIN_COUNT) - 1u);
#if BOARD_SYNC_INPUT_BITS_REVERSED
    uint32_t logical = 0u;
    for (uint32_t i = 0u; i < BOARD_SYNC_INPUT_PIN_COUNT; ++i) {
        logical |= ((raw >> i) & 1u) << (BOARD_SYNC_INPUT_PIN_COUNT - 1u - i);
    }
    return logical;
#else
    return raw;
#endif
}

uint32_t sync_io_sequence_read_outputs(void)
{
    return (gpio_get_all() >> BOARD_SYNC_OUTPUT_BASE_PIN) & SMA_MASK;
}

bool sync_io_sequence_legacy_begin(void)
{
    const uint32_t saved = save_and_disable_interrupts();
    const uint core = get_core_num();
    bool accepted = s_legacy_depth[core] != 0u;
    if (!accepted) {
        accepted = resource_arbiter_acquire_owned(
            RESOURCE_ARBITER_RESOURCE_SMA_GPIO, LEGACY_OWNER);
    }
    if (accepted) ++s_legacy_depth[core];
    restore_interrupts(saved);
    return accepted;
}

void sync_io_sequence_legacy_end(void)
{
    const uint32_t saved = save_and_disable_interrupts();
    const uint core = get_core_num();
    if (s_legacy_depth[core] != 0u && --s_legacy_depth[core] == 0u) {
        resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_SMA_GPIO, LEGACY_OWNER);
    }
    restore_interrupts(saved);
}

static bool outputs_available(void)
{
    sync_io_sma_frequency_tx_status_t pwm = {0};
    sync_io_sma_frequency_tx_get_status(&pwm);
    if (!sync_io_core_initialized() || pwm.running ||
        sync_io_seq_step_is_running() || sync_io_enc_count_is_running() ||
        sync_io_core_model_output_active() ||
        sync_io_core_wave_output_persona_active()) return false;
    for (uint32_t i = 0u; i < BOARD_SYNC_OUTPUT_PIN_COUNT; ++i) {
        const gpio_function_t function = gpio_get_function(BOARD_SYNC_OUTPUT_BASE_PIN + i);
        if (function != GPIO_FUNC_SIO && function != GPIO_FUNC_NULL) return false;
    }
    return sync_io_sequence_read_outputs() == 0u;
}

bool sync_io_sequence_reserve(void)
{
    if (get_core_num() != 0u ||
        !resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_SMA_GPIO, SEQUENCE_OWNER)) {
        return false;
    }
    if (!outputs_available()) {
        resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_SMA_GPIO, SEQUENCE_OWNER);
        return false;
    }
    __atomic_store_n(&s_reserved, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_owned_mask, SMA_MASK, __ATOMIC_RELEASE);
    return true;
}

void sync_io_sequence_release(void)
{
    if (sync_io_sequence_is_armed()) return;
    if (__atomic_exchange_n(&s_reserved, 0u, __ATOMIC_ACQ_REL) != 0u) {
        __atomic_store_n(&s_owned_mask, 0u, __ATOMIC_RELEASE);
        resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_SMA_GPIO, SEQUENCE_OWNER);
    }
}



static uint32_t sm_pc(uint sm, uint program)
{
    return pio_sm_get_pc(BOARD_SYNC_PIO_FAST, sm) - s_sequence.offset[program];
}

static void clear_owned_flags(void)
{
    pio_interrupt_clear(BOARD_SYNC_PIO_FAST, READY_IRQ);
    pio_interrupt_clear(BOARD_SYNC_PIO_FAST, REQUEST_IRQ);
}

static void safe_low(void)
{
    const uint32_t owned = s_sequence.config.sequence_output_mask |
        s_sequence.config.status_output_mask | s_sequence.config.gateway_output_mask;
    const uint32_t mask = owned << BOARD_SYNC_OUTPUT_BASE_PIN;
    gpio_put_masked(mask, 0u);
    for (uint i = 0u; i < BOARD_SYNC_OUTPUT_PIN_COUNT; ++i) {
        if ((owned & (1u << i)) == 0u) continue;
        gpio_set_function(BOARD_SYNC_OUTPUT_BASE_PIN + i, GPIO_FUNC_SIO);
        gpio_set_dir(BOARD_SYNC_OUTPUT_BASE_PIN + i, GPIO_OUT);
    }
}

static void stop_hardware(void)
{
    pio_set_sm_mask_enabled(BOARD_SYNC_PIO_FAST, s_sequence.sm_claimed, false);
    for (uint i = 0u; i < 4u; ++i) {
        if (s_sequence.dma[i] >= 0)
            hw_clear_bits(&dma_hw->ch[(uint)s_sequence.dma[i]].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    }
    for (uint i = 0u; i < 4u; ++i) {
        if (s_sequence.dma[i] >= 0) dma_channel_abort((uint)s_sequence.dma[i]);
    }
    if (s_sequence.sm_claimed != 0u) clear_owned_flags();
    if (s_sequence.pins_saved) safe_low();
}

static void cleanup(void *context, const sync_io_persona_descriptor_t *descriptor,
                    uint32_t dma_mask)
{
    (void)context; (void)descriptor; (void)dma_mask;
    stop_hardware();
    const struct pio_program *programs[3] = {
        s_sequence.config.gateway_input_channel ? &sequence_gateway_program :
        s_sequence.config.step_limit_enabled ? &sequence_finite_ingress_program : &sequence_ingress_program,
        &sequence_executor_program, &sequence_counter_program
    };
    for (uint i = 0u; i < 3u; ++i) {
        if ((s_sequence.loaded & (1u << i)) != 0u)
            pio_remove_program(BOARD_SYNC_PIO_FAST, programs[i], s_sequence.offset[i]);
        if ((s_sequence.sm_claimed & (1u << (i + 1u))) != 0u) {
            pio_sm_clear_fifos(BOARD_SYNC_PIO_FAST, i + 1u);
            pio_sm_unclaim(BOARD_SYNC_PIO_FAST, i + 1u);
        }
    }
    for (uint i = 0u; i < 4u; ++i) {
        if (s_sequence.dma[i] >= 0) dma_channel_unclaim((uint)s_sequence.dma[i]);
        s_sequence.dma[i] = -1;
    }
    if (s_sequence.pins_saved) {
        const uint32_t owned = s_sequence.config.sequence_output_mask |
            s_sequence.config.status_output_mask | s_sequence.config.gateway_output_mask;
        for (uint i = 0u; i < BOARD_SYNC_OUTPUT_PIN_COUNT; ++i) {
            if ((owned & (1u << i)) == 0u) continue;
            const uint pin = BOARD_SYNC_OUTPUT_BASE_PIN + i;
            gpio_set_dir(pin, s_sequence.saved_direction[i]);
            gpio_set_function(pin, s_sequence.saved_function[i]);
        }
    }
    s_sequence.pins_saved = false;
    s_sequence.loaded = 0u;
    s_sequence.sm_claimed = 0u;
}

static bool load_hardware(void *context, const sync_io_persona_descriptor_t *descriptor,
                          uint32_t dma_mask)
{
    (void)context;
    if (descriptor == NULL || descriptor->id != SYNC_IO_PERSONA_ID_SEQUENCE ||
        (BOARD_SYNC_PIO_FAST->irq & IRQ_MASK) != 0u) return false;
    uint count = 0u;
    for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
        if ((dma_mask & (1u << channel)) == 0u) continue;
        if (count == 4u || dma_channel_is_claimed(channel)) return false;
        dma_channel_claim(channel);
        s_sequence.dma[count++] = (int)channel;
    }
    if (count != 4u) return false;
    s_sequence.dma_mask = dma_mask;
    const struct pio_program *programs[3] = {
        s_sequence.config.gateway_input_channel ? &sequence_gateway_program :
        s_sequence.config.step_limit_enabled ? &sequence_finite_ingress_program : &sequence_ingress_program,
        &sequence_executor_program, &sequence_counter_program
    };
    for (uint i = 0u; i < 3u; ++i) {
        const uint sm = i + 1u;
        if (pio_sm_is_claimed(BOARD_SYNC_PIO_FAST, sm)) return false;
        pio_sm_claim(BOARD_SYNC_PIO_FAST, sm);
        s_sequence.sm_claimed |= 1u << sm;
        if (!pio_can_add_program(BOARD_SYNC_PIO_FAST, programs[i])) return false;
        s_sequence.offset[i] = pio_add_program(BOARD_SYNC_PIO_FAST, programs[i]);
        s_sequence.loaded |= 1u << i;
    }
    for (uint i = 0u; i < BOARD_SYNC_OUTPUT_PIN_COUNT; ++i) {
        const uint pin = BOARD_SYNC_OUTPUT_BASE_PIN + i;
        s_sequence.saved_function[i] = gpio_get_function(pin);
        s_sequence.saved_direction[i] = gpio_get_dir(pin);
    }
    s_sequence.pins_saved = true;
    return true;
}

static bool arm_hardware(void *context, const sync_io_persona_descriptor_t *descriptor,
                         uint32_t dma_mask)
{
    (void)context; (void)descriptor; (void)dma_mask;
    const uint32_t hz = clock_get_hz(clk_sys);
    if (hz < TICK_HZ || hz % TICK_HZ != 0u) return false;
    PIO pio = BOARD_SYNC_PIO_FAST;
    pio_set_sm_mask_enabled(pio, SM_MASK, false);
    clear_owned_flags();
    pio->fdebug = (1u << (PIO_FDEBUG_RXSTALL_LSB + EXECUTOR_SM)) |
        (1u << (PIO_FDEBUG_RXUNDER_LSB + EXECUTOR_SM)) |
        (1u << (PIO_FDEBUG_TXOVER_LSB + EXECUTOR_SM));
    const bool gateway = s_sequence.config.gateway_input_channel != 0u;
    pio_sm_config ingress = gateway ?
        sequence_gateway_program_get_default_config(s_sequence.offset[0]) :
        s_sequence.config.step_limit_enabled ?
        sequence_finite_ingress_program_get_default_config(s_sequence.offset[0]) :
        sequence_ingress_program_get_default_config(s_sequence.offset[0]);
    sm_config_set_in_pins(&ingress, s_sequence.input_pin);
    sm_config_set_mov_status(&ingress, STATUS_IRQ_SET, READY_IRQ);
    if (gateway) {
        uint output = 0u;
        while ((s_sequence.config.gateway_output_mask & (1u << output)) == 0u) ++output;
        sm_config_set_set_pins(&ingress, BOARD_SYNC_OUTPUT_BASE_PIN + output, 1u);
    }
    sm_config_set_clkdiv(&ingress, (float)(hz / TICK_HZ));
    pio_sm_init(pio, INGRESS_SM, s_sequence.offset[0], &ingress);
    if (s_sequence.config.step_limit_enabled) {
        pio_sm_put(pio, INGRESS_SM, s_sequence.config.max_steps - 1u);
        pio_sm_exec(pio, INGRESS_SM, pio_encode_pull(false, true));
        pio_sm_exec(pio, INGRESS_SM, pio_encode_mov(pio_y, pio_osr));
        if (s_sequence.config.max_steps == 0u)
            pio_sm_exec(pio, INGRESS_SM, pio_encode_jmp(s_sequence.offset[0] +
                sequence_finite_ingress_offset_parked));
    }
    pio_sm_config counter = sequence_counter_program_get_default_config(s_sequence.offset[2]);
    sm_config_set_in_pins(&counter, s_sequence.input_pin);
    sm_config_set_clkdiv(&counter, (float)(hz / TICK_HZ));
    sm_config_set_fifo_join(&counter, PIO_FIFO_JOIN_RX);
    pio_sm_init(pio, COUNTER_SM, s_sequence.offset[2], &counter);
    pio_sm_exec(pio, COUNTER_SM, pio_encode_mov_not(pio_x, pio_null));
    for (uint i = 0u; i < 3u; i += 2u) {
        if (gateway && i == 0u) continue;
        const bool falling = gateway ? s_sequence.config.gateway_falling : s_sequence.config.falling;
        pio->instr_mem[s_sequence.offset[i]] = pio_encode_wait_pin(falling, 0u);
        pio->instr_mem[s_sequence.offset[i] + 1u] = pio_encode_wait_pin(!falling, 0u);
    }
    pio_sm_config executor = sequence_executor_program_get_default_config(s_sequence.offset[1]);
    sm_config_set_out_pins(&executor, BOARD_SYNC_OUTPUT_BASE_PIN, BOARD_SYNC_OUTPUT_PIN_COUNT);
    sm_config_set_out_shift(&executor, true, false, 32u);
    sm_config_set_in_shift(&executor, true, true, 32u);
    sm_config_set_clkdiv(&executor, (float)(hz / TICK_HZ));
    pio_sm_init(pio, EXECUTOR_SM, s_sequence.offset[1], &executor);
    if (s_sequence.config.status_mode != SYNC_IO_SEQUENCE_STATUS_PULSE) {
        /* NONE uses the same bounded settle/receipt path, with no status bits
         * in the plan and no pulse countdown. Only the DUT code is driven. */
        pio->instr_mem[s_sequence.offset[1] + sequence_executor_offset_status_mode] =
            pio_encode_jmp(s_sequence.offset[1] + sequence_executor_offset_status_active);
    }
    const uint32_t owned = s_sequence.config.sequence_output_mask |
        s_sequence.config.status_output_mask | s_sequence.config.gateway_output_mask;
    pio_sm_set_pins_with_mask(
        pio, EXECUTOR_SM,
        (s_sequence.initial_output & s_sequence.config.sequence_output_mask) << BOARD_SYNC_OUTPUT_BASE_PIN,
        owned << BOARD_SYNC_OUTPUT_BASE_PIN);
    for (uint i = 0u; i < BOARD_SYNC_OUTPUT_PIN_COUNT; ++i) {
        if ((owned & (1u << i)) != 0u) {
            pio_sm_set_consecutive_pindirs(pio, EXECUTOR_SM, BOARD_SYNC_OUTPUT_BASE_PIN + i, 1u, true);
            pio_gpio_init(pio, BOARD_SYNC_OUTPUT_BASE_PIN + i);
        }
    }

    const uint tx = (uint)s_sequence.dma[0], reload = (uint)s_sequence.dma[1];
    const uint rx = (uint)s_sequence.dma[2], edges = (uint)s_sequence.dma[3];
    dma_channel_config c = dma_channel_get_default_config(tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, EXECUTOR_SM, true));
    channel_config_set_chain_to(&c, reload);
    dma_channel_configure(tx, &c, &pio->txf[EXECUTOR_SM], s_plan,
                         s_sequence.status.plan_count * SYNC_IO_SEQUENCE_PLAN_WORDS, false);
    s_sequence.read_address = (uint32_t)(uintptr_t)s_plan;
    c = dma_channel_get_default_config(reload);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(reload, &c, &dma_hw->ch[tx].al3_read_addr_trig,
                         &s_sequence.read_address, 1u, false);
    c = dma_channel_get_default_config(rx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, RECEIPT_BITS);
    channel_config_set_dreq(&c, pio_get_dreq(pio, EXECUTOR_SM, false));
    dma_channel_configure(rx, &c, s_receipts, &pio->rxf[EXECUTOR_SM], RX_TRANSFERS, false);
    c = dma_channel_get_default_config(edges);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, COUNTER_SM, false));
    dma_channel_configure(edges, &c, &s_edge_latest, &pio->rxf[COUNTER_SM],
                         dma_encode_endless_transfer_count(), false);
    s_sequence.prime_ready_at_us = time_us_64() + s_sequence.config.settle_us;
    s_sequence.priming = true;
    return true;
}

static bool start_hardware(void *context, const sync_io_persona_descriptor_t *descriptor,
                           uint32_t dma_mask)
{
    (void)context; (void)descriptor; (void)dma_mask;
    const bool empty = s_sequence.config.step_limit_enabled && s_sequence.config.max_steps == 0u;
    dma_start_channel_mask((empty ? 0u : 1u << (uint)s_sequence.dma[0]) |
                          (1u << (uint)s_sequence.dma[2]));
    if (!empty) pio_enable_sm_mask_in_sync(BOARD_SYNC_PIO_FAST, 1u << EXECUTOR_SM);
    return true;
}

static void stop_hook(void *context, const sync_io_persona_descriptor_t *descriptor,
                      uint32_t dma_mask)
{
    (void)context; (void)descriptor; (void)dma_mask;
    stop_hardware();
}

static void fail(uint32_t reason)
{
    if (s_sequence.status.fault == 0u) s_sequence.status.fault = reason;
    stop_hardware();
    s_sequence.status.ready = false;
    s_sequence.status.pending = false;
    s_sequence.status.busy = false;
    s_sequence.status.gateway_waiting = false;
    s_sequence.status.gateway_pulse_busy = false;
    if (s_sequence.status.accepted > s_sequence.status.completed)
        s_sequence.status.cancelled = s_sequence.status.accepted - s_sequence.status.completed;
}

static bool config_valid(const sync_io_sequence_config_t *config)
{
    if (config == NULL) return false;
    if (config->step_limit_enabled && config->gateway_input_channel != 0u) return false;
    if (config->gateway_input_channel != 0u) {
        const uint32_t mask = config->gateway_output_mask;
        if (config->gateway_input_channel > BOARD_SYNC_INPUT_PIN_COUNT ||
            config->input_channel != 0u || config->status_mode != SYNC_IO_SEQUENCE_STATUS_NONE ||
            mask == 0u || (mask & (mask - 1u)) != 0u || (mask & ~SMA_MASK) != 0u ||
            (mask & config->sequence_output_mask) != 0u ||
            config->gateway_pulse_us == 0u ||
            config->gateway_pulse_us > SYNC_IO_SEQUENCE_TIME_MAX_US) return false;
    } else if (config->gateway_output_mask != 0u || config->gateway_pulse_us != 0u ||
               config->gateway_falling) return false;
    return config != NULL && config->input_channel <= BOARD_SYNC_INPUT_PIN_COUNT &&
        config->sequence_output_mask != 0u &&
        ((config->status_mode == SYNC_IO_SEQUENCE_STATUS_NONE) == (config->status_output_mask == 0u)) &&
        ((config->sequence_output_mask | config->status_output_mask) & ~SMA_MASK) == 0u &&
        (config->sequence_output_mask & config->status_output_mask) == 0u &&
        (config->status_mode == SYNC_IO_SEQUENCE_STATUS_LEVEL ||
         config->status_mode == SYNC_IO_SEQUENCE_STATUS_PULSE ||
         config->status_mode == SYNC_IO_SEQUENCE_STATUS_NONE) &&
        config->settle_us <= SYNC_IO_SEQUENCE_TIME_MAX_US &&
        ((config->status_mode == SYNC_IO_SEQUENCE_STATUS_PULSE &&
          config->pulse_us != 0u && config->pulse_us <= SYNC_IO_SEQUENCE_TIME_MAX_US) ||
         (config->status_mode != SYNC_IO_SEQUENCE_STATUS_PULSE && config->pulse_us == 0u));
}

static uint32_t logical_index_for_transfer(uint32_t transfer, uint32_t count)
{
    return (transfer % count + 1u) % count;
}

bool sync_io_sequence_arm_plan(const sync_io_sequence_config_t *config,
                               const uint32_t *values, uint32_t count)
{
    if (get_core_num() != 1u || sync_io_sequence_is_armed()) return false;
    memset(&s_sequence, 0, sizeof(s_sequence));
    for (uint i = 0u; i < 4u; ++i) s_sequence.dma[i] = -1;
    s_sequence.status.current_index = NO_INDEX;
    s_sequence.status.completed_index = NO_INDEX;
    s_sequence.status.tick_ns = SYNC_IO_SEQUENCE_TICK_NS;
    s_sequence.status.timing_kind = SYNC_IO_SEQUENCE_TIMING_PIO0;
    s_edge_latest = 0u;
    if (!config_valid(config) || values == NULL || count == 0u ||
        count > SYNC_IO_SEQUENCE_PLAN_MAX) {
        s_sequence.status.fault = SYNC_IO_SEQUENCE_FAULT_CONFIG;
        goto rejected;
    }
    for (uint i = 0u; i < count; ++i) {
        if ((values[i] & ~config->sequence_output_mask) != 0u) {
            s_sequence.status.fault = SYNC_IO_SEQUENCE_FAULT_CONFIG;
            goto rejected;
        }
    }
    for (uint i = 0u; i < count; ++i) {
        const uint32_t logical = logical_index_for_transfer(i, count);
        const uint32_t base = i * SYNC_IO_SEQUENCE_PLAN_WORDS;
        s_plan[base] = values[logical] | (logical << PLAN_INDEX_SHIFT) |
            ((values[logical] | config->status_output_mask) << ACTIVE_VALUE_SHIFT);
        s_plan[base + 1u] = config->settle_us == 0u ? 0u : config->settle_us * 10u - 5u;
        s_plan[base + 2u] = config->status_mode == SYNC_IO_SEQUENCE_STATUS_PULSE ?
            config->pulse_us * 10u - 4u : 0u;
    }
    if (__atomic_load_n(&s_reserved, __ATOMIC_ACQUIRE) == 0u || !outputs_available()) {
        s_sequence.status.fault = SYNC_IO_SEQUENCE_FAULT_RESOURCE;
        goto rejected;
    }
    s_sequence.config = *config;
    s_sequence.initial_output = values[0];
    s_sequence.status.plan_count = count;
    s_sequence.input_pin = BOARD_SYNC_INPUT_BASE_PIN;
    const uint32_t input_channel = config->gateway_input_channel != 0u ?
        config->gateway_input_channel : config->input_channel;
    if (input_channel != 0u) {
#if BOARD_SYNC_INPUT_BITS_REVERSED
        s_sequence.input_pin += BOARD_SYNC_INPUT_PIN_COUNT - input_channel;
#else
        s_sequence.input_pin += input_channel - 1u;
#endif
    }
    const sync_io_persona_manager_hooks_t hooks = {
        .load = load_hardware, .arm = arm_hardware, .start = start_hardware,
        .stop = stop_hook, .cleanup = cleanup
    };
    sync_io_persona_manager_init(&s_manager, &hooks, NULL);
    /* The manager's allocator must also exclude live SDK DMA owners. */
    for (uint channel = 0u; channel < NUM_DMA_CHANNELS; ++channel) {
        if (dma_channel_is_claimed(channel)) s_manager.used_dma_channel_mask |= 1u << channel;
    }
    if (!sync_io_persona_manager_claim(&s_manager, SYNC_IO_PERSONA_ID_SEQUENCE, &s_handle, NULL)) {
        s_sequence.status.fault = SYNC_IO_SEQUENCE_FAULT_RESOURCE;
        goto rejected;
    }
    s_sequence.manager_claimed = true;
    if (!sync_io_persona_manager_load(&s_manager, &s_handle) ||
        !sync_io_persona_manager_arm(&s_manager, &s_handle) ||
        !sync_io_persona_manager_start(&s_manager, &s_handle)) {
        if (sync_io_persona_manager_handle_valid(&s_manager, &s_handle))
            (void)sync_io_persona_manager_release(&s_manager, &s_handle);
        s_sequence.manager_claimed = false;
        s_sequence.status.fault = SYNC_IO_SEQUENCE_FAULT_RESOURCE;
        goto rejected;
    }
    s_sequence.status.armed = true;
    s_sequence.status.current_index = 0u;
    s_sequence.status.output_ownership_mask = config->sequence_output_mask |
        config->status_output_mask | config->gateway_output_mask;
    __atomic_store_n(&s_owned_mask, s_sequence.status.output_ownership_mask,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_armed, 1u, __ATOMIC_RELEASE);
    publish();
    return true;
rejected:
    sync_io_sequence_release();
    publish();
    return false;
}

static uint32_t produced_receipts(void)
{
    if (s_sequence.rx_stopped) return s_sequence.rx_stopped_produced;
    const uint32_t remaining = dma_hw->ch[(uint)s_sequence.dma[2]].transfer_count & RX_TRANSFERS;
    __dmb();
    return RX_TRANSFERS - remaining;
}

/* The executor must be disabled or idle at WAIT REQUEST. Abort retires the
 * in-flight writes but does not preserve the live TRANS_COUNT value. */
static void stop_receipt_dma(void)
{
    if (s_sequence.rx_stopped) return;
    const uint channel = (uint)s_sequence.dma[2];
    hw_clear_bits(&dma_hw->ch[channel].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    const uint32_t before = produced_receipts();
    dma_channel_abort(channel);
    __dmb();
    const uint32_t final_index = (dma_hw->ch[channel].write_addr &
        ((1u << RECEIPT_BITS) - 1u)) / sizeof(uint32_t);
    /* Only the bounded DMA pipeline can advance after EN is cleared, so the
     * ring index recovers final writes, including a wrap during abort. */
    const uint32_t tail = (final_index + RECEIPT_WORDS - before % RECEIPT_WORDS) % RECEIPT_WORDS;
    s_sequence.rx_stopped_produced = before + tail;
    s_sequence.rx_stopped = true;
}

static void resume_receipt_dma(void)
{
    const uint channel = (uint)s_sequence.dma[2];
    const uint32_t remaining = RX_TRANSFERS - s_sequence.rx_stopped_produced;
    /* CTRL_TRIG would start the old RELOAD before the new count is installed. */
    hw_set_bits(&dma_hw->ch[channel].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_set_trans_count(channel, remaining, true);
    s_sequence.rx_stopped = false;
}

static bool receive_word(uint32_t word)
{
    const bool completion = (word & 0x80000000u) != 0u;
    const uint32_t decoded = completion ? ~word : word;
    const uint32_t transfer_index =
        (completion ? s_sequence.status.completed : s_sequence.status.written) %
        s_sequence.status.plan_count;
    const uint32_t logical_index =
        logical_index_for_transfer(transfer_index, s_sequence.status.plan_count);
    if (decoded != s_plan[transfer_index * SYNC_IO_SEQUENCE_PLAN_WORDS] ||
        (completion ? s_sequence.status.written != s_sequence.status.completed + 1u :
                      s_sequence.status.written != s_sequence.status.completed)) {
        fail(SYNC_IO_SEQUENCE_FAULT_RECEIPT);
        return false;
    }
    if (completion) {
        ++s_sequence.status.completed;
        s_sequence.status.completed_index = logical_index;
    } else {
        ++s_sequence.status.written;
        if (s_sequence.status.accepted < s_sequence.status.written)
            s_sequence.status.accepted = s_sequence.status.written;
        s_sequence.status.current_index = logical_index;
    }
    return true;
}

static bool drain_receipts(void)
{
    const uint32_t produced = produced_receipts();
    if (produced < s_sequence.rx_consumed) {
        fail(SYNC_IO_SEQUENCE_FAULT_RECEIPT_REGRESSION);
        return false;
    }
    if (produced >= RX_TRANSFERS - RECEIPT_WORDS) {
        fail(SYNC_IO_SEQUENCE_FAULT_RECEIPT_CAPACITY);
        return false;
    }
    if (produced - s_sequence.rx_consumed > RECEIPT_WORDS) {
        fail(SYNC_IO_SEQUENCE_FAULT_OVERFLOW);
        return false;
    }
    while (s_sequence.rx_consumed != produced) {
        uint32_t word = s_receipts[s_sequence.rx_consumed % RECEIPT_WORDS];
        __dmb();
        if (produced_receipts() - s_sequence.rx_consumed > RECEIPT_WORDS) {
            fail(SYNC_IO_SEQUENCE_FAULT_RECEIPT_RACE);
            return false;
        }
        ++s_sequence.rx_consumed;
        if (!receive_word(word)) return false;
    }
    return true;
}

static bool pending_request(void)
{
    PIO pio = BOARD_SYNC_PIO_FAST;
    if ((pio->irq & (1u << REQUEST_IRQ)) != 0u) return true;
    const uint pc = sm_pc(EXECUTOR_SM, 1u);
    /* A consumed request before MOV/PUSH is accepted even without a receipt. */
    return pc >= sequence_executor_offset_writing && pc <= sequence_executor_offset_written;
}

static bool drain_idle_executor(void)
{
    PIO pio = BOARD_SYNC_PIO_FAST;
    if ((pio->irq & IRQ_MASK) != (1u << READY_IRQ) ||
        sm_pc(EXECUTOR_SM, 1u) != sequence_executor_offset_waiting) return false;
    stop_receipt_dma();
    if (!drain_receipts()) return false;
    while (!pio_sm_is_rx_fifo_empty(pio, EXECUTOR_SM)) {
        if (!receive_word(pio_sm_get(pio, EXECUTOR_SM))) return false;
    }
    return true;
}

static void account_input(uint32_t edges, bool settled)
{
    if (edges < s_sequence.last_edges) {
        fail(SYNC_IO_SEQUENCE_FAULT_COUNTER_REGRESSION);
        return;
    }
    if (edges >= UINT32_MAX - 32u) {
        fail(SYNC_IO_SEQUENCE_FAULT_COUNTER_OVERFLOW);
        return;
    }
    s_sequence.last_edges = edges;
    s_sequence.status.input_events = edges;
    const uint32_t notready = s_sequence.paused_edges +
        (s_sequence.paused ? edges - s_sequence.pause_started : 0u);
    if (settled) {
        const uint32_t accepted = s_sequence.status.accepted;
        if (edges < accepted || edges - accepted < notready) {
            fail(SYNC_IO_SEQUENCE_FAULT_RECEIPT);
            return;
        }
        s_sequence.status.notready_rejected = notready;
        s_sequence.status.busy_rejected = edges - accepted - notready;
    } else if (s_sequence.paused) {
        s_sequence.status.notready_rejected = notready;
    }
    if (settled) s_sequence.status.rejection_counts_pending = false;
    else if (!s_sequence.paused) s_sequence.status.rejection_counts_pending = true;
}

static void mark_initial_status_ready(void)
{
    if (s_sequence.config.status_mode != SYNC_IO_SEQUENCE_STATUS_LEVEL) return;
    const uint32_t mask = s_sequence.config.status_output_mask <<
        BOARD_SYNC_OUTPUT_BASE_PIN;
    pio_sm_set_pins_with_mask(BOARD_SYNC_PIO_FAST, EXECUTOR_SM, mask, mask);
}

void sync_io_sequence_service(void)
{
    if (get_core_num() != 1u || !s_sequence.status.armed || s_sequence.status.fault != 0u) return;
    for (uint i = 0u; i < 4u; ++i) {
        if ((dma_hw->ch[(uint)s_sequence.dma[i]].ctrl_trig & DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS) != 0u) {
            fail(SYNC_IO_SEQUENCE_FAULT_DMA);
            publish();
            return;
        }
    }
    const uint32_t rx_errors = (1u << (PIO_FDEBUG_RXSTALL_LSB + EXECUTOR_SM)) |
        (1u << (PIO_FDEBUG_RXUNDER_LSB + EXECUTOR_SM)) |
        (1u << (PIO_FDEBUG_TXOVER_LSB + EXECUTOR_SM));
    if ((BOARD_SYNC_PIO_FAST->fdebug & rx_errors) != 0u) {
        fail(SYNC_IO_SEQUENCE_FAULT_RECEIPT);
    } else if (!drain_receipts()) {
        /* drain_receipts owns the fault reason. */
    } else {
        const bool no_steps = s_sequence.config.step_limit_enabled && s_sequence.config.max_steps == 0u;
        if (s_sequence.priming &&
            (no_steps || (BOARD_SYNC_PIO_FAST->irq & (1u << READY_IRQ)) != 0u) &&
            time_us_64() >= s_sequence.prime_ready_at_us) {
            mark_initial_status_ready();
            if (s_sequence.config.input_channel != 0u && !no_steps) {
                dma_start_channel_mask(1u << (uint)s_sequence.dma[3]);
                pio_enable_sm_mask_in_sync(BOARD_SYNC_PIO_FAST, INPUT_SM_MASK);
            }
            s_sequence.priming = false;
        }
        const bool settled = s_sequence.paused && drain_idle_executor();
        if (s_sequence.status.fault != 0u) {
            publish();
            return;
        }
        const bool pending = pending_request();
        if (pending && s_sequence.status.accepted == s_sequence.status.completed) {
            ++s_sequence.status.accepted;
            s_sequence.status.current_index = logical_index_for_transfer(
                s_sequence.status.written, s_sequence.status.plan_count);
        }
        s_sequence.status.pending = s_sequence.status.accepted > s_sequence.status.written;
        s_sequence.status.busy = s_sequence.status.accepted > s_sequence.status.completed;
        s_sequence.status.finished = s_sequence.config.step_limit_enabled && !s_sequence.priming &&
            s_sequence.status.completed == s_sequence.config.max_steps;
        s_sequence.status.ready = !s_sequence.status.finished && !s_sequence.priming && !s_sequence.paused && !s_sequence.status.busy &&
            (BOARD_SYNC_PIO_FAST->irq & (1u << READY_IRQ)) != 0u;
        if (s_sequence.config.input_channel != 0u) {
            const uint32_t edges = s_edge_latest;
            __dmb();
            account_input(edges, settled);
        }
        if (s_sequence.config.gateway_input_channel != 0u) gateway_service();
    }
    publish();
}

bool sync_io_sequence_software_step(void)
{
    if (get_core_num() != 1u || !s_sequence.status.armed || s_sequence.config.input_channel != 0u ||
        s_sequence.paused || s_sequence.status.fault != 0u) return false;
    sync_io_sequence_service();
    if (!s_sequence.status.ready ||
        (s_sequence.config.step_limit_enabled && s_sequence.status.accepted >= s_sequence.config.max_steps) ||
        s_sequence.status.gateway_waiting ||
        s_sequence.status.gateway_pulse_busy) return false;
    pio_interrupt_clear(BOARD_SYNC_PIO_FAST, READY_IRQ);
    BOARD_SYNC_PIO_FAST->irq_force = 1u << REQUEST_IRQ;
    ++s_sequence.status.accepted;
    s_sequence.status.current_index = logical_index_for_transfer(
        s_sequence.status.written, s_sequence.status.plan_count);
    s_sequence.status.ready = false;
    s_sequence.status.pending = true;
    s_sequence.status.busy = true;
    publish();
    return true;
}

/* These routines run only with the indicated SM disabled. */
static uint32_t read_sm_register(uint sm, enum pio_src_dest source, bool invert)
{
    PIO pio = BOARD_SYNC_PIO_FAST;
    pio_sm_clear_fifos(pio, sm);
    pio_sm_exec(pio, sm, invert ? pio_encode_mov_not(pio_isr, source) :
                                pio_encode_mov(pio_isr, source));
    pio_sm_exec(pio, sm, pio_encode_push(false, false));
    return pio_sm_get(pio, sm);
}

static uint32_t stop_counter(void)
{
    PIO pio = BOARD_SYNC_PIO_FAST;
    hw_clear_bits(&dma_hw->ch[(uint)s_sequence.dma[3]].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort((uint)s_sequence.dma[3]);
    if (sm_pc(COUNTER_SM, 2u) == sequence_counter_offset_observed)
        pio_sm_exec(pio, COUNTER_SM, pio_encode_jmp_x_dec(
            s_sequence.offset[2] + sequence_counter_offset_counted));
    const uint32_t edges = read_sm_register(COUNTER_SM, pio_x, true);
    /* A suspended PUSH must retain its cumulative value after the CPU read. */
    pio_sm_exec(pio, COUNTER_SM, pio_encode_mov_not(pio_isr, pio_x));
    s_edge_latest = edges;
    return edges;
}

static void gateway_service(void)
{
    PIO pio = BOARD_SYNC_PIO_FAST;
    if (s_sequence.status.gateway_waiting && s_edge_latest != 0u) {
        /* The counter is private to this trigger. Multiple READY edges become
         * one receipt, never queued requests for later sequence states. */
        pio_set_sm_mask_enabled(pio, 1u << COUNTER_SM, false);
        s_sequence.status.gateway_waiting = false;
        ++s_sequence.status.gateway_ready_count;
    }
    /* Hardware completion receipt avoids a PC/FIFO observation race at the
     * initial PULL, which could otherwise report done before the pulse rises. */
    if (s_sequence.status.gateway_pulse_busy && !pio_sm_is_rx_fifo_empty(pio, INGRESS_SM)) {
        (void)pio_sm_get(pio, INGRESS_SM);
        s_sequence.status.gateway_pulse_busy = false;
    }
}

static void gateway_cancel(void)
{
    if (s_sequence.config.gateway_input_channel == 0u) return;
    PIO pio = BOARD_SYNC_PIO_FAST;
    pio_set_sm_mask_enabled(pio, INPUT_SM_MASK, false);
    hw_clear_bits(&dma_hw->ch[(uint)s_sequence.dma[3]].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort((uint)s_sequence.dma[3]);
    pio_sm_clear_fifos(pio, INGRESS_SM);
    pio_sm_clear_fifos(pio, COUNTER_SM);
    pio_sm_set_pins_with_mask(pio, INGRESS_SM, 0u,
        s_sequence.config.gateway_output_mask << BOARD_SYNC_OUTPUT_BASE_PIN);
    if (s_sequence.status.gateway_waiting) ++s_sequence.status.gateway_cancelled;
    s_sequence.status.gateway_waiting = false;
    s_sequence.status.gateway_pulse_busy = false;
}

bool sync_io_sequence_gateway_fire(void)
{
    if (get_core_num() != 1u || !s_sequence.status.armed || s_sequence.paused ||
        s_sequence.status.fault != 0u || s_sequence.config.gateway_input_channel == 0u)
        return false;
    sync_io_sequence_service();
    if (!s_sequence.status.ready || s_sequence.status.gateway_waiting ||
        s_sequence.status.gateway_pulse_busy ||
        s_sequence.status.gateway_trigger_count == UINT32_MAX) return false;
    PIO pio = BOARD_SYNC_PIO_FAST;
    /* Stop and flush the previous DMA pipeline before zeroing the receipt.
     * No old edge can be reinterpreted as this trigger's READY. */
    pio_set_sm_mask_enabled(pio, INPUT_SM_MASK, false);
    const uint channel = (uint)s_sequence.dma[3];
    hw_clear_bits(&dma_hw->ch[channel].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort(channel);
    pio_sm_clear_fifos(pio, COUNTER_SM);
    pio_sm_clear_fifos(pio, INGRESS_SM);
    pio_sm_restart(pio, COUNTER_SM);
    pio_sm_restart(pio, INGRESS_SM);
    pio_sm_exec(pio, COUNTER_SM, pio_encode_mov_not(pio_x, pio_null));
    pio_sm_exec(pio, COUNTER_SM, pio_encode_jmp(s_sequence.offset[2]));
    pio_sm_exec(pio, INGRESS_SM, pio_encode_jmp(s_sequence.offset[0]));
    s_edge_latest = 0u;
    __dmb();
    hw_set_bits(&dma_hw->ch[channel].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_set_trans_count(channel, dma_encode_endless_transfer_count(), true);
    pio_sm_put(pio, INGRESS_SM, s_sequence.config.gateway_pulse_us * 10u - 2u);
    s_sequence.status.gateway_waiting = true;
    s_sequence.status.gateway_pulse_busy = true;
    ++s_sequence.status.gateway_trigger_count;
    pio_enable_sm_mask_in_sync(pio, INPUT_SM_MASK);
    publish();
    return true;
}

static void finish_ingress(void)
{
    PIO pio = BOARD_SYNC_PIO_FAST;
    const uint pc = sm_pc(INGRESS_SM, 0u);
    const bool finite = s_sequence.config.step_limit_enabled;
    if (finite && pc == sequence_finite_ingress_offset_parked) return;
    const bool request_issued = finite && pc == sequence_finite_ingress_offset_quota;
    bool admit = pc == sequence_ingress_offset_admitted || pc == sequence_ingress_offset_request;
    /* An edge not yet checked by MOV STATUS has not entered admission. */
    if (pc == sequence_ingress_offset_observed) ++s_sequence.paused_edges;
    if (pc == sequence_ingress_offset_decide)
        admit = read_sm_register(INGRESS_SM, pio_x, false) != 0u;
    if (admit && !request_issued) {
        pio_interrupt_clear(pio, READY_IRQ);
        pio->irq_force = 1u << REQUEST_IRQ;
    }
    uint restart = s_sequence.offset[0];
    if (finite && (admit || request_issued)) {
        /* PAUSE may interrupt after admission but before the quota branch.
         * Retire that debit once, without replaying an already-issued IRQ. */
        const uint32_t remaining = read_sm_register(INGRESS_SM, pio_y, false);
        if (remaining == 0u) restart += sequence_finite_ingress_offset_parked;
        else {
            pio_sm_put(pio, INGRESS_SM, remaining - 1u);
            pio_sm_exec(pio, INGRESS_SM, pio_encode_pull(false, true));
            pio_sm_exec(pio, INGRESS_SM, pio_encode_mov(pio_y, pio_osr));
        }
    }
    pio_sm_exec(pio, INGRESS_SM, pio_encode_jmp(restart));
}

bool sync_io_sequence_pause(bool paused)
{
    if (get_core_num() != 1u || !s_sequence.status.armed || s_sequence.status.fault != 0u) return false;
    if (paused == s_sequence.paused) return true;
    PIO pio = BOARD_SYNC_PIO_FAST;
    /* A paused, idle executor leaves receipt DMA stopped after settlement.
     * Restore its consumer before either ingress can issue another request. */
    if (!paused && s_sequence.rx_stopped) resume_receipt_dma();
    if (paused && s_sequence.config.gateway_input_channel != 0u) gateway_cancel();
    if (s_sequence.config.input_channel != 0u) {
        pio_set_sm_mask_enabled(pio, INPUT_SM_MASK, false);
        if (paused) finish_ingress();
        const uint32_t edges = stop_counter();
        if (paused) {
            s_sequence.pause_started = edges;
        } else {
            s_sequence.paused_edges += edges - s_sequence.pause_started;
            if (!s_sequence.config.step_limit_enabled ||
                sm_pc(INGRESS_SM, 0u) != sequence_finite_ingress_offset_parked)
                pio_sm_exec(pio, INGRESS_SM, pio_encode_jmp(s_sequence.offset[0]));
        }
        s_sequence.paused = paused;
        account_input(edges, false);
        if (s_sequence.status.fault != 0u) {
            publish();
            return false;
        }
        hw_set_bits(&dma_hw->ch[(uint)s_sequence.dma[3]].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
        dma_channel_set_trans_count((uint)s_sequence.dma[3], dma_encode_endless_transfer_count(), true);
        pio_set_sm_mask_enabled(pio, (1u << COUNTER_SM) | (paused ? 0u : 1u << INGRESS_SM), true);
    } else {
        s_sequence.paused = paused;
    }
    s_sequence.status.paused = paused;
    sync_io_sequence_service();
    return s_sequence.status.fault == 0u;
}

void sync_io_sequence_stop(void)
{
    if (get_core_num() != 1u) return;
    if (s_sequence.status.armed && s_sequence.config.gateway_input_channel != 0u)
        gateway_cancel();
    if (s_sequence.status.armed && s_sequence.status.fault == 0u) {
        PIO pio = BOARD_SYNC_PIO_FAST;
        pio_set_sm_mask_enabled(pio, SM_MASK, false);
        const uint executor_pc = sm_pc(EXECUTOR_SM, 1u);
        uint32_t final_edges = 0u;
        if (s_sequence.config.input_channel != 0u) {
            finish_ingress();
            final_edges = stop_counter();
        }
        const bool pending = pending_request();
        stop_receipt_dma();
        if (drain_receipts()) {
            while (!pio_sm_is_rx_fifo_empty(pio, EXECUTOR_SM)) {
                if (!receive_word(pio_sm_get(pio, EXECUTOR_SM))) break;
            }
        }
        if (s_sequence.status.fault == 0u && executor_pc > sequence_executor_offset_writing &&
            executor_pc <= sequence_executor_offset_written &&
            s_sequence.status.written == s_sequence.status.completed) {
            (void)receive_word(s_plan[(s_sequence.status.written % s_sequence.status.plan_count) *
                SYNC_IO_SEQUENCE_PLAN_WORDS]);
        }
        if (s_sequence.status.fault == 0u &&
            executor_pc >= sequence_executor_offset_status_active &&
            s_sequence.status.written == s_sequence.status.completed + 1u) {
            (void)receive_word(~s_plan[(s_sequence.status.completed % s_sequence.status.plan_count) *
                SYNC_IO_SEQUENCE_PLAN_WORDS]);
        }
        if (pending && s_sequence.status.accepted == s_sequence.status.completed) {
            ++s_sequence.status.accepted;
            s_sequence.status.current_index = logical_index_for_transfer(
                s_sequence.status.written, s_sequence.status.plan_count);
        }
        if (s_sequence.status.accepted > s_sequence.status.completed)
            s_sequence.status.cancelled = s_sequence.status.accepted - s_sequence.status.completed;
        if (s_sequence.config.input_channel != 0u && s_sequence.status.fault == 0u)
            account_input(final_edges, true);
    }
    if (s_sequence.manager_claimed) {
        (void)sync_io_persona_manager_release(&s_manager, &s_handle);
        s_sequence.manager_claimed = false;
    }
    s_sequence.status.armed = false;
    s_sequence.status.ready = false;
    s_sequence.status.pending = false;
    s_sequence.status.busy = false;
    s_sequence.status.paused = false;
    s_sequence.status.gateway_waiting = false;
    s_sequence.status.gateway_pulse_busy = false;
    s_sequence.priming = false;
    s_sequence.status.output_ownership_mask = 0u;
    __atomic_store_n(&s_armed, 0u, __ATOMIC_RELEASE);
    publish();
    sync_io_sequence_release();
}
