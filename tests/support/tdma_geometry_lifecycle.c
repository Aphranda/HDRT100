#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tdma_pio_spi_phys.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_rx_sequence.h"

/* Windows CRT assertion dialogs hide the failure until pytest's timeout.
 * Keep test assertions synchronous and preserve their exact expression. */
#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "ASSERT %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    fflush(stderr); _Exit(99); } } while (0)

static tdma_pio_spi_phys_t physical;
static tdma_pio_spi_ring_adapter_t adapter;
static tdma_overlay_prepare_t overlay;
static tdma_rx_scan_t scanner;
static tdma_rx_prepare_t station;
static tdma_ring_runtime_config_t config;
static tdma_rx_dma_counter_t s_tdma_pio_spi_rx_sequence;
static uint64_t s_tdma_pio_spi_rx_arm_epoch;
static bool s_tdma_pio_spi_rx_arm_valid;
static tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
static uint32_t clock_hz = 250000000u;
static uint64_t host_time;
static uint64_t time_step;
static uint32_t irq_depth, irq_saves, irq_restores;
static uint32_t physical_arms, stopped_callbacks, output_enables, register_writes;
static bool dma_stop_ok = true, resource_release_ok = true, fail_early_arm, fail_after_begin;
static bool clock_changes_at_bind;
static bool reuse_arm_epoch, change_request_seq;

#define clk_sys 0
#define GPIO_FUNC_SIO 5u
#define GPIO_IN false
static uint32_t clock_get_hz(int clock) { assert(clock == clk_sys); return clock_hz; }
static uint32_t pio_get_index(PIO pio)
{
    assert(pio == pio0 || pio == pio1 || pio == pio2);
    return pio == pio0 ? 0u : pio == pio1 ? 1u : 2u;
}
static void __dmb(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
static uint32_t save_and_disable_interrupts(void)
{
    ++irq_saves;
    return irq_depth++;
}
static void restore_interrupts(uint32_t previous)
{
    assert(irq_depth == previous + 1u);
    irq_depth = previous;
    ++irq_restores;
}
static uint64_t time_us_64(void)
{
    const uint64_t value = host_time;
    host_time += time_step;
    return value;
}
static void tdma_pio_spi_phys_set_error(tdma_pio_spi_phys_t *phys, uint32_t error)
{ phys->snapshot.last_error = error; }

/* This is the complete production include, with actual physical/header types. */
/* Archive behavior is exercised by test_tdma_rx_first_window. These are
 * diagnostic sinks only; geometry decisions and worker cancellation are real. */
static void tdma_rx_first_window_admit(const tdma_ring_runtime_config_t *c) { (void)c; }
static void tdma_rx_first_window_arm_failed(void) { }
static void tdma_rx_first_window_retire(uint32_t reason, bool stopped) { (void)reason; (void)stopped; }
#include "tdma_pio_spi_phys_geometry.inc"

/* Register/physical cleanup boundaries only. All three worker cancellation
 * implementations and the complete adapter implementation are linked below. */
static void tdma_pio_spi_phys_event_stop(tdma_pio_spi_phys_t *phys) { (void)phys; }
static bool tdma_pio_spi_phys_rx_scan_cancel(tdma_pio_spi_phys_t *phys)
{ (void)phys; return tdma_rx_scan_cancel(&scanner); }
static bool tdma_pio_spi_phys_stop_command_dma(tdma_pio_spi_phys_t *phys)
{ (void)phys; ++register_writes; return dma_stop_ok; }
static bool tdma_pio_spi_phys_restore_clock_latch(tdma_pio_spi_phys_t *phys, bool arm)
{ (void)phys; assert(!arm); ++register_writes; return true; }
static void tdma_pio_spi_phys_set_line_drivers(bool enabled)
{ ++register_writes; if (enabled) ++output_enables; }
static bool tdma_pio_spi_phys_is_flight_persona(void) { return true; }
static void tdma_pio_spi_phys_prepare_sm_pair(tdma_pio_spi_phys_t *phys)
{ (void)phys; ++register_writes; }
static void pio_sm_set_enabled(PIO pio, uint32_t sm, bool enabled)
{ (void)pio; (void)sm; ++register_writes; if (enabled) ++output_enables; }
static void pio_sm_clear_fifos(PIO pio, uint32_t sm)
{ (void)pio; (void)sm; ++register_writes; }
static void gpio_set_function(uint32_t pin, uint32_t function)
{ (void)pin; assert(function == GPIO_FUNC_SIO); ++register_writes; }
static void gpio_set_dir(uint32_t pin, bool output)
{ (void)pin; assert(!output); ++register_writes; }
static void tdma_pio_spi_phys_clk_train_reset(tdma_pio_spi_phys_t *phys) { (void)phys; }
static void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys) { (void)phys; }
static void tdma_pio_spi_phys_release_flight_resources(tdma_pio_spi_phys_t *phys)
{
    if (resource_release_ok) {
        phys->flight_resource_claimed = false;
        phys->flight_origin_resource_claimed = false;
    }
}
static void tdma_rx_start_cut_disarmed(void) { }

/* Generated verbatim from the current production physical source. */
#include "geometry_physical_stop.inc"
#include "geometry_arm_reject.inc"

static tdma_frozen_geometry_snapshot_t snapshot(void)
{
    tdma_frozen_geometry_snapshot_t result;
    assert(tdma_pio_spi_phys_get_frozen_geometry(&result));
    assert(result.version == 2u && !irq_depth && irq_saves == irq_restores);
    return result;
}

static bool arm_backend(void *context, const tdma_ring_runtime_config_t *selected)
{
    assert(context == &physical);
    ++physical_arms;
    if (fail_early_arm)
        return tdma_pio_spi_phys_arm_reject(&physical, TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
    ++physical.geometry_map_generation;
    tdma_ring_runtime_config_t changed = *selected;
    if (change_request_seq) {
        ++changed.owner_config_seq;
        selected = &changed;
    }
    const uint32_t writes_before = register_writes;
    if (!tdma_geometry_arm_begin(&physical, selected,
            TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER)) return false;
    assert(register_writes == writes_before);
    if (fail_after_begin)
        return tdma_pio_spi_phys_arm_reject(&physical, TDMA_PIO_SPI_PHYS_ERROR_RX_ARM);
    /* Hardware ARM seam: model only the successful capture reset result.
     * This is not a simulation of PIO wire timing or physical prefix proof. */
    physical.flight_overlay_alignment_locked = false;
    physical.flight_overlay_alignment_samples = 0u;
    physical.flight_alignment_byte_shift = physical.flight_alignment_bit_shift = 0u;
    tdma_geometry_persona(TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER);
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
    s_tdma_pio_spi_rx_arm_valid = true;
    if (!reuse_arm_epoch) ++s_tdma_pio_spi_rx_arm_epoch;
    assert(tdma_rx_dma_counter_reset(&s_tdma_pio_spi_rx_sequence,
        physical.flight_physical_byte_count, 0u));
    physical.flight_resource_claimed = true;
    if (clock_changes_at_bind) ++clock_hz;
    const bool bound = tdma_geometry_arm_bound(&physical);
    if (!bound) return false;
    physical.armed = physical.rx_capture_active = true;
    return true;
}

static void stopped_backend(void *context)
{
    ++stopped_callbacks;
    assert(context == &physical && !adapter.started);
    assert(tdma_rx_prepare_state(adapter.rx_preparation) == TDMA_RX_PREPARE_IDLE);
    tdma_pio_spi_phys_geometry_stopped(context);
}

static void setup(void)
{
    physical = (tdma_pio_spi_phys_t){
        .role = TDMA_PIO_SPI_ROLE_SLAVE, .process_image_enabled = true,
        .node_count = 4u, .flight_local_slot_id = 1u,
        .flight_physical_byte_count = 173u, .flight_payload_size = 132u,
        .flight_tail_bytes = 5u, .tx_sck_pin = 25u, .tx_csn_pin = 26u,
        .tx_pin = 29u, .rx_sck_pin = 28u, .rx_csn_pin = 27u, .rx_pin = 24u,
        .flight_marker_phase_delay_cycles = 4u, .flight_sck_phase_delay_cycles = 6u,
        .flight_data_phase_delay_cycles = 2u,
        .geometry_topology_generation = 3u, .geometry_topology_crc32 = 7u,
        .geometry_calibration_generation = 5u, .overlay_preparation = &overlay,
        .geometry_map_generation = 1u, .geometry_map_crc32 = 19u,
        .geometry_stage_enabled = 1u, .geometry_marker_source = 0u,
        .geometry_marker_destination = 1u, .geometry_data_source = 2u,
        .geometry_data_destination = 1u};
    physical.flight_resources = tdma_state_machine_resource_contract();
    config = (tdma_ring_runtime_config_t){
        .enabled = 1u, .node_count = 4u, .local_slot_id = 1u, .reference_slot_id = 0u,
        .up_group_id = 1u, .down_group_id = 2u,
        .flags = TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN,
        .ring_profile_crc32 = 11u, .schedule_crc32 = 13u,
        .operating_profile_crc32 = 17u, .baud_hz = 10000000u,
        .cycle_period_ns = 1500000u, .feedback_timeout_ns = 6000000u,
        .tx_dma_channel_id = 3u, .rx_dma_channel_id = 4u, .owner_config_seq = 10u};
    physical.baud_hz = config.baud_hz;
    assert(tdma_pio_spi_ring_adapter_init(&adapter));
    adapter.rx_preparation = &station;
    tdma_pio_spi_ring_adapter_set_phys_ctrl(&adapter, arm_backend,
        tdma_pio_spi_phys_disarm, NULL, NULL, &physical);
    tdma_pio_spi_ring_adapter_set_phys_geometry_lifecycle(&adapter,
        tdma_pio_spi_phys_geometry_arm_requested, stopped_backend);
}

static bool start(void) { return tdma_pio_spi_ring_adapter_ops()->start(&adapter, &config); }
static bool stop(void) { return tdma_pio_spi_ring_adapter_ops()->stop(&adapter); }

static void train(void)
{
    assert(start());
    physical.flight_overlay_alignment_samples = TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES;
    physical.flight_alignment_byte_shift = 3u;
    physical.flight_alignment_bit_shift = 5u;
    tdma_geometry_trained(&physical);
    physical.flight_overlay_alignment_locked = true;
}
static uint32_t freeze(void)
{
    train();
    assert(stop());
    tdma_frozen_geometry_snapshot_t frozen = snapshot();
    assert(frozen.state == TDMA_GEOMETRY_FROZEN && frozen.reason == TDMA_GEOMETRY_OK);
    assert(frozen.source_config_seq == 10u && frozen.source_arm_epoch == 1u);
    assert(frozen.source_observation_epoch == 0u && frozen.generation != 0u);
    assert(frozen.dma_byte_shift == 3u && frozen.dma_bit_shift == 5u);
    assert(!physical.flight_overlay_alignment_locked && !physical.flight_overlay_alignment_samples);
    config.geometry_generation = frozen.generation;
    config.owner_config_seq = 11u;
    return frozen.generation;
}

static void case_cancel_ack(void)
{
    train();
    overlay.state = TDMA_OVERLAY_PREPARE_BUILDING;
    scanner.state = TDMA_RX_SCAN_BUILDING;
    assert(!stop());
    tdma_frozen_geometry_snapshot_t candidate = snapshot();
    assert(candidate.state == TDMA_GEOMETRY_CANDIDATE && stopped_callbacks == 0u);
    assert(candidate.dma_byte_shift == 3u && candidate.dma_bit_shift == 5u);
    assert(candidate.training_samples == TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES);
    assert(!physical.flight_overlay_alignment_locked && !physical.flight_overlay_alignment_samples);
    assert(!s_geometry_physical_stopped && !physical.armed);
    assert(!tdma_pio_spi_phys_geometry_arm_requested(&physical, &config));
    assert(snapshot().state == TDMA_GEOMETRY_CANDIDATE);
    for (unsigned i = 0u; i < 3u; ++i) {
        assert(!stop());
        const tdma_frozen_geometry_snapshot_t retry = snapshot();
        assert(retry.generation == candidate.generation);
        assert(retry.source_arm_epoch == candidate.source_arm_epoch);
        assert(retry.dma_byte_shift == 3u && retry.dma_bit_shift == 5u);
        tdma_pio_spi_phys_geometry_stopped(&physical);
        assert(snapshot().state == TDMA_GEOMETRY_CANDIDATE);
    }
    tdma_overlay_prepare_core0_build_claimed(&overlay);
    assert(tdma_overlay_prepare_state(&overlay) == TDMA_OVERLAY_PREPARE_IDLE);
    assert(!stop() && snapshot().state == TDMA_GEOMETRY_CANDIDATE);
    tdma_rx_scan_core0_build_claimed(&scanner);
    assert(tdma_rx_scan_state(&scanner) == TDMA_RX_SCAN_IDLE);
    station.state = TDMA_RX_PREPARE_BUILDING;
    assert(!stop());
    assert(s_geometry_physical_stopped && stopped_callbacks == 0u);
    assert(snapshot().state == TDMA_GEOMETRY_CANDIDATE);
    assert(!stop() && stopped_callbacks == 0u);
    tdma_rx_prepare_core0_build_claimed(&station);
    assert(tdma_rx_prepare_state(&station) == TDMA_RX_PREPARE_IDLE);
    assert(stop() && stopped_callbacks == 1u);
    const tdma_frozen_geometry_snapshot_t frozen = snapshot();
    assert(frozen.state == TDMA_GEOMETRY_FROZEN && frozen.generation == candidate.generation);
}

static void case_select(void)
{
    const uint32_t generation = freeze();
    const tdma_frozen_geometry_snapshot_t old = snapshot();
    const uint32_t writes = register_writes;
    assert(start());
    const tdma_frozen_geometry_snapshot_t selected = snapshot();
    assert(selected.state == TDMA_GEOMETRY_SELECTED && selected.requested_generation == generation);
    assert(selected.bound_config_seq == 11u && selected.source_config_seq == 10u);
    assert(selected.bound_arm_epoch > old.source_arm_epoch);
    assert(selected.bound_observation_epoch == 0u); /* scoped by new ARM */
    assert(selected.bound_map_generation > old.source_map_generation);
    assert(!physical.flight_overlay_alignment_locked && !physical.flight_overlay_alignment_samples);
    assert(physical.flight_alignment_byte_shift == 0u && physical.flight_alignment_bit_shift == 0u);
    assert(register_writes == writes && output_enables == 0u);
    assert(stop());
    assert(snapshot().state == TDMA_GEOMETRY_RETIRED); /* no new training */
    ++config.owner_config_seq;
    assert(!start()); /* consumed generation cannot be used again */
    assert(snapshot().state == TDMA_GEOMETRY_REJECTED);
}

static void case_observer_binding(void)
{
    const uint32_t generation = freeze();
    assert(start());
    tdma_frozen_geometry_snapshot_t g;
    assert(tdma_geometry_observer_select(&physical, &g));
    assert(g.observer_state == TDMA_GEOMETRY_OBSERVER_NONE);
    const tdma_pio_spi_phys_t saved = physical;
    for (unsigned which = 0u; which < 16u; ++which) {
        tdma_frozen_geometry_snapshot_t wrong = g;
        if (which == 0u) ++wrong.generation;
        if (which == 1u) ++wrong.bound_config_seq;
        if (which == 2u) ++wrong.bound_map_generation;
        if (which == 3u) ++wrong.bound_arm_epoch;
        if (which == 4u) ++wrong.bound_observation_epoch;
        if (which == 5u) ++physical.geometry_map_generation;
        if (which == 6u) ++physical.geometry_map_crc32;
        if (which == 7u) ++physical.geometry_marker_source;
        if (which == 8u) ++physical.geometry_data_destination;
        if (which == 9u) ++physical.flight_data_phase_delay_cycles;
        if (which == 10u) ++physical.flight_marker_offset_sample_count;
        if (which == 11u) ++physical.flight_resources.data_in_capture_dma;
        if (which == 12u) physical.flight_resource_claimed = false;
        if (which == 13u) ++physical.tx_csn_pin;
        if (which == 14u) ++physical.flight_physical_byte_count;
        if (which == 15u) ++physical.baud_hz;
        assert(!tdma_geometry_observer_current(&physical, &wrong));
        physical = saved;
        assert(tdma_geometry_observer_current(&physical, &g));
    }
    tdma_geometry_observer_record(generation, 19u, TDMA_GEOMETRY_OBSERVER_ACTIVE,
        TDMA_GEOMETRY_OBSERVER_OK, 105u);
    assert(snapshot().observer_epoch == 19u && snapshot().observer_prefix_bits == 105u);
    tdma_geometry_observer_record(generation, 19u, TDMA_GEOMETRY_OBSERVER_REJECTED,
        TDMA_GEOMETRY_OBSERVER_DIRTY_START, 105u);
    tdma_geometry_observer_record(generation, 0u, TDMA_GEOMETRY_OBSERVER_RETIRED,
        TDMA_GEOMETRY_OBSERVER_STOP, 0u);
    assert(snapshot().observer_state == TDMA_GEOMETRY_OBSERVER_REJECTED);
    assert(snapshot().observer_reason == TDMA_GEOMETRY_OBSERVER_DIRTY_START);
    assert(stop());
    assert(!tdma_geometry_observer_current(&physical, &g));
    assert(snapshot().observer_epoch == 19u);
}

static void case_stale(const char *name)
{
    if (!strcmp(name, "missing")) {
        config.geometry_generation = 1u;
        config.owner_config_seq = 11u;
    } else {
        freeze();
        if (!strcmp(name, "wrong_generation")) ++config.geometry_generation;
        else if (!strcmp(name, "same_config")) config.owner_config_seq = 10u;
        else if (!strcmp(name, "zero_config")) config.owner_config_seq = 0u;
        else assert(!"unknown stale case");
    }
    const uint32_t arms = physical_arms;
    assert(!start() && physical_arms == arms);
    assert(snapshot().state == TDMA_GEOMETRY_REJECTED);
    assert(snapshot().reason == TDMA_GEOMETRY_STALE);
}

static void case_key(unsigned field)
{
    freeze();
    const tdma_pio_spi_phys_t original_physical = physical;
    const tdma_ring_runtime_config_t original_config = config;
    const uint32_t original_clock = clock_hz;
    switch (field) {
    case 0: ++config.schedule_crc32; break;
    case 1: ++config.operating_profile_crc32; break;
    case 2: ++config.cycle_period_ns; break;
    case 3: ++config.baud_hz; break;
    case 4: ++physical.flight_marker_phase_delay_cycles; break;
    case 5: ++physical.flight_sck_phase_delay_cycles; break;
    case 6: ++physical.flight_data_phase_delay_cycles; break;
    case 7: ++physical.flight_origin_capture_phase_delay_cycles; break;
    case 8: ++physical.geometry_topology_generation; break;
    case 9: ++physical.geometry_topology_crc32; break;
    case 10: ++physical.geometry_calibration_generation; break;
    case 11: ++physical.flight_physical_byte_count; break;
    case 12: ++physical.flight_payload_size; break;
    case 13: ++physical.flight_tail_bytes; break;
    case 14: ++physical.tx_pin; break;
    case 15: ++physical.rx_csn_pin; break;
    case 16: physical.flight_resources.tx_pio = pio2; break;
    case 17: ++physical.flight_resources.tx_data_capture_sm; break;
    case 18: ++physical.flight_resources.data_in_capture_dma; break;
    case 19: ++physical.flight_marker_offset_sample_count; break;
    case 20: ++physical.flight_sck_offset_sample_count; break;
    case 21: ++physical.flight_data_offset_sample_count; break;
    case 22: ++clock_hz; break;
    case 23: ++physical.geometry_map_crc32; break;
    case 24: physical.geometry_stage_enabled = 0u; break;
    case 25: ++physical.geometry_marker_source; break;
    case 26: ++physical.geometry_marker_destination; break;
    case 27: ++physical.geometry_data_source; break;
    case 28: ++physical.geometry_data_destination; break;
    default: assert(!"unknown key field");
    }
    assert(!start());
    if (field >= 14u && field <= 18u) {
        assert(snapshot().state == TDMA_GEOMETRY_RETIRED);
        assert(snapshot().reason == TDMA_GEOMETRY_ARM_FAILED);
    } else {
        assert(snapshot().state == TDMA_GEOMETRY_REJECTED);
        assert(snapshot().reason == TDMA_GEOMETRY_CONTENT);
    }
    assert(!physical.armed && !output_enables);
    /* Correcting content does not resurrect a consumed frozen descriptor. */
    const uint32_t first_reason = snapshot().reason;
    physical = original_physical;
    config = original_config;
    clock_hz = original_clock;
    assert(!tdma_pio_spi_phys_geometry_arm_requested(&physical, &config));
    assert(snapshot().reason == first_reason); /* same publication retry */
    ++config.owner_config_seq;
    assert(!tdma_pio_spi_phys_geometry_arm_requested(&physical, &config));
    assert(snapshot().reason == TDMA_GEOMETRY_STALE);
}

static void case_early(const char *name)
{
    freeze();
    const uint32_t arms = physical_arms;
    if (!strcmp(name, "adapter_early")) config.up_group_id = 0u;
    else if (!strcmp(name, "physical_early")) fail_early_arm = true;
    else if (!strcmp(name, "physical_late")) fail_after_begin = true;
    else assert(!"unknown early case");
    assert(!start());
    assert(snapshot().state != TDMA_GEOMETRY_FROZEN);
    if (!strcmp(name, "adapter_early")) assert(physical_arms == arms);
    config.up_group_id = 1u;
    fail_early_arm = fail_after_begin = false;
    ++config.owner_config_seq;
    assert(!start()); /* no STOP/retry may restore old generation */
    assert(stop());
    assert(snapshot().state != TDMA_GEOMETRY_FROZEN);
}

static void case_retire(const char *name)
{
    if (!strcmp(name, "clock_binding")) {
        freeze(); clock_changes_at_bind = true;
        assert(!start());
        assert(snapshot().state == TDMA_GEOMETRY_RETIRED);
        return;
    }
    train();
    if (!strcmp(name, "persona")) tdma_geometry_persona(TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL);
    else if (!strcmp(name, "clock_training")) ++clock_hz;
    else if (!strcmp(name, "arm_epoch")) ++s_tdma_pio_spi_rx_arm_epoch;
    else if (!strcmp(name, "observation_epoch")) ++s_tdma_pio_spi_rx_sequence.observation_epoch;
    else if (!strcmp(name, "generation_exhaustion")) s_geometry_next_generation = UINT32_MAX;
    else if (!strcmp(name, "untrained")) physical.flight_overlay_alignment_locked = false;
    else if (!strcmp(name, "bit_shift")) physical.flight_alignment_bit_shift = 8u;
    else if (!strcmp(name, "byte_shift")) physical.flight_alignment_byte_shift = physical.flight_physical_byte_count;
    else assert(!"unknown retirement case");
    assert(stop());
    assert(snapshot().state == TDMA_GEOMETRY_RETIRED);
    assert(snapshot().reason != TDMA_GEOMETRY_OK);
}

static void case_hardware_stop(const char *name)
{
    train();
    if (!strcmp(name, "dma_stop")) dma_stop_ok = false;
    else resource_release_ok = false;
    assert(!stop() && snapshot().state == TDMA_GEOMETRY_CANDIDATE);
    assert(!s_geometry_physical_stopped);
    const uint32_t generation = snapshot().generation;
    assert(!stop() && snapshot().generation == generation);
    dma_stop_ok = resource_release_ok = true;
    assert(stop() && snapshot().state == TDMA_GEOMETRY_FROZEN);
    assert(snapshot().generation == generation);
}

static void case_clock_stopped(void)
{
    train();
    station.state = TDMA_RX_PREPARE_BUILDING;
    assert(!stop() && snapshot().state == TDMA_GEOMETRY_CANDIDATE);
    ++clock_hz;
    tdma_rx_prepare_core0_build_claimed(&station);
    assert(stop() && snapshot().state == TDMA_GEOMETRY_RETIRED);
    assert(snapshot().reason == TDMA_GEOMETRY_CLOCK);
}

static void case_publication(void)
{
    freeze();
    s_geometry_guard = UINT32_MAX - 3u; /* final even readable guard */
    assert(!start());
    tdma_frozen_geometry_snapshot_t out;
    assert(!tdma_pio_spi_phys_get_frozen_geometry(&out));
    assert(s_geometry_guard == UINT32_MAX && !s_geometry_active);
    assert(!irq_depth && irq_saves == irq_restores);
    assert(!start()); /* no wrap back into the readable generation */
}

static void case_reader(void)
{
    freeze();
    assert(!tdma_pio_spi_phys_get_frozen_geometry(NULL));
    tdma_frozen_geometry_snapshot_t out;
    const uint32_t guard = s_geometry_guard;
    s_geometry_guard |= 1u;
    assert(!tdma_pio_spi_phys_get_frozen_geometry(&out));
    s_geometry_guard = guard;
    time_step = 1001u;
    assert(!tdma_pio_spi_phys_get_frozen_geometry(&out));
    time_step = 0u;
    host_time = UINT64_MAX - 1u; time_step = 2u;
    assert(!tdma_pio_spi_phys_get_frozen_geometry(&out));
    time_step = 0u; host_time = 0u;
    assert(tdma_pio_spi_phys_get_frozen_geometry(&out));
    assert(!irq_depth && irq_saves == irq_restores);
}

static void case_binding_replacement(const char *name)
{
    freeze();
    if (!strcmp(name, "request_seq_changed")) change_request_seq = true;
    else reuse_arm_epoch = true;
    assert(!start());
    assert(!s_geometry_active && !physical.armed);
    assert(snapshot().state != TDMA_GEOMETRY_FROZEN);
}

static void case_idle_before_training(void)
{
    assert(start());
    /* Exercise the actual counter horizon during an idle ARM, followed by
     * a verified training transition in the new observation epoch. */
    uint64_t produced;
    bool lost;
    const uint64_t later = s_tdma_pio_spi_rx_sequence.sample_before_ticks +
        s_tdma_pio_spi_rx_sequence.reload_words;
    assert(tdma_rx_dma_counter_observe(&s_tdma_pio_spi_rx_sequence,
        s_tdma_pio_spi_rx_sequence.reload_words, later, later, &produced, &lost));
    assert(lost && s_tdma_pio_spi_rx_sequence.observation_epoch == 1u);
    physical.flight_overlay_alignment_samples = TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES;
    physical.flight_alignment_byte_shift = 3u;
    physical.flight_alignment_bit_shift = 5u;
    tdma_geometry_trained(&physical);
    physical.flight_overlay_alignment_locked = true;
    assert(stop());
    tdma_frozen_geometry_snapshot_t g = snapshot();
    assert(g.state == TDMA_GEOMETRY_FROZEN && g.source_observation_epoch == 1u);
    assert(g.bound_observation_epoch == 0u);
    config.geometry_generation = g.generation;
    ++config.owner_config_seq;
    assert(start());
    g = snapshot();
    assert(g.state == TDMA_GEOMETRY_SELECTED && g.bound_observation_epoch == 0u);
    assert(g.source_observation_epoch == 1u);
    assert(stop());
    assert(snapshot().state == TDMA_GEOMETRY_RETIRED);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    setup();
    const char *name = argv[1];
    if (!strcmp(name, "cancel_ack")) case_cancel_ack();
    else if (!strcmp(name, "idle_before_training")) case_idle_before_training();
    else if (!strcmp(name, "select")) case_select();
    else if (!strcmp(name, "observer_binding")) case_observer_binding();
    else if (!strncmp(name, "key_", 4u)) case_key((unsigned)strtoul(name + 4, NULL, 10));
    else if (!strcmp(name, "missing") || !strcmp(name, "wrong_generation") ||
             !strcmp(name, "same_config") || !strcmp(name, "zero_config")) case_stale(name);
    else if (!strcmp(name, "adapter_early") || !strcmp(name, "physical_early") ||
             !strcmp(name, "physical_late")) case_early(name);
    else if (!strcmp(name, "dma_stop") || !strcmp(name, "resource_release")) case_hardware_stop(name);
    else if (!strcmp(name, "clock_stopped")) case_clock_stopped();
    else if (!strcmp(name, "publication_exhaustion")) case_publication();
    else if (!strcmp(name, "reader")) case_reader();
    else if (!strcmp(name, "request_seq_changed") || !strcmp(name, "reused_arm_epoch"))
        case_binding_replacement(name);
    else case_retire(name);
    assert(!output_enables);
    return 0;
}
