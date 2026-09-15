"""Run production prelaunch/service and the unmodified final physical ARM block.

PIO/MMIO and prior ARM configuration are seams. Geometry's actual lifecycle
and exact binding are tested separately in test_tdma_frozen_geometry. No host
case establishes an electrical CS origin or physical packet identity.
"""
import hashlib
import json
from pathlib import Path
import re

from test_tdma_rx_event_candidate import enabled_source, run
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_production_selected_observer_prelaunch_and_arm_order(tmp_path):
    event = (ROOT / "components/tdma/src/tdma_pio_spi_phys_event.inc").read_text(encoding="utf-8")
    phys = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(encoding="utf-8")
    arm = c_definition_body(phys, "tdma_pio_spi_phys_arm")
    begin = arm.rindex("    if (process_follower) {")
    tail = arm[begin:]
    disarm = c_definition_body(phys, "tdma_pio_spi_phys_disarm")
    stop_begin = disarm.index("    tdma_pio_spi_phys_event_stop(phys);")
    stop_end = disarm.index("    const bool worker_retired")
    stop_prefix = disarm[stop_begin:stop_end]
    assert stop_prefix.index("tdma_pio_spi_phys_event_stop") < stop_prefix.index("tdma_geometry_stop_begin")
    assert arm.index("tdma_pio_spi_phys_rx_arm(phys)") < begin
    assert tail.index("pio_encode_wait_gpio") < tail.index("tdma_geometry_arm_bound")
    assert tail.index("tdma_geometry_arm_bound") < tail.index("tdma_pio_spi_phys_enable_sm_pair")
    assert tail.index("phys->armed = true") < tail.index("tdma_pio_spi_phys_event_prelaunch")
    assert tail.index("tdma_pio_spi_phys_event_prelaunch") < tail.index("return true;")
    base = enabled_source(tmp_path)
    fields = set(re.findall(r"phys->snapshot\.(\w+)", tail)) - {"event", "rx_observation_drop_count"}
    base = base.replace("uint32_t rx_observation_drop_count;", "uint32_t rx_observation_drop_count; " +
                        " ".join(f"uint64_t {name};" for name in sorted(fields)))
    base = base.replace("bool armed, rx_capture_active;", "uint32_t flight_tx_edge_capture_generation; bool armed, rx_capture_active, process_image_enabled;")
    base = base.replace("#undef main\n", "#undef main\n#define main candidate_regression_main\n", 1)
    base = base.replace("static uint64_t clock_us =", "static bool arm_tail_active;\nstatic unsigned final_wait_count;\nstatic uint64_t clock_us =")
    base = base.replace("(void)pio; (void)instruction; assert(sm != 0u);",
                        "(void)pio; (void)instruction; assert(sm != 0u || arm_tail_active); if(sm == 0u) ++final_wait_count;")
    prelaunch = "static bool tdma_pio_spi_phys_event_prelaunch(tdma_pio_spi_phys_t *phys, const tdma_ring_runtime_config_t *config) {" + c_definition_body(event, "tdma_pio_spi_phys_event_prelaunch") + "}\n"
    source = (base + "\n#undef main\n" + SEAMS.replace("/* DISARM_PREFIX */", stop_prefix) +
              prelaunch + "static bool arm_finish(tdma_pio_spi_phys_t *phys, const tdma_ring_runtime_config_t *config) {\n" +
              "const bool process_follower = true, phase_admission_warning = false;\n" + tail + "}\n" + CASES)
    (tmp_path / "integration.json").write_text(json.dumps({
        "scope": "production prelaunch/start/service/STOP; verbatim physical ARM final WAIT-to-return block; real DMA counter; prior ARM and MMIO seams",
        "arm_tail_sha256": hashlib.sha256(tail.encode()).hexdigest(),
        "disarm_prefix_sha256": hashlib.sha256(stop_prefix.encode()).hexdigest(),
        "event_observed_sha256": hashlib.sha256(event.encode()).hexdigest(),
    }, indent=2), encoding="utf-8")
    output = run(tmp_path, source, "prelaunch", enabled=True)
    assert "prelaunch: 18 production groups passed" in output


def test_geometry_query_preserves_prefix_and_appends_observer_tuple(tmp_path):
    scpi = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    body = c_definition_body(scpi, "scpi_cmd_system_tdma_ring_geometry_q")
    source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tdma_frozen_geometry.h"
typedef int scpi_result_t;
typedef int scpi_t;
typedef struct { uint32_t config_seq, applied_config_seq, enabled, adapter_started; } tdma_ring_runtime_snapshot_t;
enum { SCPI_RES_OK=1, SCPI_RES_ERR=-1 };
static uint32_t words[40], used, reads, errors;
static bool change_config;
static void SCPI_ResultUInt32(scpi_t *p,uint32_t word) { (void)p; assert(used<40); words[used++]=word; }
static void scpi_port_push_exec_error(scpi_t *p,const char *s) { (void)p; assert(!strcmp(s,"TDMA_RING_GEOMETRY_QUERY")); ++errors; }
static bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *p) {
    *p=(tdma_ring_runtime_snapshot_t){91u,91u,1u,1u};
    if(++reads==2 && change_config) ++p->config_seq;
    return true;
}
bool tdma_pio_spi_phys_get_frozen_geometry(tdma_frozen_geometry_snapshot_t *p) {
    *p=(tdma_frozen_geometry_snapshot_t){.version=2,.generation=37,.bound_config_seq=91,
        .source_arm_epoch=UINT64_C(0x1122334455667788),.bound_map_generation=10,
        .observer_epoch=123,.observer_state=TDMA_GEOMETRY_OBSERVER_ACTIVE,
        .observer_reason=TDMA_GEOMETRY_OBSERVER_OK,.observer_prefix_bits=105}; return true;
}
''' + "scpi_result_t scpi_cmd_system_tdma_ring_geometry_q(scpi_t *context) {" + body + r'''}
int main(void) {
    assert(scpi_cmd_system_tdma_ring_geometry_q(NULL)==SCPI_RES_OK);
    assert(used==36 && words[0]==2 && words[3]==37 && words[6]==91);
    assert(words[7]==0x55667788u && words[8]==0x11223344u);
    assert(words[30]==10 && words[31]==91);
    assert(words[32]==123 && words[33]==2 && words[34]==0 && words[35]==105);
    used=reads=0; change_config=true;
    assert(scpi_cmd_system_tdma_ring_geometry_q(NULL)==SCPI_RES_ERR);
    assert(used==0 && errors==1);
    puts("geometry v2: 36 fields and stable ACK checked"); return 0;
}
'''
    assert "36 fields and stable ACK checked" in run(tmp_path, source, "geometry_query", enabled=False)


SEAMS = r'''
static bool tdma_geometry_observer_select(const tdma_pio_spi_phys_t *p,
    tdma_frozen_geometry_snapshot_t *out) {
    *out = geometry_fixture;
    return tdma_geometry_observer_current(p, out);
}
static bool tdma_geometry_arm_bound(const tdma_pio_spi_phys_t *p) {
    (void)p; assert(final_wait_count == 1u); return geometry_binding_valid;
}
static void tdma_pio_spi_phys_enable_sm_pair(tdma_pio_spi_phys_t *p) {
    (void)p; assert(final_wait_count == 1u); rx_bank.ctrl |= 1u << 2u;
}
static void tdma_geometry_stop_begin(tdma_pio_spi_phys_t *p) { (void)p; geometry_binding_valid=false; }
static bool tdma_pio_spi_phys_disarm(tdma_pio_spi_phys_t *phys) {
    /* DISARM_PREFIX */
    phys->armed = phys->rx_capture_active = false; return true;
}
static bool tdma_pio_spi_phys_arm_reject(tdma_pio_spi_phys_t *p, unsigned reason) {
    (void)p; assert(reason == 22u); return false;
}
static void tdma_pio_spi_phys_clk_train_reset(tdma_pio_spi_phys_t *p) { (void)p; }
static void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *p) { (void)p; }
#define TDMA_PIO_SPI_RX_DMA_CHANNEL 4u
#define TDMA_PIO_SPI_OVERLAY_ERROR_NONE 0u
#define TDMA_PIO_SPI_PHYS_ERROR_GEOMETRY 22u
#define TDMA_PIO_SPI_PHYS_ERROR_NONE 0u
#define TDMA_PIO_SPI_PHYS_ERROR_PHASE_ADMISSION 1u
static uint32_t s_tdma_pio_spi_flight_process_follower_offset;
'''


CASES = r'''
static tdma_ring_runtime_config_t selected_config;
static void prelaunch_setup(void) {
    geometry_binding_valid = true; cut_enable_hook = NULL;
    setup_candidate(false);
    physical.process_image_enabled = true;
    physical.armed = false;
    physical.flight_overlay_alignment_locked = false;
    physical.flight_overlay_alignment_samples = 0u;
    physical.flight_alignment_byte_shift = physical.flight_alignment_bit_shift = 0u;
    memset(&rx_bank, 0, sizeof(rx_bank));
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,
        physical.flight_physical_byte_count, tick_now));
    dma_bank.ch[4].ctrl_trig = DMA_CH0_CTRL_TRIG_EN_BITS;
    dma_bank.ch[4].transfer_count =
        (DMA_CH0_TRANS_COUNT_MODE_VALUE_TRIGGER_SELF << DMA_CH0_TRANS_COUNT_MODE_LSB) |
        s_tdma_pio_spi_rx_sequence.reload_words;
    selected_config = (tdma_ring_runtime_config_t){.cycle_period_ns=1500000u,
        .geometry_generation=37u, .owner_config_seq=91u};
    geometry_fixture = (tdma_frozen_geometry_snapshot_t){.generation=37u,
        .requested_generation=37u, .bound_config_seq=91u, .bound_map_generation=10u,
        .bound_arm_epoch=s_tdma_pio_spi_rx_arm_epoch, .bound_observation_epoch=0u,
        .clk_sys_hz=125000000u, .physical_bytes=physical.flight_physical_byte_count,
        .dma_byte_shift=3u, .dma_bit_shift=1u};
    final_wait_count = 0u; arm_tail_active = true;
}
static bool finish(void) {
    const bool result = arm_finish(&physical, &selected_config);
    arm_tail_active = false;
    return result;
}
static void require_active(void) {
    assert(geometry_fixture.observer_state == TDMA_GEOMETRY_OBSERVER_ACTIVE);
    assert(geometry_fixture.observer_epoch == s_tdma_event_observer.epoch);
    assert(s_tdma_event_arm_epoch == geometry_fixture.bound_arm_epoch);
    assert(!s_tdma_event_waiting && s_tdma_event_observer.state == TDMA_EVENT_ACTIVE);
    assert(!physical.flight_overlay_alignment_locked && !physical.flight_overlay_alignment_samples);
    assert(physical.flight_alignment_byte_shift == 0u && physical.flight_alignment_bit_shift == 0u);
    assert(s_tdma_rx_start_cut.alignment_byte_shift == 3u && s_tdma_rx_start_cut.alignment_bit_shift == 1u);
    assert((s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_RETIRED) == 0u);
    assert(s_tdma_rx_start_cut.flags & TDMA_RX_START_CUT_UNRESOLVED);
}
static void require_rejected(uint32_t reason) {
    assert(geometry_fixture.observer_state == TDMA_GEOMETRY_OBSERVER_REJECTED);
    assert(geometry_fixture.observer_reason == reason);
    assert(!physical.armed && !s_tdma_event_waiting);
    const uint32_t epoch = s_tdma_event_epoch;
    gpio_script_count = 0;
    physical.armed = true; /* Even a caller mistake cannot request a late start. */
    tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_event_epoch == epoch && s_tdma_event_observer.state != TDMA_EVENT_ACTIVE);
}
static void move_during_enable(void) { --dma_bank.ch[4].transfer_count; }
static void change_binding_during_enable(void) { geometry_binding_valid = false; }
int main(void) {
    assert(candidate_regression_main() == 0);
    prelaunch_setup(); assert(finish()); require_active();
    assert(geometry_fixture.observer_prefix_bits ==
        (3u + TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_SEQUENCE_OFFSET) * 8u + 1u);
    assert(s_tdma_event_snapshot.published == 0u && s_tdma_event_history.accepted == 0u);
    /* Many bounded phases exceed the old observation horizon in total. */
    const uint64_t begin = tick_now;
    for (unsigned i=0; i<20; ++i) {
        tick_now += s_tdma_pio_spi_rx_sequence.reload_words / 5u;
        tdma_pio_spi_phys_event_service(&physical);
        assert(s_tdma_pio_spi_rx_sequence.observation_epoch == 0u);
        require_active();
    }
    assert(tick_now - begin > s_tdma_pio_spi_rx_sequence.reload_words);
    push_first_event(); tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_event_history.accepted == 1u && s_tdma_event_snapshot.sequence == 1u);
    assert(s_tdma_event_snapshot.ordinal == 0u);
    capture.observation_epoch = 0u;
    pin = tdma_pio_spi_phys_rx_event_pin(&physical,&capture);
    query(1u,TDMA_RX_EVENT_MATCHED); /* Query oracle keeps all unproved flags. */
    prelaunch_setup(); assert(finish());
    tick_now += s_tdma_pio_spi_rx_sequence.reload_words;
    tdma_pio_spi_phys_event_service(&physical);
    assert(s_tdma_pio_spi_rx_sequence.observation_epoch == 1u);
    assert(geometry_fixture.observer_state == TDMA_GEOMETRY_OBSERVER_RETIRED);
    assert(geometry_fixture.observer_reason == TDMA_GEOMETRY_OBSERVER_OBSERVATION);
    assert(physical.armed); /* Diagnostic loss does not stop the data plane. */
    const uint32_t low = UINT32_MAX & ~(1u << BOARD_TDMA_SPI_UPLINK_CSN_PIN);
    for (unsigned stage=0; stage<3; ++stage) {
        prelaunch_setup();
        script_start_pads(stage==0?low:UINT32_MAX,stage==1?low:UINT32_MAX,stage==2?low:UINT32_MAX,stage==2?3u:2u);
        assert(!finish());
        require_rejected(stage==2?TDMA_GEOMETRY_OBSERVER_DIRTY_START:TDMA_GEOMETRY_OBSERVER_CS);
    }
    prelaunch_setup(); bank.level[TDMA_EVENT_RX_SM]=1u;
    assert(!finish()); require_rejected(TDMA_GEOMETRY_OBSERVER_FIFO);
    prelaunch_setup(); rx_bank.level[2]=1u;
    assert(!finish()); require_rejected(TDMA_GEOMETRY_OBSERVER_FIFO);
    prelaunch_setup(); --dma_bank.ch[4].transfer_count;
    assert(!finish()); require_rejected(TDMA_GEOMETRY_OBSERVER_DMA_PROGRESS);
    prelaunch_setup(); cut_enable_hook=move_during_enable;
    assert(!finish()); require_rejected(TDMA_GEOMETRY_OBSERVER_DMA_PROGRESS);
    prelaunch_setup(); ++selected_config.owner_config_seq;
    assert(!finish()); require_rejected(TDMA_GEOMETRY_OBSERVER_BINDING);
    prelaunch_setup(); cut_enable_hook=change_binding_during_enable;
    assert(!finish()); require_rejected(TDMA_GEOMETRY_OBSERVER_BINDING);
    prelaunch_setup(); ++geometry_fixture.clk_sys_hz;
    assert(!finish()); require_rejected(TDMA_GEOMETRY_OBSERVER_CLOCK);
    prelaunch_setup(); assert(finish());
    physical.flight_overlay_alignment_samples=TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES;
    physical.flight_alignment_byte_shift=3u; physical.flight_alignment_bit_shift=1u;
    tdma_pio_spi_phys_event_service(&physical);
    assert(geometry_fixture.observer_state==TDMA_GEOMETRY_OBSERVER_ACTIVE);
    physical.flight_alignment_bit_shift=2u;
    tdma_pio_spi_phys_event_service(&physical);
    assert(geometry_fixture.observer_reason==TDMA_GEOMETRY_OBSERVER_GEOMETRY && physical.armed);
    prelaunch_setup(); assert(finish());
    const uint32_t old_epoch=s_tdma_event_epoch;
    assert(tdma_pio_spi_phys_disarm(&physical));
    assert(geometry_fixture.observer_reason==TDMA_GEOMETRY_OBSERVER_STOP);
    assert(s_tdma_rx_start_cut.retire_reasons==TDMA_RX_START_CUT_STOP);
    prelaunch_setup(); assert(finish()); assert(s_tdma_event_epoch>old_epoch);
    prelaunch_setup(); assert(finish()); geometry_binding_valid=false;
    assert(tdma_pio_spi_phys_disarm(&physical));
    assert(geometry_fixture.observer_reason==TDMA_GEOMETRY_OBSERVER_BINDING);
    assert(s_tdma_rx_start_cut.retire_reasons & TDMA_RX_START_CUT_GEOMETRY);
    prelaunch_setup(); selected_config.geometry_generation=0u;
    assert(finish()); assert(!s_tdma_event_prelaunch && s_tdma_event_waiting);
    prelaunch_setup(); assert(finish());
    rx_bank.ctrl=0u; tdma_pio_spi_phys_event_service(&physical);
    assert(geometry_fixture.observer_reason==TDMA_GEOMETRY_OBSERVER_FAULT && physical.armed);
    prelaunch_setup(); assert(finish());
    clock_us=s_tdma_event_base_us+61000000u;
    tdma_pio_spi_phys_event_service(&physical);
    assert(geometry_fixture.observer_state==TDMA_GEOMETRY_OBSERVER_RETIRED);
    puts("prelaunch: 18 production groups passed");
    return 0;
}
'''
