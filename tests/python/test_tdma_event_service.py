"""Run the production TDMA phase path while receive preparation skips capture.

Only register/FIFO and unrelated origin/parser boundaries are simulated. The
actual component -> runtime owner -> physical service -> observer/core chain
executes, together with the actual RX queue/preparation early-return branches.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def source_fixture() -> str:
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    end = header.index("} tdma_pio_spi_event_snapshot_t;") + len("} tdma_pio_spi_event_snapshot_t;")
    snapshot = header[header.rfind("typedef struct {", 0, end):end]
    job_header = (ROOT / "components/tdma/inc/tdma_rx_prepare.h").read_text(encoding="utf-8")
    states = re.search(r"typedef enum \{.*?\} tdma_rx_prepare_state_t;", job_header, re.S).group(0)
    return PREFIX + snapshot + "\n" + states + FIXTURE


def production_routines() -> str:
    functions = [
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "bool", "tdma_pio_spi_phys_event_selected", "const tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "uint64_t", "tdma_event_cycles", "uint64_t us, bool upper"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "uint32_t", "tdma_event_faults", "tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "void", "tdma_event_publish_state", "tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "void", "tdma_pio_spi_phys_event_service", "tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_pio_spi_phys.c", "bool", "tdma_pio_spi_phys_capture_words", "tdma_pio_spi_phys_t *phys, size_t max_words, size_t *received_words"),
        ("components/tdma/src/tdma_pio_spi_phys_flight_io.inc", "void", "tdma_pio_spi_phys_service_tx", "void *context, uint64_t now_ns"),
        ("components/tdma/src/tdma_runtime_owner.c", "void", "tdma_runtime_owner_service_phys_tx", "uint64_t now_ns"),
        ("components/tdma/src/tdma_rx_prepare.c", "uint32_t", "tdma_rx_prepare_state", "const tdma_rx_prepare_t *job"),
        ("components/tdma/src/tdma_pio_spi_ring_adapter.c", "bool", "tdma_pio_spi_ring_adapter_rx_once_impl", "tdma_pio_spi_ring_adapter_t *adapter"),
        ("components/vdc_dpll_manager/src/vdc_dpll_manager.c", "void", "tdma_component_core1_service", "void"),
    ]
    return "\n".join(f"static {result} {name}({args}) {{" +
                     c_definition_body((ROOT / path).read_text(encoding="utf-8"), name) + "}\n"
                     for path, result, name, args in functions)


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tdma_event_observer.h"
typedef unsigned uint;
'''

FIXTURE = r'''
enum { TDMA_EVENT_RX_SM=1, TDMA_EVENT_TX_SM=2, TDMA_EVENT_SEQUENCE_SM=3,
    TDMA_EVENT_SM_MASK=14, PIO_FDEBUG_RXSTALL_LSB=0, tdma_event_sequence_offset_bad=8,
    TDMA_PIO_SPI_ROLE_MASTER=0, TDMA_PIO_SPI_ROLE_SLAVE=1,
    TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL=1, TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN=11,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER=13, clk_sys=0,
    TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY=1, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET=2,
    TDMA_PIO_SPI_PHYS_ERROR_NONE=0, TDMA_PIO_SPI_PACKET_HEADER_SIZE=4,
    TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS=512, TDMA_TRANSPORT_SHORT_PACKET_MAX=512,
    TDMA_TIMING_RX_CAPTURE, TDMA_TIMING_RX_REQUEST, TDMA_TIMING_PHYS_SERVICE,
    TDMA_TIMING_OWNER_SERVICE, TDMA_TIMING_REFMEM_PUBLISH, TDMA_TIMING_TRAINING_GATE };
typedef struct { uint32_t ctrl, fdebug, fifo[4][8], level[4], pc[4]; } bank_t;
typedef bank_t *PIO;
typedef struct {
    bool armed, flight_tx_pending, flight_tx_completion_pending;
    uint32_t role, flight_tx_packet_size, flight_tx_wire_bytes;
    uint64_t flight_tx_deadline_ns, flight_tx_completion_timestamp_ns, flight_tx_launch_timestamp_ns;
    void *rx_scan_preparation;
    struct { PIO tx_pio; } flight_resources;
    uint32_t flight_event_guard;
    tdma_pio_spi_event_snapshot_t flight_event_alternate;
    struct { tdma_pio_spi_event_snapshot_t event;
        uint32_t tx_timeout_count, origin_clock_timeout_count, origin_data_timeout_count;
        uint32_t origin_done_irq_count, origin_done_txstall_count, tx_count, tx_edge_count;
        uint32_t last_tx_size, last_error;
        uint64_t last_tx_edge_timestamp_ns, last_tx_done_timestamp_ns;
    } snapshot;
} tdma_pio_spi_phys_t;
typedef struct { uint32_t dummy; } tdma_origin_observation_t;
typedef struct { uint32_t state; uint64_t capture_service_ns; uint8_t packet[512]; } tdma_rx_prepare_t;
typedef struct {
    tdma_rx_prepare_t *rx_preparation;
    uint64_t last_service_ns;
    uint32_t rx_queue_count;
    bool (*phys_rx)(void *, uint8_t *, size_t, size_t *, uint64_t *);
    void *phys_context, *phys_ctrl_context;
    struct { uint32_t active; } origin;
    struct { bool (*take_rx_observation)(void *, tdma_origin_observation_t *); } phys_origin;
} tdma_pio_spi_ring_adapter_t;
static bank_t bank;
static tdma_pio_spi_phys_t s_tdma_pio_spi_phys;
static tdma_pio_spi_ring_adapter_t adapter;
static tdma_rx_prepare_t job;
static tdma_event_observer_t s_tdma_event_observer;
static tdma_pio_spi_event_snapshot_t s_tdma_event_snapshot;
static tdma_event_batch_t s_tdma_event_batch;
static tdma_event_record_t s_tdma_event_records[TDMA_EVENT_MAX_RECORDS];
static uint32_t s_tdma_event_hz=125000000u, s_tdma_event_sequence_offset=20u;
static uint64_t s_tdma_event_base_us, s_tdma_event_last_service_us, now_us;
static bool s_tdma_event_waiting, s_tdma_runtime_owner_initialized;
static unsigned s_tdma_pio_spi_program_persona;
static int s_tdma_pio_spi_tx_dma_channel=-1, service_instance;
static int *s_vdc_tdma_service=&service_instance;
static unsigned fifo_reads, fifo_level_reads, capture_calls, queue_calls, accept_calls;
static unsigned owner_lifetimes, observer_disable_calls, refmem_calls, training_calls;
static bool ota_active, skip_phase;
static uint64_t time_us_64(void) { return now_us; }
static uint64_t vdc_dpll_manager_now_ns(void) { return now_us*1000u; }
static uint64_t vdc_timestamp_clock_now_ns(void) { return now_us*1000u; }
static uint64_t tdma_service_timing_now(void) { return now_us; }
static void tdma_service_timing_record(unsigned phase, uint64_t start) { (void)phase; (void)start; }
static void tdma_service_timing_rx_station(uint32_t state, uint64_t now, uint64_t captured) {
    (void)state; (void)now; (void)captured;
}
static uint32_t clock_get_hz(unsigned clock) { (void)clock; return s_tdma_event_hz; }
static uint32_t pio_sm_get_pc(PIO pio, uint sm) { return pio->pc[sm]; }
static uint pio_sm_get_rx_fifo_level(PIO pio, uint sm) {
    assert(sm>0u); ++fifo_level_reads; return pio->level[sm];
}
static uint32_t pio_sm_get(PIO pio, uint sm) {
    assert(sm>0u && pio->level[sm]>0u && pio->level[sm]<=8u);
    const uint32_t word=pio->fifo[sm][0];
    --pio->level[sm];
    memmove(pio->fifo[sm], pio->fifo[sm]+1u, pio->level[sm]*sizeof(uint32_t));
    ++fifo_reads; return word;
}
static bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) { return pio->level[sm]==0u; }
static void pio_set_sm_mask_enabled(PIO pio, uint mask, bool enabled) {
    assert(mask==TDMA_EVENT_SM_MASK && !enabled); pio->ctrl &= ~mask; ++observer_disable_calls;
}
static void tdma_event_start(tdma_pio_spi_phys_t *phys) { (void)phys; assert(false); }
#define __dmb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
static bool tdma_pio_spi_phys_is_flight_persona(void) {
    return s_tdma_pio_spi_program_persona==TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
}
static bool tdma_pio_spi_phys_capture_words_async(tdma_pio_spi_phys_t *phys, size_t max, size_t *received) {
    (void)phys; (void)max; *received=0u; ++capture_calls; return false;
}
static bool tdma_pio_spi_phys_capture_words_legacy(tdma_pio_spi_phys_t *phys, size_t max, size_t *received) {
    return tdma_pio_spi_phys_capture_words_async(phys,max,received);
}
static PIO tdma_pio_spi_phys_control_pio(tdma_pio_spi_phys_t *phys) { return phys->flight_resources.tx_pio; }
static uint tdma_pio_spi_phys_control_sm(tdma_pio_spi_phys_t *phys) { (void)phys; return 0u; }
static uint32_t tdma_pio_spi_phys_txstall_mask(uint sm) { return 1u<<sm; }
static bool pio_interrupt_get(PIO pio, uint irq) { (void)pio; (void)irq; assert(false); return false; }
static bool dma_channel_is_busy(uint channel) { (void)channel; assert(false); return false; }
static void pio_interrupt_clear(PIO pio, uint irq) { (void)pio; (void)irq; assert(false); }
static void tdma_pio_spi_phys_set_error(tdma_pio_spi_phys_t *phys, uint32_t error) { phys->snapshot.last_error=error; }
static void tdma_pio_spi_phys_flight_origin_recover(tdma_pio_spi_phys_t *phys) { (void)phys; assert(false); }
static bool tdma_pio_spi_phys_clock_latch_read_and_rearm(tdma_pio_spi_phys_t *phys, uint64_t *stamp) {
    (void)phys; (void)stamp; assert(false); return false;
}
static void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys) { (void)phys; assert(false); }
static void tdma_runtime_owner_origin_lifetime_core1(void) { ++owner_lifetimes; }
static bool tdma_pio_spi_ring_rx_accept(tdma_pio_spi_ring_adapter_t *a, tdma_rx_prepare_t *j) {
    (void)a; assert(j->state!=TDMA_RX_PREPARE_IDLE); ++accept_calls; return false;
}
static bool tdma_pio_spi_ring_adapter_queue_pop(tdma_pio_spi_ring_adapter_t *a,
    uint8_t *packet, size_t capacity, size_t *size, uint64_t *stamp) {
    (void)a; (void)packet; (void)capacity; (void)size; (void)stamp; ++queue_calls; return false;
}
static bool tdma_pio_spi_ring_adapter_process_rx(tdma_pio_spi_ring_adapter_t *a,
    const uint8_t *packet, size_t size, uint64_t stamp, const tdma_origin_observation_t *observation) {
    (void)a; (void)packet; (void)size; (void)stamp; (void)observation; assert(false); return false;
}
static bool tdma_pio_spi_ring_rx_request(tdma_pio_spi_ring_adapter_t *a, tdma_rx_prepare_t *j,
    size_t size, uint64_t stamp, const tdma_origin_observation_t *observation) {
    (void)a; (void)j; (void)size; (void)stamp; (void)observation; assert(false); return false;
}
static bool tdma_pio_spi_ring_adapter_rx_once_impl(tdma_pio_spi_ring_adapter_t *a);
static void tdma_service_core1_service(int *service) {
    assert(service==s_vdc_tdma_service); assert(!tdma_pio_spi_ring_adapter_rx_once_impl(&adapter));
}
static bool ota_ao_is_active(void) { return ota_active; }
static bool tdma_runtime_owner_skip_tdma_service(void) { return skip_phase; }
static void distributed_refmem_tdma_publish_service(void) { ++refmem_calls; }
static void tdma_runtime_owner_update_training_gate(void) { ++training_calls; }
'''

ASSERTIONS = r'''
static bool capture(void *context,uint8_t *packet,size_t capacity,size_t *size,uint64_t *stamp) {
    (void)packet; (void)stamp;
    return tdma_pio_spi_phys_capture_words(context,capacity/4u,size);
}
static void reset(void) {
    memset(&bank,0,sizeof(bank)); memset(&s_tdma_pio_spi_phys,0,sizeof(s_tdma_pio_spi_phys));
    memset(&s_tdma_event_snapshot,0,sizeof(s_tdma_event_snapshot));
    memset(&adapter,0,sizeof(adapter)); memset(&job,0,sizeof(job));
    now_us=s_tdma_event_base_us=s_tdma_event_last_service_us=0u;
    fifo_reads=fifo_level_reads=capture_calls=queue_calls=accept_calls=0u;
    owner_lifetimes=observer_disable_calls=refmem_calls=training_calls=0u;
    ota_active=skip_phase=s_tdma_event_waiting=false;
    s_tdma_pio_spi_program_persona=TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
    s_tdma_runtime_owner_initialized=true;
    s_tdma_pio_spi_phys.armed=true; s_tdma_pio_spi_phys.role=TDMA_PIO_SPI_ROLE_SLAVE;
    s_tdma_pio_spi_phys.flight_resources.tx_pio=&bank;
    bank.ctrl=15u; bank.pc[3]=20u;
    tdma_event_observer_init(&s_tdma_event_observer);
    const tdma_event_config_t cfg={.pio_hz=125000000u,.epoch_limit_cycles=UINT64_C(7500000000),
        .join_timeout_cycles=750000u,.min_frame_cycles=30000u,.max_tx_delay_cycles=8u};
    assert(tdma_event_observer_start(&s_tdma_event_observer,&cfg,1u,(tdma_event_interval_t){0u,0u},0u));
    adapter.rx_preparation=&job; adapter.phys_rx=capture; adapter.phys_context=&s_tdma_pio_spi_phys;
    job.state=TDMA_RX_PREPARE_BUILDING;
}
static void push(uint sm,uint32_t word) {
    if(bank.level[sm]==8u) { bank.fdebug|=1u<<sm; return; }
    bank.fifo[sm][bank.level[sm]++]=word;
}
static uint32_t wire(uint32_t seq) {
    return seq>>24u | (seq>>8u & 0xff00u) | (seq<<8u & 0xff0000u) | seq<<24u;
}
static void event(uint32_t ordinal) {
    const uint32_t raw=UINT32_MAX-31250u-62517u*ordinal;
    push(1u,raw); push(1u,UINT32_MAX-ordinal);
    push(2u,raw-1u); push(2u,UINT32_MAX-ordinal); push(3u,wire(ordinal+1u));
}
static void test_rx_preparation_and_queue_cannot_starve_observer(void) {
    reset();
    uint32_t emitted=0u;
    for(uint phase=1u;phase<=24u;++phase) {
        now_us=(uint64_t)phase*1500u;
        /* Approx. 1 ms events with a 1.5 ms TDMA owner phase: one/two arrivals.
         * X decrement is independently converted by the production lift. */
        while(62501u+(uint64_t)emitted*125039u < now_us*125u) event(emitted++);
        if(phase<=12u) job.state=TDMA_RX_PREPARE_REQUESTED+(phase%4u);
        else { adapter.rx_preparation=NULL; adapter.rx_queue_count=1u; }
        const unsigned read_before=fifo_reads;
        tdma_component_core1_service();
        assert(s_tdma_event_observer.state==TDMA_EVENT_ACTIVE);
        assert(s_tdma_event_snapshot.published==emitted);
        assert(s_tdma_event_snapshot.service_count==phase);
        assert(s_tdma_event_observer.reads_last<=24u && fifo_reads-read_before<=24u);
        assert(bank.level[1]==0u && bank.level[2]==0u && bank.level[3]==0u && bank.fdebug==0u);
        assert(!s_tdma_pio_spi_phys.flight_tx_pending && capture_calls==0u);
    }
    assert(emitted>24u && accept_calls==12u && queue_calls==12u);
    assert(owner_lifetimes==24u && refmem_calls==24u && training_calls==24u);
    assert(s_tdma_event_snapshot.service_gap_max_us==1500u && observer_disable_calls==0u);
}
static void test_capture_has_no_second_harvest(void) {
    reset(); job.state=TDMA_RX_PREPARE_IDLE;
    for(uint phase=1u;phase<=2u;++phase) {
        now_us=phase*1500u;
        s_tdma_pio_spi_phys.rx_scan_preparation=phase==1u ? &job : NULL;
        tdma_component_core1_service();
        assert(capture_calls==phase && s_tdma_event_snapshot.service_count==phase);
        assert(fifo_level_reads==phase*3u);
    }
}
static void test_not_armed_master_and_uninitialized_are_noops(void) {
    reset(); event(0u); now_us=1000u;
    tdma_pio_spi_phys_service_tx(NULL,now_us*1000u);
    s_tdma_pio_spi_phys.armed=false;
    tdma_pio_spi_phys_service_tx(&s_tdma_pio_spi_phys,now_us*1000u);
    s_tdma_pio_spi_phys.armed=true; s_tdma_pio_spi_phys.role=TDMA_PIO_SPI_ROLE_MASTER;
    tdma_pio_spi_phys_service_tx(&s_tdma_pio_spi_phys,now_us*1000u);
    s_tdma_pio_spi_phys.role=TDMA_PIO_SPI_ROLE_SLAVE;
    s_tdma_pio_spi_program_persona=TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL;
    tdma_pio_spi_phys_service_tx(&s_tdma_pio_spi_phys,now_us*1000u);
    s_tdma_pio_spi_program_persona=TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
    s_tdma_runtime_owner_initialized=false;
    tdma_runtime_owner_service_phys_tx(now_us*1000u);
    assert(fifo_reads==0u && fifo_level_reads==0u && s_tdma_event_snapshot.service_count==0u);
    assert(bank.level[1]==2u && bank.ctrl==15u);
    s_tdma_runtime_owner_initialized=true;
    ota_active=true; tdma_component_core1_service(); ota_active=false;
    skip_phase=true; tdma_component_core1_service(); skip_phase=false;
    assert(fifo_reads==0u && owner_lifetimes==0u); /* Existing explicit owner exclusions preserved. */
}
static void test_stall_remains_permanent_failure(void) {
    reset(); for(uint ordinal=0u;ordinal<5u;++ordinal) event(ordinal);
    assert(bank.fdebug==6u && bank.level[1]==8u && bank.level[2]==8u && bank.level[3]==5u);
    now_us=4500u; tdma_component_core1_service();
    assert(s_tdma_event_observer.state==TDMA_EVENT_INVALID);
    assert(s_tdma_event_observer.reason==TDMA_EVENT_PRE_FAULT);
    assert(s_tdma_event_snapshot.published==0u && observer_disable_calls==1u);
    assert(fifo_reads==21u && bank.ctrl==1u);
    now_us=6000u; tdma_component_core1_service();
    assert(fifo_reads==21u && s_tdma_event_observer.state==TDMA_EVENT_INVALID);
}
int main(void) {
    test_rx_preparation_and_queue_cannot_starve_observer();
    test_capture_has_no_second_harvest();
    test_not_armed_master_and_uninitialized_are_noops();
    test_stall_remains_permanent_failure();
    puts("production TDMA owner: 24 phases, busy RX/queue, single bounded harvest, permanent faults passed");
    return 0;
}
'''


def test_owner_phase_services_observer_independently_of_rx_capture(tmp_path: Path) -> None:
    unit = tmp_path / "event_service.c"
    unit.write_text(source_fixture() + production_routines() + ASSERTIONS, encoding="utf-8")
    gcc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "event_service.exe"
    commands = [[gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-I" + str(ROOT / "components/tdma/inc"), str(unit),
                 str(ROOT / "components/tdma/src/tdma_event_observer.c"), "-o", str(executable)],
                [str(executable)]]
    for index, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True)
        (tmp_path / f"command-{index}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
    assert "24 phases" in result.stdout
