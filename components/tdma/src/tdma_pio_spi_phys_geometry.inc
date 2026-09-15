/* Included only by the physical owner. No allocation or peripheral write.
 * The exact key is private; the bounded snapshot is read
 * with a seqlock. No consumer can turn a historical descriptor into a lease. */
typedef struct {
    uint32_t config[18];
    uint32_t clock_hz, persona, physical_bytes, payload_bytes, tail_bytes;
    uint32_t topology_generation, topology_crc32, calibration_generation;
    uint32_t stage_enabled, marker_source, marker_destination, data_source, data_destination;
    int32_t marker_offset, sck_offset, data_offset;
    uint32_t marker_phase, sck_phase, data_phase, origin_phase;
    uint32_t tx_pins[3], rx_pins[3];
    uint32_t tx_pio, rx_pio, capture_sm, forward_sm, capture_dma, output_dma;
} tdma_geometry_key_t;
static tdma_geometry_key_t s_geometry_active_key, s_geometry_frozen_key;
static tdma_frozen_geometry_snapshot_t s_geometry, s_geometry_published;
static uint32_t s_geometry_next_generation, s_geometry_active_config;
static uint32_t s_geometry_active_map;
static uint32_t s_geometry_request_config;
static volatile uint32_t s_geometry_guard;
static uint64_t s_geometry_active_arm, s_geometry_active_observation;
static uint64_t s_geometry_training_observation;
static bool s_geometry_training_valid;
static bool s_geometry_pending_arm, s_geometry_active, s_geometry_physical_stopped;
static bool s_geometry_request_pending;

static void tdma_geometry_publish(void)
{
    uint32_t guard = __atomic_load_n(&s_geometry_guard, __ATOMIC_RELAXED);
    /* Never wrap a publication generation into a previously readable lease. */
    if (guard >= UINT32_MAX - 2u) {
        __atomic_store_n(&s_geometry_guard, UINT32_MAX, __ATOMIC_RELEASE);
        return;
    }
    __atomic_store_n(&s_geometry_guard, guard + 1u, __ATOMIC_RELEASE);
    __dmb();
    s_geometry.version = 2u;
    s_geometry_published = s_geometry;
    __atomic_store_n(&s_geometry_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool tdma_pio_spi_phys_get_frozen_geometry(tdma_frozen_geometry_snapshot_t *out)
{
    if (out == NULL) return false;
    const uint32_t irq = save_and_disable_interrupts();
    const uint64_t begin = time_us_64();
    bool copied = false;
    for (uint32_t attempt = 0u; attempt < 3u; ++attempt) {
        const uint32_t guard = __atomic_load_n(&s_geometry_guard, __ATOMIC_ACQUIRE);
        if (guard & 1u) continue;
        *out = s_geometry_published;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t end_guard = __atomic_load_n(&s_geometry_guard, __ATOMIC_ACQUIRE);
        const uint64_t end = time_us_64();
        if (end < begin || end - begin > 1000u) break;
        if (guard == end_guard) { copied = true; break; }
    }
    restore_interrupts(irq);
    return copied;
}

static void tdma_geometry_retire(uint32_t reason)
{
    s_geometry_active = false;
    s_geometry_pending_arm = false;
    s_geometry_request_pending = false;
    s_geometry_training_valid = false;
    if (s_geometry.state == TDMA_GEOMETRY_EMPTY ||
        s_geometry.state == TDMA_GEOMETRY_REJECTED ||
        s_geometry.state == TDMA_GEOMETRY_RETIRED) return;
    s_geometry.state = TDMA_GEOMETRY_RETIRED;
    s_geometry.reason = reason;
    tdma_geometry_publish();
}

static void tdma_geometry_arm_failed(void)
{
    tdma_geometry_retire(TDMA_GEOMETRY_ARM_FAILED);
}

bool tdma_pio_spi_phys_geometry_arm_requested(void *context,
    const tdma_ring_runtime_config_t *config)
{
    tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL || config == NULL) {
        tdma_geometry_arm_failed();
        return false;
    }
    if (s_geometry.state == TDMA_GEOMETRY_CANDIDATE) {
        s_geometry.reason = TDMA_GEOMETRY_INCOMPLETE_STOP;
        tdma_geometry_publish();
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_GEOMETRY);
        return false;
    }
    const uint32_t selected = config->geometry_generation;
    /* A runtime retry is not a second selection. Preserve its first refusal. */
    if (selected != 0u && selected == s_geometry.requested_generation &&
        config->owner_config_seq == s_geometry_request_config &&
        (s_geometry.state == TDMA_GEOMETRY_REJECTED ||
         s_geometry.state == TDMA_GEOMETRY_RETIRED)) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_GEOMETRY);
        return false;
    }
    s_geometry_request_config = config->owner_config_seq;
    const bool stale = selected != 0u &&
        (s_geometry.state != TDMA_GEOMETRY_FROZEN ||
         selected != s_geometry.generation || config->owner_config_seq == 0u ||
         config->owner_config_seq == s_geometry.source_config_seq);
    s_geometry.requested_generation = selected;
    s_geometry_active = false;
    s_geometry_pending_arm = false;
    s_geometry_request_pending = !stale;
    s_geometry_training_valid = false;
    s_geometry.state = stale ? TDMA_GEOMETRY_REJECTED :
        (selected ? TDMA_GEOMETRY_SELECTING : TDMA_GEOMETRY_TRAINING);
    s_geometry.reason = stale ? TDMA_GEOMETRY_STALE : TDMA_GEOMETRY_OK;
    tdma_geometry_publish();
    if (stale) tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_GEOMETRY);
    return !stale;
}

static tdma_geometry_key_t tdma_geometry_key(const tdma_pio_spi_phys_t *p,
    const tdma_ring_runtime_config_t *c, uint32_t persona)
{
    /* Every field is assigned; no CRC or padding comparison substitutes for
     * exact admissible content. Config/ARM generations are separate bindings. */
    const tdma_state_machine_resource_contract_t r = tdma_state_machine_resource_contract();
    return (tdma_geometry_key_t){
        .config = {c->enabled, c->node_count, c->local_slot_id, c->reference_slot_id,
            c->up_group_id, c->down_group_id, c->flags, c->ring_profile_crc32,
            c->schedule_crc32, c->operating_profile_crc32, c->baud_hz,
            c->cycle_period_ns, c->loop_delay_ns, c->loop_delay_tolerance_ns,
            c->feedback_timeout_ns, c->tx_dma_channel_id, c->rx_dma_channel_id,
            p->geometry_map_crc32},
        .clock_hz = clock_get_hz(clk_sys), .persona = persona,
        .physical_bytes = p->flight_physical_byte_count,
        .payload_bytes = p->flight_payload_size, .tail_bytes = p->flight_tail_bytes,
        .topology_generation = p->geometry_topology_generation,
        .topology_crc32 = p->geometry_topology_crc32,
        .calibration_generation = p->geometry_calibration_generation,
        .stage_enabled = p->geometry_stage_enabled,
        .marker_source = p->geometry_marker_source,
        .marker_destination = p->geometry_marker_destination,
        .data_source = p->geometry_data_source,
        .data_destination = p->geometry_data_destination,
        .marker_offset = p->flight_marker_offset_sample_count,
        .sck_offset = p->flight_sck_offset_sample_count,
        .data_offset = p->flight_data_offset_sample_count,
        .marker_phase = p->flight_marker_phase_delay_cycles,
        .sck_phase = p->flight_sck_phase_delay_cycles,
        .data_phase = p->flight_data_phase_delay_cycles,
        .origin_phase = p->flight_origin_capture_phase_delay_cycles,
        .tx_pins = {r.tx_clk_out_pin, r.tx_sync_out_pin, r.rx_data_out_pin},
        .rx_pins = {r.rx_clk_in_pin, r.rx_sync_in_pin, r.tx_data_in_pin},
        .tx_pio = pio_get_index(r.tx_pio), .rx_pio = pio_get_index(r.rx_pio),
        .capture_sm = r.tx_data_capture_sm, .forward_sm = r.rx_data_flight_sm,
        .capture_dma = r.data_in_capture_dma, .output_dma = r.data_out_dma
    };
}
_Static_assert(sizeof(tdma_geometry_key_t) == 50u * sizeof(uint32_t),
               "geometry key must contain no padding");

static bool tdma_geometry_arm_begin(tdma_pio_spi_phys_t *phys,
    const tdma_ring_runtime_config_t *config, uint32_t persona)
{
    const uint32_t selected = config->geometry_generation;
    if (s_geometry.state == TDMA_GEOMETRY_CANDIDATE) {
        s_geometry.reason = TDMA_GEOMETRY_INCOMPLETE_STOP;
        tdma_geometry_publish();
        return false;
    }
    const tdma_geometry_key_t key = tdma_geometry_key(phys, config, persona);
    uint32_t reason = TDMA_GEOMETRY_OK;
    if (selected != 0u) {
        if (!s_geometry_request_pending || s_geometry.state != TDMA_GEOMETRY_SELECTING ||
            config->owner_config_seq != s_geometry_request_config ||
            selected != s_geometry.generation) reason = TDMA_GEOMETRY_STALE;
        else if (memcmp(&key, &s_geometry_frozen_key, sizeof(key)) != 0)
            reason = TDMA_GEOMETRY_CONTENT;
        else if (config->owner_config_seq == 0u ||
                 config->owner_config_seq == s_geometry.source_config_seq)
            reason = TDMA_GEOMETRY_STALE;
        else if (s_geometry_guard >= UINT32_MAX - 2u)
            reason = TDMA_GEOMETRY_EXHAUSTED;
    }
    s_geometry.requested_generation = selected;
    s_geometry_active = false;
    s_geometry_pending_arm = false;
    s_geometry_request_pending = false;
    if (reason != TDMA_GEOMETRY_OK) {
        s_geometry.state = TDMA_GEOMETRY_REJECTED;
        s_geometry.reason = reason;
        tdma_geometry_publish();
        return false;
    }
    s_geometry_active_key = key;
    s_geometry_active_config = config->owner_config_seq;
    s_geometry_active_map = phys->geometry_map_generation;
    s_geometry_physical_stopped = false;
    s_geometry_pending_arm = true;
    s_geometry.state = selected ? TDMA_GEOMETRY_SELECTING : TDMA_GEOMETRY_TRAINING;
    s_geometry.reason = TDMA_GEOMETRY_OK;
    s_geometry.bound_config_seq = 0u;
    s_geometry.bound_arm_epoch = 0u;
    s_geometry.bound_observation_epoch = 0u;
    s_geometry.observer_epoch = s_geometry.observer_prefix_bits = 0u;
    s_geometry.observer_state = TDMA_GEOMETRY_OBSERVER_NONE;
    s_geometry.observer_reason = TDMA_GEOMETRY_OBSERVER_OK;
    tdma_geometry_publish();
    return true;
}

static void tdma_geometry_persona(uint32_t persona)
{
    /* Only the pending ARM may install its already checked persona. All
     * maintenance/replacement entries end the previous geometry lifetime. */
    if (!s_geometry_pending_arm || persona != s_geometry_active_key.persona)
        tdma_geometry_retire(TDMA_GEOMETRY_PERSONA);
}

static bool tdma_geometry_arm_bound(const tdma_pio_spi_phys_t *phys)
{
    if (!s_geometry_pending_arm) return s_geometry.requested_generation == 0u;
    s_geometry_pending_arm = false;
    if (!s_tdma_pio_spi_rx_arm_valid || s_tdma_pio_spi_rx_arm_epoch == 0u ||
        (s_geometry.requested_generation != 0u &&
         s_tdma_pio_spi_rx_arm_epoch <= s_geometry.source_arm_epoch) ||
        clock_get_hz(clk_sys) != s_geometry_active_key.clock_hz ||
        !phys->flight_resource_claimed ||
        pio_get_index(phys->flight_resources.tx_pio) != s_geometry_active_key.tx_pio ||
        pio_get_index(phys->flight_resources.rx_pio) != s_geometry_active_key.rx_pio ||
        phys->flight_resources.tx_data_capture_sm != s_geometry_active_key.capture_sm ||
        phys->flight_resources.rx_data_flight_sm != s_geometry_active_key.forward_sm ||
        phys->flight_resources.data_in_capture_dma != s_geometry_active_key.capture_dma ||
        phys->flight_resources.data_out_dma != s_geometry_active_key.output_dma ||
        phys->tx_sck_pin != s_geometry_active_key.tx_pins[0] ||
        phys->tx_csn_pin != s_geometry_active_key.tx_pins[1] ||
        phys->tx_pin != s_geometry_active_key.tx_pins[2] ||
        phys->rx_sck_pin != s_geometry_active_key.rx_pins[0] ||
        phys->rx_csn_pin != s_geometry_active_key.rx_pins[1] ||
        phys->rx_pin != s_geometry_active_key.rx_pins[2]) {
        const bool required = s_geometry.requested_generation != 0u;
        tdma_geometry_retire(TDMA_GEOMETRY_ARM_FAILED);
        return !required;
    }
    s_geometry_active_arm = s_tdma_pio_spi_rx_arm_epoch;
    /* counter_reset starts at zero on every ARM. Only the enclosing ARM
     * epoch is globally monotonic; this coordinate detects loss within it. */
    s_geometry_active_observation = s_tdma_pio_spi_rx_sequence.observation_epoch;
    s_geometry_active = true;
    s_geometry.bound_config_seq = s_geometry_active_config;
    s_geometry.bound_arm_epoch = s_geometry_active_arm;
    s_geometry.bound_observation_epoch = s_geometry_active_observation;
    s_geometry.bound_map_generation = s_geometry_active_map;
    if (s_geometry.requested_generation != 0u) s_geometry.state = TDMA_GEOMETRY_SELECTED;
    tdma_geometry_publish();
    return true;
}

static void tdma_geometry_trained(const tdma_pio_spi_phys_t *phys)
{
    /* Called once when verified training becomes live alignment. ARM may
     * spend longer than the counter's observation horizon waiting for START;
     * that idle prefix is not the provenance of later trained geometry. */
    if (!s_geometry_active || !s_tdma_pio_spi_rx_arm_valid ||
        s_tdma_pio_spi_rx_arm_epoch != s_geometry_active_arm ||
        phys->flight_overlay_alignment_samples < TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES)
        return;
    s_geometry_training_observation = s_tdma_pio_spi_rx_sequence.observation_epoch;
    s_geometry_training_valid = true;
}

/* Core1-local lifecycle check. Config is immutable within this physical ARM;
 * map, calibrated endpoints/phases and installed resources are checked again
 * independently of any subsequently trained live alignment. */
static inline bool tdma_geometry_observer_current(const tdma_pio_spi_phys_t *p,
    const tdma_frozen_geometry_snapshot_t *g)
{
    const tdma_geometry_key_t *k = &s_geometry_active_key;
    return p != NULL && s_geometry_guard < UINT32_MAX - 2u &&
        s_geometry_active && s_geometry.state == TDMA_GEOMETRY_SELECTED &&
        g->generation != 0u && s_geometry.generation == g->generation &&
        s_geometry.requested_generation == g->generation &&
        s_geometry_active_config == g->bound_config_seq &&
        s_geometry_active_map == g->bound_map_generation &&
        p->geometry_map_generation == g->bound_map_generation &&
        p->geometry_map_crc32 == k->config[17] &&
        s_tdma_pio_spi_rx_arm_valid && s_tdma_pio_spi_rx_arm_epoch == g->bound_arm_epoch &&
        s_tdma_pio_spi_rx_sequence.observation_epoch == g->bound_observation_epoch &&
        s_tdma_pio_spi_program_persona == g->persona &&
        p->role == TDMA_PIO_SPI_ROLE_SLAVE && p->flight_resource_claimed &&
        p->flight_physical_byte_count == g->physical_bytes &&
        p->flight_payload_size == k->payload_bytes && p->flight_tail_bytes == k->tail_bytes &&
        p->baud_hz == k->config[10] && p->node_count == g->node_count &&
        p->flight_local_slot_id == g->local_slot &&
        p->geometry_topology_generation == k->topology_generation &&
        p->geometry_topology_crc32 == k->topology_crc32 &&
        p->geometry_calibration_generation == k->calibration_generation &&
        p->geometry_stage_enabled == k->stage_enabled &&
        p->geometry_marker_source == k->marker_source && p->geometry_marker_destination == k->marker_destination &&
        p->geometry_data_source == k->data_source && p->geometry_data_destination == k->data_destination &&
        p->flight_marker_offset_sample_count == k->marker_offset &&
        p->flight_sck_offset_sample_count == k->sck_offset && p->flight_data_offset_sample_count == k->data_offset &&
        p->flight_marker_phase_delay_cycles == k->marker_phase &&
        p->flight_sck_phase_delay_cycles == k->sck_phase && p->flight_data_phase_delay_cycles == k->data_phase &&
        p->flight_origin_capture_phase_delay_cycles == k->origin_phase &&
        pio_get_index(p->flight_resources.tx_pio) == k->tx_pio &&
        pio_get_index(p->flight_resources.rx_pio) == k->rx_pio &&
        p->flight_resources.tx_data_capture_sm == k->capture_sm &&
        p->flight_resources.rx_data_flight_sm == k->forward_sm &&
        p->flight_resources.data_in_capture_dma == k->capture_dma &&
        p->flight_resources.data_out_dma == k->output_dma &&
        p->tx_sck_pin == k->tx_pins[0] && p->tx_csn_pin == k->tx_pins[1] && p->tx_pin == k->tx_pins[2] &&
        p->rx_sck_pin == k->rx_pins[0] && p->rx_csn_pin == k->rx_pins[1] && p->rx_pin == k->rx_pins[2];
}

static inline void tdma_geometry_observer_record(uint32_t generation, uint32_t epoch,
    uint32_t state, uint32_t reason, uint32_t prefix_bits)
{
    if (generation == 0u || generation != s_geometry.generation ||
        generation != s_geometry.requested_generation ||
        s_geometry.observer_state == TDMA_GEOMETRY_OBSERVER_RETIRED ||
        s_geometry.observer_state == TDMA_GEOMETRY_OBSERVER_REJECTED) return;
    s_geometry.observer_epoch = epoch;
    s_geometry.observer_state = state;
    s_geometry.observer_reason = reason;
    s_geometry.observer_prefix_bits = prefix_bits;
    tdma_geometry_publish();
}

static inline bool tdma_geometry_observer_select(const tdma_pio_spi_phys_t *phys,
    tdma_frozen_geometry_snapshot_t *out)
{
    *out = s_geometry;
    return tdma_geometry_observer_current(phys, out);
}

static void tdma_geometry_stop_begin(const tdma_pio_spi_phys_t *phys)
{
    /* First call owns the capture. Subsequent partial cleanup must retain it. */
    if (!s_geometry_active) {
        if (s_geometry_pending_arm || s_geometry_request_pending) tdma_geometry_arm_failed();
        return;
    }
    s_geometry_active = false;
    const bool valid = phys->armed && s_tdma_pio_spi_rx_arm_valid &&
        s_tdma_pio_spi_rx_arm_epoch == s_geometry_active_arm &&
        s_geometry_training_valid &&
        s_tdma_pio_spi_rx_sequence.observation_epoch == s_geometry_training_observation &&
        s_tdma_pio_spi_program_persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER &&
        phys->flight_overlay_alignment_locked &&
        phys->flight_overlay_alignment_samples >= TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES &&
        phys->flight_alignment_byte_shift < phys->flight_physical_byte_count &&
        phys->flight_alignment_bit_shift < 8u && s_geometry_active_config != 0u &&
        clock_get_hz(clk_sys) == s_geometry_active_key.clock_hz;
    if (!valid) { tdma_geometry_retire(TDMA_GEOMETRY_UNTRAINED); return; }
    if (s_geometry_next_generation == UINT32_MAX) {
        tdma_geometry_retire(TDMA_GEOMETRY_EXHAUSTED); return;
    }
    s_geometry_frozen_key = s_geometry_active_key;
    s_geometry.generation = ++s_geometry_next_generation;
    s_geometry.observer_epoch = s_geometry.observer_prefix_bits = 0u;
    s_geometry.observer_state = TDMA_GEOMETRY_OBSERVER_NONE;
    s_geometry.observer_reason = TDMA_GEOMETRY_OBSERVER_OK;
    s_geometry.source_config_seq = s_geometry_active_config;
    s_geometry.source_arm_epoch = s_geometry_active_arm;
    s_geometry.source_observation_epoch = s_geometry_training_observation;
    s_geometry.source_map_generation = s_geometry_active_map;
    s_geometry.physical_bytes = phys->flight_physical_byte_count;
    s_geometry.dma_byte_shift = phys->flight_alignment_byte_shift;
    s_geometry.dma_bit_shift = phys->flight_alignment_bit_shift;
    s_geometry.training_samples = phys->flight_overlay_alignment_samples;
    s_geometry.clk_sys_hz = s_geometry_active_key.clock_hz;
    s_geometry.persona = s_geometry_active_key.persona;
    s_geometry.node_count = phys->node_count;
    s_geometry.local_slot = phys->flight_local_slot_id;
    s_geometry.reference_slot = s_geometry_active_key.config[3];
    s_geometry.topology_generation = phys->geometry_topology_generation;
    s_geometry.topology_crc32 = phys->geometry_topology_crc32;
    s_geometry.calibration_generation = phys->geometry_calibration_generation;
    s_geometry.schedule_crc32 = s_geometry_active_key.config[8];
    s_geometry.operating_profile_crc32 = s_geometry_active_key.config[9];
    s_geometry.state = TDMA_GEOMETRY_CANDIDATE;
    s_geometry.reason = TDMA_GEOMETRY_INCOMPLETE_STOP;
    tdma_geometry_publish();
}

void tdma_pio_spi_phys_geometry_stopped(void *context)
{
    const tdma_pio_spi_phys_t *phys = context;
    if (phys == NULL || !s_geometry_physical_stopped || phys->armed ||
        phys->rx_capture_active || phys->flight_resource_claimed ||
        phys->flight_origin_resource_claimed || phys->flight_origin_workspace_owned ||
        s_geometry.state != TDMA_GEOMETRY_CANDIDATE) return;
    if (clock_get_hz(clk_sys) != s_geometry.clk_sys_hz) {
        tdma_geometry_retire(TDMA_GEOMETRY_CLOCK); return;
    }
    s_geometry.state = TDMA_GEOMETRY_FROZEN;
    s_geometry.reason = TDMA_GEOMETRY_OK;
    tdma_geometry_publish();
}
