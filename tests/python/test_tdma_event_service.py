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
from test_vdc_command_owner import function_body

ROOT = Path(__file__).resolve().parents[2]


def source_fixture() -> str:
    header = (ROOT / "components/tdma/inc/tdma_pio_spi_phys.h").read_text(encoding="utf-8")
    end = header.index("} tdma_pio_spi_event_snapshot_t;") + len("} tdma_pio_spi_event_snapshot_t;")
    snapshot = header[header.rfind("typedef struct {", 0, end):end]
    recovery_start = header.index("enum {\n    TDMA_EVENT_RECOVERY_NONE")
    recovery_end = header.index("} tdma_pio_spi_event_live_snapshot_t;") + len("} tdma_pio_spi_event_live_snapshot_t;")
    snapshot += "\n" + header[recovery_start:recovery_end]
    job_header = (ROOT / "components/tdma/inc/tdma_rx_prepare.h").read_text(encoding="utf-8")
    states = re.search(r"typedef enum \{.*?\} tdma_rx_prepare_state_t;", job_header, re.S).group(0)
    return PREFIX + snapshot + "\n" + states + FIXTURE


def production_routines() -> str:
    functions = [
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "bool", "tdma_pio_spi_phys_event_selected", "const tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "uint64_t", "tdma_event_cycles", "uint64_t us, bool upper"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "uint32_t", "tdma_event_faults", "tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "void", "tdma_event_publish_snapshot", "tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "void", "tdma_event_candidate_retire", "void"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "void", "tdma_event_publish_state", "tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_pio_spi_phys_event.inc", "void", "tdma_pio_spi_phys_event_service", "tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_pio_spi_phys.c", "void", "tdma_pio_spi_phys_service_observer", "tdma_pio_spi_phys_t *phys"),
        ("components/tdma/src/tdma_runtime_owner.c", "void", "tdma_runtime_owner_service_observer", "void"),
        ("components/tdma/src/tdma_pio_spi_phys.c", "bool", "tdma_pio_spi_phys_capture_words", "tdma_pio_spi_phys_t *phys, size_t max_words, size_t *received_words"),
        ("components/tdma/src/tdma_pio_spi_phys_flight_io.inc", "void", "tdma_pio_spi_phys_service_tx", "void *context, uint64_t now_ns"),
        ("components/tdma/src/tdma_runtime_owner.c", "void", "tdma_runtime_owner_service_phys_tx", "uint64_t now_ns"),
        ("components/tdma/src/tdma_rx_prepare.c", "uint32_t", "tdma_rx_prepare_state", "const tdma_rx_prepare_t *job"),
        ("components/tdma/src/tdma_pio_spi_ring_adapter.c", "bool", "tdma_pio_spi_ring_adapter_rx_capture", "tdma_pio_spi_ring_adapter_t *adapter, uint8_t *packet, size_t packet_capacity"),
        ("components/tdma/src/tdma_pio_spi_ring_adapter.c", "bool", "tdma_pio_spi_ring_adapter_rx_legacy", "tdma_pio_spi_ring_adapter_t *adapter"),
        ("components/tdma/src/tdma_pio_spi_ring_adapter.c", "bool", "tdma_pio_spi_ring_adapter_rx_once_impl", "tdma_pio_spi_ring_adapter_t *adapter"),
        ("components/vdc_dpll_manager/src/vdc_dpll_manager.c", "void", "tdma_component_core1_service", "void"),
    ]
    units = []
    for path, result, name, args in functions:
        source = (ROOT / path).read_text(encoding="utf-8")
        # These helpers retain production's noinline attribute; the shared
        # typed extractor recognizes its placement without rewriting source.
        extract = function_body if name in (
            "tdma_pio_spi_ring_adapter_rx_capture", "tdma_pio_spi_ring_adapter_rx_legacy") else c_definition_body
        units.append(f"static {result} {name}({args}) {{" + extract(source, name) + "}\n")
    return "\n".join(units)


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tdma_event_observer.h"
#include "tdma_event_history.h"
#include "tdma_rx_capture.h"
#include "tdma_transport_frame.h"
#include "tdma_rx_event_candidate.h"
#include "tdma_frozen_geometry.h"
#include "tdma_rx_first_window.h"
#define TDMA_SERVICE_TIMING_ENABLED 1
#include "tdma_service_timing.h"
typedef unsigned uint;
'''

FIXTURE = r'''
enum { TDMA_EVENT_RX_SM=1, TDMA_EVENT_TX_SM=2, TDMA_EVENT_SEQUENCE_SM=3,
    TDMA_EVENT_SM_MASK=14, PIO_FDEBUG_RXSTALL_LSB=0, tdma_event_sequence_offset_bad=8,
    TDMA_PIO_SPI_ROLE_MASTER=0, TDMA_PIO_SPI_ROLE_SLAVE=1,
    TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL=1, TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN=11,
    TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER=13, clk_sys=0,
    TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY=1, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET=2,
    TDMA_PIO_SPI_PHYS_ERROR_NONE=0, TDMA_PIO_SPI_FLIGHT_OVERLAY_SCRIPT_WORDS=512 };
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
typedef struct {
    uint32_t state; uint64_t capture_service_ns; uint8_t packet[512];
    tdma_rx_capture_t capture;
    uint32_t capture_observer_epoch;
} tdma_rx_prepare_t;
typedef struct {
    tdma_rx_prepare_t *rx_preparation;
    uint64_t last_service_ns;
    uint32_t rx_queue_count;
    bool (*phys_rx)(void *, uint8_t *, size_t, size_t *, uint64_t *);
    bool (*phys_rx_ex)(void *, uint8_t *, size_t, size_t *, uint64_t *, tdma_rx_capture_t *);
    uint32_t (*phys_rx_event_pin)(void *, const tdma_rx_capture_t *);
    void *phys_context, *phys_ctrl_context;
    struct { uint32_t active; } origin;
    struct { bool (*take_rx_observation)(void *, tdma_origin_observation_t *); } phys_origin;
} tdma_pio_spi_ring_adapter_t;
static bank_t bank;
static tdma_pio_spi_phys_t s_tdma_pio_spi_phys;
static tdma_pio_spi_ring_adapter_t adapter;
static tdma_rx_prepare_t job;
static tdma_event_observer_t s_tdma_event_observer;
static tdma_event_history_t s_tdma_event_history;
static tdma_pio_spi_event_snapshot_t s_tdma_event_snapshot;
static bool s_tdma_event_candidate_dirty;
static uint64_t s_tdma_event_arm_epoch;
static tdma_event_batch_t s_tdma_event_batch;
static tdma_event_record_t s_tdma_event_records[TDMA_EVENT_MAX_RECORDS];
static uint32_t s_tdma_event_hz=125000000u, s_tdma_event_sequence_offset=20u;
static uint64_t s_tdma_event_base_us, s_tdma_event_last_service_us, now_us;
static bool s_tdma_event_waiting, s_tdma_runtime_owner_initialized;
/* This regression exercises the unchanged ordinary path. Selected geometry
 * and its real counter are executed by test_tdma_observer_prelaunch. */
static bool s_tdma_event_prelaunch;
static uint32_t tdma_event_prelaunch_observe(tdma_pio_spi_phys_t *p) {
    (void)p; assert(false); return TDMA_GEOMETRY_OBSERVER_OK;
}
static void tdma_event_prelaunch_mark(uint32_t state, uint32_t reason) {
    (void)state; (void)reason; assert(!s_tdma_event_prelaunch);
}
static void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *p) { (void)p; assert(false); }
static unsigned s_tdma_pio_spi_program_persona;
static int s_tdma_pio_spi_tx_dma_channel=-1, service_instance;
static int *s_vdc_tdma_service=&service_instance;
static unsigned fifo_reads, fifo_level_reads, capture_calls, queue_calls, accept_calls;
static unsigned owner_lifetimes, observer_disable_calls, refmem_calls, training_calls;
static unsigned hz_reads, fault_at_hz_read;
static unsigned cut_monitor_calls;
static bool ota_active, skip_phase;
static uint64_t time_us_64(void) { return now_us; }
static uint64_t vdc_dpll_manager_now_ns(void) { return now_us*1000u; }
static uint64_t vdc_timestamp_clock_now_ns(void) { return now_us*1000u; }
static uint64_t probe_ticks;
static uint32_t probe_calls[TDMA_TIMING_STAGE_COUNT];
static uint64_t probe_elapsed[TDMA_TIMING_STAGE_COUNT];
uint64_t tdma_service_timing_now(void) { return ++probe_ticks; }
void tdma_service_timing_record(tdma_service_timing_stage_t phase, uint64_t start) {
    assert(phase<TDMA_TIMING_STAGE_COUNT && start<=probe_ticks);
    ++probe_calls[phase]; probe_elapsed[phase] += ++probe_ticks-start;
}
void tdma_service_timing_rx_station(uint32_t state, uint64_t now, uint64_t captured) {
    (void)state; (void)now; (void)captured;
}
static uint32_t clock_get_hz(unsigned clock) {
    (void)clock; ++hz_reads;
    return s_tdma_event_hz + (fault_at_hz_read != 0u && hz_reads == fault_at_hz_read ? 1u : 0u);
}
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
/* Recovery runs through the full production adapter in its dedicated tests.
 * This fixture remains an ordinary observer with no explicit tap configured. */
static tdma_pio_spi_event_recovery_snapshot_t s_tdma_event_recovery;
static void tdma_event_recovery_cancel(uint32_t reason) { (void)reason; assert(false); }
static void tdma_event_recovery_step(tdma_pio_spi_phys_t *phys) { (void)phys; assert(false); }
static void tdma_event_recovery_publish(void) { assert(false); }
static void tdma_event_recovery_record_failure(tdma_pio_spi_phys_t *phys,
    const tdma_event_batch_t *batch, uint32_t faults) { (void)phys; (void)batch; (void)faults; }
/* Full live-record publication executes in test_tdma_event_live. */
static void tdma_event_live_monitor(void) { }
static void tdma_event_live_retire(uint32_t reason) { (void)reason; }
static void tdma_event_live_record(const tdma_event_record_t *record) { (void)record; }
/* Archive storage/retirement is executed by the prelaunch/candidate tests.
 * This fixture retains its ordinary-service boundary with no selected archive. */
static void tdma_rx_first_window_words(const tdma_event_batch_t *batch) { (void)batch; }
static void tdma_rx_first_window_raw(tdma_pio_spi_phys_t *phys) { (void)phys; }
static void tdma_rx_first_window_event(tdma_pio_spi_phys_t *phys, size_t count) {
    (void)phys; (void)count;
}
static void tdma_rx_first_window_retire(uint32_t reason, bool stopped) {
    assert(reason == TDMA_RX_FIRST_OBSERVER_FAILED && !stopped);
}
/* DMA/register start-cut semantics run in test_tdma_event_adapter.py. This
 * owner-path fixture checks that service still calls the monitor each time. */
static void tdma_rx_start_cut_monitor(tdma_pio_spi_phys_t *phys) {
    assert(phys==&s_tdma_pio_spi_phys); ++cut_monitor_calls;
}
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
    probe_ticks=0u;
    memset(probe_calls,0,sizeof(probe_calls)); memset(probe_elapsed,0,sizeof(probe_elapsed));
    fifo_reads=fifo_level_reads=capture_calls=queue_calls=accept_calls=0u;
    owner_lifetimes=observer_disable_calls=refmem_calls=training_calls=0u;
    hz_reads=fault_at_hz_read=0u;
    cut_monitor_calls=0u;
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
    tdma_event_history_init(&s_tdma_event_history);
    assert(tdma_event_history_start(&s_tdma_event_history,1u,cfg.pio_hz,(tdma_event_interval_t){0u,0u}));
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
        /* Every retained event is queried, including earlier entries in a
         * multi-event harvest. Never substitute the latest snapshot. */
        for(uint32_t ordinal=0u;ordinal<emitted;++ordinal) {
            tdma_event_record_t raw;
            const bool retained=tdma_event_history_lookup(&s_tdma_event_history,1u,ordinal+1u,&raw);
            assert(retained == (emitted-ordinal<=TDMA_EVENT_HISTORY_CAPACITY));
            if(retained) {
                assert(raw.ordinal==ordinal && raw.sequence==ordinal+1u);
                assert(raw.raw_rx==UINT32_MAX-31250u-62517u*ordinal);
                assert(raw.tx_elapsed_cycles-raw.rx_elapsed_cycles==2u);
                assert(raw.diagnostic_only && raw.identity_unproved && raw.physical_first_unproved);
                assert(!raw.timestamp_valid && !raw.dpll_eligible);
            }
        }
        assert(s_tdma_event_snapshot.service_count==phase);
        assert(cut_monitor_calls==phase);
        uint64_t event_ticks=0u;
        for(unsigned stage=TDMA_TIMING_EVENT_ENTRY; stage<=TDMA_TIMING_EVENT_PUBLISH; ++stage) {
            assert(probe_calls[stage]==phase);
            event_ticks+=probe_elapsed[stage];
        }
        assert(event_ticks<probe_elapsed[TDMA_TIMING_PHYS_SERVICE]);
        assert(probe_calls[TDMA_TIMING_REFERENCE_TX]==0u);
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
    for(unsigned stage=TDMA_TIMING_EVENT_ENTRY; stage<=TDMA_TIMING_EVENT_PUBLISH; ++stage)
        assert(probe_calls[stage]==0u);
    assert(bank.level[1]==2u && bank.ctrl==15u);
    s_tdma_runtime_owner_initialized=true;
    ota_active=true; tdma_component_core1_service(); ota_active=false;
    assert(fifo_reads==0u && owner_lifetimes==0u); /* OTA still excludes all work. */
    skip_phase=true; tdma_component_core1_service(); skip_phase=false;
    /* A finite origin blackout still harvests the independent observer. Its
     * TX/owner/RefMem/training work remains suppressed. */
    assert(fifo_reads==5u && s_tdma_event_snapshot.service_count==1u);
    assert(s_tdma_event_observer.state==TDMA_EVENT_ACTIVE && s_tdma_event_observer.joined==1u);
    assert(owner_lifetimes==0u && capture_calls==0u && refmem_calls==0u && training_calls==0u);
    assert(probe_calls[TDMA_TIMING_PHYS_SERVICE]==0u && probe_calls[TDMA_TIMING_OWNER_SERVICE]==0u);
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
    assert(probe_calls[TDMA_TIMING_EVENT_ENTRY]==2u);
    assert(probe_calls[TDMA_TIMING_EVENT_START_CUT]==2u);
    assert(probe_calls[TDMA_TIMING_EVENT_HARVEST]==1u);
}
static void test_final_fault_retires_prior_and_whole_new_batch(void) {
    reset(); event(0u); now_us=1500u; tdma_component_core1_service();
    tdma_event_record_t raw;
    assert(tdma_event_history_lookup(&s_tdma_event_history,1u,1u,&raw));
    event(1u); event(2u); now_us=3000u;
    fault_at_hz_read=hz_reads+3u; /* final sticky-fault check after feed */
    tdma_component_core1_service();
    assert(s_tdma_event_observer.state==TDMA_EVENT_INVALID);
    assert(s_tdma_event_snapshot.published==1u);
    assert(probe_calls[TDMA_TIMING_EVENT_FINAL_CHECK]==2u);
    for(uint seq=1u;seq<=3u;++seq)
        assert(!tdma_event_history_lookup(&s_tdma_event_history,1u,seq,&raw));
}
int main(void) {
    test_rx_preparation_and_queue_cannot_starve_observer();
    test_capture_has_no_second_harvest();
    test_not_armed_master_and_uninitialized_are_noops();
    test_stall_remains_permanent_failure();
    test_final_fault_retires_prior_and_whole_new_batch();
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
                 str(ROOT / "components/tdma/src/tdma_event_observer.c"),
                 str(ROOT / "components/tdma/src/tdma_event_history.c"), "-o", str(executable)],
                [str(executable)]]
    for index, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True)
        (tmp_path / f"command-{index}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
    assert "24 phases" in result.stdout
