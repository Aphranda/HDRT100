"""Execute the production P3 FSM against bounded PIO/DMA owner doubles."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def _definition(text, first, last):
    start = text.index(first)
    return text[start:text.index(last, start) + len(last)]


def test_p3_intent_payload_is_ordered_before_guard_recheck():
    manager = (ROOT / "components/calibration_manager/src/calibration_manager.c").read_text(
        encoding="utf-8")
    reader = _definition(manager, "static bool calibration_manager_p3_read(",
                         "    return false;\n}")
    copy = reader.index("*intent = s_p3_intent;")
    fence = reader.index("__atomic_thread_fence(__ATOMIC_ACQUIRE);", copy)
    recheck = reader.index("__atomic_load_n(&s_p3_intent.guard, __ATOMIC_ACQUIRE)", copy)
    assert copy < fence < recheck


def test_sck_loopback_has_independent_pio_programs_and_offsets():
    pio = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(encoding="utf-8")
    for name in ("origin", "forward"):
        program = _definition(pio, f".program tdma_pio_spi_sck_loopback_{name}\n", ".wrap\n")
        assert "marker" not in program.lower()
        initializer = _definition(pio, f"static inline void tdma_pio_spi_sck_loopback_{name}_program_init(", "\n}")
        assert f"tdma_pio_spi_sck_loopback_{name}_program_get_default_config(offset)" in initializer
        assert "marker" not in initializer.lower()
    phy = (ROOT / "components/tdma/src/tdma_pio_spi_phys_p3.inc").read_text(encoding="utf-8")
    assert "sck ? &s_tdma_pio_spi_sck_loopback_origin_offset" in phy
    assert "sck ? &s_tdma_pio_spi_sck_loopback_forward_offset" in phy
    owner = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    manager = (ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c").read_text(encoding="utf-8")
    context = (ROOT / "components/tdma/inc/tdma_pio_spi_phys_programs.h").read_text(encoding="utf-8")
    for name in ("origin", "forward"):
        member = f"sck_loopback_{name}_offset"
        symbol = f"s_tdma_pio_spi_{member}"
        assert f"uint *{member};" in context
        assert f".{member} = &{symbol}," in owner
        assert f"#define {symbol} (*manager->{member})" in manager


def test_marker_and_sck_stop_callbacks_are_scoped():
    commands = (ROOT / "middleware/scpi_port/inc/scpi_calibration_commands.h").read_text(encoding="utf-8")
    for scope in ("marker", "sck"):
        spelling = "MARKer" if scope == "marker" else "SCK"
        assert (f'.pattern = "CALibration:{spelling}:LOOPback:STOP", '
                f'.callback = scpi_calibration_{scope}_loopback_stop') in commands


def test_mark_rxphase_catalog_protocol_and_owner_wiring():
    pio = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(encoding="utf-8")
    sink = _definition(pio, ".program tdma_pio_spi_mark_rxphase_sink\n", ".wrap\n")
    assert sink.splitlines()[1:5] == ["    wait 1 gpio 0", "    wait 0 gpio 0", "    wait 1 gpio 0", "    nop"]
    commands = (ROOT / "middleware/scpi_port/inc/scpi_calibration_commands.h").read_text(encoding="utf-8")
    for suffix, callback in ((":STARt", "start"), (":STOP", "stop"), ("?", "q"), (":DATA?", "data_q")):
        assert f'MARKer:RXPHASE{suffix}", .callback = scpi_calibration_mark_rxphase_{callback}' in commands
    manager = (ROOT / "components/calibration_manager/src/calibration_manager.c").read_text(encoding="utf-8")
    for section in ("static void calibration_manager_p3_publish", "if (tdma_runtime_owner_p3_start_core1"):
        start = manager.index(section)
        assert "TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE" in manager[start:start + 1300]
    program_manager = (ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c").read_text(encoding="utf-8")
    for name in ("origin", "sink"):
        assert f"&tdma_pio_spi_mark_rxphase_{name}_program" in program_manager
        assert f"s_tdma_pio_spi_mark_rxphase_{name}_offset" in program_manager


@pytest.fixture(scope="module")
def firmware_executable(tmp_path_factory):
    target = tmp_path_factory.mktemp("p3-firmware")
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(
        encoding="utf-8")
    definitions = _definition(
        header, "typedef enum {\n    TDMA_PIO_SPI_P3_IDLE",
        "} tdma_pio_spi_data_loop_snapshot_t;")
    definitions += _definition(
        header, "typedef enum {\n    TDMA_PIO_SPI_PROGRAM_PERSONA_NONE",
        "} tdma_pio_spi_program_persona_t;")
    fields = _definition(header, "    volatile uint32_t p3_guard;",
                         "    uint64_t data_loop_deadline;")
    harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", \
    __LINE__, #x); exit(1); } } while (0)
typedef unsigned uint;
'''
    harness += definitions
    harness += "\ntypedef struct {\n" + fields + r'''
    bool armed;
    uint tx_sm, rx_sm;
    struct { uint32_t program_persona; } snapshot;
    struct { uint32_t state; } marker;
} tdma_pio_spi_phys_t;
#define BOARD_TDMA_SPI_MASTER_SM 0u
#define BOARD_TDMA_SPI_SLAVE_SM 1u
#define BOARD_TDMA_SPI_CAPTURE_SM 2u
#define BOARD_TDMA_SPI_DOWNLINK_SCK_PIN 25u
#define BOARD_TDMA_SPI_DOWNLINK_CSN_PIN 26u
#define BOARD_TDMA_SPI_UPLINK_CSN_PIN 27u
#define BOARD_TDMA_SPI_UPLINK_SCK_PIN 28u
#define BOARD_TDMA_SPI_DOWNLINK_TX_PIN 29u
#define BOARD_UP_BISS_DE_PIN 30u
#define BOARD_DN_BISS_DE_PIN 31u
#define BOARD_TRIG_DE_PIN 32u
#define TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS 256u
#define TDMA_PIO_SPI_MARKER_BUFFER_WORDS 512u
#define TDMA_PIO_SPI_MARKER_FORWARD_DELAY_CYCLES 1u
#define PIO_FDEBUG_TXSTALL_LSB 24u
#define TDMA_PIO_SPI_MARKER_ARMED 1u
#define TDMA_PIO_SPI_MARKER_RUNNING 2u
#define clk_sys 0
#define DMA_SIZE_32 2
#define CALIBRATION_TRAINING_PHASE_MIN_OFFSET_SAMPLES (-10)
#define CALIBRATION_TRAINING_PHASE_MAX_OFFSET_SAMPLES 10
bool calibration_training_phase_delay_from_base_samples(uint32_t, int32_t, uint32_t, uint32_t *);
typedef struct { unsigned id, length; } pio_program_t;
static const pio_program_t tdma_pio_spi_tx_byte_program = {0, 8};
static const pio_program_t tdma_pio_spi_rx_byte_program = {1, 8};
static const pio_program_t tdma_pio_spi_p3_initiator_program = {2, 7};
static const pio_program_t tdma_pio_spi_p3_responder_program = {3, 10};
static const pio_program_t tdma_pio_spi_cal_loopback_capture_program = {4, 1};
static const pio_program_t tdma_pio_spi_p3_responder_capture_program = {5, 3};
static const pio_program_t tdma_pio_spi_marker_origin_program = {6, 2};
static const pio_program_t tdma_pio_spi_marker_forward_program = {7, 5};
static const pio_program_t tdma_pio_spi_sck_loopback_origin_program = {8, 2};
static const pio_program_t tdma_pio_spi_sck_loopback_forward_program = {9, 5};
static const pio_program_t tdma_pio_spi_sck_train_sink_program = {10, 5};
static const pio_program_t tdma_pio_spi_mark_rxphase_origin_program = {11, 2};
static const pio_program_t tdma_pio_spi_mark_rxphase_sink_program = {12, 5};
static const uint16_t tdma_pio_spi_mark_rxphase_origin_program_instructions[2];
static const uint16_t tdma_pio_spi_mark_rxphase_sink_program_instructions[5];
static const uint16_t tdma_pio_spi_sck_train_sink_program_instructions[5];
static const uint16_t tdma_pio_spi_sck_loopback_origin_program_instructions[2];
static const uint16_t tdma_pio_spi_sck_loopback_forward_program_instructions[5];
static const uint16_t tdma_pio_spi_marker_origin_program_instructions[2];
static const uint16_t tdma_pio_spi_marker_forward_program_instructions[5];
static const uint16_t tdma_pio_spi_p3_initiator_program_instructions[7];
static const uint16_t tdma_pio_spi_p3_responder_program_instructions[10];
static const uint16_t tdma_pio_spi_cal_loopback_capture_program_instructions[1];
static struct { uint32_t rxf[4], txf[4], fdebug, instr_mem[32]; } mock_pio;
#define BOARD_TDMA_SPI_PIO (&mock_pio)
static struct { struct { uint32_t transfer_count; } ch[2]; } dma_storage;
#define dma_hw (&dma_storage)
typedef unsigned dma_channel_config;
static unsigned loaded, enabled, fail_program = 99, used_words, configured;
static bool fail_dma, fail_tx_dma, fail_resources, drivers[33];
static int s_tdma_pio_spi_rx_dma_channel = -1;
static int s_tdma_pio_spi_tx_dma_channel = -1;
static bool tx_fifo_empty = true;
static uint64_t now_us;
static uint64_t time_us_64(void) { return now_us; }
static bool pio_sm_is_tx_fifo_empty(void *p, uint sm)
{ (void)p; (void)sm; return tx_fifo_empty; }
static tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
static uint s_tdma_pio_spi_tx_offset, s_tdma_pio_spi_rx_offset;
static uint s_tdma_pio_spi_p3_initiator_offset;
static uint s_tdma_pio_spi_p3_responder_offset;
static uint s_tdma_pio_spi_p3_capture_offset;
static uint s_tdma_pio_spi_p3_responder_capture_offset;
static uint s_tdma_pio_spi_sck_loopback_origin_offset;
static uint s_tdma_pio_spi_sck_loopback_forward_offset;
static uint s_tdma_pio_spi_rxphase_origin_offset, s_tdma_pio_spi_rxphase_sink_offset;
static uint s_tdma_pio_spi_mark_rxphase_origin_offset, s_tdma_pio_spi_mark_rxphase_sink_offset;
static uint pio_encode_nop(void) { return 0xa042; }
static uint pio_encode_delay(uint delay) { return delay << 8; }
static uint32_t s_tdma_pio_spi_cal_ring[TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS];
static uint32_t s_tdma_pio_spi_marker_loopback_capture[TDMA_PIO_SPI_MARKER_LOOPBACK_CAPTURE_MAX_WORDS];
static void *rx_dma_target;
static uint32_t s_tdma_pio_spi_marker_tx[TDMA_PIO_SPI_MARKER_BUFFER_WORDS];
static uint32_t clock_get_hz(unsigned c) { (void)c; return 250000000; }
static void gpio_put(uint pin, bool v) { drivers[pin] = v; }
static bool tdma_pio_spi_phys_leave_flight_for_maintenance(void *p)
{ (void)p; return true; }
static bool tdma_pio_spi_phys_ensure_sms_claimed(void) { return !fail_resources; }
static bool tdma_pio_spi_phys_cal_persona_switch_ready(void) { return !enabled; }
static bool pio_can_add_program(void *p, const pio_program_t *program)
{ (void)p; return program->id != fail_program && used_words + program->length <= 32; }
static uint pio_add_program(void *p, const pio_program_t *program)
{ (void)p; assert(!(loaded & (1u << program->id))); loaded |= 1u << program->id;
  used_words += program->length; return program->id; }
static void pio_remove_program(void *p, const pio_program_t *program, uint offset)
{ (void)p; assert(offset == program->id); assert(loaded & (1u << program->id));
  assert(!enabled); loaded &= ~(1u << program->id); used_words -= program->length; }
static void tdma_pio_spi_phys_cal_mark_persona(tdma_pio_spi_phys_t *p,
    tdma_pio_spi_program_persona_t persona)
{ s_tdma_pio_spi_program_persona = persona; p->snapshot.program_persona = persona; }
static void pio_sm_set_enabled(void *p, uint sm, bool on)
{ (void)p; if (on) enabled |= 1u << sm; else enabled &= ~(1u << sm); }
static void pio_sm_clear_fifos(void *p, uint sm) { (void)p; (void)sm; }
static void pio_sm_restart(void *p, uint sm) { (void)p; (void)sm; }
static void pio_interrupt_clear(void *p, uint irq) { (void)p; (void)irq; }
static void pio_sm_put(void *p, uint sm, uint32_t word)
{ (void)p; (void)word; assert(configured & (1u << sm)); }
static void pio_enable_sm_mask_in_sync(void *p, uint mask)
{ (void)p; assert((configured & mask) == mask); enabled |= mask; }
static void tdma_pio_spi_p3_initiator_program_init(void *p, uint sm, uint off,
    uint sync, uint forward, uint32_t baud)
{ (void)p; (void)baud; assert(off == 2); assert(sync != forward); configured |= 1u << sm; }
static void tdma_pio_spi_p3_responder_program_init(void *p, uint sm, uint off,
    uint sync, uint forward, uint data, uint32_t baud)
{ (void)p; (void)baud; assert(off == 3); assert(sync != forward); assert(data == 29);
  configured |= 1u << sm; }
static void tdma_pio_spi_cal_loopback_capture_program_init(void *p, uint sm,
    uint off, uint hz)
{ (void)p; assert(off == 4); assert(hz == 250000000); configured |= 1u << sm; }
static void tdma_pio_spi_p3_responder_capture_program_init(void *p, uint sm,
    uint off, uint sync)
{ (void)p; (void)sync; assert(off == 5); configured |= 1u << sm; }
static void tdma_pio_spi_marker_origin_program_init(void *p, uint sm, uint off, uint pin)
{ (void)p; assert(off == 6 && pin == 26 && sm == 0); configured |= 1u << sm; }
static void tdma_pio_spi_marker_forward_program_init(void *p, uint sm, uint off,
    uint rx, uint tx, uint phase)
{ (void)p; assert(off == 7 && rx == 27 && tx == 29 && sm == 1 && phase == 1);
  configured |= 1u << sm; }
static void tdma_pio_spi_sck_loopback_origin_program_init(void *p, uint sm, uint off, uint pin)
{ (void)p; assert(off == 8 && (pin == 25 || pin == 29) && sm == 0); configured |= 1u << sm; }
static void tdma_pio_spi_sck_loopback_forward_program_init(void *p, uint sm, uint off, uint rx, uint tx)
{ (void)p; assert(off == 9 && rx == 28 && tx == 29 && sm == 1); configured |= 1u << sm; }
static uint32_t tdma_pio_spi_sck_train_sink_program_init(void *p, uint sm, uint off, uint pin, uint phase)
{
    (void)p; assert(off == 10 && sm == 1 && (pin == 28 || pin == 24) && phase >= 1 && phase <= 32);
    /* RP2350 INSTR_MEM reads zero: never emulate readable instruction RAM. */
    mock_pio.instr_mem[off + 3] = 0;
    configured |= 1u << sm;
    return pio_encode_nop() | pio_encode_delay(phase - 1);
}
static void tdma_pio_spi_mark_rxphase_origin_program_init(void *p, uint sm, uint off, uint pin)
{ (void)p; assert(off == 11 && pin == 26 && sm == 0); configured |= 1u << sm; }
static uint32_t tdma_pio_spi_mark_rxphase_sink_program_init(void *p, uint sm, uint off, uint pin, uint phase)
{ (void)p; assert(off == 12 && pin == 27 && sm == 1 && phase >= 1 && phase <= 32);
  configured |= 1u << sm; return pio_encode_nop() | pio_encode_delay(phase - 1); }
static uint32_t tdma_pio_spi_p3_data_high_cycles(uint32_t baud)
{ return 250000000u / baud / 2; }
static bool tdma_pio_spi_phys_ensure_rx_dma(void)
{ if (fail_dma) return false; s_tdma_pio_spi_rx_dma_channel = 0; return true; }
static bool tdma_pio_spi_phys_ensure_tx_dma(void)
{ if (fail_dma || fail_tx_dma) return false; s_tdma_pio_spi_tx_dma_channel = 1; return true; }
static bool dma_channel_is_busy(uint ch)
{ return dma_hw->ch[ch].transfer_count != 0; }
static void dma_channel_abort(uint ch) { dma_hw->ch[ch].transfer_count = 0; }
static dma_channel_config dma_channel_get_default_config(uint ch) { return ch; }
#define channel_config_set_transfer_data_size(c,v) ((void)(c), (void)(v))
#define channel_config_set_read_increment(c,v) ((void)(c), (void)(v))
#define channel_config_set_write_increment(c,v) ((void)(c), (void)(v))
#define channel_config_set_dreq(c,v) ((void)(c), (void)(v))
static uint pio_get_dreq(void *p, uint sm, bool tx) { (void)p; (void)tx; return sm; }
static void dma_channel_configure(uint ch, dma_channel_config *dc, void *target,
    void *source, uint count, bool start)
{ (void)dc; (void)target; (void)source;
  if (ch == 0) rx_dma_target = target;
  if (start) { assert(ch == 1); tx_fifo_empty = false; }
  dma_hw->ch[ch].transfer_count = count; }
static void dma_start_channel_mask(uint mask) { assert(mask == 1); }
static void tdma_pio_spi_phys_cal_cleanup(tdma_pio_spi_phys_t *p)
{ assert(!enabled); drivers[30] = drivers[31] = drivers[32] = false; p->armed = false; }
static tdma_pio_spi_phys_t *concurrent_reader_phys;
static uint32_t concurrent_reads;
bool tdma_pio_spi_phys_get_p3_snapshot(const tdma_pio_spi_phys_t *, tdma_pio_spi_p3_snapshot_t *);
bool tdma_pio_spi_phys_get_marker_loopback_snapshot(const tdma_pio_spi_phys_t *, tdma_pio_spi_marker_loopback_snapshot_t *);
bool tdma_pio_spi_phys_get_sck_loopback_snapshot(const tdma_pio_spi_phys_t *, tdma_pio_spi_marker_loopback_snapshot_t *);
static uint32_t tdma_pio_spi_cal_sample_byte(uint32_t word, uint32_t i)
{
    /* Deterministically interleave a Core0 status read inside every Core1
     * sample iteration, including the first/last beat of both endpoints. */
    assert(concurrent_reader_phys != NULL);
    tdma_pio_spi_p3_snapshot_t raw;
    assert(tdma_pio_spi_phys_get_p3_snapshot(concurrent_reader_phys, &raw));
    assert(raw.state == TDMA_PIO_SPI_P3_ARMED);
    tdma_pio_spi_marker_loopback_snapshot_t snapshot;
    if (raw.role == TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK)
        assert(tdma_pio_spi_phys_get_marker_loopback_snapshot(concurrent_reader_phys, &snapshot));
    if (raw.role == TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK)
        assert(tdma_pio_spi_phys_get_sck_loopback_snapshot(concurrent_reader_phys, &snapshot));
    ++concurrent_reads;
    return (word >> (i * 8u)) & 0xffu;
}
'''
    harness += r'''
/* DATA_LOOP_STUBS_BEGIN */
static bool tdma_pio_spi_phys_data_loop_start(tdma_pio_spi_phys_t *p,const tdma_pio_spi_p3_request_t *r)
{ (void)p;(void)r;return false; }
static void tdma_pio_spi_phys_data_loop_stop(tdma_pio_spi_phys_t *p) { (void)p; }
static void tdma_pio_spi_phys_data_loop_service(tdma_pio_spi_phys_t *p) { (void)p; }
/* DATA_LOOP_STUBS_END */
'''
    harness += (ROOT / "components/tdma/src/tdma_pio_spi_phys_p3.inc").read_text(
        encoding="utf-8")
    harness += r'''
typedef struct { uint32_t guard, sequence, opcode; tdma_pio_spi_p3_request_t request; }
    calibration_p3_intent_t;
static calibration_p3_intent_t s_p3_intent;
static uint32_t s_p3_intent_consumed_sequence;
static uint32_t s_marker_loopback_accepted_sequence;
static bool s_p3_offline_session, s_p3_offline_seen_armed, s_p3_offline_stopping;
static unsigned manager_published;
static tdma_pio_spi_p3_snapshot_t manager_raw;
#define CALIBRATION_P3_INTENT_START 1u
static bool tdma_runtime_owner_get_p3_snapshot(tdma_pio_spi_p3_snapshot_t *raw)
{ *raw = manager_raw; return true; }
static unsigned manager_stops;
static void calibration_manager_stop_p3(void) { ++manager_stops; }
static void calibration_manager_p3_publish(uint32_t kind, const tdma_pio_spi_p3_request_t *req)
{ assert(kind == 1 && req->role >= 1 && req->role <= 3); ++manager_published; }
static void calibration_manager_set_training_activity_core0(bool active)
{ assert(active); }
static tdma_pio_spi_phys_t *manager_phys;
static unsigned manager_copy_race;
static bool tdma_runtime_owner_copy_marker_loopback_capture(uint32_t epoch,
    uint32_t offset, uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total)
{
    const bool copied = tdma_pio_spi_phys_copy_marker_loopback_capture(
        manager_phys, epoch, offset, count, generation, words, total);
    if (manager_copy_race == 1) s_p3_intent.guard += 2;
    if (manager_copy_race == 2) ++s_p3_intent.sequence;
    if (manager_copy_race == 3) s_marker_loopback_accepted_sequence = 0;
    if (manager_copy_race == 4) s_p3_intent_consumed_sequence = 0;
    return copied;
}
static bool tdma_runtime_owner_copy_sck_loopback_capture(uint32_t epoch,
    uint32_t offset, uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total)
{
    return tdma_pio_spi_phys_copy_sck_loopback_capture(
        manager_phys, epoch, offset, count, generation, words, total);
}
static bool tdma_runtime_owner_copy_rxphase_capture(uint32_t epoch,
    uint32_t offset, uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total)
{
    return tdma_pio_spi_phys_copy_rxphase_capture(manager_phys, epoch, offset, count, generation, words, total);
}
static bool tdma_runtime_owner_copy_mark_rxphase_capture(uint32_t epoch,
    uint32_t offset, uint32_t count, uint32_t generation, uint32_t *words, uint32_t *total)
{
    return tdma_pio_spi_phys_copy_mark_rxphase_capture(manager_phys, epoch, offset, count, generation, words, total);
}
'''
    pio = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(encoding="utf-8")
    harness += r'''
typedef struct { int unused; } pio_sm_config;
typedef void *PIO;
#define PIO_FIFO_JOIN_RX 1
static uint pio_encode_wait_gpio(bool level, uint pin) { return 0x2000u | (level ? 128u : 0u) | pin; }
static void pio_gpio_init(void *p, uint pin) { (void)p; (void)pin; }
static void gpio_pull_down(uint pin) { (void)pin; }
static void gpio_pull_up(uint pin) { (void)pin; }
static void pio_sm_set_consecutive_pindirs(void *p, uint sm, uint pin, uint count, bool out)
{ (void)p; (void)sm; (void)pin; (void)count; (void)out; }
static pio_sm_config tdma_pio_spi_sck_train_sink_program_get_default_config(uint offset)
{ (void)offset; return (pio_sm_config){0}; }
static pio_sm_config tdma_pio_spi_mark_rxphase_sink_program_get_default_config(uint offset)
{ (void)offset; return (pio_sm_config){0}; }
static void sm_config_set_in_pins(pio_sm_config *c, uint pin) { (void)c; (void)pin; }
static void sm_config_set_in_shift(pio_sm_config *c, bool right, bool autopush, uint threshold)
{ (void)c; assert(right && autopush && threshold == 32); }
static void sm_config_set_fifo_join(pio_sm_config *c, uint mode) { (void)c; assert(mode == 1); }
static void sm_config_set_clkdiv(pio_sm_config *c, float divider) { (void)c; assert(divider == 1.0f); }
static void pio_sm_init(void *p, uint sm, uint off, pio_sm_config *c) { (void)p; (void)sm; (void)off; (void)c; }
'''
    # Compile the real generated-header initializer as well as the FSM doubles.
    initializer = _definition(pio, "static inline uint32_t tdma_pio_spi_sck_train_sink_program_init(", "\n}")
    initializer = initializer.replace("tdma_pio_spi_sck_train_sink_program_init", "actual_sink_init")
    initializer = initializer.replace("PIO pio", "typeof(&mock_pio) pio")
    harness += initializer
    initializer = _definition(pio, "static inline uint32_t tdma_pio_spi_mark_rxphase_sink_program_init(", "\n}")
    harness += initializer.replace("tdma_pio_spi_mark_rxphase_sink_program_init", "actual_mark_sink_init").replace("PIO pio", "typeof(&mock_pio) pio")
    harness += "\n" + (ROOT / "components/calibration_manager/src/calibration_training_phase.c").read_text(encoding="utf-8").replace('#include "calibration_training_phase.h"', '')
    sck_model = (ROOT / "components/calibration_manager/src/calibration_training_sck.c").read_text(encoding="utf-8")
    harness += "\n#define CALIBRATION_TRAINING_SCK_CAPTURE_PIPELINE_SAMPLES 1u\n"
    harness += _definition(sck_model, "bool calibration_training_sck_map_offset_to_phase_cycles(", "    return true;\n}")
    manager = (ROOT / "components/calibration_manager/src/calibration_manager.c").read_text(
        encoding="utf-8")
    harness += _definition(manager, "static bool calibration_manager_p3_read(",
                           "    return false;\n}")
    harness += _definition(manager, "bool calibration_manager_request_p3(",
                           "    return true;\n}")
    harness += _definition(manager, "static bool calibration_manager_copy_codeword_loopback_capture(",
                           "after.sequence == __atomic_load_n(&s_marker_loopback_accepted_sequence, __ATOMIC_ACQUIRE);\n}")
    harness += _definition(manager, "bool calibration_manager_copy_marker_loopback_capture(",
                           "words, total, TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK);\n}")
    harness += _definition(manager, "bool calibration_manager_copy_sck_loopback_capture(",
                           "words, total, TDMA_PIO_SPI_P3_ROLE_SCK_LOOPBACK);\n}")
    harness += _definition(manager, "bool calibration_manager_copy_rxphase_capture(",
                           "words, total, TDMA_PIO_SPI_P3_ROLE_SCK_RXPHASE);\n}")
    harness += _definition(manager, "bool calibration_manager_copy_mark_rxphase_capture(",
                           "words, total, TDMA_PIO_SPI_P3_ROLE_MARK_RXPHASE);\n}")
    harness += _definition(manager, "bool calibration_manager_stop_codeword_loopback(",
                           "    return true;\n}")
    harness += r'''
typedef struct { uint32_t config_seq, applied_config_seq; } tdma_ring_runtime_snapshot_t;
typedef struct { uint32_t node_count, local_slot_id, reference_slot_id, ring_profile_crc32,
    operating_profile_crc32, schedule_crc32; } tdma_service_ring_runtime_config_t;
typedef struct { uint32_t enabled, calibration_generation; } tdma_ring_calibration_stage_t;
static uint32_t config_mode, config_reads;
static bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *snapshot)
{ snapshot->config_seq = config_mode == 1 ? ++config_reads : 9; snapshot->applied_config_seq = 9; return true; }
static bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *staged)
{
    *staged = (tdma_service_ring_runtime_config_t){2, 0, 0, 31, 41, 51};
    if (config_mode == 2) staged->schedule_crc32 += ++config_reads;
    if (config_mode == 3) staged->local_slot_id = 2;
    if (config_mode == 4) staged->schedule_crc32 = 0;
    return true;
}
static bool tdma_runtime_owner_get_calibration_stage(tdma_ring_calibration_stage_t *stage, bool *complete)
{ *stage = (tdma_ring_calibration_stage_t){config_mode == 5, 71}; *complete = false; return stage->enabled != 0; }
static uint32_t calibration_manager_topology_generation(const tdma_service_ring_runtime_config_t *staged)
{ return staged->schedule_crc32 == 0 ? 0 : 61; }
'''
    harness += _definition(manager, "bool calibration_manager_get_rxphase_config(", "    return false;\n}")
    harness += r'''
static void manager_capture_gate(tdma_pio_spi_phys_t *p)
{
    manager_phys = p;
    uint32_t word, total;
    const calibration_p3_intent_t valid = {.guard=2, .sequence=91, .opcode=1,
        .request={.role=4, .marker_generation=91}};
    for (unsigned mode = 0; mode < 12; ++mode) {
        s_p3_intent = valid;
        s_p3_intent_consumed_sequence = s_marker_loopback_accepted_sequence = 91;
        manager_copy_race = 0;
        if (mode == 1) s_p3_intent.guard |= 1;
        if (mode == 2) s_p3_intent.sequence = 92;
        if (mode == 3) s_marker_loopback_accepted_sequence = 0;
        if (mode == 4) s_p3_intent.opcode = 2;
        if (mode == 5) s_p3_intent.request.role = 3;
        if (mode == 6) s_p3_intent_consumed_sequence = 90;
        if (mode >= 7 && mode <= 10) manager_copy_race = mode - 6;
        assert(calibration_manager_copy_marker_loopback_capture(51, 0, 1,
            mode == 11 ? 92 : 91, &word, &total) == (mode == 0));
    }
    manager_copy_race = 0;
}
static void initialize(tdma_pio_spi_phys_t *p)
{
    concurrent_reader_phys = p;
    memset(p, 0, sizeof(*p));
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL;
    s_tdma_pio_spi_tx_offset = 0;
    s_tdma_pio_spi_rx_offset = 1;
    loaded = 3; used_words = 16; enabled = configured = 0;
    memset(drivers, 0, sizeof(drivers));
    s_tdma_pio_spi_rx_dma_channel = -1;
    dma_hw->ch[0].transfer_count = 0;
}
static void service_until(tdma_pio_spi_phys_t *p, uint transition)
{
    for (unsigned i = 0; p->p3_transition != transition && i < 1024; ++i) {
        const uint32_t word = p->p3_decode_word;
        const uint32_t endpoint = p->p3_decode_endpoint;
        tdma_pio_spi_phys_p3_service(p);
        if (p->p3_decode_endpoint == endpoint && p->p3_decode_word >= word)
            assert(p->p3_decode_word - word <= 8);
    }
    assert(p->p3_transition == transition);
}
static void assert_restored(tdma_pio_spi_phys_t *p)
{
    assert(loaded == 3 && used_words == 16 && enabled == 0);
    assert(!drivers[30] && !drivers[31] && !drivers[32]);
    assert(!p->armed);
    assert(s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
}
static void waveform(unsigned group, bool missing)
{
    memset(s_tdma_pio_spi_cal_ring, 0, sizeof(s_tdma_pio_spi_cal_ring));
    for (unsigned sample = 0; sample < 256; ++sample) {
        uint32_t value = 0;
        /* Non-overlapping bit domains prove the endpoint pairs are distinct. */
        for (unsigned pulse = 0; pulse < 4; ++pulse) {
            unsigned t = 20 + pulse * 40;
            if (sample >= t && sample < t + 10) value |= 1u << (group ? 2 : 1);
            if (!missing && sample >= t + 3 && sample < t + 13)
                value |= 1u << (group ? 3 : 4);
            if (!missing && sample >= t + 5 && sample < t + 15) value |= 1u << 5;
            if (!missing && sample >= t + 9 && sample < t + 19) value |= 1u;
        }
        s_tdma_pio_spi_cal_ring[sample / 4] |= value << ((sample % 4) * 8);
    }
}
static void marker_waveform(unsigned mode, unsigned words)
{
    memset(s_tdma_pio_spi_marker_loopback_capture, 0, sizeof(s_tdma_pio_spi_marker_loopback_capture));
    for (unsigned sample = 0; sample < words * 4; ++sample) {
        uint32_t value = 0x2d;
        if (mode != 4) {
            if (sample >= 300) value &= ~(1u << 2);
            if (sample >= (mode == 3 ? 290u : 313u)) value &= ~(1u << 3);
            if (sample >= 317) value &= ~(1u << 5);
            if (mode != 1 && sample >= 330) value &= ~1u;
        }
        if (mode == 2 && sample == 0) value &= ~1u;
        s_tdma_pio_spi_marker_loopback_capture[sample / 4] |= value << ((sample % 4) * 8);
    }
}
static void marker_drain(tdma_pio_spi_phys_t *p)
{
    dma_hw->ch[0].transfer_count = 0;
    /* RX prefix completion must leave full codeword TX and forwarding live. */
    mock_pio.fdebug = 1u << 24;
    tdma_pio_spi_phys_p3_service(p);
    assert(enabled == 3 && p->p3.state == TDMA_PIO_SPI_P3_ARMED);
    assert(!(p->p3.flags & TDMA_PIO_SPI_P3_FLAG_TX_DRAINED));
    dma_hw->ch[1].transfer_count = 0;
    tx_fifo_empty = true;
    tdma_pio_spi_phys_p3_service(p);
    assert(p->p3_marker_drain_check);
    assert(!(p->p3.flags & TDMA_PIO_SPI_P3_FLAG_TX_DRAINED));
    /* Emulate fdebug W1C, then a later fresh empty-OSR stall. */
    mock_pio.fdebug = 0;
    tdma_pio_spi_phys_p3_service(p);
    assert(enabled == 3 && p->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_IDLE);
    mock_pio.fdebug = 1u << 24;
    tdma_pio_spi_phys_p3_service(p);
    assert(p->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_FREEZE);
}
static void marker_case(tdma_pio_spi_phys_t *p, unsigned which)
{
    const uint32_t codeword[] = {0x01234567, 0x76543210, 5};
    tdma_pio_spi_p3_request_t request = {
        .role=TDMA_PIO_SPI_P3_ROLE_MARKER_LOOPBACK, .capture_words=256,
        .epoch=51, .signal_group=1, .marker_words=codeword,
        .marker_word_count=3, .marker_sample_count=67, .marker_codebook=2,
        .marker_fault=which == 4 || which == 67, .marker_generation=91};
    if (which == 5) request.capture_words = 16;
    if (which >= 66) request.capture_words = TDMA_PIO_SPI_MARKER_LOOPBACK_CAPTURE_MAX_WORDS;
    tdma_pio_spi_p3_request_t invalid = request;
    invalid.capture_words = TDMA_PIO_SPI_MARKER_LOOPBACK_CAPTURE_MAX_WORDS + 1;
    assert(!tdma_pio_spi_phys_p3_start(p, &invalid));
    invalid = request; invalid.marker_generation = 0;
    assert(!tdma_pio_spi_phys_p3_start(p, &invalid));
    memset(s_tdma_pio_spi_marker_loopback_capture, 0xaa, sizeof(s_tdma_pio_spi_marker_loopback_capture));
    assert(tdma_pio_spi_phys_p3_start(p, &request));
    assert(!tdma_pio_spi_phys_p3_start(p, &request));
    assert(p->p3_marker_tx_words == 13);
    for (uint i = 0; i < 8; ++i) assert(s_tdma_pio_spi_marker_tx[i] == UINT32_MAX);
    assert(s_tdma_pio_spi_marker_tx[8] == (request.marker_fault ? UINT32_MAX : codeword[0]));
    assert(s_tdma_pio_spi_marker_tx[9] == (request.marker_fault ? UINT32_MAX : codeword[1]));
    assert(s_tdma_pio_spi_marker_tx[10] == (request.marker_fault ? UINT32_MAX : 0xfffffffdu));
    assert(s_tdma_pio_spi_marker_tx[11] == UINT32_MAX);
    assert(s_tdma_pio_spi_marker_tx[12] == UINT32_MAX);
    if (which >= 14 && which < 38) {
        for (uint i = 0; i < which - 14; ++i) tdma_pio_spi_phys_p3_service(p);
    } else if (which >= 8 && which <= 12) {
        if (which < 11) fail_program = which == 8 ? 6 : which == 9 ? 7 : 4;
        if (which == 11) fail_dma = true;
        if (which == 12) fail_tx_dma = true;
        service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(p->p3.state == TDMA_PIO_SPI_P3_ERROR);
        assert(p->p3.reject_reason == (which < 11 ?
            TDMA_PIO_SPI_P3_REJECT_RESOURCE : TDMA_PIO_SPI_P3_REJECT_DMA));
        assert_restored(p);
        return;
    } else if (which == 7) {
        service_until(p, TDMA_PIO_SPI_P3_TRANSITION_START_ARM);
        tx_fifo_empty = true;
        now_us = p->p3_marker_deadline_us;
        tdma_pio_spi_phys_p3_service(p);
        service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(p->p3.reject_reason == TDMA_PIO_SPI_P3_REJECT_TIMEOUT);
        assert(p->p3.state == TDMA_PIO_SPI_P3_ERROR);
        assert_restored(p);
        return;
    } else {
        service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(enabled == 7 && drivers[30] && !drivers[31] && drivers[32]);
        assert(rx_dma_target == s_tdma_pio_spi_marker_loopback_capture);
        assert(s_tdma_pio_spi_marker_loopback_capture[0] == 0xaaaaaaaau);
        assert(s_tdma_pio_spi_marker_loopback_capture[2047] == 0xaaaaaaaau);
        uint32_t chunk[32], total = 0;
        assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 1, 91, chunk, &total));
        marker_waveform(which == 67 ? 4 : which, request.capture_words);
        if (which == 6) {
            now_us = p->p3_marker_deadline_us;
            tdma_pio_spi_phys_p3_service(p);
            service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
            assert(p->p3.reject_reason == TDMA_PIO_SPI_P3_REJECT_TIMEOUT);
            assert(p->p3.state == TDMA_PIO_SPI_P3_ERROR);
            assert_restored(p);
            return;
        }
        marker_drain(p);
        if (which >= 62 && which <= 65) {
            service_until(p, which == 62 ? TDMA_PIO_SPI_P3_TRANSITION_RESTORE_UNLOAD :
                which == 65 ? TDMA_PIO_SPI_P3_TRANSITION_PUBLISH :
                TDMA_PIO_SPI_P3_TRANSITION_RESTORE_LOAD);
            if (which == 64) tdma_pio_spi_phys_p3_service(p);
        } else if (which >= 38 && which < 62) {
            for (uint i = 0; i < which - 38; ++i) tdma_pio_spi_phys_p3_service(p);
        } else {
            service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
            assert_restored(p);
            if (which == 0 || which == 13 || which == 66) {
                assert(p->p3.state == TDMA_PIO_SPI_P3_COMPLETE && p->p3.flags == 127);
                assert(p->p3.t1_clk_tx == 1200 && p->p3.t2_clk_rx == 1252);
                assert(p->p3.t3_data_tx == 1268 && p->p3.t4_data_rx == 1320);
            } else {
                assert(p->p3.state == TDMA_PIO_SPI_P3_ERROR);
                assert(!(p->p3.flags & TDMA_PIO_SPI_P3_FLAG_SYNC_MATCH));
                assert(p->p3.reject_reason == (which == 2 ? TDMA_PIO_SPI_P3_REJECT_PREEDGE :
                    which == 3 ? TDMA_PIO_SPI_P3_REJECT_EDGE_ORDER : TDMA_PIO_SPI_P3_REJECT_EDGE_MISSING));
            }
            tdma_pio_spi_marker_loopback_snapshot_t snapshot;
            assert(tdma_pio_spi_phys_get_marker_loopback_snapshot(p, &snapshot));
            assert(snapshot.codebook == 2 && snapshot.marker_samples == 67);
            assert(snapshot.capture_generation == 91);
            assert(concurrent_reads >= request.capture_words * 8);
            assert(snapshot.tx_words == 13 && snapshot.tx_dma_remaining == 0);
            assert(tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 1, 91, chunk, &total));
            assert(total == request.capture_words);
            const uint32_t frozen = chunk[0];
            memset(s_tdma_pio_spi_cal_ring, 0xcc, sizeof(s_tdma_pio_spi_cal_ring));
            assert(tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 1, 91, chunk, &total));
            assert(chunk[0] == frozen);
            assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 52, 0, 1, 91, chunk, &total));
            assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 0, 91, chunk, &total));
            assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 33, 91, chunk, &total));
            assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, total, 1, 91, chunk, &total));
            assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, UINT32_MAX, 1, 91, chunk, &total));
            assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 1, 92, chunk, &total));
            assert(tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 1, 0, chunk, &total) ==
                (request.capture_words <= TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS));
            ++p->p3_guard;
            assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 1, 91, chunk, &total));
            ++p->p3_guard;
            manager_capture_gate(p);
            if (which == 66) {
                for (uint32_t offset = 0; offset < total; offset += 32) {
                    assert(tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, offset, 32, 91, chunk, &total));
                    assert(memcmp(chunk, s_tdma_pio_spi_marker_loopback_capture + offset, sizeof(chunk)) == 0);
                }
                /* Reuse epoch deliberately: only the new generation may read
                 * the next complete capture, including its later chunks. */
                request.marker_generation = 92;
                assert(tdma_pio_spi_phys_p3_start(p, &request));
                assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 32, 32, 91, chunk, &total));
                service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
                marker_waveform(0, request.capture_words);
                marker_drain(p);
                service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
                assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 32, 32, 91, chunk, &total));
                assert(tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 32, 32, 92, chunk, &total));
            }
        }
    }
    tdma_pio_spi_phys_p3_stop(p);
    for (uint i = 0; i < 80 && p->p3_transition != 0; ++i) {
        tdma_pio_spi_phys_p3_service(p);
        tdma_pio_spi_phys_p3_stop(p);
    }
    assert(p->p3.state == TDMA_PIO_SPI_P3_IDLE && p->p3_transition == 0);
    assert_restored(p);
    uint32_t word, total;
    assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 1, 91, &word, &total));
}
static void scoped_stop_tests(uint32_t role)
{
    const uint32_t other = role == 7 ? 6 : role == 6 ? 7 : role == 4 ? 5 : 4;
    for (uint32_t state = 1; state <= 3; ++state) {
        manager_raw.state = state; manager_raw.role = role;
        s_p3_intent = (calibration_p3_intent_t){.guard=2, .sequence=91, .opcode=1,
            .request={.role=role}};
        s_p3_intent_consumed_sequence = 91;
        const unsigned stops = manager_stops;
        assert(!calibration_manager_stop_codeword_loopback(other));
        assert(manager_stops == stops);
        assert(calibration_manager_stop_codeword_loopback(role));
        assert(manager_stops == stops + 1);
        /* A not-yet-consumed START also belongs only to its requested scope. */
        s_p3_intent.sequence = 92;
        manager_raw.state = 0;
        assert(!calibration_manager_stop_codeword_loopback(other));
        assert(calibration_manager_stop_codeword_loopback(role));
    }
    memset(&manager_raw, 0, sizeof(manager_raw));
}
static unsigned tx_sample(uint32_t sample, uint32_t words)
{
    return sample < words * 32 ? (s_tdma_pio_spi_marker_tx[sample / 32] >> (sample % 32)) & 1 : 0;
}
static void sck_waveform(uint32_t words, uint32_t tx_words)
{
    const uint32_t pins[] = {1, 4, 5, 0};
    const uint32_t delays[] = {1, 14, 20, 33};
    memset(s_tdma_pio_spi_marker_loopback_capture, 0, sizeof(s_tdma_pio_spi_marker_loopback_capture));
    for (uint32_t sample = 0; sample < words * 4; ++sample) {
        uint32_t value = 0;
        for (uint32_t channel = 0; channel < 4; ++channel)
            if (sample >= delays[channel])
                value |= tx_sample(sample - delays[channel], tx_words) << pins[channel];
        s_tdma_pio_spi_marker_loopback_capture[sample / 4] |= value << ((sample % 4) * 8);
    }
}
static void sck_case(tdma_pio_spi_phys_t *p, unsigned which)
{
    uint32_t codeword[201];
    for (uint32_t i = 0; i < 201; ++i) codeword[i] = 0x13579bdfu ^ (0x01234567u * i);
    const int32_t offset = which <= 20 ? (int32_t)which - 10 : 0;
    tdma_pio_spi_p3_request_t request = {.role=5, .capture_words=2048, .epoch=51,
        .signal_group=0, .marker_words=codeword, .marker_word_count=201,
        .marker_sample_count=6420, .marker_codebook=1, .marker_generation=91,
        .marker_fault=which == 21, .sck_launch_offset_samples=offset};
    tdma_pio_spi_p3_request_t bad = request;
    bad.sck_launch_offset_samples = -11;
    assert(!tdma_pio_spi_phys_p3_start(p, &bad));
    bad.sck_launch_offset_samples = 11;
    assert(!tdma_pio_spi_phys_p3_start(p, &bad));
    bad = request; bad.signal_group = 1;
    assert(!tdma_pio_spi_phys_p3_start(p, &bad));
    bad = request; bad.marker_word_count = 202;
    assert(!tdma_pio_spi_phys_p3_start(p, &bad));
    const tdma_pio_spi_program_persona_t foreign[] = {
        TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER, TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN,
        TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN, TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED,
        TDMA_PIO_SPI_PROGRAM_PERSONA_CAL_LOOPBACK};
    for (uint32_t i = 0; i < sizeof(foreign)/sizeof(foreign[0]); ++i) {
        s_tdma_pio_spi_program_persona = foreign[i];
        assert(!tdma_pio_spi_phys_p3_start(p, &request));
        assert(p->p3.state == 0 && loaded == 3);
    }
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL;
    assert(tdma_pio_spi_phys_p3_start(p, &request));
    assert(!tdma_pio_spi_phys_p3_start(p, &request));
    const uint32_t code_start = (uint32_t)(256 + offset);
    assert(p->p3_marker_tx_words == (code_start + 6420 + 64 + 31) / 32);
    for (uint32_t sample = 0; sample < p->p3_marker_tx_words * 32; ++sample) {
        unsigned expected = sample >= 64 && sample < 128;
        if (sample >= code_start && sample < code_start + 6420) {
            const uint32_t index = sample - code_start;
            expected = (codeword[index / 32] >> (index % 32)) & 1;
        }
        if (which == 21) expected = 0;
        assert(tx_sample(sample, p->p3_marker_tx_words) == expected);
    }
    if (which >= 22 && which <= 25) {
        if (which < 25) fail_program = which == 22 ? 8 : which == 23 ? 9 : 4;
        else fail_tx_dma = true;
        service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(p->p3.state == TDMA_PIO_SPI_P3_ERROR);
        assert_restored(p);
        return;
    }
    if (which >= 26 && which < 50) {
        for (uint32_t beat = 0; beat < which - 26; ++beat) tdma_pio_spi_phys_p3_service(p);
    } else {
        service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(enabled == 7 && drivers[30] && drivers[31] && !drivers[32]);
        assert(loaded == ((1u << 8) | (1u << 9) | (1u << 4)) && used_words == 8);
        assert(rx_dma_target == s_tdma_pio_spi_marker_loopback_capture);
        sck_waveform(2048, p->p3_marker_tx_words);
        marker_drain(p);
        if (which >= 50) {
            const uint32_t targets[] = {TDMA_PIO_SPI_P3_TRANSITION_CAPTURE_DECODE,
                TDMA_PIO_SPI_P3_TRANSITION_RESTORE_UNLOAD, TDMA_PIO_SPI_P3_TRANSITION_RESTORE_LOAD,
                TDMA_PIO_SPI_P3_TRANSITION_PUBLISH};
            service_until(p, targets[(which - 50) % 4]);
        } else {
            service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
            assert_restored(p);
            assert(p->p3.flags == (which == 21 ? 119u : 127u));
            assert(p->p3.state == (which == 21 ? TDMA_PIO_SPI_P3_ERROR : TDMA_PIO_SPI_P3_COMPLETE));
            if (which != 21) {
                assert(p->p3.t1_clk_tx == 516 && p->p3.t2_clk_rx == 568);
                assert(p->p3.t3_data_tx == 592 && p->p3.t4_data_rx == 644);
            }
            tdma_pio_spi_marker_loopback_snapshot_t snapshot;
            assert(!tdma_pio_spi_phys_get_marker_loopback_snapshot(p, &snapshot));
            assert(tdma_pio_spi_phys_get_sck_loopback_snapshot(p, &snapshot));
            assert(snapshot.sck_launch_offset_samples == offset && snapshot.capture_generation == 91);
            assert(concurrent_reads == 2048 * 8);
            uint32_t words[32], total;
            assert(!tdma_pio_spi_phys_copy_marker_loopback_capture(p, 51, 0, 1, 91, words, &total));
            assert(!tdma_pio_spi_phys_copy_sck_loopback_capture(p, 51, 0, 1, 0, words, &total));
            assert(!tdma_pio_spi_phys_copy_sck_loopback_capture(p, 51, 0, 1, 92, words, &total));
            manager_phys = p;
            s_p3_intent = (calibration_p3_intent_t){.guard=2, .sequence=91, .opcode=1, .request={.role=5}};
            s_p3_intent_consumed_sequence = s_marker_loopback_accepted_sequence = 91;
            assert(!calibration_manager_copy_marker_loopback_capture(51, 0, 1, 91, words, &total));
            for (uint32_t start = 0; start < 2048; start += 32) {
                assert(calibration_manager_copy_sck_loopback_capture(51, start, 32, 91, words, &total));
                assert(total == 2048);
                assert(memcmp(words, s_tdma_pio_spi_marker_loopback_capture + start, sizeof(words)) == 0);
            }
            scoped_stop_tests(5);
            scoped_stop_tests(4);
        }
    }
    tdma_pio_spi_phys_p3_stop(p);
    for (uint32_t i = 0; i < 80 && p->p3_transition != 0; ++i) {
        tdma_pio_spi_phys_p3_service(p);
        tdma_pio_spi_phys_p3_stop(p);
    }
    assert(p->p3.state == 0 && p->p3_transition == 0);
    assert_restored(p);
}
static void rxphase_case(tdma_pio_spi_phys_t *p, unsigned which, bool mark)
{
    uint32_t source_cycles, destination_cycles;
    for (int32_t scan = -7; scan <= 10; ++scan) {
        assert(calibration_training_sck_map_offset_to_phase_cycles(26, 4, scan, &source_cycles, &destination_cycles));
        assert(source_cycles == 1 && destination_cycles == (uint32_t)(8 + scan));
        assert(actual_sink_init(&mock_pio, 1, 10, 28, destination_cycles) ==
               (0xa042u | ((7u + scan) << 8)));
        assert(mock_pio.instr_mem[13] == (0xa042u | ((7u + scan) << 8)));
        actual_sink_init(&mock_pio, 1, 10, 24, destination_cycles);
        assert(mock_pio.instr_mem[10] == pio_encode_wait_gpio(false, 24));
        assert(actual_mark_sink_init(&mock_pio, 1, 12, 27, destination_cycles) ==
               (0xa042u | ((7u + scan) << 8)));
        assert(mock_pio.instr_mem[12] == pio_encode_wait_gpio(true, 27));
        assert(mock_pio.instr_mem[13] == pio_encode_wait_gpio(false, 27));
        assert(mock_pio.instr_mem[14] == pio_encode_wait_gpio(true, 27));
        assert(mock_pio.instr_mem[15] == (0xa042u | ((7u + scan) << 8)));
    }
    assert(!calibration_training_sck_map_offset_to_phase_cycles(26, 4, -8, &source_cycles, &destination_cycles));
    uint32_t codeword[201];
    for (uint32_t i = 0; i < 201; ++i) codeword[i] = 0x13579bdfu ^ (0x01234567u * i);
    const int32_t offset = (int32_t)(which % 3) - 1;
    const uint32_t direction = mark ? 0 : (which / 3) % 2;
    const uint32_t role = mark ? 7 : 6;
    tdma_pio_spi_p3_request_t request = {.role=role, .capture_words=256, .epoch=51,
        .signal_group=mark ? 1u : 0u, .marker_words=codeword, .marker_word_count=201,
        .marker_sample_count=6420, .marker_codebook=1, .marker_generation=91,
        .marker_fault=which == 6, .sck_launch_offset_samples=offset,
        .rxphase_base_ns=26, .rxphase_direction=direction, .rxphase_destination_cycles=8+offset};
    tdma_pio_spi_p3_request_t bad = request;
    bad.rxphase_base_ns = 0; assert(!tdma_pio_spi_phys_p3_start(p, &bad));
    bad = request; bad.rxphase_direction = 2; assert(!tdma_pio_spi_phys_p3_start(p, &bad));
    if (mark) {
        bad = request; bad.rxphase_direction = 1; assert(!tdma_pio_spi_phys_p3_start(p, &bad));
        bad = request; bad.capture_words = 255; assert(!tdma_pio_spi_phys_p3_start(p, &bad));
        bad = request; bad.signal_group = 0; assert(!tdma_pio_spi_phys_p3_start(p, &bad));
    }
    bad = request; bad.capture_words = 257; assert(!tdma_pio_spi_phys_p3_start(p, &bad));
    bad = request; bad.rxphase_destination_cycles = 32; assert(!tdma_pio_spi_phys_p3_start(p, &bad));
    assert(loaded == 3 && p->p3.state == 0);
    assert(tdma_pio_spi_phys_p3_start(p, &request));
    assert(p->p3_marker_tx_words == 211);
    for (uint32_t sample = 0; sample < 211 * 32; ++sample) {
        unsigned expected = (sample >= 64 && sample < 128) != mark;
        if (sample >= 256 && sample < 256 + 6420) {
            const uint32_t bit = sample - 256;
            expected = (codeword[bit / 32] >> (bit % 32)) & 1;
        }
        if (which == 6) expected = mark;
        assert(tx_sample(sample, 211) == expected);
    }
    if (which >= 9 && which <= 12) {
        if (which == 9) fail_program = mark ? 11 : 8;
        if (which == 10) fail_program = mark ? 12 : 10;
        if (which == 11) fail_dma = true;
        if (which == 12) fail_resources = true;
        service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(p->p3.state == TDMA_PIO_SPI_P3_ERROR);
        assert_restored(p);
        return;
    }
    if (which >= 13) {
        for (uint32_t i = 0; i < which - 13; ++i) tdma_pio_spi_phys_p3_service(p);
        tdma_pio_spi_phys_p3_stop(p);
        service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(p->p3.state == 0); assert_restored(p); return;
    }
    service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
    assert(enabled == 3 && loaded == (mark ? ((1u << 11) | (1u << 12)) : ((1u << 8) | (1u << 10))) && used_words == 7);
    assert(drivers[30] == (!mark && direction == 1) && drivers[31] == (!mark && direction == 0) && drivers[32] == mark);
    assert(rx_dma_target == s_tdma_pio_spi_marker_loopback_capture);
    tdma_pio_spi_rxphase_snapshot_t snapshot;
    assert(tdma_pio_spi_phys_get_rxphase_snapshot(p, &snapshot));
    assert(!(mark ? tdma_pio_spi_phys_get_rxphase_snapshot : tdma_pio_spi_phys_get_mark_rxphase_snapshot)(p, &snapshot));
    assert(tdma_pio_spi_phys_get_rxphase_snapshot(p, &snapshot));
    assert(snapshot.fields[0] == 2 && snapshot.fields[11] == 7 && snapshot.fields[12] == 28);
    assert(snapshot.fields[16] == (uint32_t)(8 + offset));
    assert(snapshot.fields[17] == (0xa042u | ((7u + offset) << 8)));
    assert(snapshot.fields[18] == (uint32_t)(8 + offset));
    assert(snapshot.fields[2] == (mark ? 5u : 4u));
    assert(snapshot.fields[19] == (mark ? 26u : direction == 0 ? 25u : 29u));
    assert(snapshot.fields[20] == (mark ? 27u : direction == 0 ? 28u : 24u));
    uint32_t words[32], total;
    assert(!tdma_pio_spi_phys_copy_rxphase_capture(p, 51, 0, 1, 91, words, &total));
    for (uint32_t w = 0; w < 256; ++w) s_tdma_pio_spi_marker_loopback_capture[w] = 0x55555555u ^ w;
    dma_hw->ch[1].transfer_count = 0; tx_fifo_empty = true;
    tdma_pio_spi_phys_p3_service(p); mock_pio.fdebug = 0;
    tdma_pio_spi_phys_p3_service(p); mock_pio.fdebug = 1u << 24;
    tdma_pio_spi_phys_p3_service(p);
    if (which == 6 || which == 7) {
        dma_hw->ch[0].transfer_count = which == 6 ? 256 : 240;
        now_us = p->p3_marker_deadline_us;
    } else {
        dma_hw->ch[0].transfer_count = 0;
        /* Even a bogus bus read must never be used as instruction readback. */
        if (which == 8) mock_pio.instr_mem[13] = 0xffffffffu;
    }
    tdma_pio_spi_phys_p3_service(p);
    service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
    assert_restored(p);
    assert(tdma_pio_spi_phys_get_rxphase_snapshot(p, &snapshot));
    assert(snapshot.fields[1] == (which == 6 || which == 7 ? 3u : 2u));
    assert(snapshot.fields[4] == (which == 6 || which == 7 ? 4u : 0u));
    assert(snapshot.fields[3] == (which == 6 || which == 7 ? 11u : 15u));
    assert(snapshot.fields[26] == (which == 6 ? 0u : which == 7 ? 16u : 256u));
    assert(snapshot.fields[27] == (which == 6 ? 256u : which == 7 ? 240u : 0u));
    assert(!tdma_pio_spi_phys_copy_rxphase_capture(p, 51, 0, 1, 90, words, &total));
    assert(!tdma_pio_spi_phys_copy_sck_loopback_capture(p, 51, 0, 1, 91, words, &total));
    assert(!(mark ? tdma_pio_spi_phys_copy_rxphase_capture : tdma_pio_spi_phys_copy_mark_rxphase_capture)(p, 51, 0, 1, 91, words, &total));
    assert(tdma_pio_spi_phys_copy_rxphase_capture(p, 51, 0, 1, 91, words, &total) == (which != 6));
    if (which != 6) { assert(total == snapshot.fields[26]); assert(words[0] == 0x55555555u); }
    manager_phys = p;
    s_p3_intent = (calibration_p3_intent_t){.guard=2, .sequence=91, .opcode=1, .request={.role=role}};
    s_p3_intent_consumed_sequence = s_marker_loopback_accepted_sequence = 91;
    assert(calibration_manager_copy_rxphase_capture(51, 0, 1, 91, words, &total) == (which != 6));
    s_p3_intent.sequence = 92;
    assert(!calibration_manager_copy_rxphase_capture(51, 0, 1, 91, words, &total));
    scoped_stop_tests(role);
    tdma_pio_spi_phys_p3_stop(p); service_until(p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
    assert(!tdma_pio_spi_phys_copy_rxphase_capture(p, 51, 0, 1, 91, words, &total));
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    unsigned which = (unsigned)strtoul(argv[1], NULL, 10);
    tdma_pio_spi_phys_t p;
    initialize(&p);
    for (config_mode = 0; config_mode < 6; ++config_mode) {
        uint32_t fields[12];
        assert(calibration_manager_get_rxphase_config(fields) == (config_mode == 0 || config_mode == 5));
        if (config_mode == 0 || config_mode == 5) {
            assert(fields[0] == 1 && fields[1] == 2 && fields[4] == 61 && fields[5] == 31);
            assert(fields[8] == 9 && fields[9] == 9 && fields[11] == 4);
            assert(fields[10] == (config_mode == 5 ? 71u : 0u));
        }
    }
    assert(!calibration_manager_request_p3(4, 10000000, 4, 256, 51, 1));
    assert(!calibration_manager_request_p3(5, 10000000, 4, 256, 51, 0));
    assert(manager_published == 0);
    for (uint32_t role = 1; role <= 3; ++role)
        assert(calibration_manager_request_p3(role, 10000000, 4, 64, 51, 1));
    assert(manager_published == 3);
    if (which >= 189) {
        rxphase_case(&p, (which - 189) % 37, which >= 226);
        puts("p3 firmware passed");
        return 0;
    }
    if (which >= 135) {
        sck_case(&p, which - 135);
        puts("p3 firmware passed");
        return 0;
    }
    if (which >= 67) {
        marker_case(&p, which - 67);
        puts("p3 firmware passed");
        return 0;
    }
    if (which >= 31) {
        tdma_pio_spi_p3_request_t completed = {
            .role=3, .baud_hz=10000000, .pulse_count=4, .capture_words=64, .epoch=51};
        assert(tdma_pio_spi_phys_p3_start(&p, &completed));
        service_until(&p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        waveform(0, false);
        dma_hw->ch[0].transfer_count = 0;
        tdma_pio_spi_phys_p3_service(&p);
        service_until(&p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(p.p3.state == TDMA_PIO_SPI_P3_COMPLETE);
        assert_restored(&p);
        const tdma_pio_spi_program_persona_t personas[] = {
            TDMA_PIO_SPI_PROGRAM_PERSONA_NONE,
            TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL,
            TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE,
            TDMA_PIO_SPI_PROGRAM_PERSONA_CAL_LOOPBACK,
            TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED,
            TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER,
            TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN,
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN,
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER,
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER,
            TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN,
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_ORIGIN,
        };
        unsigned index = (which - 31) % 12;
        unsigned prior_state = (which - 31) / 12;
        assert(prior_state < 3);
        /* The foreign owner retains an instruction allocation and active
         * SM/DMA/drivers. Redundant P3 STOP may only clear P3 snapshots. */
        tdma_pio_spi_phys_cal_mark_persona(&p, personas[index]);
        loaded = 1u << 6;
        used_words = 13;
        enabled = configured = 3;
        drivers[30] = drivers[31] = drivers[32] = true;
        s_tdma_pio_spi_rx_dma_channel = 0;
        dma_hw->ch[0].transfer_count = 42;
        p.armed = true;
        p.p3.state = prior_state == 0 ? TDMA_PIO_SPI_P3_IDLE :
            prior_state == 1 ? TDMA_PIO_SPI_P3_COMPLETE : TDMA_PIO_SPI_P3_ERROR;
        p.p3_responder.state = p.p3.state;
        p.p3.epoch = p.p3_responder.epoch = 51;
        tdma_pio_spi_phys_t expected = p;
        memset(&expected.p3, 0, sizeof(expected.p3));
        memset(&expected.p3_responder, 0, sizeof(expected.p3_responder));
        for (unsigned repeat = 0; repeat < 4; ++repeat) {
            tdma_pio_spi_phys_p3_stop(&p);
            tdma_pio_spi_phys_p3_service(&p);
            assert(loaded == (1u << 6) && used_words == 13);
            assert(enabled == 3 && configured == 3);
            assert(drivers[30] && drivers[31] && drivers[32]);
            assert(s_tdma_pio_spi_rx_dma_channel == 0);
            assert(dma_hw->ch[0].transfer_count == 42);
            assert(s_tdma_pio_spi_program_persona == personas[index]);
            expected.p3_guard = p.p3_guard;
            assert(memcmp(&p, &expected, sizeof(p)) == 0);
        }
        puts("p3 firmware passed");
        return 0;
    }
    tdma_pio_spi_p3_request_t request = {.role=3, .baud_hz=10000000,
        .pulse_count=4, .capture_words=64, .epoch=51, .signal_group=which%2};
    tdma_pio_spi_p3_request_t oversized = request;
    oversized.capture_words = 257;
    assert(!tdma_pio_spi_phys_p3_start(&p, &oversized));
    assert(tdma_pio_spi_phys_p3_start(&p, &request));
    assert(!tdma_pio_spi_phys_p3_start(&p, &request));
    if (which < 4) {
        service_until(&p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(enabled == 7 && drivers[30] && drivers[31] && drivers[32]);
        waveform(request.signal_group, which >= 2);
        dma_hw->ch[0].transfer_count = 0;
        tdma_pio_spi_phys_p3_service(&p);
        service_until(&p, TDMA_PIO_SPI_P3_TRANSITION_PUBLISH);
        tdma_pio_spi_p3_snapshot_t a, b;
        assert(tdma_pio_spi_phys_get_p3_endpoint_snapshot(&p, 1, &a));
        assert(a.state == TDMA_PIO_SPI_P3_ARMED);
        tdma_pio_spi_phys_p3_service(&p);
        assert_restored(&p);
        assert(tdma_pio_spi_phys_get_p3_endpoint_snapshot(&p, 1, &a));
        assert(tdma_pio_spi_phys_get_p3_endpoint_snapshot(&p, 2, &b));
        assert(a.role == 1 && b.role == 2 && a.epoch == 51 && b.epoch == 51);
        assert(a.t2_clk_rx == 0 && a.t3_data_tx == 0);
        assert(b.t1_clk_tx == 0 && b.t4_data_rx == 0);
        if (which < 2) {
            assert(a.state == 2 && b.state == 2 && p.p3.edge_mask == 15);
            assert(a.edge_mask == 9 && b.edge_mask == 6);
            assert(a.t1_clk_tx == 80 && b.t2_clk_rx == 92);
            assert(b.t3_data_tx == 100 && a.t4_data_rx == 116);
            assert(a.data_pulse_count == 4 && b.data_pulse_count == 4);
            assert(a.clock_high_ns == 40 && b.clock_high_ns == 40);
            assert(a.clock_low_ns == 120 && b.clock_low_ns == 120);
        } else {
            assert(a.state == 3 && b.state == 3 && p.p3.edge_mask != 15);
            assert(a.reject_reason == TDMA_PIO_SPI_P3_REJECT_EDGE_MISSING);
            assert(b.clock_high_ns == 0 && b.data_high_ns == 0);
        }
        p.p3_guard |= 1;
        assert(!tdma_pio_spi_phys_get_p3_endpoint_snapshot(&p, 1, &a));
        ++p.p3_guard;
        assert(!tdma_pio_spi_phys_get_p3_endpoint_snapshot(&p, 3, &a));
    } else if (which < 24) {
        /* Inject STOP at every hotload/configuration boundary, plus partial
         * normal restoration; the fake instruction allocator catches double
         * unloads, mistaken offsets and leaked program words. */
        unsigned beats = which - 4;
        for (unsigned i = 0; i < beats; ++i) tdma_pio_spi_phys_p3_service(&p);
        tdma_pio_spi_phys_p3_stop(&p);
        for (unsigned i = 0; i < 40 && p.p3_transition != 0; ++i) {
            tdma_pio_spi_phys_p3_service(&p);
            tdma_pio_spi_phys_p3_stop(&p);
        }
        assert(p.p3.state == 0 && p.p3_transition == 0);
        assert_restored(&p);
    } else if (which < 29) {
        /* Isolate every resource failure so a preceding load rejection
         * cannot mask the DMA branch or partially loaded program cleanup. */
        if (which < 27) fail_program = which - 22;
        if (which == 27) fail_dma = true;
        if (which == 28) fail_resources = true;
        for (unsigned i = 0; i < 80 && p.p3_transition != 0; ++i)
            tdma_pio_spi_phys_p3_service(&p);
        assert(p.p3.state == 3 && p.p3_transition == 0);
        assert_restored(&p);
    } else {
        /* Cancellation during success restoration never interprets NORMAL
         * program_count as a P3 unload count. */
        service_until(&p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        waveform(request.signal_group, false);
        dma_hw->ch[0].transfer_count = 0;
        tdma_pio_spi_phys_p3_service(&p);
        service_until(&p, TDMA_PIO_SPI_P3_TRANSITION_RESTORE_LOAD);
        if (which == 30) tdma_pio_spi_phys_p3_service(&p);
        tdma_pio_spi_phys_p3_stop(&p);
        service_until(&p, TDMA_PIO_SPI_P3_TRANSITION_IDLE);
        assert(p.p3.state == 0);
        assert_restored(&p);
    }
    puts("p3 firmware passed");
    return 0;
}
'''
    # Both independent role entry points exercise the same FSM scenarios.
    start = harness.index("static void rxphase_case(")
    end = harness.index("\nint main(", start)
    block = harness[start:end]
    for name in ("tdma_pio_spi_phys_get_rxphase_snapshot", "tdma_pio_spi_phys_copy_rxphase_capture",
                 "calibration_manager_copy_rxphase_capture"):
        other = name.replace("rxphase", "mark_rxphase")
        block = block.replace(name + "(", f"(mark ? {other} : {name})(")
    harness = harness[:start] + block + harness[end:]
    source = target / "p3_firmware.c"
    source.write_text(harness, encoding="utf-8")
    executable = target / "p3_firmware.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    result = subprocess.run([compiler, "-std=gnu11", "-O2", "-Wall", "-Wextra",
                             "-Werror", str(source), "-o", str(executable)],
                            text=True, capture_output=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("case", range(263))
def test_p3_firmware(firmware_executable, case):
    result = subprocess.run([str(firmware_executable), str(case)],
                            text=True, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "p3 firmware passed" in result.stdout
