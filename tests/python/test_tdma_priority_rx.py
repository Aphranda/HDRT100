"""Execute production fixed mailbox ingress and the actual physical IRQ include."""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("short_enums", [False, True])
@pytest.mark.parametrize("node_capacity", [6, 8])
def test_priority_ingress_and_irq_lifetime(tmp_path: Path, short_enums: bool, node_capacity: int) -> None:
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    source = tmp_path / "priority_ingress.c"
    source.write_text(HARNESS, encoding="utf-8")
    executable = tmp_path / "priority_ingress.exe"
    command = [gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPROJECT_NODE_CAPACITY={node_capacity}",
               "-I" + str(ROOT / "components/tdma/inc"),
               "-I" + str(ROOT / "components/tdma/src"),
               str(ROOT / "components/tdma/src/tdma_priority_rx.c"),
               str(ROOT / "components/tdma/src/tdma_rx_sequence.c"),
               str(ROOT / "components/tdma/src/tdma_transport_frame.c"),
               str(source), "-o", str(executable)]
    if short_enums:
        command.insert(1, "-fshort-enums")
    for index, cmd in enumerate([command, [str(executable)]]):
        result = subprocess.run(cmd, capture_output=True, text=True)
        (tmp_path / f"command-{index}.json").write_text(json.dumps({
            "command": cmd, "returncode": result.returncode,
            "stdout": result.stdout, "stderr": result.stderr,
        }, indent=2), encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
    assert "priority ingress: 24 case groups passed" in result.stdout


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "tdma_priority_rx.h"
#include "tdma_profile.h"
#include "tdma_process_image_layout.h"
#include "tdma_rx_sequence.h"
#include "tdma_rx_scan.h"
typedef unsigned uint;
typedef struct { uint32_t inte0; bool pending; } fake_pio_t;
static fake_pio_t pio_instance;
#define BOARD_TDMA_RX_PIO (&pio_instance)
#define BOARD_SYS_CLOCK_HZ 250000000u
#define PROJECT_CORE1_PRIORITY_RX_MIN_PHYSICAL_CYCLES 250000u
#define TDMA_PIO_SPI_RX_RING_WORDS 1024u
#define DMA_CH0_TRANS_COUNT_MODE_BITS 0xf0000000u
#define DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF 1u
#define DMA_CH0_TRANS_COUNT_MODE_LSB 28u
#define DMA_CH0_TRANS_COUNT_COUNT_BITS 0x0fffffffu
#define PICO_DEFAULT_IRQ_PRIORITY 0x80u
#define pis_interrupt3 3u
static struct { uint32_t timerawh, timerawl; } timer_instance;
#define timer1_hw (&timer_instance)
static struct { struct { uint32_t transfer_count; } ch[1]; } dma_instance;
#define dma_hw (&dma_instance)
static struct { uint32_t scratch[8]; } watchdog_instance;
#define watchdog_hw (&watchdog_instance)
static uint32_t progress_trace[32], progress_trace_count;
static bool progress_trace_enabled;
static uint32_t progress_tick_step;
static void drv_watchdog_mark_progress(uint32_t core_index, uint32_t marker) {
    assert(core_index==1u); watchdog_hw->scratch[6]=marker;
    const uint32_t old_tick=timer1_hw->timerawl;
    timer1_hw->timerawl+=progress_tick_step;
    if (timer1_hw->timerawl<old_tick) ++timer1_hw->timerawh;
    if (progress_trace_enabled) {
        assert(progress_trace_count<sizeof(progress_trace)/sizeof(progress_trace[0]));
        progress_trace[progress_trace_count++]=marker;
    }
}
typedef struct {
    bool armed, flight_overlay_alignment_locked, flight_overlay_pending;
    uint32_t flight_payload_size, flight_physical_byte_count;
    uint32_t flight_alignment_byte_shift, flight_alignment_bit_shift, rx_csn_pin;
    struct { uint32_t overlay_frame_boundary_count, overlay_reuse_observation_count; } snapshot;
} tdma_pio_spi_phys_t;
typedef struct {
    uint32_t reference_slot_id, local_slot_id, node_count, schedule_crc32;
    uint32_t ring_profile_crc32, cycle_period_ns;
} tdma_ring_runtime_config_t;
static int s_tdma_pio_spi_rx_dma_channel = 0;
static bool s_tdma_pio_spi_rx_arm_valid = true;
static tdma_rx_dma_counter_t s_tdma_pio_spi_rx_sequence;
static unsigned core = 1u, ack_count, irq_enable_calls, copy_calls;
static bool irq_enabled, gpio_high = true, shared_handler, overwrite_on_copy;
static void (*handler)(void);
static uint32_t raw_ring[TDMA_PIO_SPI_RX_RING_WORDS];
static void __dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static unsigned get_core_num(void) { return core; }
static bool gpio_get(uint pin) { (void)pin; return gpio_high; }
static uint pio_get_irq_num(fake_pio_t *p, uint irq) { assert(p == BOARD_TDMA_RX_PIO && irq == 0u); return 7u; }
static bool pio_interrupt_get(fake_pio_t *p, uint source) { assert(source == 3u); return p->pending; }
static void pio_interrupt_clear(fake_pio_t *p, uint source) { assert(source == 3u); ++ack_count; p->pending = false; }
static void pio_set_irq0_source_enabled(fake_pio_t *p, uint source, bool enabled) { assert(source == 3u); p->inte0 = enabled ? 8u : 0u; }
static void irq_set_enabled(uint irq, bool enabled) { assert(irq == 7u); ++irq_enable_calls; irq_enabled = enabled; }
static bool irq_is_enabled(uint irq) { assert(irq == 7u); return irq_enabled; }
static void irq_clear(uint irq) { assert(irq == 7u); }
static void irq_remove_handler(uint irq, void (*fn)(void)) { assert(irq == 7u && handler == fn); handler = NULL; }
static void (*irq_get_exclusive_handler(uint irq))(void) { assert(irq == 7u); return handler; }
static bool irq_has_shared_handler(uint irq) { assert(irq == 7u); return shared_handler; }
static void irq_set_exclusive_handler(uint irq, void (*fn)(void)) { assert(irq == 7u && !handler); handler = fn; }
static void irq_set_priority(uint irq, uint priority) { assert(irq == 7u && priority == PICO_DEFAULT_IRQ_PRIORITY); }
static uint32_t save_and_disable_interrupts(void) { return 0u; }
static void restore_interrupts(uint32_t value) { assert(value == 0u); }
static uint32_t reverse(uint32_t x) {
    uint32_t y = 0u; for (unsigned i = 0; i < 32u; ++i) { y = (y << 1u) | (x & 1u); x >>= 1u; } return y;
}
static void tdma_pio_spi_phys_rx_ring_copy(uint8_t *dst, uint64_t start, uint32_t count, uint32_t shift) {
    ++copy_calls;
    for (uint32_t i = 0u; i < count; ++i) {
        uint8_t a = (uint8_t)reverse(raw_ring[(start + i) & 1023u]);
        uint8_t b = (uint8_t)reverse(raw_ring[(start + i + 1u) & 1023u]);
        dst[i] = shift ? (uint8_t)((a << shift) | (b >> (8u - shift))) : a;
    }
    if (overwrite_on_copy) {
        dma_hw->ch[0].transfer_count -= 400u;
        timer1_hw->timerawl += 1000u;
    }
}
#include "tdma_pio_spi_phys_priority.inc"

static void put16(uint8_t *p, uint32_t value) { p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8u); }
static void put32(uint8_t *p, uint32_t value) { put16(p,value); put16(p+2,value>>16u); }
static uint8_t packet[TDMA_TRANSPORT_FRAME_HEADER_SIZE + 4u * 32u + 4u];
static void make_packet(uint32_t sequence) {
    uint8_t payload[4u * 32u + 4u] = {0};
    uint8_t *m=payload;
    put16(m,TDMA_FLIGHT_MAILBOX_MAGIC); m[2]=TDMA_FLIGHT_MAILBOX_VERSION;
    m[3]=TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS; m[4]=0u; m[5]=15u;
    put16(m+6u, sequence+40u); m[8]=7u; m[9]=16u;
    put32(m+10u, 0x12345678u); put16(m+30u,tdma_process_image_crc16_ccitt(m,30u));
    tdma_transport_frame_build_t b={.frame_class=TDMA_TRANSPORT_FRAME_CLASS_SHORT,
        .origin_slot_id=0u,.transport_sequence=sequence,.payload_class=TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
        .flags=TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE,.schedule_crc32=0x1234u,
        .ring_profile_crc32=0x5678u,.hop_limit=4u,.payload=payload,.payload_size=sizeof(payload)};
    size_t size; tdma_transport_result_t result;
    assert(tdma_transport_frame_encode(&b,packet,sizeof(packet),&size,&result)); assert(size==sizeof(packet));
}
static void write_wire(uint64_t candidate,uint32_t sequence,uint32_t shift) {
    make_packet(sequence);
    uint8_t wire[4u+sizeof(packet)]={0x54u,0x44u}; put16(wire+2u,sizeof(packet)); memcpy(wire+4u,packet,sizeof(packet));
    uint8_t shifted[sizeof(wire)+1u]; memset(shifted,0,sizeof(shifted));
    if (!shift) memcpy(shifted,wire,sizeof(wire));
    else for(unsigned i=0;i<sizeof(wire);++i) { shifted[i] |= wire[i]>>shift; shifted[i+1] |= (uint8_t)(wire[i]<<(8u-shift)); }
    for(unsigned i=0;i<sizeof(shifted);++i) raw_ring[(candidate+i)&1023u]=reverse(shifted[i]);
}
static void produced(uint32_t words) {
    dma_hw->ch[0].transfer_count=(1u<<28u)|(s_tdma_pio_spi_rx_sequence.reload_words-words);
    timer1_hw->timerawl += 10000u;
}
static void open_irq(void) { tdma_pio_spi_phys_priority_rx_window_core1(true,8u,timer1_hw->timerawl+1000000u); }
static void invoke(void) {
    assert(handler && irq_enabled); pio_instance.pending=true;
    const uint32_t foreground=watchdog_hw->scratch[6]; handler();
    assert(watchdog_hw->scratch[6]==foreground);
}

static void trace_begin(uint32_t foreground) {
    watchdog_hw->scratch[6]=foreground;
    progress_trace_count=0u; progress_trace_enabled=true;
}
static void trace_check(const uint32_t *expected, uint32_t count) {
    progress_trace_enabled=false;
    assert(progress_trace_count==count);
    assert(!memcmp(progress_trace,expected,count*sizeof(*expected)));
    assert(watchdog_hw->scratch[6]==expected[count-1u]);
}

static unsigned sink_calls, sink_stops;
static tdma_priority_rx_record_t sink_record;
static void test_sink(const tdma_priority_rx_record_t *record) {
    assert(core == 1u);
    if (!record) { ++sink_stops; return; }
    ++sink_calls;
    sink_record = *record;
    tdma_priority_rx_record_t retained;
    assert(tdma_pio_spi_phys_copy_priority_rx_live(record->epoch,record->sequence,&retained));
    assert(!memcmp(record,&retained,sizeof(retained)));
    timer1_hw->timerawl += 100u; /* Must remain charged to the IRQ body. */
}

static void direct_sink_tests(void) {
    tdma_pio_spi_phys_t phys={.armed=true,.flight_payload_size=132u,
        .flight_physical_byte_count=176u,.flight_overlay_alignment_locked=true,
        .flight_alignment_byte_shift=3u,.rx_csn_pin=27u};
    tdma_ring_runtime_config_t config={.reference_slot_id=0u,.local_slot_id=1u,.node_count=4u,
        .schedule_crc32=0x1234u,.ring_profile_crc32=0x5678u,.cycle_period_ns=1000000u};
    assert(tdma_pio_spi_phys_set_priority_rx_sink(test_sink));
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,176u,timer1_hw->timerawl));
    produced(0u); assert(tdma_priority_start(&phys,&config)); open_irq(); tdma_priority_boundary_service(&phys);
    assert(!tdma_pio_spi_phys_set_priority_rx_sink(NULL));
    const uint32_t epoch=s_tdma_priority_rx.status.epoch;
    write_wire(3u,UINT32_MAX,0u); produced(176u); invoke();
    assert(sink_calls==1u && sink_record.epoch==epoch && sink_record.sequence==UINT32_MAX);
    assert(s_tdma_priority_rx.status.irq_last_cycles>=100u);
    invoke(); assert(sink_calls==1u); /* Replayed carrier is not delivered twice. */
    write_wire(179u,0u,0u); produced(352u); invoke();
    assert(sink_calls==2u && sink_record.sequence==0u && sink_record.epoch==epoch+1u);
    gpio_high=false; invoke(); gpio_high=true; assert(sink_calls==2u);
    tdma_priority_stop(); assert(sink_stops==1u);
    assert(tdma_pio_spi_phys_set_priority_rx_sink(NULL));
}

static void breadcrumb_tests(void) {
    tdma_pio_spi_phys_t phys={.armed=true,.flight_payload_size=132u,
        .flight_physical_byte_count=176u,.flight_overlay_alignment_locked=true,
        .flight_alignment_byte_shift=3u,.rx_csn_pin=27u};
    tdma_ring_runtime_config_t config={.reference_slot_id=0u,.local_slot_id=1u,.node_count=4u,
        .schedule_crc32=0x1234u,.ring_profile_crc32=0x5678u,.cycle_period_ns=1000000u};
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,176u,timer1_hw->timerawl));
    produced(0u); assert(tdma_priority_start(&phys,&config)); open_irq(); tdma_priority_boundary_service(&phys);
    const uint32_t foreground=0x0106u;
    const uint32_t early[]={TDMA_PRIORITY_PROGRESS_ENTRY,foreground};
    pio_instance.pending=false; trace_begin(foreground); handler(); trace_check(early,2u);
    tdma_pio_spi_phys_priority_rx_window_core1(false,0u,0u);
    pio_instance.pending=true; unsigned ack=ack_count;
    trace_begin(foreground); handler(); trace_check(early,2u);
    assert(ack_count==ack && pio_instance.pending && !irq_enabled);
    tdma_pio_spi_phys_priority_rx_window_core1(true,1u,timer1_hw->timerawl);
    trace_begin(foreground); handler(); trace_check(early,2u);
    assert(ack_count==ack && pio_instance.pending && !irq_enabled);
    open_irq(); gpio_high=false;
    const uint32_t rejected[]={TDMA_PRIORITY_PROGRESS_ENTRY,TDMA_PRIORITY_PROGRESS_ACK,
        TDMA_PRIORITY_PROGRESS_FINISH,TDMA_PRIORITY_PROGRESS_FINISHED,foreground};
    trace_begin(foreground); invoke(); trace_check(rejected,5u); gpio_high=true;
    write_wire(3u,37u,0u); produced(176u);
    const uint32_t accepted[]={TDMA_PRIORITY_PROGRESS_ENTRY,TDMA_PRIORITY_PROGRESS_ACK,
        TDMA_PRIORITY_PROGRESS_DMA_BEFORE,TDMA_PRIORITY_PROGRESS_DMA_AFTER,
        TDMA_PRIORITY_PROGRESS_CANDIDATE,TDMA_PRIORITY_PROGRESS_COPY,
        TDMA_PRIORITY_PROGRESS_RECHECK,TDMA_PRIORITY_PROGRESS_VALIDATE,
        TDMA_PRIORITY_PROGRESS_VALIDATED,TDMA_PRIORITY_PROGRESS_PUBLISH,
        TDMA_PRIORITY_PROGRESS_PUBLISHED,TDMA_PRIORITY_PROGRESS_FINISH,
        TDMA_PRIORITY_PROGRESS_FINISHED,foreground};
    trace_begin(foreground); invoke(); trace_check(accepted,14u);
    tdma_priority_rx_snapshot_t snapshot;
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snapshot));
    assert(snapshot.publish_count==1u && snapshot.latest_sequence==37u);
    tdma_priority_stop();
}

static void node_geometry_matrix(void) {
    const uint32_t nodes[] = {4u,5u,6u,8u};
    for (uint32_t n=0u;n<sizeof(nodes)/sizeof(nodes[0]);++n) {
        const uint32_t count=nodes[n], reference=count-1u;
        const uint32_t payload_bytes=count*32u+4u;
        const uint32_t packet_bytes=32u+payload_bytes, stride=packet_bytes+12u;
        uint8_t payload[8u*32u+4u]={0};
        uint8_t *mailbox=payload+reference*32u;
        put16(mailbox,TDMA_FLIGHT_MAILBOX_MAGIC); mailbox[2]=TDMA_FLIGHT_MAILBOX_VERSION;
        mailbox[3]=TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS; mailbox[4]=(uint8_t)reference;
        mailbox[5]=(uint8_t)((1u<<count)-1u); put16(mailbox+6u,0x4321u);
        put32(mailbox+10u,0x98765432u); put16(mailbox+30u,tdma_process_image_crc16_ccitt(mailbox,30u));
        uint8_t frame[32u+sizeof(payload)]; size_t encoded_size; tdma_transport_result_t result;
        tdma_transport_frame_build_t build={.frame_class=TDMA_TRANSPORT_FRAME_CLASS_SHORT,
            .origin_slot_id=reference,.transport_sequence=100u+count,
            .payload_class=TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE,
            .flags=TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE,.schedule_crc32=0x1234u,
            .ring_profile_crc32=0x5678u,.hop_limit=count,.payload=payload,.payload_size=payload_bytes};
        assert(tdma_transport_frame_encode(&build,frame,sizeof(frame),&encoded_size,&result));
        assert(encoded_size==packet_bytes);
        for (uint32_t shift=0u;shift<8u;++shift) {
            tdma_pio_spi_phys_t phys={.armed=true,.flight_payload_size=payload_bytes,
                .flight_physical_byte_count=stride,.flight_overlay_alignment_locked=true,
                .flight_alignment_byte_shift=3u,.flight_alignment_bit_shift=shift,.rx_csn_pin=27u};
            tdma_ring_runtime_config_t config={.reference_slot_id=reference,.local_slot_id=0u,
                .node_count=count,.schedule_crc32=0x1234u,.ring_profile_crc32=0x5678u,
                .cycle_period_ns=1000000u};
            assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,stride,timer1_hw->timerawl));
            produced(0u); assert(tdma_priority_start(&phys,&config)); open_irq(); tdma_priority_boundary_service(&phys);
            /* The completed packet straddles SRAM ring end for each geometry. */
            const uint32_t candidate=3u+(TDMA_PIO_SPI_RX_RING_WORDS/stride)*stride;
            const uint32_t produced_words=(candidate-3u)+stride;
            uint8_t wire[4u+sizeof(frame)]={0x54u,0x44u}; put16(wire+2u,packet_bytes);
            memcpy(wire+4u,frame,packet_bytes);
            uint8_t shifted[sizeof(wire)+1u]={0};
            if (!shift) memcpy(shifted,wire,4u+packet_bytes);
            else for (uint32_t i=0u;i<4u+packet_bytes;++i) {
                shifted[i]|=wire[i]>>shift;
                shifted[i+1u]|=(uint8_t)(wire[i]<<(8u-shift));
            }
            for (uint32_t i=0u;i<5u+packet_bytes;++i)
                raw_ring[(candidate+i)&1023u]=reverse(shifted[i]);
            produced(produced_words); invoke();
            tdma_priority_rx_snapshot_t snapshot; tdma_priority_rx_record_t record;
            assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snapshot));
            if (count>TDMA_FLIGHT_SHORT_SLOT_COUNT) {
                assert(!snapshot.publish_count && snapshot.last_reject==TDMA_PRIORITY_RX_HEADER);
                tdma_priority_stop();
                continue;
            }
            if (snapshot.publish_count!=1u || snapshot.latest_sequence!=100u+count)
                fprintf(stderr,"geometry nodes=%u shift=%u reject=%u publish=%u sequence=%u\n",
                    count,shift,snapshot.last_reject,snapshot.publish_count,snapshot.latest_sequence);
            assert(snapshot.publish_count==1u && snapshot.latest_sequence==100u+count);
            assert(tdma_pio_spi_phys_copy_priority_rx(snapshot.epoch,100u+count,&record));
            assert(record.candidate_word==candidate && !memcmp(record.header,frame,32u));
            assert(!memcmp(record.mailbox,mailbox,32u));
            tdma_priority_stop();
        }
    }
}

static tdma_priority_rx_record_t sequence_record(uint32_t epoch, uint32_t sequence) {
    make_packet(sequence);
    tdma_priority_rx_record_t record={.epoch=epoch,.sequence=sequence};
    memcpy(record.header,packet,32u); memcpy(record.mailbox,packet+32u,32u);
    return record;
}

static void live_copy_lifetime_tests(void) {
    tdma_priority_rx_t lane={0};
    tdma_priority_rx_record_t sentinel;
    memset(&sentinel,0xa5,sizeof(sentinel));
    tdma_priority_rx_record_t out=sentinel;
    assert(!tdma_priority_rx_copy_live(NULL,1u,0u,&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    assert(tdma_priority_rx_start(&lane));
    uint32_t epoch=lane.status.epoch;
    assert(!tdma_priority_rx_copy_live(&lane,epoch,0u,&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    tdma_priority_rx_record_t record=sequence_record(epoch,7u);
    assert(tdma_priority_rx_publish(&lane,&record));
    assert(tdma_priority_rx_copy_live(&lane,epoch,7u,&out));
    assert(!memcmp(&out,&record,sizeof(out)));
    const tdma_priority_rx_t before=lane;
    assert(tdma_priority_rx_copy_live(&lane,epoch,7u,&out));
    assert(!memcmp(&lane,&before,sizeof(lane))); /* Read, never dequeue. */
    out=sentinel;
    assert(!tdma_priority_rx_copy_live(&lane,0u,7u,&out));
    assert(!tdma_priority_rx_copy_live(&lane,epoch+1u,7u,&out));
    assert(!tdma_priority_rx_copy_live(&lane,epoch,7u,NULL));
    assert(!memcmp(&out,&sentinel,sizeof(out)));

    /* STOP preserves diagnostics but revokes the same exact live record. */
    tdma_priority_rx_stop(&lane);
    assert(!tdma_priority_rx_copy_live(&lane,epoch,7u,&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    assert(tdma_priority_rx_copy(&lane,epoch,7u,&out));
    assert(!memcmp(&out,&record,sizeof(out)));
    assert(tdma_priority_rx_start(&lane));
    out=sentinel;
    assert(!tdma_priority_rx_copy_live(&lane,epoch,7u,&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    epoch=lane.status.epoch;
    record=sequence_record(epoch,7u);
    assert(tdma_priority_rx_publish(&lane,&record));
    assert(tdma_priority_rx_copy_live(&lane,epoch,7u,&out));

    /* A later carrier in the same modulo slot is not the requested record. */
    record=sequence_record(epoch,7u+TDMA_PRIORITY_RX_CAPACITY);
    assert(tdma_priority_rx_publish(&lane,&record));
    out=sentinel;
    assert(!tdma_priority_rx_copy_live(&lane,epoch,7u,&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    assert(tdma_priority_rx_copy_live(&lane,epoch,record.sequence,&out));
    assert(!memcmp(&out,&record,sizeof(out)));
    const uint32_t stable_guard=lane.guard;
    lane.guard|=1u;
    out=sentinel;
    assert(!tdma_priority_rx_copy_live(&lane,epoch,record.sequence,&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    lane.guard=stable_guard;
    assert(tdma_priority_rx_copy_live(&lane,epoch,record.sequence,&out));

    /* Observer rebase retires every old ID without requiring STOP. */
    assert(tdma_priority_rx_rebase(&lane));
    out=sentinel;
    assert(!tdma_priority_rx_copy_live(&lane,epoch,record.sequence,&out));
    assert(!tdma_priority_rx_copy_live(&lane,lane.status.epoch,record.sequence,&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    epoch=lane.status.epoch;
    record=sequence_record(epoch,UINT32_MAX);
    assert(tdma_priority_rx_publish(&lane,&record));
    assert(tdma_priority_rx_copy_live(&lane,epoch,UINT32_MAX,&out));
    record=sequence_record(epoch,0u);
    assert(tdma_priority_rx_publish(&lane,&record));
    out=sentinel;
    assert(!tdma_priority_rx_copy_live(&lane,epoch,UINT32_MAX,&out));
    assert(!tdma_priority_rx_copy_live(&lane,epoch,0u,&out));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    assert(tdma_priority_rx_copy_live(&lane,lane.status.epoch,0u,&out));
    assert(out.epoch==epoch+1u && out.sequence==0u);
}

static void wrap_lifetime_tests(void) {
    tdma_priority_rx_t lane={0}; tdma_priority_rx_snapshot_t snapshot;
    tdma_priority_rx_record_t out, record;
    assert(tdma_priority_rx_start(&lane));
    const uint32_t first_epoch=lane.status.epoch;
    record=sequence_record(first_epoch,UINT32_MAX-1u);
    assert(tdma_priority_rx_publish(&lane,&record));
    lane.guard=UINT32_MAX-3u;
    assert(tdma_priority_rx_snapshot(&lane,&snapshot) && snapshot.active);
    record=sequence_record(first_epoch,UINT32_MAX);
    assert(tdma_priority_rx_publish(&lane,&record) && lane.guard==UINT32_MAX-1u);
    assert(tdma_priority_rx_copy(&lane,first_epoch,UINT32_MAX,&out));
    tdma_priority_rx_finish_irq(&lane,123u,9u);
    assert(lane.guard==0u && tdma_priority_rx_snapshot(&lane,&snapshot));
    assert(snapshot.irq_count==1u && snapshot.irq_total_cycles==9u);
    /* Odd guard still rejects both kinds of reader at the wrap boundary. */
    lane.guard=UINT32_MAX;
    assert(!tdma_priority_rx_snapshot(&lane,&snapshot));
    assert(!tdma_priority_rx_copy(&lane,first_epoch,UINT32_MAX,&out));
    lane.guard=0u;
    record=sequence_record(first_epoch,0u);
    assert(tdma_priority_rx_publish(&lane,&record));
    assert(tdma_priority_rx_snapshot(&lane,&snapshot));
    assert(snapshot.epoch==first_epoch+1u && snapshot.retained_mask==1u);
    assert(snapshot.irq_count==1u && snapshot.irq_total_cycles==9u && snapshot.publish_count==3u);
    assert(!snapshot.sequence_gap_count && snapshot.latest_sequence==0u);
    assert(!tdma_priority_rx_copy(&lane,first_epoch,UINT32_MAX,&out));
    assert(!tdma_priority_rx_copy(&lane,first_epoch,0u,&out));
    assert(tdma_priority_rx_copy(&lane,snapshot.epoch,0u,&out) && out.epoch==snapshot.epoch);
    record=sequence_record(first_epoch,1u); assert(!tdma_priority_rx_publish(&lane,&record));
    record=sequence_record(snapshot.epoch,1u); assert(tdma_priority_rx_publish(&lane,&record));
    record.irq_entry_ticks=99u; assert(!tdma_priority_rx_publish(&lane,&record));
    record.mailbox[29]^=1u;
    put16(record.mailbox+30u,tdma_process_image_crc16_ccitt(record.mailbox,30u));
    assert(!tdma_priority_rx_publish(&lane,&record)); /* Last compared word differs. */
    record=sequence_record(snapshot.epoch,UINT32_MAX); assert(!tdma_priority_rx_publish(&lane,&record));
    record=sequence_record(snapshot.epoch,0x80000001u); assert(!tdma_priority_rx_publish(&lane,&record));
    tdma_priority_rx_finish_irq(&lane,124u,10u);
    assert(tdma_priority_rx_snapshot(&lane,&snapshot) && snapshot.last_reject==TDMA_PRIORITY_RX_SEQUENCE);
    assert(snapshot.latest_sequence==1u && snapshot.reject_count==3u && snapshot.duplicate_count==1u);
    lane.guard=UINT32_MAX-1u;
    tdma_priority_rx_stop(&lane);
    assert(lane.guard==0u && tdma_priority_rx_snapshot(&lane,&snapshot) && !snapshot.active);
    assert(tdma_priority_rx_copy(&lane,snapshot.epoch,1u,&out));
    assert(tdma_priority_rx_start(&lane));
    assert(!tdma_priority_rx_copy(&lane,snapshot.epoch,1u,&out));
    record=sequence_record(lane.status.epoch,UINT32_MAX-1u); assert(tdma_priority_rx_publish(&lane,&record));
    record=sequence_record(lane.status.epoch,1u); assert(tdma_priority_rx_publish(&lane,&record));
    assert(tdma_priority_rx_snapshot(&lane,&snapshot) && snapshot.sequence_gap_count==2u);
    assert(tdma_priority_rx_copy(&lane,snapshot.epoch,1u,&out));
    /* Namespace exhaustion is explicit, readable, and never aliases IDs. */
    tdma_priority_rx_t exhausted={0}; exhausted.status.epoch=UINT32_MAX-1u;
    assert(tdma_priority_rx_start(&exhausted));
    record=sequence_record(UINT32_MAX,UINT32_MAX); assert(tdma_priority_rx_publish(&exhausted,&record));
    record=sequence_record(UINT32_MAX,0u); assert(!tdma_priority_rx_publish(&exhausted,&record));
    assert(tdma_priority_rx_snapshot(&exhausted,&snapshot));
    assert(!snapshot.active && snapshot.last_reject==TDMA_PRIORITY_RX_EXHAUSTED && snapshot.epoch==UINT32_MAX);
    assert(tdma_priority_rx_copy(&exhausted,UINT32_MAX,UINT32_MAX,&out));
    const tdma_priority_rx_record_t retained=out;
    assert(!tdma_priority_rx_copy_live(&exhausted,UINT32_MAX,UINT32_MAX,&out));
    assert(!memcmp(&out,&retained,sizeof(out)));
    assert(!tdma_priority_rx_start(&exhausted));
    /* The physical producer acquires the new epoch on the next IRQ. */
    tdma_pio_spi_phys_t phys={.armed=true,.flight_payload_size=132u,
        .flight_physical_byte_count=176u,.flight_overlay_alignment_locked=true,
        .flight_alignment_byte_shift=3u,.rx_csn_pin=27u};
    tdma_ring_runtime_config_t config={.reference_slot_id=0u,.local_slot_id=1u,.node_count=4u,
        .schedule_crc32=0x1234u,.ring_profile_crc32=0x5678u,.cycle_period_ns=1000000u};
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,176u,timer1_hw->timerawl));
    produced(0u); assert(tdma_priority_start(&phys,&config)); open_irq(); tdma_priority_boundary_service(&phys);
    write_wire(3u,UINT32_MAX,0u); produced(176u); invoke();
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snapshot));
    const uint32_t physical_epoch=snapshot.epoch;
    assert(snapshot.latest_sequence==UINT32_MAX && snapshot.publish_count==1u);
    write_wire(179u,0u,0u); produced(352u); invoke();
    write_wire(355u,1u,0u); produced(528u); invoke();
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snapshot));
    assert(snapshot.epoch==physical_epoch+1u && snapshot.latest_sequence==1u);
    assert(snapshot.publish_count==3u && snapshot.irq_count==3u);
    assert(!tdma_pio_spi_phys_copy_priority_rx(physical_epoch,UINT32_MAX,&out));
    assert(tdma_pio_spi_phys_copy_priority_rx(snapshot.epoch,0u,&out));
    assert(tdma_pio_spi_phys_copy_priority_rx(snapshot.epoch,1u,&out));
    tdma_priority_stop();
}

static void timing_tests(void) {
    _Static_assert(sizeof(tdma_priority_rx_timing_t)==20u,"bounded diagnostic API");
    _Static_assert(sizeof(s_tdma_priority_timing)==24u,"bounded diagnostic RAM");
    tdma_pio_spi_phys_t phys={.armed=true,.flight_payload_size=132u,
        .flight_physical_byte_count=176u,.flight_overlay_alignment_locked=true,
        .flight_alignment_byte_shift=3u,.rx_csn_pin=27u};
    tdma_ring_runtime_config_t config={.reference_slot_id=0u,.local_slot_id=1u,.node_count=4u,
        .schedule_crc32=0x1234u,.ring_profile_crc32=0x5678u,.cycle_period_ns=1000000u};
    tdma_pio_spi_phys_priority_rx_window_core1(false,0u,0u);
    timer1_hw->timerawh=0u; timer1_hw->timerawl=1000u;
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,176u,timer1_hw->timerawl));
    produced(0u); assert(tdma_priority_start(&phys,&config)); open_irq(); tdma_priority_boundary_service(&phys);
    tdma_priority_rx_timing_t timing;
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing));
    assert(!timing.samples && !timing.max_cycles[0] && !timing.max_cycles[1] &&
        !timing.max_cycles[2] && !timing.max_cycles[3]);
    write_wire(3u,1u,0u); produced(176u); progress_tick_step=10u; invoke(); progress_tick_step=0u;
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing));
    assert(timing.samples==1u && timing.max_cycles[0]==50u && timing.max_cycles[1]==10u &&
        timing.max_cycles[2]==20u && timing.max_cycles[3]==20u);
    write_wire(179u,2u,0u); produced(352u); progress_tick_step=20u; invoke(); progress_tick_step=0u;
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing));
    assert(timing.samples==2u && timing.max_cycles[0]==100u && timing.max_cycles[1]==20u &&
        timing.max_cycles[2]==40u && timing.max_cycles[3]==40u);
    write_wire(355u,3u,0u); produced(528u); progress_tick_step=5u; invoke(); progress_tick_step=0u;
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing)); assert(timing.samples==3u);
    const tdma_priority_rx_timing_t accepted=timing;
    /* Duplicate, invalid CRC and early GPIO reject never publish a partial sample. */
    progress_tick_step=100u; invoke(); gpio_high=false; invoke(); gpio_high=true;
    write_wire(531u,4u,0u); raw_ring[(531u+4u+32u+10u)&1023u]^=0x80000000u;
    produced(704u); invoke(); progress_tick_step=0u;
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing));
    assert(!memcmp(&timing,&accepted,sizeof(timing)));
    assert(!tdma_pio_spi_phys_get_priority_rx_timing(NULL));
    s_tdma_priority_timing.guard|=1u;
    assert(!tdma_pio_spi_phys_get_priority_rx_timing(&timing));
    assert(!memcmp(&timing,&accepted,sizeof(timing))); ++s_tdma_priority_timing.guard;
    tdma_priority_stop(); core=0u;
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing));
    assert(!memcmp(&timing,&accepted,sizeof(timing))); core=1u;
    assert(tdma_priority_start(&phys,&config));
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing));
    assert(!timing.samples && !timing.max_cycles[0] && !timing.max_cycles[1] &&
        !timing.max_cycles[2] && !timing.max_cycles[3]);
    tdma_priority_stop();
    /* Low32 rollover is valid; non-monotonic partitions are rejected. */
    const uint32_t wrap_cuts[3]={UINT32_MAX-5u,4u,14u};
    tdma_priority_timing_account(UINT32_MAX-15u,24u,wrap_cuts);
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing));
    assert(timing.samples==1u);
    for (uint32_t i=0u;i<4u;++i) assert(timing.max_cycles[i]==10u);
    const uint32_t invalid_cuts[3]={10u,9u,20u};
    tdma_priority_timing_account(0u,30u,invalid_cuts);
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing)); assert(timing.samples==1u);
    s_tdma_priority_timing.value.samples=UINT32_MAX;
    tdma_priority_timing_account(UINT32_MAX-15u,24u,wrap_cuts);
    assert(tdma_pio_spi_phys_get_priority_rx_timing(&timing)); assert(timing.samples==UINT32_MAX);
}

static void counters_tests(void) {
    _Static_assert(sizeof(tdma_priority_rx_counters_t)==24u,"fixed scheduler counters");
    tdma_pio_spi_phys_t phys={.armed=true,.flight_payload_size=132u,
        .flight_physical_byte_count=176u,.rx_csn_pin=27u};
    tdma_ring_runtime_config_t config={.reference_slot_id=0u,.local_slot_id=1u,.node_count=4u,
        .schedule_crc32=0x1234u,.ring_profile_crc32=0x5678u,.cycle_period_ns=1000000u};
    tdma_pio_spi_phys_priority_rx_window_core1(false,0u,0u);
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,176u,timer1_hw->timerawl));
    produced(0u); assert(tdma_priority_start(&phys,&config)); assert(!irq_enabled);
    /* Exercise a total above 32 bits while the only writer is the owner. */
    tdma_priority_rx_finish_irq(&s_tdma_priority_rx,12u,0xf0000000u);
    tdma_priority_rx_finish_irq(&s_tdma_priority_rx,24u,0xf0000000u);
    tdma_priority_rx_counters_t counters;
    assert(tdma_pio_spi_phys_priority_rx_counters_core1(&counters));
    assert(counters.cycles==UINT64_C(0x1e0000000) && counters.count==2u);
    assert(counters.maximum==0xf0000000u && counters.active==1u);
    assert(counters.epoch==s_tdma_priority_rx.status.epoch);
    const tdma_priority_rx_counters_t before=counters;
    const tdma_priority_rx_t lane_before=s_tdma_priority_rx;
    const unsigned copies=copy_calls, acks=ack_count, enables=irq_enable_calls;
    assert(!tdma_pio_spi_phys_priority_rx_counters_core1(NULL));
    core=0u;
    assert(!tdma_pio_spi_phys_priority_rx_counters_core1(&counters));
    assert(!memcmp(&counters,&before,sizeof(counters))); core=1u;
    assert(tdma_pio_spi_phys_priority_rx_counters_core1(&counters));
    assert(!memcmp(&counters,&before,sizeof(counters)));
    assert(!memcmp(&s_tdma_priority_rx,&lane_before,sizeof(lane_before)));
    assert(copy_calls==copies && ack_count==acks && irq_enable_calls==enables);
    open_irq(); assert(irq_enabled);
    pio_instance.pending=true;
    assert(!tdma_pio_spi_phys_priority_rx_counters_core1(&counters));
    assert(!memcmp(&counters,&before,sizeof(counters)) && pio_instance.pending);
    tdma_pio_spi_phys_priority_rx_window_core1(false,0u,0u);
    assert(tdma_pio_spi_phys_priority_rx_counters_core1(&counters));
    assert(!memcmp(&counters,&before,sizeof(counters)) && pio_instance.pending);
    assert(copy_calls==copies && ack_count==acks);
    tdma_priority_stop();
    assert(tdma_pio_spi_phys_priority_rx_counters_core1(&counters));
    assert(!counters.active && counters.epoch==before.epoch && counters.cycles==before.cycles);
}

int main(void) {
    _Static_assert(sizeof(tdma_priority_rx_t)<700u,"small fixed slots");
    tdma_pio_spi_phys_t phys={.armed=true,.flight_payload_size=132u,.flight_physical_byte_count=176u,
        .flight_overlay_alignment_locked=true,.flight_alignment_byte_shift=3u,.rx_csn_pin=27u};
    tdma_ring_runtime_config_t config={.reference_slot_id=0u,.local_slot_id=1u,.node_count=4u,
        .schedule_crc32=0x1234u,.ring_profile_crc32=0x5678u,.cycle_period_ns=1000000u};
    tdma_priority_rx_snapshot_t snap; tdma_priority_rx_record_t record;
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,176u,0u));
    produced(0u); assert(tdma_priority_start(&phys,&config)); assert(!irq_enabled && handler);
    open_irq(); tdma_priority_boundary_service(&phys);
    /* Real counter deadline: first physical START long after ARM. The first
     * event is explicitly invalid; the next completed frame must recover. */
    write_wire(3u,9u,0u); produced(176u);
    timer1_hw->timerawl += s_tdma_pio_spi_rx_sequence.reload_words + 100u;
    open_irq(); invoke();
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap));
    assert(snap.active && snap.epoch==2u && !snap.retained_mask && snap.last_reject==TDMA_PRIORITY_RX_DMA);
    write_wire(179u,10u,0u); produced(352u); invoke();
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(snap.publish_count==1u && snap.latest_sequence==10u);
    assert(tdma_pio_spi_phys_copy_priority_rx(snap.epoch,10u,&record));
    assert(tdma_pio_spi_phys_copy_priority_rx_live(snap.epoch,10u,&record));
    assert(!memcmp(record.header,packet,32u) && !memcmp(record.mailbox,packet+32u,32u));
    assert(record.irq_entry_ticks && record.candidate_word==179u);
    assert(s_tdma_pio_spi_rx_sequence.produced_words==0u); /* Private ISR counter. */
    unsigned ack=ack_count; tdma_priority_boundary_service(&phys); assert(ack_count==ack && phys.snapshot.overlay_frame_boundary_count==2u);
    invoke(); assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(snap.duplicate_count==1u);
    write_wire(355u,11u,0u); raw_ring[(355u+4u+32u+10u)&1023u]^=0x80000000u; produced(528u); invoke();
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(snap.last_reject==TDMA_PRIORITY_RX_MAILBOX_CRC && snap.publish_count==1u);
    write_wire(531u,12u,0u); produced(704u); invoke();
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(snap.latest_sequence==12u && snap.sequence_gap_count==1u);
    write_wire(707u,16u,0u); produced(880u); invoke();
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(snap.overwrite_count==1u);
    assert(!tdma_pio_spi_phys_copy_priority_rx(snap.epoch,12u,&record));
    tdma_pio_spi_phys_priority_rx_window_core1(false,0u,0u); assert(!irq_enabled);
    pio_instance.pending=true; ack=ack_count; handler(); assert(ack_count==ack && pio_instance.pending);
    tdma_pio_spi_phys_priority_rx_window_core1(true,1u,timer1_hw->timerawl); handler(); assert(!irq_enabled && pio_instance.pending);
    open_irq(); gpio_high=false; invoke(); gpio_high=true;
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(snap.last_reject==TDMA_PRIORITY_RX_INCOMPLETE);
    write_wire(883u,17u,0u); produced(1056u); overwrite_on_copy=true; invoke(); overwrite_on_copy=false;
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(snap.last_reject==TDMA_PRIORITY_RX_OVERWRITTEN);
    const uint32_t old_epoch=snap.epoch; tdma_priority_stop(); assert(!irq_enabled && !handler && !pio_instance.inte0);
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(!snap.active && snap.latest_sequence==16u);
    assert(tdma_pio_spi_phys_copy_priority_rx(old_epoch,16u,&record));
    const tdma_priority_rx_record_t retained=record;
    assert(!tdma_pio_spi_phys_copy_priority_rx_live(old_epoch,16u,&record));
    assert(!memcmp(&record,&retained,sizeof(record)));
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,176u,timer1_hw->timerawl)); produced(0u);
    phys.flight_alignment_bit_shift=3u; assert(tdma_priority_start(&phys,&config)); open_irq(); tdma_priority_boundary_service(&phys);
    assert(!tdma_pio_spi_phys_copy_priority_rx(old_epoch,16u,&record));
    assert(!tdma_pio_spi_phys_copy_priority_rx_live(old_epoch,16u,&record));
    assert(!memcmp(&record,&retained,sizeof(record)));
    write_wire(3u,20u,3u); produced(176u); invoke();
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(snap.publish_count==1u && snap.latest_sequence==20u);
    tdma_pio_spi_phys_priority_rx_window_core1(true,1u,timer1_hw->timerawl+10000u); invoke(); assert(!irq_enabled);
    tdma_priority_stop(); core=0u; assert(!tdma_priority_start(&phys,&config));
    unsigned enables=irq_enable_calls; tdma_pio_spi_phys_priority_rx_window_core1(true,2u,1000u); assert(enables==irq_enable_calls); core=1u;
    config.cycle_period_ns=100000u; assert(tdma_priority_start(&phys,&config));
    assert(tdma_pio_spi_phys_get_priority_rx_snapshot(&snap)); assert(!snap.active && snap.last_reject==TDMA_PRIORITY_RX_CADENCE);
    pio_instance.pending=true; ack=ack_count; tdma_priority_boundary_service(&phys); assert(ack_count==ack+1u);
    config.cycle_period_ns=1000000u; shared_handler=true; assert(!tdma_priority_start(&phys,&config)); shared_handler=false;
    tdma_priority_rx_t pure={0}; assert(tdma_priority_rx_start(&pure));
    make_packet(0xffffffffu); record=(tdma_priority_rx_record_t){.epoch=pure.status.epoch,.sequence=0xffffffffu};
    memcpy(record.header,packet,32u); memcpy(record.mailbox,packet+32u,32u);
    tdma_priority_rx_binding_t binding={.packet_bytes=sizeof(packet),.reference_slot=0u,.local_slot=1u,
        .node_count=4u,.schedule_crc32=0x1234u,.profile_crc32=0x5678u};
    assert(tdma_priority_rx_validate(&binding,&record)==TDMA_PRIORITY_RX_OK);
    /* Typed synchronization is admitted by the priority ingress only; the
     * ordinary process-image class validator still rejects it. */
    record.mailbox[3]=TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS;
    put16(record.mailbox+30u,tdma_process_image_crc16_ccitt(record.mailbox,30u));
    assert(!tdma_process_image_transport_class_valid(record.mailbox[3]));
    assert(tdma_process_image_typed_sync_class_valid(record.mailbox[3]));
    assert(tdma_priority_rx_validate(&binding,&record)==TDMA_PRIORITY_RX_OK);
    record.mailbox[3]=TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS;
    put16(record.mailbox+30u,tdma_process_image_crc16_ccitt(record.mailbox,30u));
    record.header[24]^=1u; uint32_t crc; assert(tdma_transport_frame_calculate_transport_crc32(record.header,32u,&crc)); put32(record.header+28u,crc);
    assert(tdma_priority_rx_validate(&binding,&record)==TDMA_PRIORITY_RX_HEADER_CRC); record.header[24]^=1u;
    assert(tdma_priority_rx_publish(&pure,&record));
    live_copy_lifetime_tests();
    wrap_lifetime_tests();
    uint64_t candidate; assert(!tdma_priority_rx_candidate(67u,176u,68u,0u,0u,1024u,&candidate));
    assert(tdma_priority_rx_candidate(200000176u,176u,68u,3u,0u,1024u,&candidate));
    assert(candidate%176u==3u && candidate+68u<=200000176u);
    node_geometry_matrix();
    breadcrumb_tests();
    counters_tests();
    timing_tests();
    direct_sink_tests();
    printf("priority ingress: 24 case groups passed; lane=%zu record=%zu snapshot=%zu\n",sizeof(pure),sizeof(record),sizeof(snap));
    return 0;
}
'''
