#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sync_io_sequence.h"
#include "sync_io_persona_manager.h"
#include "resource_arbiter.h"
typedef unsigned uint;
typedef uint gpio_function_t;
enum pio_src_dest { pio_null = 3, pio_x = 1, pio_y = 2, pio_isr = 6, pio_osr = 7 };
typedef struct { uint32_t irq, irq_force, fdebug; } fake_pio_t;
static fake_pio_t fake_pio;
typedef fake_pio_t *PIO;
struct pio_program { uint length; };
static const struct pio_program sequence_ingress_program = {6u};
static const struct pio_program sequence_finite_ingress_program = {8u};
static const struct pio_program sequence_gateway_program = {7u};
static const struct pio_program sequence_executor_program = {18u};
static const struct pio_program sequence_none_executor_program = {12u};
static const struct pio_program sequence_ready_counter_program = {6u};
static const struct pio_program sequence_counter_program = {5u};
#define BOARD_SYNC_PIO_FAST (&fake_pio)
#define BOARD_SYNC_OUTPUT_BASE_PIN 16u
#define BOARD_SYNC_OUTPUT_PIN_COUNT 4u
#define SMA_MASK 15u
#define NUM_DMA_CHANNELS 16u
#define INGRESS_SM 1u
#define EXECUTOR_SM 2u
#define COUNTER_SM 3u
#define TURNTABLE_SM 0u
#define DMA_SLOTS 5u
#define READY_IRQ 4u
#define REQUEST_IRQ 5u
#define GATEWAY_GRANT_IRQ 6u
#define PROGRAM_SLOTS 4u
#define sequence_none_executor_offset_status_active 10u
#define sequence_none_executor_offset_completed 11u
#define IRQ_MASK 48u
#define INPUT_SM_MASK 10u
#define SM_MASK 14u
#define PIO_FDEBUG_RXSTALL_LSB 0u
#define PIO_FDEBUG_RXUNDER_LSB 8u
#define PIO_FDEBUG_TXOVER_LSB 16u
#define DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS (1u << 31u)
#define GPIO_FUNC_SIO 5u
#define GPIO_OUT true
#define DMA_CH0_CTRL_TRIG_EN_BITS 1u
#define RECEIPT_BITS 10u
#define RECEIPT_WORDS 256u
#define RX_TRANSFERS 0x0fffffffu
#define __dmb() ((void)0)
#define sequence_counter_offset_observed 2u
#define sequence_counter_offset_counted 3u
#define sequence_ingress_offset_observed 2u
#define sequence_ingress_offset_decide 3u
#define sequence_ingress_offset_admitted 4u
#define sequence_ingress_offset_request 5u
#define sequence_finite_ingress_offset_quota 6u
#define sequence_finite_ingress_offset_parked 7u
#define sequence_executor_offset_waiting 5u
#define sequence_executor_offset_writing 6u
#define sequence_executor_offset_written 7u
#define sequence_executor_offset_status_active 16u
static struct { struct { uint32_t ctrl_trig, al1_ctrl, transfer_count, write_addr, reload; bool busy; } ch[16]; } fake_dma;
#define dma_hw (&fake_dma)
static struct {
    sync_io_sequence_config_t config;
    sync_io_sequence_snapshot_t status;
    uint offset[PROGRAM_SLOTS];
    int dma[DMA_SLOTS];
    uint32_t dma_mask, sm_claimed, loaded, paused_edges;
    uint32_t rx_consumed, rx_stopped_produced;
    uint32_t last_edges, pause_started, initial_output;
    uint32_t prime_receipts_remaining;
    uint32_t counter_baseline, gateway_edge_consumed;
    gpio_function_t saved_function[4];
    bool saved_direction[4], pins_saved, rx_stopped, paused;
    bool priming, manager_claimed;
    bool capture_leased, gateway_running;
} s_sequence;
static sync_io_persona_manager_t s_manager;
static sync_io_persona_manager_handle_t s_handle;
static uint32_t s_armed;
static uint32_t s_receipts[RECEIPT_WORDS];
static uint32_t s_plan[SYNC_IO_SEQUENCE_PLAN_MAX * SYNC_IO_SEQUENCE_PLAN_WORDS];
static uint32_t abort_tail, receipt_aborts, all_aborts, sm_restarts, fifo_clears, edge_abort_tail;
static void fail(uint32_t reason) { s_sequence.status.fault = reason; }
static uint get_core_num(void) { return 1u; }
static void publish(void) {}
static uint32_t dma_encode_endless_transfer_count(void) { return 0xf0000000u; }
static volatile uint32_t s_edge_latest;
static volatile uint32_t s_counter_latest;
static uint32_t s_counter_injected;
static bool capture_available = true, capture_borrowed;
static bool sync_io_core_capture_sm_lease(const void *owner) {
    (void)owner;
    if (!capture_available || capture_borrowed) return false;
    capture_borrowed = true;
    return true;
}
static void sync_io_core_capture_sm_restore(const void *owner) {
    (void)owner; assert(capture_borrowed); capture_borrowed = false;
}
static uint32_t dma_claims, sm_claims, resources, words, pads, enabled;
static uint32_t directions[32], functions[32], touches[32], pcs[4], xs[4], isrs[4], rx[4];
static uint32_t ys[4], osrs[4];
static bool rx_valid[4];
static bool tx_valid[4];
static uint32_t tx_value[4];
static uint checkpoint, fail_at;
static bool arm_allowed, start_allowed;
static bool inject(void) { return ++checkpoint == fail_at; }
static bool dma_channel_is_claimed(uint ch) { return (dma_claims & (1u << ch)) || inject(); }
static void dma_channel_claim(uint ch) { assert(!(dma_claims & (1u << ch))); dma_claims |= 1u << ch; }
static void dma_channel_unclaim(uint ch) { assert(dma_claims & (1u << ch)); dma_claims &= ~(1u << ch); }
static void dma_channel_abort(uint ch) {
    ++all_aborts;
    if (ch == (uint)s_sequence.dma[3] && edge_abort_tail) {
        s_edge_latest = edge_abort_tail; /* final old DMA write before abort ACK */
        edge_abort_tail = 0u;
    }
    assert(dma_claims & (1u << ch));
    assert((fake_dma.ch[ch].ctrl_trig & DMA_CH0_CTRL_TRIG_EN_BITS) == 0u);
    fake_dma.ch[ch].transfer_count = 0u;
    fake_dma.ch[ch].busy = false;
    if (ch == (uint)s_sequence.dma[2]) {
        ++receipt_aborts;
        fake_dma.ch[ch].write_addr = (fake_dma.ch[ch].write_addr + abort_tail * 4u) & 1023u;
        abort_tail = 0u;
    }
}
static void dma_channel_set_trans_count(uint ch, uint32_t count, bool trigger) {
    assert(trigger && (fake_dma.ch[ch].ctrl_trig & DMA_CH0_CTRL_TRIG_EN_BITS));
    fake_dma.ch[ch].reload = count;
    if (!fake_dma.ch[ch].busy) {
        fake_dma.ch[ch].transfer_count = count;
        fake_dma.ch[ch].busy = true;
    }
}
static bool pio_sm_is_claimed(PIO pio, uint sm) { (void)pio; return (sm_claims & (1u << sm)) || inject(); }
static void pio_sm_claim(PIO pio, uint sm) { (void)pio; assert(!(sm_claims & (1u << sm))); sm_claims |= 1u << sm; }
static void pio_sm_unclaim(PIO pio, uint sm) { (void)pio; assert(sm_claims & (1u << sm)); sm_claims &= ~(1u << sm); }
static bool pio_can_add_program(PIO pio, const struct pio_program *program) {
    (void)pio; return !inject() && words + program->length <= 32u;
}
static uint pio_add_program(PIO pio, const struct pio_program *program) {
    (void)pio; uint offset = words; words += program->length; return offset;
}
static void pio_remove_program(PIO pio, const struct pio_program *program, uint offset) {
    (void)pio; assert(offset >= 1u); assert(words >= program->length + 1u); words -= program->length;
}
static void pio_set_sm_mask_enabled(PIO pio, uint32_t mask, bool on) {
    (void)pio; assert(!(mask & 1u) || capture_borrowed);
    if (on) enabled |= mask; else enabled &= ~mask;
}
static void pio_enable_sm_mask_in_sync(PIO pio, uint32_t mask) {
    pio_set_sm_mask_enabled(pio, mask, true);
}
static void dma_start_channel_mask(uint32_t mask) {
    for (uint ch = 0u; ch < NUM_DMA_CHANNELS; ++ch) {
        if ((mask & (1u << ch)) == 0u) continue;
        fake_dma.ch[ch].ctrl_trig |= DMA_CH0_CTRL_TRIG_EN_BITS;
        fake_dma.ch[ch].al1_ctrl = fake_dma.ch[ch].ctrl_trig;
        fake_dma.ch[ch].busy = true;
    }
}
static void pio_sm_clear_fifos(PIO pio, uint sm) { ++fifo_clears; (void)pio; rx_valid[sm] = tx_valid[sm] = false; }
static void pio_sm_restart(PIO pio, uint sm) { ++sm_restarts; (void)pio; (void)sm; }
static void pio_sm_put(PIO pio, uint sm, uint32_t value) {
    (void)pio; assert(!tx_valid[sm]); tx_valid[sm] = true; tx_value[sm] = value;
}
static uint pio_sm_get_pc(PIO pio, uint sm) { (void)pio; return pcs[sm]; }
static void pio_interrupt_clear(PIO pio, uint irq) { pio->irq &= ~(1u << irq); }
static void dma_control_write(uint32_t *reg, uint32_t bits, bool set) {
    for (uint ch = 0u; ch < NUM_DMA_CHANNELS; ++ch) {
        if (reg != &fake_dma.ch[ch].ctrl_trig && reg != &fake_dma.ch[ch].al1_ctrl) continue;
        if (set) fake_dma.ch[ch].ctrl_trig |= bits;
        else fake_dma.ch[ch].ctrl_trig &= ~bits;
        fake_dma.ch[ch].al1_ctrl = fake_dma.ch[ch].ctrl_trig;
        /* Atomic writes retain CTRL_TRIG's trigger side effect. Busy triggers
         * are ignored; AL1_CTRL only updates the control bits. */
        if (reg == &fake_dma.ch[ch].ctrl_trig &&
            (fake_dma.ch[ch].ctrl_trig & DMA_CH0_CTRL_TRIG_EN_BITS) && !fake_dma.ch[ch].busy) {
            fake_dma.ch[ch].transfer_count = fake_dma.ch[ch].reload;
            fake_dma.ch[ch].busy = true;
        }
        return;
    }
    assert(false);
}
static void hw_clear_bits(uint32_t *reg, uint32_t bits) { dma_control_write(reg, bits, false); }
static void hw_set_bits(uint32_t *reg, uint32_t bits) { dma_control_write(reg, bits, true); }
static gpio_function_t gpio_get_function(uint pin) { return functions[pin]; }
static bool gpio_get_dir(uint pin) { return directions[pin]; }
static void gpio_set_function(uint pin, uint f) { functions[pin] = f; ++touches[pin]; }
static void gpio_set_dir(uint pin, bool d) { directions[pin] = d; ++touches[pin]; }
static void gpio_put_masked(uint32_t mask, uint32_t value) { pads = (pads & ~mask) | value; }
static void pio_sm_set_pins_with_mask(PIO pio, uint sm, uint32_t value, uint32_t mask) {
    (void)pio; (void)sm; pads = (pads & ~mask) | (value & mask);
}
static uint pio_encode_mov(uint dest, uint source) { return 0xa000u | (dest << 5u) | source; }
static uint pio_encode_mov_not(uint dest, uint source) { return pio_encode_mov(dest, source) | 8u; }
static uint pio_encode_push(bool conditional, bool block) { (void)conditional; (void)block; return 0x8000u; }
static uint pio_encode_pull(bool conditional, bool block) { (void)conditional; (void)block; return 0x80a0u; }
static uint pio_encode_jmp(uint target) { return target; }
static uint pio_encode_jmp_x_dec(uint target) { return 0x40u | target; }
static void pio_sm_exec(PIO pio, uint sm, uint instruction) {
    (void)pio;
    if ((instruction >> 13u) == 5u) {
        const uint source = instruction & 7u;
        uint32_t value = source == pio_x ? xs[sm] : source == pio_y ? ys[sm] :
            source == pio_osr ? osrs[sm] : 0u;
        value = (instruction & 8u) ? ~value : value;
        if (((instruction >> 5u) & 7u) == pio_x) xs[sm] = value;
        else if (((instruction >> 5u) & 7u) == pio_y) ys[sm] = value;
        else isrs[sm] = value;
    } else if ((instruction >> 13u) == 4u) {
        if (instruction & 0x80u) {
            assert(tx_valid[sm]); osrs[sm] = tx_value[sm]; tx_valid[sm] = false;
        } else {
            assert(!rx_valid[sm]); rx[sm] = isrs[sm]; isrs[sm] = 0u; rx_valid[sm] = true;
        }
    } else {
        if (instruction & 0x40u) --xs[sm];
        pcs[sm] = instruction & 31u;
    }
}
static uint32_t pio_sm_get(PIO pio, uint sm) { (void)pio; assert(rx_valid[sm]); rx_valid[sm] = false; return rx[sm]; }
static bool pio_sm_is_tx_fifo_full(PIO pio, uint sm) { (void)pio; return tx_valid[sm]; }
static bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) { (void)pio; return !rx_valid[sm]; }
bool resource_arbiter_acquire_owned(uint32_t mask, const char *owner) {
    (void)owner; if (resources & mask) return false; resources |= mask; return true;
}
void resource_arbiter_release_owned(uint32_t mask, const char *owner) { (void)owner; resources &= ~mask; }
void sync_io_sequence_release(void) {
    assert(!s_armed);
    resources &= ~RESOURCE_ARBITER_RESOURCE_SMA_GPIO;
}

/* PRODUCTION_FUNCTIONS */

static bool arm_hook(void *c, const sync_io_persona_descriptor_t *d, uint32_t m) {
    (void)c; (void)d; (void)m; return arm_allowed;
}
static bool start_hook(void *c, const sync_io_persona_descriptor_t *d, uint32_t m) {
    (void)c; (void)d; (void)m; return start_allowed;
}
static void reset(void) {
    memset(&s_sequence, 0, sizeof(s_sequence));
    memset(&fake_pio, 0, sizeof(fake_pio));
    memset(&fake_dma, 0, sizeof(fake_dma));
    for (uint i = 0u; i < NUM_DMA_CHANNELS; ++i) fake_dma.ch[i].reload = RX_TRANSFERS;
    memset(touches, 0, sizeof(touches));
    memset(rx_valid, 0, sizeof(rx_valid));
    memset(tx_valid, 0, sizeof(tx_valid));
    for (uint i = 0u; i < DMA_SLOTS; ++i) s_sequence.dma[i] = -1;
    s_sequence.config = (sync_io_sequence_config_t){
        1u, false, 3u, 8u, SYNC_IO_SEQUENCE_STATUS_PULSE, 1u, 1u,
        false, 0u, 0u, 0u, false, false, 0u, 0u, 0u};
    capture_available = true;
    capture_borrowed = false;
    s_counter_latest = s_edge_latest = s_counter_injected = 0u;
    dma_claims = 0x1fbu; /* RS485, capture and TDMA survive every rollback. */
    sm_claims = enabled = 1u;
    resources = RESOURCE_ARBITER_RESOURCE_SMA_GPIO;
    words = 1u;
    pads = 0xffff0000u;
    for (uint i = 0u; i < 32u; ++i) { functions[i] = 5u; directions[i] = i & 1u; }
    checkpoint = fail_at = abort_tail = receipt_aborts = 0u;
    all_aborts = sm_restarts = fifo_clears = edge_abort_tail = 0u;
    arm_allowed = start_allowed = true;
}
int main(void) {
    /* Real startup register/FIFO setup: ordinary execution begins with state
     * zero, even when there are no later admitted steps. */
    for (uint empty = 0u; empty < 2u; ++empty) {
        reset();
        s_sequence.status.plan_count = 3u;
        s_sequence.config.step_limit_enabled = empty != 0u;
        s_sequence.config.max_steps = 0u;
        s_plan[6] = 5u | (13u << 12u);
        s_plan[7] = 195u;
        s_plan[8] = 96u;
        s_sequence.offset[1] = 8u;
        prime_initial_state();
        assert(ys[EXECUTOR_SM] == s_plan[6] && xs[EXECUTOR_SM] == 195u);
        assert(tx_valid[EXECUTOR_SM] && tx_value[EXECUTOR_SM] == 96u);
        assert(pcs[EXECUTOR_SM] == 8u + sequence_executor_offset_writing);
        assert(s_sequence.priming && s_sequence.prime_receipts_remaining == 2u);
        assert(!pending_request());
        s_sequence.dma[0] = 1; s_sequence.dma[2] = 2;
        assert(start_hardware(NULL, NULL, 0u));
        assert(enabled == (1u | (1u << EXECUTOR_SM)));
        assert(fake_dma.ch[1].busy && fake_dma.ch[2].busy);
    }
    /* Execute production STOP across initial-write/pulse/completion windows.
     * None of these initial receipts may become an advance or cancellation. */
    for (uint received = 0u; received <= 2u; ++received) {
        reset();
        s_sequence.config.input_channel = 0u;
        s_sequence.status.plan_count = 3u;
        s_sequence.status.current_index = 0u;
        s_plan[6] = 1u | (9u << 12u);
        s_plan[7] = 5u;
        s_plan[8] = 6u;
        const sync_io_persona_manager_hooks_t hooks = {
            .load = load_hardware, .arm = arm_hook, .start = start_hook,
            .stop = stop_hook, .cleanup = cleanup
        };
        sync_io_persona_manager_init(&s_manager, &hooks, NULL);
        s_manager.used_dma_channel_mask = dma_claims;
        assert(sync_io_persona_manager_claim(&s_manager, SYNC_IO_PERSONA_ID_SEQUENCE, &s_handle, NULL));
        assert(sync_io_persona_manager_load(&s_manager, &s_handle));
        assert(sync_io_persona_manager_arm(&s_manager, &s_handle));
        assert(sync_io_persona_manager_start(&s_manager, &s_handle));
        s_sequence.manager_claimed = true;
        s_sequence.status.armed = true;
        s_armed = 1u;
        prime_initial_state();
        pcs[EXECUTOR_SM] = s_sequence.offset[1] +
            (received == 0u ? sequence_executor_offset_written :
             received == 1u ? 14u : 0u);
        const uint rx_dma = (uint)s_sequence.dma[2];
        fake_dma.ch[rx_dma].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
        fake_dma.ch[rx_dma].transfer_count = RX_TRANSFERS - received;
        fake_dma.ch[rx_dma].write_addr = received * 4u;
        s_receipts[0] = s_plan[6];
        s_receipts[1] = ~s_plan[6];
        sync_io_sequence_stop();
        assert(s_sequence.status.fault == 0u && !s_sequence.status.armed && !s_armed);
        assert(s_sequence.status.accepted == 0u && s_sequence.status.written == 0u &&
               s_sequence.status.completed == 0u && s_sequence.status.cancelled == 0u);
        assert(s_sequence.rx_consumed == received);
        assert(!s_sequence.priming && !s_sequence.manager_claimed);
        assert((pads & (11u << BOARD_SYNC_OUTPUT_BASE_PIN)) == 0u);
        assert(words == 1u && sm_claims == 1u && dma_claims == 0x1fbu && resources == 0u);
        assert(sync_io_persona_manager_deinit(&s_manager));
    }
    for (uint failure = 0u; failure <= 13u; ++failure) {
        reset();
        fail_at = failure <= 10u ? failure : 0u;
        arm_allowed = failure != 11u;
        start_allowed = failure != 12u;
        sync_io_persona_manager_t manager;
        sync_io_persona_manager_handle_t handle;
        const sync_io_persona_manager_hooks_t hooks = {
            .load = load_hardware, .arm = arm_hook, .start = start_hook,
            .stop = stop_hook, .cleanup = cleanup
        };
        sync_io_persona_manager_init(&manager, &hooks, NULL);
        manager.used_dma_channel_mask = dma_claims;
        assert(sync_io_persona_manager_claim(&manager, SYNC_IO_PERSONA_ID_SEQUENCE, &handle, NULL));
        bool loaded = sync_io_persona_manager_load(&manager, &handle);
        if (failure >= 1u && failure <= 10u) assert(!loaded);
        if (loaded) {
            assert(words == 30u && sm_claims == 15u);
            assert((s_sequence.dma_mask & 0x1fbu) == 0u);
            bool armed = sync_io_persona_manager_arm(&manager, &handle);
            assert(armed == arm_allowed);
            if (armed) assert(sync_io_persona_manager_start(&manager, &handle) == start_allowed);
        }
        if (sync_io_persona_manager_handle_valid(&manager, &handle))
            assert(sync_io_persona_manager_release(&manager, &handle));
        assert(words == 1u && sm_claims == 1u && enabled == 1u);
        assert(dma_claims == 0x1fbu && resources == RESOURCE_ARBITER_RESOURCE_SMA_GPIO);
        assert(touches[18] == 0u && (pads & (1u << 18u)) != 0u);
        cleanup(NULL, NULL, 0u);
        assert(words == 1u && dma_claims == 0x1fbu);
        assert(sync_io_persona_manager_deinit(&manager));
    }
    /* START receipts never debit the admission quota. Even READY cannot
     * admit an edge until both initial write and status completion arrive. */
    reset();
    s_sequence.status.armed = true;
    s_sequence.status.plan_count = 3u;
    s_sequence.status.current_index = 0u;
    s_sequence.status.completed_index = UINT32_MAX;
    s_sequence.status.rejection_counts_pending = true;
    s_sequence.config.status_mode = SYNC_IO_SEQUENCE_STATUS_LEVEL;
    s_sequence.config.pulse_us = 0u;
    s_sequence.priming = true;
    s_sequence.prime_receipts_remaining = 2u;
    s_plan[6] = 8u << 12u;
    for (uint i = 0u; i < 4u; ++i) s_sequence.dma[i] = (int)i;
    dma_claims = 15u;
    fake_dma.ch[2].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
    fake_dma.ch[2].transfer_count = RX_TRANSFERS;
    pcs[EXECUTOR_SM] = sequence_executor_offset_waiting;
    fake_pio.irq = 1u << READY_IRQ;
    pads = 0u;
    sync_io_sequence_service();
    assert(s_sequence.priming && !s_sequence.status.ready);
    assert((enabled & INPUT_SM_MASK) == 0u && !fake_dma.ch[3].busy);
    assert(receive_word(s_plan[6]));
    sync_io_sequence_service();
    assert(s_sequence.priming && !s_sequence.status.ready);
    assert(sync_io_sequence_pause(true));
    assert((enabled & INPUT_SM_MASK) == 0u);
    assert(sync_io_sequence_pause(false));
    assert((enabled & INPUT_SM_MASK) == 0u);
    assert(receive_word(~s_plan[6]));
    sync_io_sequence_service();
    assert(!s_sequence.priming && s_sequence.status.ready);
    assert(s_sequence.status.accepted == 0u && s_sequence.status.written == 0u &&
           s_sequence.status.completed == 0u);
    assert((enabled & INPUT_SM_MASK) == INPUT_SM_MASK && fake_dma.ch[3].busy);
    /* A one-state/one-round START settles without granting any input edge. */
    s_sequence.config.step_limit_enabled = true;
    s_sequence.config.max_steps = 0u;
    s_sequence.priming = true;
    s_sequence.prime_receipts_remaining = 2u;
    enabled = 1u;
    fake_pio.irq = 0u;
    sync_io_sequence_service();
    assert(!s_sequence.status.finished);
    assert(receive_word(s_plan[6]));
    assert(receive_word(~s_plan[6]));
    fake_pio.irq = 1u << READY_IRQ;
    sync_io_sequence_service();
    assert(s_sequence.status.finished && !s_sequence.status.ready);
    assert((enabled & INPUT_SM_MASK) == 0u);
    assert(!sync_io_sequence_software_step());
    for (uint pc = 0u; pc < 5u; ++pc) {
        reset();
        s_sequence.dma[3] = 0;
        s_sequence.offset[2] = 20u;
        pcs[COUNTER_SM] = 20u + pc;
        xs[COUNTER_SM] = ~7u;
        const uint32_t expected = pc == 2u ? 8u : 7u;
        assert(stop_counter() == expected);
        assert(isrs[COUNTER_SM] == expected);
        pio_sm_exec(BOARD_SYNC_PIO_FAST, COUNTER_SM, pio_encode_push(false, false));
        assert(pio_sm_get(BOARD_SYNC_PIO_FAST, COUNTER_SM) == expected);
    }
    for (uint pc = 0u; pc < 6u; ++pc) {
        reset();
        s_sequence.offset[0] = 10u;
        pcs[INGRESS_SM] = 10u + pc;
        xs[INGRESS_SM] = UINT32_MAX;
        fake_pio.irq = 1u << READY_IRQ;
        finish_ingress();
        assert(s_sequence.paused_edges == (pc == 2u ? 1u : 0u));
        assert((fake_pio.irq_force != 0u) == (pc >= 3u));
        assert(pcs[INGRESS_SM] == 10u);
    }
    for (uint pc = 0u; pc < 8u; ++pc) {
        for (uint remaining = 0u; remaining < 3u; ++remaining) {
            reset();
            s_sequence.config.step_limit_enabled = true;
            s_sequence.config.max_steps = 79999u;
            s_sequence.offset[0] = 10u;
            pcs[INGRESS_SM] = 10u + pc;
            xs[INGRESS_SM] = UINT32_MAX;
            ys[INGRESS_SM] = remaining;
            fake_pio.irq = 1u << READY_IRQ;
            finish_ingress();
            const bool admitted = pc >= 3u && pc <= 6u;
            const bool parked = pc == 7u || (admitted && remaining == 0u);
            assert(pcs[INGRESS_SM] == 10u + (parked ? 7u : 0u));
            assert((fake_pio.irq_force != 0u) == (pc >= 3u && pc <= 5u));
            assert(ys[INGRESS_SM] == (admitted && remaining > 0u ? remaining - 1u : remaining));
        }
    }
    /* STOP after nine complete MANUAL steps: hardware abort clears TRANS_COUNT. */
    reset();
    s_sequence.dma[2] = 0;
    s_sequence.status.plan_count = 1u;
    s_sequence.status.written = s_sequence.status.completed = s_sequence.status.accepted = 9u;
    s_sequence.rx_consumed = 18u;
    fake_dma.ch[0].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
    fake_dma.ch[0].transfer_count = RX_TRANSFERS - 18u;
    fake_dma.ch[0].write_addr = 18u * 4u;
    for (uint pause = 0u; pause < 3u; ++pause) {
        stop_receipt_dma();
        assert(fake_dma.ch[0].transfer_count == 0u);
        assert(produced_receipts() == 18u);
        assert(drain_receipts() && s_sequence.status.fault == 0u);
        resume_receipt_dma();
        assert(fake_dma.ch[0].transfer_count == RX_TRANSFERS - 18u);
        assert(produced_receipts() == 18u);
    }
    /* Final writes retiring during abort cross the circular buffer boundary. */
    s_sequence.status.written = s_sequence.status.completed = s_sequence.status.accepted = 127u;
    s_sequence.rx_consumed = 254u;
    fake_dma.ch[0].transfer_count = RX_TRANSFERS - 255u;
    fake_dma.ch[0].write_addr = 255u * 4u;
    s_plan[0] = 3u;
    s_receipts[254] = 3u; s_receipts[255] = ~3u;
    s_receipts[0] = 3u; s_receipts[1] = ~3u;
    abort_tail = 3u;
    stop_receipt_dma();
    assert(produced_receipts() == 258u && fake_dma.ch[0].write_addr == 8u);
    assert(drain_receipts());
    assert(s_sequence.status.written == 129u && s_sequence.status.completed == 129u);
    assert(s_sequence.status.fault == 0u);
    resume_receipt_dma();
    assert(produced_receipts() == 258u);
    /* Exercise the actual PAUSE/service/CONTINUE path after a receipt-ring wrap. */
    reset();
    s_sequence.status.armed = true;
    s_sequence.status.plan_count = 8u;
    s_sequence.status.written = s_sequence.status.completed = s_sequence.status.accepted = 154u;
    s_sequence.rx_consumed = 308u;
    s_sequence.last_edges = s_edge_latest = 154u;
    for (uint i = 0u; i < 4u; ++i) s_sequence.dma[i] = (int)i;
    dma_claims = 15u;
    pcs[INGRESS_SM] = 0u;
    pcs[COUNTER_SM] = 1u;
    pcs[EXECUTOR_SM] = sequence_executor_offset_waiting;
    fake_pio.irq = 1u << READY_IRQ;
    xs[COUNTER_SM] = ~154u;
    fake_dma.ch[2].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
    fake_dma.ch[2].transfer_count = RX_TRANSFERS - 308u;
    fake_dma.ch[2].write_addr = (308u % RECEIPT_WORDS) * 4u;
    assert(sync_io_sequence_pause(true));
    assert(s_sequence.status.paused && !s_sequence.status.ready);
    assert(s_sequence.status.input_events == 154u);
    assert(s_sequence.rx_stopped && receipt_aborts == 1u);
    assert(!s_sequence.status.rejection_counts_pending);
    for (uint edge = 155u; edge <= 180u; ++edge) {
        s_edge_latest = edge;
        xs[COUNTER_SM] = ~edge;
        sync_io_sequence_service();
        assert(s_sequence.status.fault == 0u);
        assert(s_sequence.status.completed == 154u);
        assert(s_sequence.status.notready_rejected == edge - 154u);
        assert(s_sequence.rx_stopped && receipt_aborts == 1u);
    }
    assert(sync_io_sequence_pause(false));
    assert(s_sequence.status.ready && !s_sequence.status.paused);
    assert(s_sequence.status.notready_rejected == 26u);
    assert((enabled & INPUT_SM_MASK) == INPUT_SM_MASK);
    assert(!s_sequence.rx_stopped);
    for (uint i = 0u; i < 8u; ++i)
        s_plan[i * SYNC_IO_SEQUENCE_PLAN_WORDS] = (i << 4u) | (i & 3u);
    s_receipts[308u % RECEIPT_WORDS] = s_plan[2u * SYNC_IO_SEQUENCE_PLAN_WORDS];
    s_receipts[309u % RECEIPT_WORDS] = ~s_plan[2u * SYNC_IO_SEQUENCE_PLAN_WORDS];
    fake_dma.ch[2].transfer_count -= 2u;
    fake_dma.ch[2].write_addr += 8u;
    xs[COUNTER_SM] = ~181u;
    s_edge_latest = 181u;
    sync_io_sequence_service();
    assert(s_sequence.status.fault == 0u && s_sequence.status.completed == 155u);
    xs[COUNTER_SM] = ~180u;
    assert(!sync_io_sequence_pause(true));
    assert(s_sequence.status.fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_REGRESSION);
    assert((enabled & INPUT_SM_MASK) == 0u);
    /* Gateway uses the same lease and a smaller ingress replacement; READY
     * reports once and cannot itself issue REQUEST_IRQ or a software step. */
    reset();
    s_sequence.config = (sync_io_sequence_config_t){
        .sequence_output_mask = 7u, .status_mode = SYNC_IO_SEQUENCE_STATUS_NONE,
        .gateway_enabled = true,
        .gateway_input_channel = 1u, .gateway_output_mask = 8u, .gateway_pulse_us = 10u};
    assert(load_hardware(NULL, sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_SEQUENCE),
                         (1u << 2u) | (1u << 9u) | (1u << 10u) | (1u << 11u)));
    assert(words == 26u && sm_claims == 15u);
    s_sequence.status.armed = true;
    s_sequence.status.plan_count = 8u;
    const uint receipt_ch = (uint)s_sequence.dma[2];
    fake_dma.ch[receipt_ch].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
    fake_dma.ch[receipt_ch].transfer_count = RX_TRANSFERS;
    pcs[EXECUTOR_SM] = s_sequence.offset[1] + sequence_executor_offset_waiting;
    fake_pio.irq = 1u << READY_IRQ;
    s_edge_latest = 0u;
    xs[COUNTER_SM] = UINT32_MAX;
    gateway_start();
    sync_io_sequence_service();
    const uint32_t fire_aborts = all_aborts, fire_restarts = sm_restarts, fire_clears = fifo_clears;
    const uint32_t fire_enabled = enabled;
    tx_valid[INGRESS_SM] = true;
    assert(!sync_io_sequence_gateway_fire()); /* full TX FIFO is not a blocking write */
    assert(!s_sequence.status.gateway_waiting && s_sequence.status.gateway_trigger_count == 0u);
    tx_valid[INGRESS_SM] = false;
    assert(sync_io_sequence_gateway_fire());
    assert(all_aborts == fire_aborts && sm_restarts == fire_restarts && fifo_clears == fire_clears);
    assert(enabled == fire_enabled && fake_dma.ch[(uint)s_sequence.dma[3]].busy);
    assert(s_edge_latest == 0u && s_sequence.status.gateway_waiting);
    assert(s_sequence.status.gateway_trigger_count == 1u);
    assert(tx_value[INGRESS_SM] == 98u && xs[COUNTER_SM] == UINT32_MAX);
    assert(!sync_io_sequence_gateway_fire());
    assert(!sync_io_sequence_software_step());
    s_edge_latest = 1u; /* Gated counter admits one READY while trigger remains high */
    pcs[INGRESS_SM] = s_sequence.offset[0] + 3u;
    tx_valid[INGRESS_SM] = false;
    sync_io_sequence_service();
    assert(s_sequence.status.gateway_ready_count == 1u);
    assert(!s_sequence.status.gateway_waiting && s_sequence.status.gateway_pulse_busy);
    assert(s_sequence.status.accepted == 0u && fake_pio.irq_force == 0u);
    assert(!sync_io_sequence_software_step());
    pcs[INGRESS_SM] = s_sequence.offset[0];
    rx_valid[INGRESS_SM] = true;
    sync_io_sequence_service();
    assert(!s_sequence.status.gateway_pulse_busy);
    sync_io_sequence_service();
    assert(s_sequence.status.gateway_ready_count == 1u);
    assert(sync_io_sequence_gateway_fire());
    assert(s_sequence.status.gateway_trigger_count == 2u && s_edge_latest == 1u);
    sync_io_sequence_service(); /* previous cumulative value cannot complete new FIRE */
    assert(s_sequence.status.gateway_waiting && s_sequence.status.gateway_ready_count == 1u);
    assert(all_aborts == fire_aborts && sm_restarts == fire_restarts && fifo_clears == fire_clears);
    assert(!sync_io_sequence_gateway_ready());
    s_edge_latest = 2u;
    tx_valid[INGRESS_SM] = false;
    sync_io_sequence_service();
    assert(!s_sequence.status.gateway_waiting && s_sequence.status.gateway_ready_count == 2u);
    assert(!sync_io_sequence_gateway_ready());
    pcs[INGRESS_SM] = s_sequence.offset[0];
    rx_valid[INGRESS_SM] = true;
    sync_io_sequence_service();
    assert(!s_sequence.status.gateway_pulse_busy);
    assert(sync_io_sequence_gateway_fire());
    assert(s_sequence.status.gateway_trigger_count == 3u);
    edge_abort_tail = 3u; /* READY completed in PIO but its DMA arrives during cancellation */
    fake_pio.irq |= 1u << GATEWAY_GRANT_IRQ;
    assert(sync_io_sequence_pause(true));
    assert(edge_abort_tail == 0u && s_edge_latest == 0u && s_sequence.gateway_edge_consumed == 0u);
    assert(!(fake_pio.irq & (1u << GATEWAY_GRANT_IRQ)) && !s_sequence.gateway_running);
    assert(s_sequence.status.gateway_cancelled == 1u);
    assert(!s_sequence.status.gateway_waiting && !s_sequence.status.gateway_pulse_busy);
    assert((pads & (8u << BOARD_SYNC_OUTPUT_BASE_PIN)) == 0u);
    assert(!sync_io_sequence_gateway_fire());
    assert(sync_io_sequence_pause(false));
    assert(s_sequence.gateway_running && s_sequence.status.gateway_ready_count == 2u);
    assert(s_sequence.status.gateway_trigger_count == 3u); /* resume never self-fires */
    assert(sync_io_sequence_gateway_fire());
    s_edge_latest = 1u;
    tx_valid[INGRESS_SM] = false;
    pcs[INGRESS_SM] = s_sequence.offset[0];
    rx_valid[INGRESS_SM] = true;
    sync_io_sequence_service();
    assert(s_sequence.status.gateway_ready_count == 3u);
    assert(sync_io_sequence_software_step());
    assert(s_sequence.status.accepted == 1u);
    assert(!sync_io_sequence_gateway_fire());
    cleanup(NULL, NULL, 0u);
    assert(words == 1u && sm_claims == 1u && dma_claims == 0x1fbu);
    assert((pads & (15u << BOARD_SYNC_OUTPUT_BASE_PIN)) == 0u);
    /* MANUAL READY still emits the VNA trigger pulse, but never starts the
     * external counter SM or its edge DMA channel. */
    reset();
    s_sequence.config = (sync_io_sequence_config_t){
        .sequence_output_mask = 7u, .status_mode = SYNC_IO_SEQUENCE_STATUS_NONE,
        .gateway_enabled = true, .gateway_output_mask = 8u, .gateway_pulse_us = 10u};
    assert(load_hardware(NULL, sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_SEQUENCE),
                         (1u << 2u) | (1u << 9u) | (1u << 10u) | (1u << 11u)));
    s_sequence.status.armed = s_sequence.status.ready = true;
    s_sequence.status.plan_count = 8u;
    const uint manual_receipt_ch = (uint)s_sequence.dma[2];
    const uint manual_edge_ch = (uint)s_sequence.dma[3];
    fake_dma.ch[manual_receipt_ch].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
    fake_dma.ch[manual_receipt_ch].transfer_count = RX_TRANSFERS;
    pcs[EXECUTOR_SM] = s_sequence.offset[1] + sequence_executor_offset_waiting;
    fake_pio.irq = 1u << READY_IRQ;
    gateway_start();
    assert(sync_io_sequence_gateway_fire());
    assert(s_sequence.status.gateway_waiting && s_sequence.status.gateway_pulse_busy);
    assert((enabled & (1u << INGRESS_SM)) != 0u);
    assert((enabled & (1u << COUNTER_SM)) == 0u);
    assert((fake_dma.ch[manual_edge_ch].ctrl_trig & DMA_CH0_CTRL_TRIG_EN_BITS) == 0u);
    assert(sync_io_sequence_gateway_ready());
    assert(!s_sequence.status.gateway_waiting && s_sequence.status.gateway_ready_count == 1u);
    s_edge_latest = 1u;
    sync_io_sequence_service();
    assert(s_sequence.status.gateway_ready_count == 1u);
    assert(!sync_io_sequence_gateway_ready());
    cleanup(NULL, NULL, 0u);
    assert(words == 1u && sm_claims == 1u && dma_claims == 0x1fbu);
    /* Compact completion is at PUSH (11), not MOV ISR (10): PC10 is
     * still one tick before the configured settle interval has elapsed. */
    for (uint pc = 10u; pc <= 11u; ++pc) {
        reset();
        s_sequence.config = (sync_io_sequence_config_t){
            .sequence_output_mask = 7u, .status_mode = SYNC_IO_SEQUENCE_STATUS_NONE,
            .gateway_enabled = true, .gateway_output_mask = 8u, .gateway_pulse_us = 10u};
        assert(load_hardware(NULL, sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_SEQUENCE),
            (1u << 2u) | (1u << 9u) | (1u << 10u) | (1u << 11u)));
        s_sequence.status.armed = true;
        s_sequence.status.plan_count = 1u;
        s_sequence.status.accepted = s_sequence.status.written = 1u;
        s_plan[0] = 1u;
        pcs[EXECUTOR_SM] = s_sequence.offset[1] + pc;
        const uint ch = (uint)s_sequence.dma[2];
        fake_dma.ch[ch].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
        fake_dma.ch[ch].transfer_count = RX_TRANSFERS;
        sync_io_sequence_stop();
        assert(!s_sequence.status.fault);
        assert(s_sequence.status.completed == (pc == 11u ? 1u : 0u));
        assert(s_sequence.status.cancelled == (pc == 10u ? 1u : 0u));
        cleanup(NULL, NULL, 0u);
        assert(words == 1u && sm_claims == 1u && dma_claims == 0x1fbu);
    }
    /* A finite ingress parked at its last debit stays parked across resume. */
    reset();
    s_sequence.config.step_limit_enabled = true;
    s_sequence.config.max_steps = 1u;
    s_sequence.status.armed = true;
    s_sequence.status.plan_count = 8u;
    s_sequence.status.accepted = s_sequence.status.written = s_sequence.status.completed = 1u;
    for (uint i = 0u; i < 4u; ++i) s_sequence.dma[i] = (int)i;
    dma_claims = 15u;
    fake_dma.ch[2].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
    fake_dma.ch[2].transfer_count = RX_TRANSFERS;
    pcs[EXECUTOR_SM] = sequence_executor_offset_waiting;
    pcs[INGRESS_SM] = sequence_finite_ingress_offset_parked;
    xs[COUNTER_SM] = ~1u;
    fake_pio.irq = 1u << READY_IRQ;
    s_edge_latest = 1u;
    assert(sync_io_sequence_pause(true));
    assert(sync_io_sequence_pause(false));
    assert(pcs[INGRESS_SM] == sequence_finite_ingress_offset_parked);
    assert(s_sequence.status.finished && !s_sequence.status.ready);
    /* The third-mode lease includes SM0 and a fifth DMA without changing the
     * resident capture claim/instruction. Failure at every acquisition point
     * releases exactly the successfully acquired resources. */
    for (uint failure = 0u; failure <= 13u; ++failure) {
        reset();
        enabled = 0u; /* capture is idle */
        s_sequence.config = (sync_io_sequence_config_t){
            .sequence_output_mask = 7u, .status_mode = SYNC_IO_SEQUENCE_STATUS_NONE,
            .gateway_enabled = true, .gateway_input_channel = 2u,
            .gateway_output_mask = 8u, .gateway_pulse_us = 10u,
            .counter_input_channel = 1u, .counter_threshold = 1000u};
        capture_available = failure != 13u;
        fail_at = failure <= 12u ? failure : 0u;
        const bool loaded = load_hardware(NULL,
            sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_SEQUENCE_COUNTER),
            (1u << 2u) | (1u << 9u) | (1u << 10u) | (1u << 11u) | (1u << 12u));
        assert(loaded == (failure == 0u));
        if (loaded) {
            assert(s_sequence.capture_leased && capture_borrowed);
            assert(words == 31u && sm_claims == 15u && s_sequence.sm_claimed == 15u);
            assert(s_sequence.dma[4] == 12);
        }
        cleanup(NULL, NULL, 0u);
        assert(!capture_borrowed && !s_sequence.capture_leased);
        assert(words == 1u && sm_claims == 1u && dma_claims == 0x1fbu);
        assert(enabled == 0u);
    }
    /* Execute the production service with independent cumulative counter DMA:
     * busy allows partial pulses, READY resets only its own counter, and
     * rearm preserves the next position's remainder. */
    reset();
    enabled = 0u;
    s_sequence.config = (sync_io_sequence_config_t){
        .sequence_output_mask = 7u, .status_mode = SYNC_IO_SEQUENCE_STATUS_NONE,
        .gateway_enabled = true, .gateway_input_channel = 2u,
        .gateway_output_mask = 8u, .gateway_pulse_us = 10u,
        .counter_input_channel = 1u, .counter_threshold = 1000u};
    assert(load_hardware(NULL, sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_SEQUENCE_COUNTER),
        (1u << 2u) | (1u << 9u) | (1u << 10u) | (1u << 11u) | (1u << 12u)));
    s_sequence.status.armed = true;
    s_sequence.status.plan_count = 8u;
    s_sequence.priming = true;
    s_sequence.prime_receipts_remaining = 0u;
    const uint counter_receipt_ch = (uint)s_sequence.dma[2];
    fake_dma.ch[counter_receipt_ch].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
    fake_dma.ch[counter_receipt_ch].transfer_count = RX_TRANSFERS;
    pcs[EXECUTOR_SM] = s_sequence.offset[1] + sequence_executor_offset_waiting;
    fake_pio.irq = 1u << READY_IRQ;
    sync_io_sequence_service();
    assert(!s_sequence.priming && (enabled & 1u) != 0u);
    assert(fake_dma.ch[(uint)s_sequence.dma[4]].busy);
    s_counter_latest = 999u;
    sync_io_sequence_service();
    assert(s_sequence.status.counter_events == 999u && !s_sequence.status.counter_busy);
    assert(!sync_io_sequence_counter_inject(2u, 1u));
    assert(sync_io_sequence_counter_inject(1u, 1u));
    assert(s_sequence.status.counter_busy && s_sequence.counter_baseline == 1000u);
    assert(sync_io_sequence_gateway_fire());
    assert(s_counter_latest == 999u && (enabled & 1u) != 0u);
    s_counter_latest = 1998u;
    sync_io_sequence_service();
    assert(s_sequence.status.fault == 0u && s_sequence.status.counter_events == 1999u);
    assert(!sync_io_sequence_counter_rearm()); /* sampling still pending */
    s_edge_latest = 1u;
    rx_valid[INGRESS_SM] = true;
    tx_valid[INGRESS_SM] = false;
    sync_io_sequence_service();
    assert(sync_io_sequence_counter_rearm());
    assert(!s_sequence.status.counter_busy && s_sequence.counter_baseline == 1000u);
    assert(s_sequence.status.counter_rearm_count == 1u && s_sequence.status.counter_events == 1999u);
    assert(!sync_io_sequence_counter_rearm()); /* exactly one acknowledgement */
    assert(sync_io_sequence_counter_inject(1u, 1u));
    assert(s_sequence.status.counter_busy && s_sequence.counter_baseline == 2000u);
    assert(s_sequence.status.fault == 0u);
    s_counter_latest = 2998u;
    sync_io_sequence_service();
    assert(s_sequence.status.fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY);
    assert(s_sequence.status.counter_events == 3000u);
    assert(!sync_io_sequence_counter_rearm());
    sync_io_sequence_stop();
    assert(s_sequence.status.counter_events == 3000u); /* preserve fault evidence */
    cleanup(NULL, NULL, 0u);
    assert(!capture_borrowed && words == 1u && sm_claims == 1u && dma_claims == 0x1fbu);
    /* Snapshot coalescing may include a partial next position, but never two
     * complete unserviced positions. PAUSE retains raw pulses, not positions. */
    reset();
    s_sequence.config.counter_threshold = 1000u;
    assert(account_counter(1999u));
    assert(s_sequence.counter_baseline == 1000u && s_sequence.status.counter_busy);
    assert(!account_counter(2000u));
    reset();
    s_sequence.config.counter_threshold = 1000u;
    assert(!account_counter(2000u));
    assert(s_sequence.status.fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY);
    reset();
    s_sequence.config.counter_threshold = 1000u;
    s_sequence.paused = true;
    assert(account_counter(999u));
    assert(!account_counter(1000u));
    assert(s_sequence.status.fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY);
    reset();
    s_sequence.config.counter_threshold = 1000u;
    assert(account_counter(123u));
    assert(!account_counter(122u));
    assert(s_sequence.status.counter_events == 123u);
    assert(s_sequence.status.fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_REGRESSION);
    reset();
    s_sequence.config.counter_threshold = 1000u;
    assert(!account_counter(UINT32_MAX - 32u));
    assert(s_sequence.status.counter_events == UINT32_MAX - 32u);
    assert(s_sequence.status.fault == SYNC_IO_SEQUENCE_FAULT_COUNTER_OVERFLOW);
    puts("PIO production allocation rollback and pause boundaries passed");
    return 0;
}
