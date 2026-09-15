/* Raw VDC feedback: Core0 RefMem task owns all state. No PIO/DMA or DCO use.
 * The atomic publications are diagnostic history, never control commands. */
typedef struct {
    uint32_t mode, local, target, node_count, role_generation;
    uint32_t ring_config_seq, schedule_crc32, clock_epoch_id, clock_run_id;
} distributed_refmem_feedback_binding_t;

typedef struct {
    refmem_sync_vdc_feedback_assembly_t assembly;
    uint32_t words[sizeof(distributed_refmem_vdc_feedback_rx_snapshot_t) / 4u];
    uint32_t guard;
} distributed_refmem_feedback_source_t;

static distributed_refmem_feedback_source_t s_feedback_rx[REFMEM_SYNC_NODE_COUNT];
static uint32_t s_feedback_tx_words[sizeof(distributed_refmem_vdc_feedback_tx_snapshot_t) / 4u];
static uint32_t s_feedback_tx_guard;
static distributed_refmem_feedback_binding_t s_feedback_binding;
static uint32_t s_feedback_admission;
static bool s_feedback_ready;
static uint8_t s_feedback_tx_record[REFMEM_VDC_FEEDBACK_RECORD_SIZE];
static uint32_t s_feedback_tx_index;
static bool s_feedback_tx_active, s_feedback_ordinary_due;
_Static_assert(sizeof(distributed_refmem_feedback_source_t) <= 224u,
               "feedback per-source storage must remain bounded");
_Static_assert(sizeof(s_feedback_rx) + sizeof(s_feedback_tx_words) +
    sizeof(s_feedback_tx_record) + sizeof(s_feedback_binding) + 32u <=
    224u * PROJECT_NODE_CAPACITY + 352u, "capacity-sized feedback storage");
_Static_assert(sizeof(((distributed_refmem_vdc_feedback_rx_snapshot_t *)0)->record) ==
    REFMEM_VDC_FEEDBACK_RECORD_SIZE, "feedback diagnostic record ABI");

static uint32_t feedback_inc(uint32_t value) { return value == UINT32_MAX ? value : value + 1u; }

static void feedback_load(const uint32_t *words, void *output, size_t size)
{
    for (size_t i = 0; i < size / 4u; ++i) {
        const uint32_t word = __atomic_load_n(words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)output + i * 4u, &word, 4u);
    }
}

static void feedback_publish(uint32_t *words, uint32_t *guard, const void *input, size_t size)
{
    (void)__atomic_add_fetch(guard, 1u, __ATOMIC_ACQ_REL);
    for (size_t i = 0; i < size / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)input + i * 4u, 4u);
        __atomic_store_n(words + i, word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(guard, 1u, __ATOMIC_RELEASE);
}

static bool feedback_read_binding(tdma_service_service_t *owner,
    distributed_refmem_feedback_binding_t *out)
{
    memset(out, 0, sizeof(*out));
    if (owner == NULL) return true;
    tdma_ring_runtime_snapshot_t ring;
    vdc_dpll_manager_refmem_snapshot_t vdc;
    if (!tdma_ring_runtime_get_snapshot(&owner->ring_runtime, &ring) ||
        !vdc_dpll_manager_get_refmem_snapshot(&vdc)) return false;
    if (!ring.enabled || !ring.adapter_started || !ring.data_enabled ||
        !ring.config_seq || ring.config_seq != ring.applied_config_seq ||
        ring.node_count < 2u || ring.node_count > REFMEM_SYNC_NODE_COUNT ||
        ring.local_slot_id >= ring.node_count || ring.reference_slot_id >= ring.node_count ||
        vdc.control_profile.valid != 1u || !vdc.control_profile.generation ||
        vdc.schedule.local_slot_id != ring.local_slot_id ||
        vdc.schedule.schedule_crc32 != ring.schedule_crc32) return true;
    const uint32_t mode = vdc.control_profile.mode;
    if (mode == VDC_DPLL_CONTROL_MODE_MASTER) {
        if (ring.local_slot_id != ring.reference_slot_id) return true;
    } else if (mode == VDC_DPLL_CONTROL_MODE_FOLLOWER) {
        if (vdc.control_profile.follow_master_slot_id != ring.reference_slot_id ||
            ring.local_slot_id == ring.reference_slot_id) return true;
    } else return true;
    *out = (distributed_refmem_feedback_binding_t){
        .mode = mode, .local = ring.local_slot_id, .target = ring.reference_slot_id,
        .node_count = ring.node_count, .role_generation = vdc.control_profile.generation,
        .ring_config_seq = ring.config_seq, .schedule_crc32 = ring.schedule_crc32,
        .clock_epoch_id = vdc.clock_epoch_id, .clock_run_id = vdc.clock_run_id};
    return true;
}

static bool feedback_same_binding(const distributed_refmem_feedback_binding_t *a,
    const distributed_refmem_feedback_binding_t *b)
{ return memcmp(a, b, sizeof(*a)) == 0; }

static void feedback_cancel_tx(void)
{
    distributed_refmem_vdc_feedback_tx_snapshot_t tx;
    feedback_load(s_feedback_tx_words, &tx, sizeof(tx));
    if (s_feedback_tx_active) tx.cancel_count = feedback_inc(tx.cancel_count);
    tx.schema = 1u; tx.active = 0u; tx.fragment_index = 0u;
    s_feedback_tx_active = false; s_feedback_tx_index = 0u;
    feedback_publish(s_feedback_tx_words, &s_feedback_tx_guard, &tx, sizeof(tx));
}

static void distributed_refmem_feedback_refresh(tdma_service_service_t *owner)
{
    const uint32_t now = osal_tick_ms();
    /* Even an empty RX FIFO must release an expired partial assembly. */
    for (uint32_t i = 0; i < REFMEM_SYNC_NODE_COUNT; ++i) {
        if (refmem_sync_vdc_feedback_expire(&s_feedback_rx[i].assembly, now)) {
            distributed_refmem_vdc_feedback_rx_snapshot_t rx;
            feedback_load(s_feedback_rx[i].words, &rx, sizeof(rx));
            rx.schema = 1u; rx.source_slot = i;
            rx.timeout_count = feedback_inc(rx.timeout_count);
            feedback_publish(s_feedback_rx[i].words, &s_feedback_rx[i].guard, &rx, sizeof(rx));
        }
    }
    s_feedback_ready = false;
    distributed_refmem_feedback_binding_t current;
    if (!feedback_read_binding(owner, &current)) return;
    if (!feedback_same_binding(&current, &s_feedback_binding)) {
        uint32_t admission = 0u;
        /* Ordinary follower RX already owns its admission refresh. Only the
         * mutually exclusive MASTER receiver advances the FIFO here. */
        if (current.ring_config_seq && current.mode == VDC_DPLL_CONTROL_MODE_MASTER) {
            admission = tdma_service_core0_advance_flight_rx_admission_epoch(owner);
            if (!admission) return;
        }
        feedback_cancel_tx();
        s_feedback_ordinary_due = false;
        for (uint32_t i = 0; i < REFMEM_SYNC_NODE_COUNT; ++i) {
            refmem_sync_vdc_feedback_reset(&s_feedback_rx[i].assembly);
            distributed_refmem_vdc_feedback_rx_snapshot_t rx;
            feedback_load(s_feedback_rx[i].words, &rx, sizeof(rx));
            rx.schema = 1u; rx.active = 0u; rx.source_slot = i;
            feedback_publish(s_feedback_rx[i].words, &s_feedback_rx[i].guard, &rx, sizeof(rx));
        }
        s_feedback_binding = current;
        s_feedback_admission = admission;
    }
    s_feedback_ready = current.ring_config_seq != 0u;
}

/* -1 = transient contention, 0 = ordinary mailbox, 1 = raw fragment. */
static int distributed_refmem_feedback_prepare(tdma_service_service_t *owner)
{
    if (!s_feedback_ready) return s_feedback_tx_active ? -1 : 0;
    if (s_feedback_binding.mode != VDC_DPLL_CONTROL_MODE_FOLLOWER) return 0;
    tdma_pio_spi_event_live_snapshot_t live;
    tdma_pio_spi_event_tap_snapshot_t tap;
    distributed_refmem_feedback_binding_t after;
    if (!tdma_runtime_owner_get_event_tap(&tap)) return s_feedback_tx_active ? -1 : 0;
    if (tap.applied_valid != 1u || tap.applied.enabled != 1u || !tap.actual_valid ||
        tap.actual_prefix_bits != tap.applied.prefix_bits ||
        tap.actual_sample_delay_cycles != tap.applied.sample_delay_cycles) {
        if (s_feedback_tx_active) feedback_cancel_tx();
        return 0;
    }
    if (!tdma_runtime_owner_get_event_live_snapshot(&live) ||
        !feedback_read_binding(owner, &after)) return s_feedback_tx_active ? -1 : 0;
    if (!feedback_same_binding(&after, &s_feedback_binding)) return -1;
    if ((live.flags & (TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID)) !=
            (TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID)) {
        if (s_feedback_tx_active) feedback_cancel_tx();
        return 0;
    }
    if (s_feedback_tx_active) {
        refmem_sync_vdc_feedback_record_t active;
        if (!refmem_sync_vdc_feedback_decode(s_feedback_tx_record, after.node_count,
                after.local, after.target, &active) || active.source_arm_epoch != live.arm_epoch ||
                active.observer_epoch != live.record.epoch || active.tick_hz != live.tick_hz) {
            feedback_cancel_tx();
            return 0;
        }
        return 1;
    }
    if (s_feedback_ordinary_due) return 0;
    refmem_sync_vdc_feedback_record_t record = {
        .schema_version = REFMEM_VDC_FEEDBACK_SCHEMA,
        .domain_flags = REFMEM_VDC_FEEDBACK_DOMAIN_FLAGS,
        .source_slot = (uint8_t)after.local, .target_slot = (uint8_t)after.target,
        .source_clock_epoch_id = after.clock_epoch_id, .source_clock_run_id = after.clock_run_id,
        .source_arm_epoch = live.arm_epoch, .observer_epoch = live.record.epoch,
        .measurement_sequence = live.record.sequence, .tick_hz = live.tick_hz,
        .rx_elapsed_cycles = live.record.rx_elapsed_cycles,
        .tx_elapsed_cycles = live.record.tx_elapsed_cycles,
        .timer1_enable_before = live.timer1_enable_before,
        .timer1_enable_after = live.timer1_enable_after};
    uint8_t candidate[REFMEM_VDC_FEEDBACK_RECORD_SIZE];
    distributed_refmem_vdc_feedback_tx_snapshot_t tx;
    feedback_load(s_feedback_tx_words, &tx, sizeof(tx));
    if (!refmem_sync_vdc_feedback_encode(&record, after.node_count, candidate)) {
        tx.reject_count = feedback_inc(tx.reject_count);
        feedback_publish(s_feedback_tx_words, &s_feedback_tx_guard, &tx, sizeof(tx));
        return 0;
    }
    const refmem_sync_vdc_feedback_order_t order = refmem_sync_vdc_feedback_compare(candidate,
        tx.history_count && tx.history[0][1] == after.local && tx.history[0][2] == after.target
            ? tx.history[0] : NULL, after.node_count, after.local, after.target);
    if (order != REFMEM_VDC_FEEDBACK_ORDER_FIRST && order != REFMEM_VDC_FEEDBACK_ORDER_NEW_NAMESPACE &&
        order != REFMEM_VDC_FEEDBACK_ORDER_NEWER) return 0;
    memcpy(s_feedback_tx_record, candidate, sizeof(candidate));
    s_feedback_tx_index = 0u; s_feedback_tx_active = true;
    tx.schema = 1u; tx.active = 1u; tx.fragment_index = 0u;
    tx.ring_config_seq = after.ring_config_seq; tx.role_generation = after.role_generation;
    tx.source_slot = after.local; tx.target_slot = after.target;
    tx.schedule_crc32 = after.schedule_crc32;
    tx.clock_epoch_id = after.clock_epoch_id; tx.clock_run_id = after.clock_run_id;
    feedback_publish(s_feedback_tx_words, &s_feedback_tx_guard, &tx, sizeof(tx));
    return 1;
}

static void distributed_refmem_feedback_fragment(uint8_t *mailbox)
{
    mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET] = (uint8_t)s_feedback_tx_index;
    mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_COUNT_OFFSET] = REFMEM_VDC_FEEDBACK_FRAGMENT_COUNT;
    memcpy(mailbox + TDMA_PROCESS_IMAGE_VDC_FRAGMENT_DATA_OFFSET,
        s_feedback_tx_record + s_feedback_tx_index * REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE,
        REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE);
}

static void distributed_refmem_feedback_published(bool feedback, uint32_t sequence)
{
    (void)sequence;
    if (!feedback) { s_feedback_ordinary_due = false; return; }
    distributed_refmem_vdc_feedback_tx_snapshot_t tx;
    feedback_load(s_feedback_tx_words, &tx, sizeof(tx));
    tx.fragments_published = feedback_inc(tx.fragments_published);
    ++s_feedback_tx_index;
    if (s_feedback_tx_index == REFMEM_VDC_FEEDBACK_FRAGMENT_COUNT) {
        memcpy(tx.history[1], tx.history[0], REFMEM_VDC_FEEDBACK_RECORD_SIZE);
        memcpy(tx.history[0], s_feedback_tx_record, REFMEM_VDC_FEEDBACK_RECORD_SIZE);
        if (tx.history_count < 2u) ++tx.history_count;
        tx.groups_published = feedback_inc(tx.groups_published);
        s_feedback_tx_index = 0u; s_feedback_tx_active = false; s_feedback_ordinary_due = true;
    }
    tx.fragment_index = s_feedback_tx_index; tx.active = s_feedback_tx_active;
    feedback_publish(s_feedback_tx_words, &s_feedback_tx_guard, &tx, sizeof(tx));
}

static void distributed_refmem_feedback_receive(tdma_service_service_t *owner, uint32_t source,
    const uint8_t *mailbox, const tdma_flight_rx_view_t *view)
{
    if (!s_feedback_ready || s_feedback_binding.mode != VDC_DPLL_CONTROL_MODE_MASTER ||
        !s_feedback_admission || view->admission_epoch != s_feedback_admission ||
        source >= s_feedback_binding.node_count || source == s_feedback_binding.local ||
        mailbox[3] != TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS) return;
    distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    feedback_load(state->words, &rx, sizeof(rx));
    rx.schema = 1u; rx.source_slot = source;
    const uint32_t seq = distributed_refmem_get_le32(mailbox + TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET);
    if (mailbox[4] != source || !seq || (uint16_t)seq != distributed_refmem_get_le16(mailbox + 6u) ||
        !(mailbox[5] & (1u << s_feedback_binding.local)) ||
        distributed_refmem_get_le16(mailbox + TDMA_PROCESS_IMAGE_CRC_OFFSET) !=
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET)) {
        rx.reject_count = feedback_inc(rx.reject_count);
        state->assembly.state &= (uint8_t)~REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE;
        feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
        return;
    }
    const uint32_t now = osal_tick_ms();
    uint8_t wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE];
    const refmem_sync_vdc_feedback_result_t result = refmem_sync_vdc_feedback_push(&state->assembly,
        source, s_feedback_binding.local, s_feedback_binding.node_count, seq,
        mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET],
        mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_COUNT_OFFSET],
        mailbox + TDMA_PROCESS_IMAGE_VDC_FRAGMENT_DATA_OFFSET, now, wire);
    if (result == REFMEM_VDC_FEEDBACK_COMPLETE) {
        const refmem_sync_vdc_feedback_order_t order = refmem_sync_vdc_feedback_compare(wire,
            rx.retained && rx.record[1] == source && rx.record[2] == s_feedback_binding.local
                ? rx.record : NULL, s_feedback_binding.node_count, source, s_feedback_binding.local);
        distributed_refmem_feedback_binding_t after;
        if (!feedback_read_binding(owner, &after) || !feedback_same_binding(&after, &s_feedback_binding)) return;
        if (order == REFMEM_VDC_FEEDBACK_ORDER_FIRST || order == REFMEM_VDC_FEEDBACK_ORDER_NEW_NAMESPACE ||
            order == REFMEM_VDC_FEEDBACK_ORDER_NEWER) {
            memcpy(rx.record, wire, sizeof(wire));
            rx.retained = 1u; rx.active = 1u;
            rx.ring_config_seq = after.ring_config_seq; rx.role_generation = after.role_generation;
            rx.schedule_crc32 = after.schedule_crc32;
            rx.clock_epoch_id = after.clock_epoch_id; rx.clock_run_id = after.clock_run_id;
            rx.receive_count = feedback_inc(rx.receive_count);
            rx.last_transport_seq = seq; rx.first_rx_ms = state->assembly.first_ms; rx.last_rx_ms = now;
        } else if (order == REFMEM_VDC_FEEDBACK_ORDER_DUPLICATE) rx.duplicate_count = feedback_inc(rx.duplicate_count);
        else rx.reject_count = feedback_inc(rx.reject_count);
    } else if (result == REFMEM_VDC_FEEDBACK_DUPLICATE) rx.duplicate_count = feedback_inc(rx.duplicate_count);
    else if (result == REFMEM_VDC_FEEDBACK_EXPIRED || result == REFMEM_VDC_FEEDBACK_EXPIRED_RESTARTED)
        rx.timeout_count = feedback_inc(rx.timeout_count);
    else if (result != REFMEM_VDC_FEEDBACK_PROGRESS && result != REFMEM_VDC_FEEDBACK_RESTARTED)
        rx.reject_count = feedback_inc(rx.reject_count);
    feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
}

bool distributed_refmem_get_vdc_feedback_rx(uint32_t source,
    distributed_refmem_vdc_feedback_rx_snapshot_t *out)
{
    if (out == NULL || source >= REFMEM_SYNC_NODE_COUNT) return false;
    distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
    const uint32_t guard = __atomic_load_n(&state->guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    feedback_load(state->words, &rx, sizeof(rx));
    distributed_refmem_feedback_binding_t current;
    const bool known = feedback_read_binding(tdma_runtime_owner_get(), &current);
    rx.schema = 1u; rx.source_slot = source;
    rx.active = rx.active && known && current.mode == VDC_DPLL_CONTROL_MODE_MASTER &&
        source < current.node_count && source != current.local &&
        rx.ring_config_seq == current.ring_config_seq && rx.role_generation == current.role_generation &&
        rx.schedule_crc32 == current.schedule_crc32 && rx.clock_epoch_id == current.clock_epoch_id &&
        rx.clock_run_id == current.clock_run_id;
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&state->guard, __ATOMIC_ACQUIRE)) return false;
    *out = rx;
    return true;
}

bool distributed_refmem_get_vdc_feedback_tx(distributed_refmem_vdc_feedback_tx_snapshot_t *out)
{
    if (out == NULL) return false;
    const uint32_t guard = __atomic_load_n(&s_feedback_tx_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    distributed_refmem_vdc_feedback_tx_snapshot_t tx;
    feedback_load(s_feedback_tx_words, &tx, sizeof(tx));
    distributed_refmem_feedback_binding_t current;
    const bool known = feedback_read_binding(tdma_runtime_owner_get(), &current);
    tx.schema = 1u;
    tx.active = tx.active && known && current.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER &&
        tx.ring_config_seq == current.ring_config_seq && tx.role_generation == current.role_generation &&
        tx.source_slot == current.local && tx.target_slot == current.target &&
        tx.schedule_crc32 == current.schedule_crc32 && tx.clock_epoch_id == current.clock_epoch_id &&
        tx.clock_run_id == current.clock_run_id;
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_feedback_tx_guard, __ATOMIC_ACQUIRE)) return false;
    *out = tx;
    return true;
}
