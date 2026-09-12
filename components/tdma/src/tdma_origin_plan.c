#include "tdma_origin_plan.h"

#include <limits.h>
#include <string.h>

/* SDK register constants only; the builder performs no hardware access. */
#include "hardware/platform_defs.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/dma.h"
#include "hardware/regs/dreq.h"
#include "hardware/regs/pio.h"

#define BIT(name) DMA_CH0_CTRL_TRIG_##name##_BITS
#define FIELD(name, value) ((uint32_t)(value) << DMA_CH0_CTRL_TRIG_##name##_LSB)
#define READ BIT(INCR_READ)
#define WRITE BIT(INCR_WRITE)
#define SKIP_WRITE BIT(INCR_WRITE_REV)
#define SWAP BIT(BSWAP)
#define SNIFF BIT(SNIFF_EN)

_Static_assert(sizeof(tdma_flight_overlay_dma_run_t) == 4u * sizeof(uint32_t),
               "origin descriptors must match RP2350 AL3 word order");
_Static_assert(sizeof(tdma_origin_observation_t) == 6u * sizeof(uint32_t) &&
               offsetof(tdma_origin_plan_state_t, rtt_present) + sizeof(uint32_t) -
                   offsetof(tdma_origin_plan_state_t, observation_sequence) ==
                       sizeof(tdma_origin_observation_t),
               "Per-bank observation copy must cover the complete guarded record");

enum {
    L_SEED, L_BOUNDARY, L_RTT_PRESENT, L_RTT_DONE, L_POLL_CAPTURE, L_POLL_OUTPUT,
    L_POLL_CAPTURE_ABORT, L_POLL_OUTPUT_ABORT, L_POLL_DECREMENT,
    L_PUBLISH_SELECT, L_PUBLISH_A, L_PUBLISH_B, L_CHECK_COUNT, L_PACK_SELECT,
    L_PACK_A, L_PACK_B, L_IDENTITY, L_MATCH, L_TRANSPORT, L_ROUTE,
    L_MAILBOX, L_MAILBOX_NEXT_0, L_MAILBOX_NEXT_7 = L_MAILBOX_NEXT_0 + 7,
    L_ACCEPT_SELECT, L_ACCEPT_A, L_ACCEPT_B, L_PREPARE_SELECT,
    L_PREPARE_A, L_PREPARE_B, L_HEADER, L_LOCAL_A, L_LOCAL_B, L_STAGE,
    L_ARM_A, L_ARM_B, L_ARM_OUTPUT, L_FAULT, L_COUNT
};

_Static_assert(L_COUNT <= TDMA_ORIGIN_BUILD_LABEL_CAPACITY, "Builder label storage");
typedef tdma_origin_plan_builder_t builder_t;

static uint32_t dma_reg(uint32_t channel, uint32_t offset)
{
    return DMA_BASE + channel * 0x40u + offset;
}

static uint32_t sm_reg(uint32_t pio, uint32_t sm, uint32_t offset)
{
    return pio + offset + sm * 0x18u;
}

static uint32_t state_member(const builder_t *b, size_t offset)
{
    return b->c->address.state + (uint32_t)offset;
}

#define STATE(member) state_member(b, offsetof(tdma_origin_plan_state_t, member))

static uint32_t control(uint32_t size, uint32_t dreq, uint32_t flags, uint32_t chain)
{
    return BIT(EN) | BIT(IRQ_QUIET) | FIELD(DATA_SIZE, size == 4u ? 2u : size == 2u ? 1u : 0u) |
           FIELD(TREQ_SEL, dreq) | FIELD(CHAIN_TO, chain) | flags;
}

static bool interval(uint32_t address, uint32_t size, uint32_t alignment)
{
    return address >= SRAM_BASE && address % alignment == 0u && size != 0u &&
           address < SRAM_END && size <= SRAM_END - address;
}

static bool host_disjoint(const void *a, size_t asize, const void *b, size_t bsize)
{
    const uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;
    return asize <= UINTPTR_MAX - first && bsize <= UINTPTR_MAX - second &&
           (first + asize <= second || second + bsize <= first);
}

static bool config_valid(const tdma_origin_plan_config_t *c, const tdma_origin_plan_t *p)
{
    if (c == NULL || p == NULL || p->runs == NULL || p->literals == NULL ||
        p->run_capacity == 0u || p->run_capacity > TDMA_ORIGIN_PLAN_RUN_MAX ||
        p->literal_capacity == 0u || p->literal_capacity > TDMA_ORIGIN_PLAN_LITERAL_MAX ||
        c->physical_bytes <= c->outer_header_bytes + TDMA_TRANSPORT_SHORT_PACKET_MAX ||
        c->physical_bytes > TDMA_TRANSPORT_SHORT_PACKET_MAX + 64u ||
        c->outer_header_bytes != 4u || c->capture_prefix_bits == 0u ||
        c->capture_prefix_bits > (c->physical_bytes - TDMA_TRANSPORT_SHORT_PACKET_MAX) * 8u ||
        c->guard_count == 0u || c->guard_count > 65536u ||
        c->abort_poll_count == 0u || c->abort_poll_count > UINT16_MAX ||
        c->local_slot >= TDMA_FLIGHT_SHORT_SLOT_COUNT || c->active_slot_mask > UINT8_MAX ||
        (c->active_slot_mask & (1u << c->local_slot)) == 0u ||
        c->control_pio < 1u || c->control_pio > 2u ||
        c->data_pio < 1u || c->data_pio > 2u || c->control_pio == c->data_pio ||
        c->control_sm > 3u || c->capture_sm > 3u || c->control_sm == c->capture_sm ||
        c->rtt_sm > 3u || c->rtt_sm == c->control_sm || c->rtt_sm == c->capture_sm ||
        c->data_sm > 3u || c->helper_sm > 3u || c->data_sm == c->helper_sm ||
        c->control_pc != TDMA_ORIGIN_CONTROL_PC || c->capture_pc != TDMA_ORIGIN_CAPTURE_PC ||
        c->data_pc != TDMA_ORIGIN_DATA_PC || c->helper_pc != TDMA_ORIGIN_HELPER_PC ||
        c->compare_pc != TDMA_ORIGIN_COMPARE_PC || c->rtt_pc != TDMA_ORIGIN_RTT_PC) return false;
    const size_t run_bytes = p->run_capacity * sizeof(p->runs[0]);
    const size_t literal_bytes = p->literal_capacity * sizeof(p->literals[0]);
    if ((uintptr_t)p->runs % _Alignof(tdma_flight_overlay_dma_run_t) != 0u ||
        (uintptr_t)p->literals % _Alignof(uint32_t) != 0u ||
        !host_disjoint(p->runs, run_bytes, p->literals, literal_bytes) ||
        !host_disjoint(p->runs, run_bytes, c, sizeof(*c)) ||
        !host_disjoint(p->literals, literal_bytes, c, sizeof(*c)) ||
        !host_disjoint(p->runs, run_bytes, p, sizeof(*p)) ||
        !host_disjoint(p->literals, literal_bytes, p, sizeof(*p))) return false;
    const uint8_t channels[] = {c->capture_dma, c->output_dma, c->loader_dma, c->executor_dma};
    for (size_t i = 0u; i < sizeof(channels); ++i) {
        if (channels[i] >= 16u || channels[i] == 7u) return false;
        for (size_t j = 0u; j < i; ++j) if (channels[i] == channels[j]) return false;
    }
    const uint32_t route = c->returned_route_word;
    const uint32_t hops = route >> 24u;
    uint32_t active_count = 0u;
    for (uint32_t bits = c->active_slot_mask; bits != 0u; bits >>= 1u) active_count += bits & 1u;
    if ((route & 0xffu) == 0u || ((route >> 8u) & 0xffu) !=
            (TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK | TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) ||
        hops == 0u || hops > TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT ||
        ((route >> 16u) & 0xffu) != hops || active_count != hops + 1u) return false;
    const uint32_t starts[] = {c->address.capture_bank[0], c->address.capture_bank[1],
        c->address.stage, c->address.tx_header, c->address.rx_packet, c->address.state,
        c->address.local_shadow[0], c->address.local_shadow[1], c->address.scratch,
        c->address.runs, c->address.literals};
    const uint32_t sizes[] = {TDMA_TRANSPORT_SHORT_PACKET_MAX, TDMA_TRANSPORT_SHORT_PACKET_MAX,
        c->physical_bytes * 2u, TDMA_TRANSPORT_FRAME_HEADER_SIZE, TDMA_TRANSPORT_SHORT_PACKET_MAX,
        sizeof(tdma_origin_plan_state_t), TDMA_ORIGIN_PLAN_SHADOW_BYTES, TDMA_ORIGIN_PLAN_SHADOW_BYTES,
        8u, p->run_capacity * sizeof(p->runs[0]), p->literal_capacity * sizeof(uint32_t)};
    for (size_t i = 0u; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        if (!interval(starts[i], sizes[i], i == 9u ? 16u : 4u)) return false;
        for (size_t j = 0u; j < i; ++j)
            if (starts[i] < starts[j] + sizes[j] && starts[j] < starts[i] + sizes[i]) return false;
    }
    return true;
}

static uint32_t block(builder_t *b, const uint32_t *values, uint32_t count)
{
    if (b->failed || count > b->p->literal_capacity - b->p->literal_count) {
        b->failed = true;
        return 0u;
    }
    const uint32_t offset = b->p->literal_count;
    memcpy(&b->p->literals[offset], values, count * sizeof(uint32_t));
    memset(&b->immutable[offset], 0, count);
    b->p->literal_count += count;
    return b->c->address.literals + offset * sizeof(uint32_t);
}

static uint32_t literal(builder_t *b, uint32_t value)
{
    for (uint32_t i = 0u; i < b->p->literal_count; ++i)
        if (b->immutable[i] != 0u && b->p->literals[i] == value)
            return b->c->address.literals + i * sizeof(uint32_t);
    const uint32_t index = b->p->literal_count;
    const uint32_t address = block(b, &value, 1u);
    if (!b->failed) b->immutable[index] = 1u;
    return address;
}

static void mark(builder_t *b, uint32_t label)
{
    const uint32_t address = b->c->address.runs + b->p->run_count * sizeof(b->p->runs[0]);
    if (b->emitting && b->label[label] != address) b->failed = true;
    b->label[label] = address;
}

static void copy(builder_t *b, uint32_t source, uint32_t destination, uint32_t count,
                 uint32_t size, uint32_t dreq, uint32_t flags, uint32_t chain)
{
    if (b->failed || b->p->run_count == b->p->run_capacity || count == 0u ||
        count > b->c->physical_bytes || source % size != 0u || destination % size != 0u) {
        b->failed = true;
        return;
    }
    b->p->runs[b->p->run_count++] = (tdma_flight_overlay_dma_run_t){
        control(size, dreq, flags, chain), destination, count, source};
}

static void move(builder_t *b, uint32_t source, uint32_t destination)
{
    copy(b, source, destination, 1u, 4u, 63u, 0u, b->c->loader_dma);
}

static void put(builder_t *b, uint32_t value, uint32_t destination)
{
    move(b, literal(b, value), destination);
}

static void fifo_put(builder_t *b, uint32_t value, uint32_t fifo, uint32_t dreq)
{
    copy(b, literal(b, value), fifo, 1u, 4u, dreq, 0u, b->c->loader_dma);
}

static void jump(builder_t *b, uint32_t label)
{
    const uint32_t value = b->label[label];
    copy(b, block(b, &value, 1u), dma_reg(b->c->loader_dma, DMA_CH0_AL3_READ_ADDR_TRIG_OFFSET),
         1u, 4u, 63u, 0u, b->c->executor_dma);
}

static void compare(builder_t *b, uint32_t actual, uint32_t expected, uint32_t equal, uint32_t different)
{
    fifo_put(b, b->c->compare_pc, b->help_tx, b->help_tx_q);
    copy(b, actual, b->help_tx, 1u, 4u, b->help_tx_q, 0u, b->c->loader_dma);
    copy(b, expected, b->help_tx, 1u, 4u, b->help_tx_q, 0u, b->c->loader_dma);
    const uint32_t paths[] = {b->label[equal], b->label[different]};
    copy(b, block(b, paths, 2u), b->help_tx, 2u, 4u, b->help_tx_q, READ, b->c->loader_dma);
    copy(b, b->help_rx, dma_reg(b->c->loader_dma, DMA_CH0_AL3_READ_ADDR_TRIG_OFFSET),
         1u, 4u, b->help_q, 0u, b->c->executor_dma);
}

static void test_bit(builder_t *b, uint32_t address, uint32_t bit, uint32_t clear, uint32_t set)
{
    /* RP2350 PIO ISA: OUT NULL,n = 0x6060|n; NOP = MOV Y,Y = 0xa042.
     * Only validated channel [0,15] or FSTAT RXEMPTY bits reach this helper.
     * OUT count zero means 32, so bit zero must use NOP. Checked by pioasm
     * execution tests. This literal never comes from a frame or shadow. */
    const uint32_t shift = bit == 0u ? 0xa042u : 0x6060u | bit;
    const uint32_t job[] = {TDMA_ORIGIN_TEST_BIT_PC, shift};
    copy(b, block(b, job, 2u), b->help_tx, 2u, 4u, b->help_tx_q, READ, b->c->loader_dma);
    copy(b, address, b->help_tx, 1u, 4u,
         b->help_tx_q, 0u, b->c->loader_dma);
    const uint32_t paths[] = {0u, b->label[clear], b->label[set]};
    copy(b, block(b, paths, 3u), b->help_tx, 3u, 4u, b->help_tx_q, READ, b->c->loader_dma);
    copy(b, b->help_rx, dma_reg(b->c->loader_dma, DMA_CH0_AL3_READ_ADDR_TRIG_OFFSET),
         1u, 4u, b->help_q, 0u, b->c->executor_dma);
}

static void add(builder_t *b, uint32_t target, uint32_t value)
{
    put(b, b->sum, DMA_BASE + DMA_SNIFF_CTRL_OFFSET);
    move(b, target, DMA_BASE + DMA_SNIFF_DATA_OFFSET);
    copy(b, literal(b, value), b->c->address.scratch, 1u, 4u, 63u, SNIFF, b->c->loader_dma);
    move(b, DMA_BASE + DMA_SNIFF_DATA_OFFSET, target);
    put(b, 0u, DMA_BASE + DMA_SNIFF_CTRL_OFFSET);
}

static void crc(builder_t *b, uint32_t header, bool identity, uint32_t target)
{
    put(b, b->crc, DMA_BASE + DMA_SNIFF_CTRL_OFFSET);
    put(b, UINT32_MAX, DMA_BASE + DMA_SNIFF_DATA_OFFSET);
    copy(b, header, b->c->address.scratch, identity ? 14u : 28u, 1u, 63u, READ | SNIFF, b->c->loader_dma);
    copy(b, identity ? header + 15u : literal(b, 0u), b->c->address.scratch,
         identity ? 9u : 4u, 1u, 63u, (identity ? READ : 0u) | SNIFF, b->c->loader_dma);
    move(b, DMA_BASE + DMA_SNIFF_DATA_OFFSET, target);
    put(b, 0u, DMA_BASE + DMA_SNIFF_CTRL_OFFSET);
}

static void emit(builder_t *b)
{
    const tdma_origin_plan_config_t *c = b->c;
    const tdma_origin_plan_addresses_t *a = &c->address;
    const uint32_t loader = c->loader_dma;
    switch (b->step) {
    case L_SEED:
    mark(b, L_SEED);
    jump(b, L_PREPARE_SELECT);
    break;
    case L_BOUNDARY:
    mark(b, L_BOUNDARY);
    copy(b, b->ctrl_rx, STATE(boundary_token), 1u, 4u, b->ctrl_q, 0u, loader);
    add(b, STATE(observation_version), 1u);
    move(b, a->tx_header + 8u, STATE(last_local_sequence));
    move(b, a->tx_header + 8u, STATE(observation_sequence));
    move(b, a->tx_header + 24u, STATE(observation_identity));
    move(b, STATE(local_selected_generation), STATE(observation_local_generation));
    put(b, (1u << c->capture_sm) | (1u << c->rtt_sm), b->tx_pio + 0x3000u);
    put(b, 1u << c->data_sm, b->rx_pio + 0x3000u);
    move(b, dma_reg(c->capture_dma, DMA_CH0_TRANS_COUNT_OFFSET), STATE(remaining_snapshot));
    move(b, dma_reg(c->output_dma, DMA_CH0_TRANS_COUNT_OFFSET), STATE(output_remaining_snapshot));
    put(b, BIT(EN), dma_reg(c->capture_dma, DMA_CH0_CTRL_TRIG_OFFSET) + 0x3000u);
    put(b, BIT(EN), dma_reg(c->output_dma, DMA_CH0_CTRL_TRIG_OFFSET) + 0x3000u);
    put(b, (1u << c->capture_dma) | (1u << c->output_dma), DMA_BASE + DMA_CHAN_ABORT_OFFSET);
    put(b, 0u, STATE(rtt_present));
    put(b, 0u, STATE(rtt_remaining));
    test_bit(b, b->tx_pio + PIO_FSTAT_OFFSET, PIO_FSTAT_RXEMPTY_LSB + c->rtt_sm,
             L_RTT_PRESENT, L_RTT_DONE);
    break;
    case L_RTT_PRESENT:
    mark(b, L_RTT_PRESENT);
    /* Producer is paused and executor is the only RX FIFO reader. FSTAT
     * proves this DREQ read cannot block on a missing returned CS edge. */
    copy(b, b->tx_pio + PIO_RXF0_OFFSET + c->rtt_sm * 4u, STATE(rtt_remaining),
         1u, 4u, b->ctrl_q - c->control_sm + c->rtt_sm, 0u, loader);
    put(b, 1u, STATE(rtt_present));
    break;
    case L_RTT_DONE:
    mark(b, L_RTT_DONE);
    put(b, c->abort_poll_count, STATE(polls_left));
    break;
    case L_POLL_CAPTURE:
    mark(b, L_POLL_CAPTURE);
    compare(b, dma_reg(c->capture_dma, DMA_CH0_CTRL_TRIG_OFFSET), literal(b, b->cap_ctrl & ~BIT(EN)), L_POLL_OUTPUT, L_POLL_DECREMENT);
    break;
    case L_POLL_OUTPUT:
    mark(b, L_POLL_OUTPUT);
    compare(b, dma_reg(c->output_dma, DMA_CH0_CTRL_TRIG_OFFSET), literal(b, b->out_ctrl & ~BIT(EN)), L_POLL_CAPTURE_ABORT, L_POLL_DECREMENT);
    break;
    case L_POLL_CAPTURE_ABORT:
    /* BUSY and ABORT may settle at different times. Only these owned bits
     * participate; an unrelated channel must not delay this boundary. */
    mark(b, L_POLL_CAPTURE_ABORT);
    test_bit(b, DMA_BASE + DMA_CHAN_ABORT_OFFSET, c->capture_dma, L_POLL_OUTPUT_ABORT, L_POLL_DECREMENT);
    break;
    case L_POLL_OUTPUT_ABORT:
    mark(b, L_POLL_OUTPUT_ABORT);
    test_bit(b, DMA_BASE + DMA_CHAN_ABORT_OFFSET, c->output_dma, L_PUBLISH_SELECT, L_POLL_DECREMENT);
    break;
    case L_POLL_DECREMENT:
    mark(b, L_POLL_DECREMENT);
    add(b, STATE(polls_left), UINT32_MAX);
    compare(b, STATE(polls_left), literal(b, 0u), L_FAULT, L_POLL_CAPTURE);
    break;
    case L_PUBLISH_SELECT:
    mark(b, L_PUBLISH_SELECT);
    /* A stable record is published only after both children have settled
     * without CTRL errors. A failed abort leaves it odd/unavailable. */
    add(b, STATE(observation_version), 1u);
    compare(b, STATE(capture_bank), literal(b, 0u), L_PUBLISH_A, L_PUBLISH_B);
    break;
    case L_PUBLISH_A:
    case L_PUBLISH_B: {
        const uint32_t bank = b->step - L_PUBLISH_A;
        mark(b, L_PUBLISH_A + bank);
        add(b, STATE(bank_version) + bank * 4u, 1u);
        jump(b, L_CHECK_COUNT);
    }
    break;
    case L_CHECK_COUNT:
    mark(b, L_CHECK_COUNT);
    compare(b, STATE(remaining_snapshot), literal(b, 0u), L_PACK_SELECT, L_PREPARE_SELECT);
    break;
    case L_PACK_SELECT:
    mark(b, L_PACK_SELECT);
    compare(b, STATE(capture_bank), literal(b, 0u), L_PACK_A, L_PACK_B);
    break;
    case L_PACK_A:
    case L_PACK_B: {
        const uint32_t bank = b->step - L_PACK_A;
        mark(b, L_PACK_A + bank);
        copy(b, a->capture_bank[bank], a->rx_packet, TDMA_TRANSPORT_SHORT_PACKET_MAX, 1u, 63u, READ | WRITE, loader);
        jump(b, L_IDENTITY);
    }
    break;
    case L_IDENTITY:
    mark(b, L_IDENTITY);
    crc(b, a->rx_packet, true, a->scratch + 4u);
    compare(b, a->scratch + 4u, a->rx_packet + 24u, L_MATCH, L_PREPARE_SELECT);
    break;
    case L_MATCH:
    mark(b, L_MATCH);
    compare(b, a->rx_packet + 24u, a->tx_header + 24u, L_TRANSPORT, L_PREPARE_SELECT);
    break;
    case L_TRANSPORT:
    mark(b, L_TRANSPORT);
    crc(b, a->rx_packet, false, a->scratch + 4u);
    compare(b, a->scratch + 4u, a->rx_packet + 28u, L_ROUTE, L_PREPARE_SELECT);
    break;
    case L_ROUTE:
    mark(b, L_ROUTE);
    compare(b, a->rx_packet + 12u, literal(b, c->returned_route_word), L_MAILBOX, L_PREPARE_SELECT);
    break;
    case L_MAILBOX:
    mark(b, L_MAILBOX);
    break;
    case L_MAILBOX_NEXT_0: case L_MAILBOX_NEXT_0 + 1:
    case L_MAILBOX_NEXT_0 + 2: case L_MAILBOX_NEXT_0 + 3:
    case L_MAILBOX_NEXT_0 + 4: case L_MAILBOX_NEXT_0 + 5:
    case L_MAILBOX_NEXT_0 + 6: case L_MAILBOX_NEXT_7: {
        const uint32_t slot = b->step - L_MAILBOX_NEXT_0;
        if ((c->active_slot_mask & (1u << slot)) == 0u) break;
        const uint32_t mailbox = a->rx_packet + TDMA_TRANSPORT_FRAME_HEADER_SIZE + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE;
        put(b, b->crc16, DMA_BASE + DMA_SNIFF_CTRL_OFFSET);
        put(b, UINT16_MAX, DMA_BASE + DMA_SNIFF_DATA_OFFSET);
        copy(b, mailbox, a->scratch, TDMA_FLIGHT_SHORT_SLOT_SIZE - 2u, 1u, 63u, READ | SNIFF, loader);
        copy(b, mailbox + TDMA_FLIGHT_SHORT_SLOT_SIZE - 2u, a->scratch, 1u, 2u, 63u, SWAP | SNIFF, loader);
        move(b, DMA_BASE + DMA_SNIFF_DATA_OFFSET, a->scratch + 4u);
        put(b, 0u, DMA_BASE + DMA_SNIFF_CTRL_OFFSET);
        compare(b, a->scratch + 4u, literal(b, 0u), L_MAILBOX_NEXT_0 + slot, L_PREPARE_SELECT);
        mark(b, L_MAILBOX_NEXT_0 + slot);
    }
    break;
    case L_ACCEPT_SELECT:
    mark(b, L_ACCEPT_SELECT);
    move(b, a->rx_packet + 8u, STATE(last_return_sequence));
    compare(b, STATE(capture_bank), literal(b, 0u), L_ACCEPT_A, L_ACCEPT_B);
    break;
    case L_ACCEPT_A:
    case L_ACCEPT_B: {
        const uint32_t bank = b->step - L_ACCEPT_A;
        mark(b, L_ACCEPT_A + bank);
        copy(b, STATE(observation_sequence), STATE(bank_observation) + bank * sizeof(tdma_origin_observation_t),
             sizeof(tdma_origin_observation_t) / sizeof(uint32_t), 4u, 63u, READ | WRITE, loader);
        put(b, bank, STATE(good_bank));
        put(b, bank ^ 1u, STATE(capture_bank));
        jump(b, L_PREPARE_A + bank);
    }
    break;
    case L_PREPARE_SELECT:
    mark(b, L_PREPARE_SELECT);
    compare(b, STATE(good_bank), literal(b, 0u), L_PREPARE_A, L_PREPARE_B);
    break;
    case L_PREPARE_A:
    case L_PREPARE_B: {
        const uint32_t bank = b->step - L_PREPARE_A;
        mark(b, L_PREPARE_A + bank);
        copy(b, a->capture_bank[bank], a->stage + c->outer_header_bytes * 2u + 1u,
             TDMA_TRANSPORT_SHORT_PACKET_MAX, 1u, 63u, READ | SKIP_WRITE, loader);
        jump(b, L_HEADER);
    }
    break;
    case L_HEADER:
    mark(b, L_HEADER);
    add(b, a->tx_header + 8u, 1u);
    crc(b, a->tx_header, true, a->tx_header + 24u);
    crc(b, a->tx_header, false, a->tx_header + 28u);
    copy(b, STATE(local_next_address), dma_reg(loader, DMA_CH0_AL3_READ_ADDR_TRIG_OFFSET),
         1u, 4u, 63u, 0u, c->executor_dma);
    break;
    case L_LOCAL_A:
    case L_LOCAL_B: {
        const uint32_t bank = b->step - L_LOCAL_A;
        mark(b, L_LOCAL_A + bank);
        copy(b, a->local_shadow[bank], a->stage + 2u * (c->outer_header_bytes +
            TDMA_TRANSPORT_FRAME_HEADER_SIZE + c->local_slot * TDMA_FLIGHT_SHORT_SLOT_SIZE) + 1u,
            TDMA_FLIGHT_SHORT_SLOT_SIZE, 1u, 63u, READ | SKIP_WRITE, loader);
        move(b, a->local_shadow[bank] + TDMA_FLIGHT_SHORT_SLOT_SIZE, STATE(local_selected_generation));
        jump(b, L_STAGE);
    }
    break;
    case L_STAGE:
    mark(b, L_STAGE);
    copy(b, a->tx_header, a->stage + c->outer_header_bytes * 2u + 1u,
         TDMA_TRANSPORT_FRAME_HEADER_SIZE, 1u, 63u, READ | SKIP_WRITE, loader);
    copy(b, literal(b, 0u), a->stage + (c->outer_header_bytes + TDMA_TRANSPORT_FRAME_HEADER_SIZE +
        TDMA_FLIGHT_NODE_IMAGE_SIZE) * 2u, TDMA_FLIGHT_DPLL_OBSERVATION_SIZE, 2u, 63u, WRITE, loader);
    copy(b, literal(b, 0u), a->stage + (c->outer_header_bytes + TDMA_TRANSPORT_SHORT_PACKET_MAX) * 2u,
        c->physical_bytes - c->outer_header_bytes - TDMA_TRANSPORT_SHORT_PACKET_MAX, 2u, 63u, WRITE, loader);
    /* Discard duplicate/late RTT words and restart at the admitted entry.
     * Never let an old FIFO word acquire the next frame's sequence tag. */
    put(b, PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS, sm_reg(b->tx_pio, c->rtt_sm, PIO_SM0_SHIFTCTRL_OFFSET));
    put(b, 0u, sm_reg(b->tx_pio, c->rtt_sm, PIO_SM0_SHIFTCTRL_OFFSET));
    put(b, 1u << (4u + c->rtt_sm), b->tx_pio + 0x2000u);
    put(b, c->rtt_pc, sm_reg(b->tx_pio, c->rtt_sm, PIO_SM0_INSTR_OFFSET));
    for (uint32_t side = 0u; side < 2u; ++side) {
        const uint32_t pio = side == 0u ? b->tx_pio : b->rx_pio;
        const uint32_t sm = side == 0u ? c->capture_sm : c->data_sm;
        const uint32_t shift = side == 0u ? PIO_SM0_SHIFTCTRL_AUTOPUSH_BITS | (8u << PIO_SM0_SHIFTCTRL_PUSH_THRESH_LSB) : 0u;
        put(b, shift | PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS, sm_reg(pio, sm, PIO_SM0_SHIFTCTRL_OFFSET));
        put(b, shift, sm_reg(pio, sm, PIO_SM0_SHIFTCTRL_OFFSET));
        put(b, 1u << (4u + sm), pio + 0x2000u);
        put(b, side == 0u ? c->capture_pc : c->data_pc, sm_reg(pio, sm, PIO_SM0_INSTR_OFFSET));
    }
    fifo_put(b, c->capture_prefix_bits - 1u, b->cap_tx, b->cap_tx_q);
    fifo_put(b, c->physical_bytes - 1u, b->data_tx, b->data_q);
    compare(b, STATE(capture_bank), literal(b, 0u), L_ARM_A, L_ARM_B);
    break;
    case L_ARM_A:
    case L_ARM_B: {
        const uint32_t bank = b->step - L_ARM_A;
        mark(b, L_ARM_A + bank);
        add(b, STATE(bank_version) + bank * 4u, 1u);
        const uint32_t words[] = {b->cap_ctrl, a->capture_bank[bank], TDMA_TRANSPORT_SHORT_PACKET_MAX, b->cap_rx};
        copy(b, block(b, words, 4u), dma_reg(c->capture_dma, DMA_CH0_AL3_CTRL_OFFSET), 4u, 4u, 63u, READ | WRITE, loader);
        jump(b, L_ARM_OUTPUT);
    }
    break;
    case L_ARM_OUTPUT: {
    mark(b, L_ARM_OUTPUT);
    const uint32_t output[] = {b->out_ctrl, b->data_tx, c->physical_bytes, a->stage};
    copy(b, block(b, output, 4u), dma_reg(c->output_dma, DMA_CH0_AL3_CTRL_OFFSET), 4u, 4u, 63u, READ | WRITE, loader);
    put(b, (1u << c->capture_sm) | (1u << c->rtt_sm), b->tx_pio + 0x2000u);
    put(b, 1u << c->data_sm, b->rx_pio + 0x2000u);
    fifo_put(b, ((c->guard_count - 1u) << 16u) | (c->physical_bytes * 8u - 1u), b->ctrl_tx, b->ctrl_tx_q);
    jump(b, L_BOUNDARY);
    }
    break;
    case L_FAULT:
    mark(b, L_FAULT);
    put(b, 15u, b->tx_pio + 0x3000u);
    put(b, (1u << c->helper_sm) | (1u << c->data_sm), b->rx_pio + 0x3000u);
    put(b, 0u, DMA_BASE + DMA_SNIFF_CTRL_OFFSET);
    put(b, 2u, b->tx_pio + PIO_IRQ_FORCE_OFFSET);
    copy(b, literal(b, 1u), STATE(fault), 1u, 4u, 63u, 0u, c->executor_dma);
    break;
    default:
        b->failed = true;
        break;
    }
}

bool tdma_origin_cadence_calculate(uint32_t clk_sys_hz, uint32_t baud_hz,
                                 uint32_t physical_bytes, uint32_t period_ns,
                                 tdma_origin_cadence_t *cadence)
{
    if (cadence == NULL) return false;
    memset(cadence, 0, sizeof(*cadence));
    if (clk_sys_hz == 0u || baud_hz == 0u || period_ns == 0u ||
        physical_bytes == 0u || physical_bytes * (uint64_t)8u > 65536u)
        return false;
    const uint64_t divider = (uint64_t)clk_sys_hz * 256u /
        ((uint64_t)baud_hz * TDMA_ORIGIN_CONTROL_CYCLES_PER_BIT);
    /* Do not use the special zero encoding for divide-by-65536. */
    if (divider < 256u || divider > 0xffffffu) return false;
    const uint64_t requested_ticks =
        ((uint64_t)period_ns * clk_sys_hz + 999999999u) / 1000000000u;
    const uint64_t required_cycles =
        (requested_ticks * 256u + divider - 1u) / divider;
    const uint64_t fixed_cycles = (uint64_t)physical_bytes * 8u *
        TDMA_ORIGIN_CONTROL_CYCLES_PER_BIT + TDMA_ORIGIN_CONTROL_CYCLE_OVERHEAD;
    /* A non-positive guard cannot close the finite launch protocol. */
    if (required_cycles <= fixed_cycles) return false;
    const uint64_t guard = (required_cycles - fixed_cycles +
        TDMA_ORIGIN_CONTROL_GUARD_CYCLES - 1u) / TDMA_ORIGIN_CONTROL_GUARD_CYCLES;
    if (guard > 65536u) return false;
    const uint64_t scaled_ticks = (fixed_cycles + guard *
        TDMA_ORIGIN_CONTROL_GUARD_CYCLES) * divider;
    cadence->divider256 = (uint32_t)divider;
    cadence->guard_count = (uint32_t)guard;
    cadence->period_floor_ticks = scaled_ticks / 256u;
    cadence->period_ceiling_ticks = (scaled_ticks + 255u) / 256u;
    cadence->guard_floor_ticks = guard * TDMA_ORIGIN_CONTROL_GUARD_CYCLES *
        divider / 256u;
    return true;
}

static void clear_entries(tdma_origin_plan_t *p)
{
    p->run_count = p->literal_count = 0u;
    p->seed_entry = p->boundary_entry = p->fault_entry = 0u;
    memset(p->local_entry, 0, sizeof(p->local_entry));
}

bool tdma_origin_plan_begin(builder_t *b, const tdma_origin_plan_config_t *c,
                           tdma_origin_plan_t *p)
{
    if (b == NULL || p == NULL || c == NULL || b->active ||
        !host_disjoint(b, sizeof(*b), p, sizeof(*p)) ||
        !host_disjoint(b, sizeof(*b), c, sizeof(*c)) ||
        !host_disjoint(b, sizeof(*b), p->runs, p->run_capacity * sizeof(*p->runs)) ||
        !host_disjoint(b, sizeof(*b), p->literals, p->literal_capacity * sizeof(*p->literals))) return false;
    clear_entries(p);
    if (!config_valid(c, p)) return false;
    memset(b, 0, sizeof(*b));
    b->config = *c;
    b->c = &b->config;
    b->p = p;
    b->active = true;
    b->tx_pio = c->control_pio == 1u ? PIO1_BASE : PIO2_BASE;
    b->rx_pio = c->data_pio == 1u ? PIO1_BASE : PIO2_BASE;
    b->ctrl_rx = b->tx_pio + PIO_RXF0_OFFSET + c->control_sm * 4u;
    b->ctrl_tx = b->tx_pio + PIO_TXF0_OFFSET + c->control_sm * 4u;
    b->cap_rx = b->tx_pio + PIO_RXF0_OFFSET + c->capture_sm * 4u;
    b->cap_tx = b->tx_pio + PIO_TXF0_OFFSET + c->capture_sm * 4u;
    b->data_tx = b->rx_pio + PIO_TXF0_OFFSET + c->data_sm * 4u;
    b->help_rx = b->rx_pio + PIO_RXF0_OFFSET + c->helper_sm * 4u;
    b->help_tx = b->rx_pio + PIO_TXF0_OFFSET + c->helper_sm * 4u;
    const uint32_t tx_q = c->control_pio == 1u ? DREQ_PIO1_TX0 : DREQ_PIO2_TX0;
    const uint32_t rx_q = c->data_pio == 1u ? DREQ_PIO1_TX0 : DREQ_PIO2_TX0;
    b->ctrl_q = tx_q + 4u + c->control_sm;
    b->ctrl_tx_q = tx_q + c->control_sm;
    b->cap_q = tx_q + 4u + c->capture_sm;
    b->cap_tx_q = tx_q + c->capture_sm;
    b->data_q = rx_q + c->data_sm;
    b->help_q = rx_q + 4u + c->helper_sm;
    b->help_tx_q = rx_q + c->helper_sm;
    b->cap_ctrl = control(1u, b->cap_q, WRITE, c->capture_dma);
    b->out_ctrl = control(2u, b->data_q, READ, c->output_dma);
    const uint32_t sniff = DMA_SNIFF_CTRL_EN_BITS | ((uint32_t)c->executor_dma << DMA_SNIFF_CTRL_DMACH_LSB);
    b->sum = sniff | (DMA_SNIFF_CTRL_CALC_VALUE_SUM << DMA_SNIFF_CTRL_CALC_LSB);
    b->crc = sniff | (DMA_SNIFF_CTRL_CALC_VALUE_CRC32R << DMA_SNIFF_CTRL_CALC_LSB) |
             DMA_SNIFF_CTRL_OUT_INV_BITS | DMA_SNIFF_CTRL_OUT_REV_BITS;
    b->crc16 = sniff | (DMA_SNIFF_CTRL_CALC_VALUE_CRC16 << DMA_SNIFF_CTRL_CALC_LSB);
    return true;
}

void tdma_origin_plan_cancel(builder_t *b)
{
    if (b == NULL) return;
    if (b->p != NULL && b->active) clear_entries(b->p);
    b->active = b->complete = false;
    b->failed = true;
}

tdma_origin_build_result_t tdma_origin_plan_step(builder_t *b)
{
    if (b == NULL || !b->active) return TDMA_ORIGIN_BUILD_FAILED;
    if (b->complete) return TDMA_ORIGIN_BUILD_DONE;
    tdma_origin_plan_t *p = b->p;
    const uint32_t before = p->run_count;
    emit(b);
    if (p->run_count - before > TDMA_ORIGIN_BUILD_STEP_RUN_MAX) b->failed = true;
    if (b->failed) {
        tdma_origin_plan_cancel(b);
        return TDMA_ORIGIN_BUILD_FAILED;
    }
    if (++b->step != L_COUNT) return TDMA_ORIGIN_BUILD_BUSY;
    if (!b->emitting) {
        p->run_count = p->literal_count = 0u;
        b->emitting = true;
        b->step = 0u;
        return TDMA_ORIGIN_BUILD_BUSY;
    }
    b->complete = true;
    p->seed_entry = b->label[L_SEED];
    p->boundary_entry = b->label[L_BOUNDARY];
    p->fault_entry = b->label[L_FAULT];
    p->local_entry[0] = b->label[L_LOCAL_A];
    p->local_entry[1] = b->label[L_LOCAL_B];
    return TDMA_ORIGIN_BUILD_DONE;
}

bool tdma_origin_plan_build(const tdma_origin_plan_config_t *c, tdma_origin_plan_t *p)
{
    builder_t b = {0};
    if (p != NULL) clear_entries(p);
    if (!tdma_origin_plan_begin(&b, c, p)) return false;
    tdma_origin_build_result_t result;
    do { result = tdma_origin_plan_step(&b); } while (result == TDMA_ORIGIN_BUILD_BUSY);
    return result == TDMA_ORIGIN_BUILD_DONE;
}
