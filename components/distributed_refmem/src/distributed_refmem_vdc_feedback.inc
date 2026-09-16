/* Typed VDC records: Core0 RefMem owns preparation/CRC and guarded reception.
 * Core1 separately admits decoded boundary commands. No PIO/DMA or DCO writes. */
typedef struct {
    uint32_t mode, local, target, node_count, role_generation;
    uint32_t ring_config_seq, schedule_crc32, clock_epoch_id, clock_run_id, session, rate_mode;
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
/* Current or last retired offer. IDs never repeat, including across STOP/ABA. */
static uint32_t s_boundary_offer_id;
/* Completed groups while pending; MAX also retires a cancelled offer. Only
 * the successful publication path may turn real completion into TX-done. */
static uint32_t s_boundary_offer_groups;
/* One selected MASTER can feed a FOLLOWER. Completed bytes remain in that
 * source's existing assembly until Core0 can validate the final handoff. */
#define BOUNDARY_ASSEMBLY_COMPLETE_PENDING 4u
/* Snapshot tag only; public diagnostics retain schema 1. No new storage. */
#define FEEDBACK_REFERENCE_PROOF_ARM 2u
static uint32_t s_boundary_pending_admission, s_boundary_pending_completed_ms;
static void distributed_refmem_boundary_service_pending(tdma_service_service_t *owner);
static void distributed_refmem_receipt_service_pending(tdma_service_service_t *owner);
_Static_assert(REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS > 0u,
               "an immutable boundary offer has a finite nonzero group budget");
_Static_assert(sizeof(distributed_refmem_vdc_feedback_rx_snapshot_t) == 192u,
               "Typed decoded union must not enlarge retained RX storage");
_Static_assert(sizeof(distributed_refmem_vdc_boundary_view_t) == 96u,
               "Core1 copies only a compact decoded command view");
_Static_assert(sizeof(distributed_refmem_feedback_source_t) <= 288u,
               "feedback per-source storage must remain bounded");
_Static_assert(sizeof(s_feedback_rx) + sizeof(s_feedback_tx_words) +
    sizeof(s_feedback_tx_record) + sizeof(s_feedback_binding) + 32u +
    sizeof(s_boundary_offer_groups) + sizeof(s_boundary_pending_admission) +
    sizeof(s_boundary_pending_completed_ms) <=
    288u * PROJECT_NODE_CAPACITY + 360u, "capacity-sized feedback storage");
_Static_assert(sizeof(((distributed_refmem_vdc_feedback_rx_snapshot_t *)0)->record) ==
    REFMEM_VDC_FEEDBACK_RECORD_SIZE, "feedback diagnostic record ABI");

static uint32_t feedback_inc(uint32_t value) { return value == UINT32_MAX ? value : value + 1u; }

static void receipt_restore_first_sequence(refmem_sync_vdc_feedback_assembly_t *assembly)
{
    const uint32_t steps = REFMEM_VDC_FEEDBACK_FRAGMENT_COUNT - 1u;
    assembly->first_sequence = assembly->last_sequence > steps
        ? assembly->last_sequence - steps : assembly->last_sequence + UINT32_MAX - steps;
}

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
    if (!tdma_ring_runtime_get_snapshot(&owner->ring_runtime, &ring)) return false;
    if (!ring.enabled || !ring.adapter_started || !ring.data_enabled ||
        !ring.config_seq || ring.config_seq != ring.applied_config_seq ||
        ring.node_count < 2u || ring.node_count > REFMEM_SYNC_NODE_COUNT ||
        ring.local_slot_id >= ring.node_count || ring.reference_slot_id >= ring.node_count) return true;
    /* Known STOP cancels typed state even when the VDC snapshot is busy. */
    if (!vdc_dpll_manager_get_refmem_snapshot(&vdc)) return false;
    if (vdc.control_profile.valid != 1u || !vdc.control_profile.generation ||
        vdc.schedule.local_slot_id != ring.local_slot_id ||
        vdc.schedule.schedule_crc32 != ring.schedule_crc32) return true;
    const uint32_t mode = vdc.control_profile.mode;
    if (mode == VDC_DPLL_CONTROL_MODE_MASTER) {
        if (ring.local_slot_id != ring.reference_slot_id) return true;
    } else if (mode == VDC_DPLL_CONTROL_MODE_FOLLOWER) {
        if (vdc.control_profile.follow_master_slot_id != ring.reference_slot_id ||
            ring.local_slot_id == ring.reference_slot_id) return true;
    } else return true;
    bool rate_mode, reference_mode;
    if (!vdc_dpll_manager_try_boundary_auto_enabled(&rate_mode) ||
        !vdc_dpll_manager_try_reference_publish_enabled(&reference_mode) ||
        (rate_mode && reference_mode)) return false;
    *out = (distributed_refmem_feedback_binding_t){
        .mode = mode, .local = ring.local_slot_id, .target = ring.reference_slot_id,
        .node_count = ring.node_count, .role_generation = vdc.control_profile.generation,
        .ring_config_seq = ring.config_seq, .schedule_crc32 = ring.schedule_crc32,
        .clock_epoch_id = vdc.clock_epoch_id, .clock_run_id = vdc.clock_run_id,
        .session = vdc_dpll_manager_feedback_session(),
        /* Reuse the existing word; no new per-node transport storage. */
        .rate_mode = reference_mode ? VDC_BOUNDARY_MODE_REFERENCE : (rate_mode ? 1u : 0u)};
    return true;
}

static bool feedback_same_binding(const distributed_refmem_feedback_binding_t *a,
    const distributed_refmem_feedback_binding_t *b)
{ return memcmp(a, b, sizeof(*a)) == 0; }

static bool feedback_record_binding(const refmem_sync_vdc_feedback_record_t *r,
    const distributed_refmem_feedback_binding_t *binding)
{
    if (!binding->session) return r->schema_version == REFMEM_VDC_FEEDBACK_SCHEMA;
    if (binding->rate_mode == VDC_BOUNDARY_MODE_AUTO)
        return r->schema_version == REFMEM_VDC_FEEDBACK_RATE_SCHEMA &&
        r->rate.control_session == binding->session;
    return r->schema_version == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA &&
        r->model.control_session == binding->session;
}

static bool boundary_offer_pending(void)
{
    return s_boundary_offer_id != 0u &&
        s_boundary_offer_groups < REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS;
}

static void feedback_cancel_tx(void)
{
    distributed_refmem_vdc_feedback_tx_snapshot_t tx;
    feedback_load(s_feedback_tx_words, &tx, sizeof(tx));
    if (s_feedback_tx_active || boundary_offer_pending()) {
        tx.cancel_count = feedback_inc(tx.cancel_count);
        s_feedback_ordinary_due = true;
    }
    s_boundary_offer_groups = REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS;
    tx.schema = 1u; tx.active = 0u; tx.fragment_index = 0u;
    s_feedback_tx_active = false; s_feedback_tx_index = 0u;
    feedback_publish(s_feedback_tx_words, &s_feedback_tx_guard, &tx, sizeof(tx));
}

/* Known inactive still permits ordinary RX; unavailable admission does not. */
static bool distributed_refmem_feedback_refresh(tdma_service_service_t *owner)
{
    const uint32_t now = osal_tick_ms();
    /* Even an empty RX FIFO must release an expired partial assembly. */
    for (uint32_t i = 0; i < REFMEM_SYNC_NODE_COUNT; ++i) {
        refmem_sync_vdc_feedback_assembly_t *assembly = &s_feedback_rx[i].assembly;
        const bool pending_expired = (assembly->state & BOUNDARY_ASSEMBLY_COMPLETE_PENDING) &&
            (uint32_t)(now - assembly->first_ms) >= REFMEM_VDC_FEEDBACK_ASSEMBLY_TIMEOUT_MS;
        if (pending_expired) {
            if (s_feedback_binding.mode == VDC_DPLL_CONTROL_MODE_MASTER &&
                s_feedback_binding.rate_mode == VDC_BOUNDARY_MODE_REFERENCE)
                receipt_restore_first_sequence(assembly);
            assembly->state &= (uint8_t)~BOUNDARY_ASSEMBLY_COMPLETE_PENDING;
            s_boundary_pending_admission = s_boundary_pending_completed_ms = 0u;
        }
        if (pending_expired || refmem_sync_vdc_feedback_expire(assembly, now)) {
            distributed_refmem_vdc_feedback_rx_snapshot_t rx;
            feedback_load(s_feedback_rx[i].words, &rx, sizeof(rx));
            if (rx.schema != FEEDBACK_REFERENCE_PROOF_ARM) rx.schema = 1u;
            rx.source_slot = i;
            rx.timeout_count = feedback_inc(rx.timeout_count);
            feedback_publish(s_feedback_rx[i].words, &s_feedback_rx[i].guard, &rx, sizeof(rx));
        }
    }
    s_feedback_ready = false;
    distributed_refmem_feedback_binding_t current;
    if (!feedback_read_binding(owner, &current)) return false;
    if (!feedback_same_binding(&current, &s_feedback_binding)) {
        uint32_t admission = 0u;
        /* Ordinary follower RX already owns its admission refresh. Only the
         * mutually exclusive MASTER receiver advances the FIFO here. */
        if (current.ring_config_seq && current.mode == VDC_DPLL_CONTROL_MODE_MASTER) {
            admission = tdma_service_core0_advance_flight_rx_admission_epoch(owner);
            if (!admission) return false;
        }
        feedback_cancel_tx();
        for (uint32_t i = 0; i < REFMEM_SYNC_NODE_COUNT; ++i) {
            refmem_sync_vdc_feedback_reset(&s_feedback_rx[i].assembly);
            distributed_refmem_vdc_feedback_rx_snapshot_t rx;
            feedback_load(s_feedback_rx[i].words, &rx, sizeof(rx));
            if (rx.schema != FEEDBACK_REFERENCE_PROOF_ARM) rx.schema = 1u;
            rx.active = 0u; rx.source_slot = i;
            feedback_publish(s_feedback_rx[i].words, &s_feedback_rx[i].guard, &rx, sizeof(rx));
        }
        s_boundary_pending_admission = s_boundary_pending_completed_ms = 0u;
        s_feedback_binding = current;
        s_feedback_admission = admission;
    }
    s_feedback_ready = current.ring_config_seq != 0u;
    /* Exactly one deferred attempt per Core0 service, including empty FIFO.
     * The receive path below never retries an already-pending transaction. */
    distributed_refmem_boundary_service_pending(owner);
    distributed_refmem_receipt_service_pending(owner);
    return true;
}

/* Master reuses the otherwise idle feedback TX record across bounded repeats.
 * Every fragment revalidates the original offer; a successful ordinary frame
 * separates groups. Cancellation retires its ID even in the inter-group gap. */
static int distributed_refmem_boundary_prepare(tdma_service_service_t *owner)
{
    refmem_sync_vdc_boundary_command_t command;
    uint32_t offer_id = 0u;
    const int available = vdc_dpll_manager_copy_boundary_command_offer(
        s_feedback_binding.ring_config_seq, s_feedback_binding.role_generation,
        s_feedback_binding.local, s_feedback_binding.schedule_crc32,
        s_feedback_binding.session, &command, &offer_id);
    if (available < 0) return s_feedback_tx_active || boundary_offer_pending() ? -1 : 0;
    if (available == 0 || offer_id == 0u) {
        if (s_feedback_tx_active || boundary_offer_pending()) feedback_cancel_tx();
        return 0;
    }
    distributed_refmem_feedback_binding_t after;
    if (!feedback_read_binding(owner, &after))
        return s_feedback_tx_active || boundary_offer_pending() ? -1 : 0;
    if (!feedback_same_binding(&after, &s_feedback_binding)) {
        if (s_feedback_tx_active || boundary_offer_pending()) feedback_cancel_tx();
        return -1;
    }
    uint8_t candidate[REFMEM_VDC_FEEDBACK_RECORD_SIZE];
    if (command.source_slot != after.local || command.target_slot == after.local ||
        command.control_session != after.session || !after.session ||
        command.flags != (after.rate_mode ? REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS :
            REFMEM_VDC_BOUNDARY_COMMAND_FLAGS) ||
        command.schedule_crc32 != after.schedule_crc32 ||
        !refmem_sync_vdc_boundary_command_encode(&command, after.node_count, candidate)) {
        if (s_feedback_tx_active || boundary_offer_pending()) feedback_cancel_tx();
        return 0;
    }
    if (s_feedback_tx_active || boundary_offer_pending()) {
        if (offer_id != s_boundary_offer_id ||
            memcmp(candidate, s_feedback_tx_record, sizeof(candidate)) != 0) {
            feedback_cancel_tx();
            return 0;
        }
        if (s_feedback_tx_active) return 2;
    }
    if (s_feedback_ordinary_due || offer_id < s_boundary_offer_id ||
        (offer_id == s_boundary_offer_id && !boundary_offer_pending())) return 0;
    distributed_refmem_vdc_feedback_tx_snapshot_t tx;
    feedback_load(s_feedback_tx_words, &tx, sizeof(tx));
    if (offer_id > s_boundary_offer_id) {
        memcpy(s_feedback_tx_record, candidate, sizeof(candidate));
        s_boundary_offer_id = offer_id;
        s_boundary_offer_groups = 0u;
    }
    s_feedback_tx_index = 0u; s_feedback_tx_active = true;
    tx.schema = 1u; tx.active = 1u; tx.fragment_index = 0u;
    tx.ring_config_seq = after.ring_config_seq; tx.role_generation = after.role_generation;
    tx.source_slot = after.local; tx.target_slot = command.target_slot;
    tx.schedule_crc32 = after.schedule_crc32;
    tx.clock_epoch_id = after.clock_epoch_id; tx.clock_run_id = after.clock_run_id;
    tx.control_session = after.session;
    feedback_publish(s_feedback_tx_words, &s_feedback_tx_guard, &tx, sizeof(tx));
    return 2;
}

static bool feedback_rx_binding_current(const distributed_refmem_vdc_feedback_rx_snapshot_t *rx,
    const distributed_refmem_feedback_binding_t *binding)
{
    return rx->ring_config_seq == binding->ring_config_seq &&
        rx->role_generation == binding->role_generation && rx->schedule_crc32 == binding->schedule_crc32 &&
        rx->clock_epoch_id == binding->clock_epoch_id && rx->clock_run_id == binding->clock_run_id;
}

/* A retained reference is already queued only if these exact bytes are in
 * the frozen TX group or were fully published under the current binding. */
static bool reference_receipt_covered(const uint8_t reference[64])
{
    uint8_t receipt[64];
    if (!refmem_sync_vdc_receipt_ack_encode(reference, s_feedback_binding.node_count, receipt)) return false;
    distributed_refmem_vdc_feedback_tx_snapshot_t tx;
    feedback_load(s_feedback_tx_words, &tx, sizeof(tx));
    if (tx.ring_config_seq != s_feedback_binding.ring_config_seq ||
        tx.role_generation != s_feedback_binding.role_generation ||
        tx.schedule_crc32 != s_feedback_binding.schedule_crc32 ||
        tx.clock_epoch_id != s_feedback_binding.clock_epoch_id ||
        tx.clock_run_id != s_feedback_binding.clock_run_id ||
        tx.control_session != s_feedback_binding.session) return false;
    return (s_feedback_tx_active && !memcmp(receipt, s_feedback_tx_record, sizeof(receipt))) ||
        (tx.history_count && !memcmp(receipt, tx.history[0], sizeof(receipt)));
}

static int distributed_refmem_receipt_prepare(tdma_service_service_t *owner)
{
    distributed_refmem_feedback_binding_t after;
    if (!feedback_read_binding(owner, &after)) return s_feedback_tx_active ? -1 : 0;
    if (!feedback_same_binding(&after, &s_feedback_binding)) return -1;
    if (!after.session || after.target >= after.node_count) return 0;
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    feedback_load(s_feedback_rx[after.target].words, &rx, sizeof(rx));
    if (s_feedback_tx_active) {
        uint8_t original[64];
        refmem_sync_vdc_feedback_record_t frozen;
        if (rx.schema == FEEDBACK_REFERENCE_PROOF_ARM || !rx.active || !rx.retained ||
            !feedback_rx_binding_current(&rx, &after) ||
            !refmem_sync_vdc_receipt_ack_restore(s_feedback_tx_record, after.node_count,
                after.local, after.target, original) ||
            !refmem_sync_vdc_feedback_decode(original, after.node_count, after.target, after.local, &frozen) ||
            frozen.source_arm_epoch != rx.sample.source_arm_epoch ||
            frozen.observer_epoch != rx.sample.observer_epoch ||
            frozen.source_clock_epoch_id != rx.sample.source_clock_epoch_id ||
            frozen.source_clock_run_id != rx.sample.source_clock_run_id ||
            frozen.model.control_session != after.session) {
            feedback_cancel_tx();
            return 0;
        }
        return 1; /* Later events in this namespace cannot mutate the group. */
    }
    if (s_feedback_ordinary_due) return 0;
    if (rx.schema == FEEDBACK_REFERENCE_PROOF_ARM || !rx.active || !rx.retained ||
        !feedback_rx_binding_current(&rx, &after) || !feedback_record_binding(&rx.sample, &after) ||
        reference_receipt_covered(rx.record) ||
        !refmem_sync_vdc_receipt_ack_encode(rx.record, after.node_count, s_feedback_tx_record)) return 0;
    distributed_refmem_vdc_feedback_tx_snapshot_t tx;
    feedback_load(s_feedback_tx_words, &tx, sizeof(tx));
    s_feedback_tx_index = 0u; s_feedback_tx_active = true;
    tx.schema = 1u; tx.active = 1u; tx.fragment_index = 0u;
    tx.ring_config_seq = after.ring_config_seq; tx.role_generation = after.role_generation;
    tx.source_slot = after.local; tx.target_slot = after.target; tx.schedule_crc32 = after.schedule_crc32;
    tx.clock_epoch_id = after.clock_epoch_id; tx.clock_run_id = after.clock_run_id;
    tx.control_session = after.session;
    feedback_publish(s_feedback_tx_words, &s_feedback_tx_guard, &tx, sizeof(tx));
    return 1;
}

/* REFERENCE is transport only. Project a fresh NO1 origin event once using
 * its committed DCO, then freeze all 16 fragments. Origin's single acquisition
 * epoch supplies both source ARM and observer namespaces (zero-extended into
 * schema2); it is not the follower EVENT observer. Targets rotate by the last
 * completed TX header, so FIFO backpressure cannot skip a target. */
static int distributed_refmem_reference_prepare(tdma_service_service_t *owner)
{
    if (s_feedback_binding.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER)
        return distributed_refmem_receipt_prepare(owner);
    distributed_refmem_feedback_binding_t after;
    uint32_t epoch;
    if (!feedback_read_binding(owner, &after) ||
        !tdma_runtime_owner_get_origin_reference_epoch(&epoch)) return s_feedback_tx_active ? -1 : 0;
    if (!feedback_same_binding(&after, &s_feedback_binding)) return -1;
    if (!epoch || !after.session) {
        if (s_feedback_tx_active) feedback_cancel_tx();
        return 0;
    }
    if (s_feedback_tx_active) {
        refmem_sync_vdc_feedback_record_t active;
        if (!refmem_sync_vdc_feedback_decode(s_feedback_tx_record, after.node_count,
                after.local, s_feedback_tx_record[2], &active) ||
            active.source_arm_epoch != epoch || active.observer_epoch != epoch ||
            !feedback_record_binding(&active, &after)) {
            feedback_cancel_tx();
            return 0;
        }
        return 1;
    }
    if (s_feedback_ordinary_due) return 0;
    tdma_origin_raw_reference_t reference;
    if (!tdma_runtime_owner_get_origin_raw_reference(&reference) || reference.epoch != epoch) return 0;
    distributed_refmem_vdc_feedback_tx_snapshot_t tx;
    feedback_load(s_feedback_tx_words, &tx, sizeof(tx));
    uint32_t target = after.local;
    if (tx.history_count && tx.history[0][0] == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA &&
        tx.history[0][1] == after.local && tx.history[0][2] < after.node_count) {
        refmem_sync_vdc_feedback_record_t previous;
        if (!refmem_sync_vdc_feedback_decode(tx.history[0], after.node_count,
                after.local, tx.history[0][2], &previous)) return 0;
        target = previous.target_slot;
        if (previous.model.control_session == after.session &&
            previous.source_arm_epoch == epoch &&
            reference.sequence <= previous.measurement_sequence) return 0;
    }
    target = target + 1u == after.node_count ? 0u : target + 1u;
    if (target == after.local) target = target + 1u == after.node_count ? 0u : target + 1u;
    vdc_dpll_manager_projected_event_t event;
    if (!vdc_dpll_manager_project_feedback_event(after.session, after.role_generation,
        after.clock_epoch_id, after.clock_run_id, after.local, after.schedule_crc32,
        reference.tick_hz, reference.timer_lower, reference.timer_upper, &event)) return 0;
    distributed_refmem_feedback_binding_t final;
    uint32_t final_epoch;
    if (!feedback_read_binding(owner, &final) || !feedback_same_binding(&after, &final) ||
        !tdma_runtime_owner_get_origin_reference_epoch(&final_epoch) || final_epoch != epoch) return -1;
    const refmem_sync_vdc_feedback_record_t record = {
        .schema_version = REFMEM_VDC_FEEDBACK_MODEL_SCHEMA,
        .domain_flags = REFMEM_VDC_FEEDBACK_MODEL_DOMAIN_FLAGS,
        .source_slot = (uint8_t)after.local, .target_slot = (uint8_t)target,
        .source_clock_epoch_id = after.clock_epoch_id, .source_clock_run_id = after.clock_run_id,
        .source_arm_epoch = epoch, .observer_epoch = epoch,
        .measurement_sequence = reference.sequence, .tick_hz = reference.tick_hz,
        .model = {.output_ns_lo = event.output_ns_lo, .output_ns_hi = event.output_ns_hi,
            .model_token = event.model_token, .applied_command_seq = event.applied_command_seq,
            .control_session = after.session}};
    if (!refmem_sync_vdc_feedback_encode(&record, after.node_count, s_feedback_tx_record)) return 0;
    s_feedback_tx_index = 0u; s_feedback_tx_active = true;
    tx.schema = 1u; tx.active = 1u; tx.fragment_index = 0u;
    tx.ring_config_seq = after.ring_config_seq; tx.role_generation = after.role_generation;
    tx.source_slot = after.local; tx.target_slot = target; tx.schedule_crc32 = after.schedule_crc32;
    tx.clock_epoch_id = after.clock_epoch_id; tx.clock_run_id = after.clock_run_id;
    tx.control_session = after.session;
    feedback_publish(s_feedback_tx_words, &s_feedback_tx_guard, &tx, sizeof(tx));
    return 1;
}

/* -1 = contention, 0 = ordinary, 1 = feedback fragment, 2 = boundary command. */
static int distributed_refmem_feedback_prepare(tdma_service_service_t *owner)
{
    if (!s_feedback_ready) return s_feedback_tx_active || boundary_offer_pending() ? -1 : 0;
    if (s_feedback_binding.rate_mode == VDC_BOUNDARY_MODE_REFERENCE)
        return distributed_refmem_reference_prepare(owner);
    if (s_feedback_binding.mode == VDC_DPLL_CONTROL_MODE_MASTER)
        return distributed_refmem_boundary_prepare(owner);
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
                active.observer_epoch != live.record.epoch || active.tick_hz != live.tick_hz ||
                !feedback_record_binding(&active, &after)) {
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
    if (after.session) {
        if (live.timer1_enable_before > UINT64_MAX - live.record.rx_elapsed_cycles ||
            live.timer1_enable_after > UINT64_MAX - live.record.rx_elapsed_cycles) return 0;
        memset(&record.model, 0, sizeof(record.model));
        if (after.rate_mode) {
            vdc_dpll_manager_rate_event_t event;
            if (!vdc_dpll_manager_project_rate_feedback_event(after.session, after.role_generation,
                    after.clock_epoch_id, after.clock_run_id, after.local, after.schedule_crc32,
                    live.tick_hz, live.timer1_enable_before + live.record.rx_elapsed_cycles,
                    live.timer1_enable_after + live.record.rx_elapsed_cycles,
                    live.record.rx_elapsed_cycles, &event)) return 0;
            record.schema_version = REFMEM_VDC_FEEDBACK_RATE_SCHEMA;
            record.domain_flags = REFMEM_VDC_FEEDBACK_RATE_FLAGS;
            record.rate.absolute_output_ns_lo = event.absolute_output_ns_lo;
            record.rate.coordinate_ns = event.coordinate_ns;
            record.rate.model_token = event.model_token;
            record.rate.applied_command_seq = event.applied_command_seq;
            record.rate.control_session = after.session;
        } else {
            vdc_dpll_manager_projected_event_t event;
            if (!vdc_dpll_manager_project_feedback_event(after.session, after.role_generation,
                after.clock_epoch_id, after.clock_run_id, after.local, after.schedule_crc32,
                live.tick_hz, live.timer1_enable_before + live.record.rx_elapsed_cycles,
                live.timer1_enable_after + live.record.rx_elapsed_cycles, &event)) return 0;
            record.schema_version = REFMEM_VDC_FEEDBACK_MODEL_SCHEMA;
            record.domain_flags = REFMEM_VDC_FEEDBACK_MODEL_DOMAIN_FLAGS;
            record.model.output_ns_lo = event.output_ns_lo;
            record.model.output_ns_hi = event.output_ns_hi;
            record.model.model_token = event.model_token;
            record.model.applied_command_seq = event.applied_command_seq;
            record.model.control_session = after.session;
        }
        distributed_refmem_feedback_binding_t final;
        if (!feedback_read_binding(owner, &final) || !feedback_same_binding(&after, &final)) return -1;
    }
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
    tx.control_session = after.session;
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
        if (s_feedback_binding.rate_mode == VDC_BOUNDARY_MODE_REFERENCE &&
            s_feedback_binding.mode == VDC_DPLL_CONTROL_MODE_MASTER &&
            s_feedback_tx_record[0] == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA &&
            s_feedback_tx_record[2] < s_feedback_binding.node_count) {
            const uint32_t source = s_feedback_tx_record[2];
            distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
            distributed_refmem_vdc_feedback_rx_snapshot_t rx;
            feedback_load(state->words, &rx, sizeof(rx));
            /* The decoded union is idle on a REFERENCE master. This proof
             * denotes a completed FIFO publication, never physical delivery. */
            if (rx.schema != FEEDBACK_REFERENCE_PROOF_ARM ||
                !feedback_rx_binding_current(&rx, &s_feedback_binding)) {
                rx.retained = 0u;
                memset(rx.record, 0, sizeof(rx.record));
            }
            rx.schema = FEEDBACK_REFERENCE_PROOF_ARM; rx.source_slot = source; rx.active = 1u;
            rx.ring_config_seq = s_feedback_binding.ring_config_seq;
            rx.role_generation = s_feedback_binding.role_generation;
            rx.schedule_crc32 = s_feedback_binding.schedule_crc32;
            rx.clock_epoch_id = s_feedback_binding.clock_epoch_id; rx.clock_run_id = s_feedback_binding.clock_run_id;
            memcpy(rx.reference_proof, s_feedback_tx_record, sizeof(rx.reference_proof));
            feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
        }
        memcpy(tx.history[1], tx.history[0], REFMEM_VDC_FEEDBACK_RECORD_SIZE);
        memcpy(tx.history[0], s_feedback_tx_record, REFMEM_VDC_FEEDBACK_RECORD_SIZE);
        if (tx.history_count < 2u) ++tx.history_count;
        tx.groups_published = feedback_inc(tx.groups_published);
        s_feedback_tx_index = 0u; s_feedback_tx_active = false; s_feedback_ordinary_due = true;
        if (s_feedback_tx_record[0] == REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA) {
            ++s_boundary_offer_groups;
            if (s_boundary_offer_groups == REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS)
                vdc_dpll_manager_boundary_command_tx_done_core0(s_boundary_offer_id);
        }
    }
    tx.fragment_index = s_feedback_tx_index; tx.active = s_feedback_tx_active;
    feedback_publish(s_feedback_tx_words, &s_feedback_tx_guard, &tx, sizeof(tx));
}

static bool distributed_refmem_boundary_admitted(uint32_t source,
    uint32_t admission_epoch)
{
    const distributed_refmem_vdc_flight_rx_binding_t *b = &s_vdc_flight_rx_binding;
    return s_feedback_ready && s_feedback_binding.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER &&
        s_vdc_flight_rx_admission_ready && b->active && b->rx_admission_epoch &&
        admission_epoch == b->rx_admission_epoch &&
        source == b->source_slot && source == s_feedback_binding.target &&
        b->local_slot == s_feedback_binding.local &&
        b->control_generation == s_feedback_binding.role_generation &&
        b->ring_config_seq == s_feedback_binding.ring_config_seq &&
        b->schedule_crc32 == s_feedback_binding.schedule_crc32 &&
        b->clock_epoch_id == s_feedback_binding.clock_epoch_id &&
        b->clock_run_id == s_feedback_binding.clock_run_id;
}

static void distributed_refmem_boundary_service_pending(tdma_service_service_t *owner)
{
    const uint32_t source = s_feedback_binding.target;
    if (s_feedback_binding.mode != VDC_DPLL_CONTROL_MODE_FOLLOWER ||
        source >= REFMEM_SYNC_NODE_COUNT) return;
    distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
    if (!(state->assembly.state & BOUNDARY_ASSEMBLY_COMPLETE_PENDING)) return;
    distributed_refmem_feedback_binding_t after;
    distributed_refmem_vdc_flight_rx_binding_t ordinary;
    /* Unavailable snapshots leave the complete bytes private to Core0. A
     * known lifecycle change retires them, even if another getter is busy. */
    bool cancelled = s_boundary_pending_admission != s_vdc_flight_rx_binding.rx_admission_epoch;
    if (!cancelled) {
        if (!feedback_read_binding(owner, &after)) return;
        cancelled = !feedback_same_binding(&after, &s_feedback_binding);
    }
    if (!cancelled) {
        if (!distributed_refmem_vdc_flight_rx_read_binding(&ordinary)) return;
        cancelled = !distributed_refmem_vdc_flight_rx_same_binding(
            &ordinary, &s_vdc_flight_rx_binding);
    }
    if (!cancelled && !distributed_refmem_boundary_admitted(source, s_boundary_pending_admission))
        return; /* Refresh could not establish current admission this beat. */

    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    feedback_load(state->words, &rx, sizeof(rx));
    if (rx.schema != FEEDBACK_REFERENCE_PROOF_ARM) rx.schema = 1u;
    rx.source_slot = source;
    refmem_sync_vdc_boundary_command_t command;
    const uint8_t *wire = state->assembly.payload;
    if (!cancelled && after.rate_mode == VDC_BOUNDARY_MODE_REFERENCE) {
        /* One retained unscheduled reference plus one complete assembly may
         * wait behind the immutable ACK in TX. Never silently overwrite an
         * unscheduled receipt, and never require an EVENT/DCO match. */
        refmem_sync_vdc_feedback_record_t sample;
        const bool decoded = refmem_sync_vdc_feedback_decode(wire, after.node_count,
            source, after.local, &sample);
        if (!decoded || !after.session || !feedback_record_binding(&sample, &after)) {
            rx.reject_count = feedback_inc(rx.reject_count);
        } else {
            const refmem_sync_vdc_feedback_order_t order = refmem_sync_vdc_feedback_compare(wire,
                rx.retained && rx.record[0] == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA &&
                    rx.record[1] == source && rx.record[2] == after.local ? rx.record : NULL,
                after.node_count, source, after.local);
            if (order == REFMEM_VDC_FEEDBACK_ORDER_FIRST ||
                order == REFMEM_VDC_FEEDBACK_ORDER_NEW_NAMESPACE ||
                order == REFMEM_VDC_FEEDBACK_ORDER_NEWER) {
                if (rx.active && rx.retained && feedback_rx_binding_current(&rx, &after) &&
                    !reference_receipt_covered(rx.record)) return;
                rx.schema = 1u;
                rx.sample = sample;
                memcpy(rx.record, wire, REFMEM_VDC_FEEDBACK_RECORD_SIZE);
                rx.retained = 1u; rx.active = 1u;
                rx.ring_config_seq = after.ring_config_seq; rx.role_generation = after.role_generation;
                rx.schedule_crc32 = after.schedule_crc32;
                /* These are receiver-local binding fields. Remote clock
                 * identifiers stay untouched inside sample/record. */
                rx.clock_epoch_id = after.clock_epoch_id; rx.clock_run_id = after.clock_run_id;
                rx.receive_count = feedback_inc(rx.receive_count);
                rx.last_transport_seq = state->assembly.last_sequence;
                rx.first_rx_ms = state->assembly.first_ms;
                rx.last_rx_ms = s_boundary_pending_completed_ms;
            } else if (order == REFMEM_VDC_FEEDBACK_ORDER_DUPLICATE)
                rx.duplicate_count = feedback_inc(rx.duplicate_count);
            else rx.reject_count = feedback_inc(rx.reject_count);
        }
        state->assembly.state &= (uint8_t)~BOUNDARY_ASSEMBLY_COMPLETE_PENDING;
        s_boundary_pending_admission = s_boundary_pending_completed_ms = 0u;
        feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
        return;
    }
    if (cancelled ||
        !refmem_sync_vdc_boundary_command_decode(wire, after.node_count, source, after.local, &command) ||
        !after.session || command.control_session != after.session ||
        command.flags != (after.rate_mode ? REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS :
            REFMEM_VDC_BOUNDARY_COMMAND_FLAGS) ||
        command.schedule_crc32 != after.schedule_crc32 ||
        command.target_clock_epoch_id != after.clock_epoch_id ||
        command.target_clock_run_id != after.clock_run_id) {
        rx.reject_count = feedback_inc(rx.reject_count);
    } else {
        const bool same_namespace = rx.retained &&
            rx.record[0] == REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA &&
            rx.ring_config_seq == after.ring_config_seq && rx.role_generation == after.role_generation &&
            rx.command.control_session == command.control_session &&
            rx.command.target_clock_epoch_id == command.target_clock_epoch_id &&
            rx.command.target_clock_run_id == command.target_clock_run_id &&
            rx.command.target_arm_epoch == command.target_arm_epoch &&
            rx.command.target_observer_epoch == command.target_observer_epoch;
        if (same_namespace && command.command_seq <= rx.command.command_seq) {
            if (command.command_seq == rx.command.command_seq &&
                !memcmp(rx.record, wire, REFMEM_VDC_FEEDBACK_RECORD_SIZE))
                rx.duplicate_count = feedback_inc(rx.duplicate_count);
            else rx.reject_count = feedback_inc(rx.reject_count);
        } else {
            rx.schema = 1u;
            rx.command = command;
            memcpy(rx.record, wire, REFMEM_VDC_FEEDBACK_RECORD_SIZE);
            rx.retained = 1u; rx.active = 1u;
            rx.ring_config_seq = after.ring_config_seq; rx.role_generation = after.role_generation;
            rx.schedule_crc32 = after.schedule_crc32;
            rx.clock_epoch_id = after.clock_epoch_id; rx.clock_run_id = after.clock_run_id;
            rx.receive_count = feedback_inc(rx.receive_count);
            rx.last_transport_seq = state->assembly.last_sequence;
            rx.first_rx_ms = state->assembly.first_ms;
            rx.last_rx_ms = s_boundary_pending_completed_ms;
        }
    }
    state->assembly.state &= (uint8_t)~BOUNDARY_ASSEMBLY_COMPLETE_PENDING;
    s_boundary_pending_admission = s_boundary_pending_completed_ms = 0u;
    feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
}

static void distributed_refmem_boundary_receive(tdma_service_service_t *owner, uint32_t source,
    const uint8_t *mailbox, const tdma_flight_rx_view_t *view)
{
    if (source >= s_feedback_binding.node_count || view == NULL ||
        !distributed_refmem_boundary_admitted(source, view->admission_epoch)) return;
    distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
    /* Do not overwrite complete bytes or retry once for every incoming mail.
     * Ordinary RefMem parsing and FIFO release remain with the caller. */
    if (state->assembly.state & BOUNDARY_ASSEMBLY_COMPLETE_PENDING) {
        if (s_feedback_binding.rate_mode == VDC_BOUNDARY_MODE_REFERENCE) {
            distributed_refmem_vdc_feedback_rx_snapshot_t blocked;
            feedback_load(state->words, &blocked, sizeof(blocked));
            blocked.reject_count = feedback_inc(blocked.reject_count);
            feedback_publish(state->words, &state->guard, &blocked, sizeof(blocked));
        }
        return;
    }
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    feedback_load(state->words, &rx, sizeof(rx));
    if (rx.schema != FEEDBACK_REFERENCE_PROOF_ARM) rx.schema = 1u;
    rx.source_slot = source;
    const uint32_t seq = distributed_refmem_get_le32(mailbox + TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET);
    if (mailbox[4] != source || !seq || (uint16_t)seq != distributed_refmem_get_le16(mailbox + 6u) ||
        !(mailbox[5] & (1u << s_feedback_binding.local)) ||
        distributed_refmem_get_le16(mailbox + TDMA_PROCESS_IMAGE_CRC_OFFSET) !=
            tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET)) {
        state->assembly.state &= (uint8_t)~REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE;
        rx.reject_count = feedback_inc(rx.reject_count);
        feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
        return;
    }
    const bool reference = s_feedback_binding.rate_mode == VDC_BOUNDARY_MODE_REFERENCE;
    if (mailbox[3] != (reference ? TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS :
                                 TDMA_PROCESS_IMAGE_VDC_BOUNDARY_COMMAND_MESSAGE_CLASS)) {
        /* A typed or ordinary group boundary cancels an unfinished command;
         * its VDC bytes must never be assembled as command continuation. */
        if (state->assembly.state & REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE) {
            state->assembly.state &= (uint8_t)~REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE;
            rx.reject_count = feedback_inc(rx.reject_count);
            feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
        }
        return;
    }
    const uint32_t now = osal_tick_ms();
    const refmem_sync_vdc_feedback_result_t result = reference
        ? refmem_sync_vdc_feedback_push(&state->assembly, source, s_feedback_binding.local,
            s_feedback_binding.node_count, seq,
            mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET],
            mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_COUNT_OFFSET],
            mailbox + TDMA_PROCESS_IMAGE_VDC_FRAGMENT_DATA_OFFSET, now, rx.record)
        : refmem_sync_vdc_boundary_command_push(&state->assembly, source, s_feedback_binding.local,
            s_feedback_binding.node_count, seq,
            mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET],
            mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_COUNT_OFFSET],
            mailbox + TDMA_PROCESS_IMAGE_VDC_FRAGMENT_DATA_OFFSET, now, rx.record);
    if (result == REFMEM_VDC_FEEDBACK_COMPLETE) {
        s_boundary_pending_admission = view->admission_epoch;
        s_boundary_pending_completed_ms = now;
        state->assembly.state |= BOUNDARY_ASSEMBLY_COMPLETE_PENDING;
        distributed_refmem_boundary_service_pending(owner);
        return;
    } else if (result == REFMEM_VDC_FEEDBACK_DUPLICATE) rx.duplicate_count = feedback_inc(rx.duplicate_count);
    else if (result == REFMEM_VDC_FEEDBACK_EXPIRED || result == REFMEM_VDC_FEEDBACK_EXPIRED_RESTARTED)
        rx.timeout_count = feedback_inc(rx.timeout_count);
    else if (result != REFMEM_VDC_FEEDBACK_PROGRESS && result != REFMEM_VDC_FEEDBACK_RESTARTED)
        rx.reject_count = feedback_inc(rx.reject_count);
    feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
}

static bool reference_proof_current(const distributed_refmem_vdc_feedback_rx_snapshot_t *rx,
    uint32_t source, const distributed_refmem_feedback_binding_t *binding, uint32_t epoch)
{
    refmem_sync_vdc_feedback_record_t record;
    return rx->schema == FEEDBACK_REFERENCE_PROOF_ARM && rx->active && epoch && binding->session &&
        binding->rate_mode == VDC_BOUNDARY_MODE_REFERENCE && binding->mode == VDC_DPLL_CONTROL_MODE_MASTER &&
        feedback_rx_binding_current(rx, binding) &&
        refmem_sync_vdc_feedback_decode(rx->reference_proof, binding->node_count,
            binding->local, source, &record) && feedback_record_binding(&record, binding) &&
        record.source_arm_epoch == epoch && record.observer_epoch == epoch &&
        record.source_clock_epoch_id == binding->clock_epoch_id && record.source_clock_run_id == binding->clock_run_id;
}

static void receipt_finish_pending(tdma_service_service_t *owner, uint32_t source)
{
    distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
    if (!(state->assembly.state & BOUNDARY_ASSEMBLY_COMPLETE_PENDING)) return;
    distributed_refmem_feedback_binding_t after;
    uint32_t epoch;
    if (!feedback_read_binding(owner, &after) ||
        !tdma_runtime_owner_get_origin_reference_epoch(&epoch)) return;
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    feedback_load(state->words, &rx, sizeof(rx));
    uint8_t original[64];
    const bool valid = feedback_same_binding(&after, &s_feedback_binding) &&
        s_feedback_admission && state->assembly.first_sequence == s_feedback_admission &&
        reference_proof_current(&rx, source, &after, epoch) &&
        refmem_sync_vdc_receipt_ack_restore(state->assembly.payload, after.node_count,
            source, after.local, original) && !memcmp(original, rx.reference_proof, sizeof(original));
    if (!valid) {
        /* Includes a valid but evicted proof. Never infer an old publication
         * from a matching sequence or count an unmatched receipt as accepted. */
        rx.reject_count = feedback_inc(rx.reject_count);
    } else if (rx.retained && !memcmp(rx.record, state->assembly.payload, sizeof(rx.record))) {
        rx.duplicate_count = feedback_inc(rx.duplicate_count);
    } else {
        memcpy(rx.record, state->assembly.payload, sizeof(rx.record));
        rx.retained = 1u;
        rx.receive_count = feedback_inc(rx.receive_count);
        rx.last_transport_seq = state->assembly.last_sequence;
        rx.first_rx_ms = state->assembly.first_ms;
        rx.last_rx_ms = osal_tick_ms(); /* Acceptance time, never an event timestamp. */
    }
    state->assembly.state &= (uint8_t)~BOUNDARY_ASSEMBLY_COMPLETE_PENDING;
    /* Restore the fragment-start watermark after the temporary admission
     * token, including the wire sequence's wrap that skips zero. */
    receipt_restore_first_sequence(&state->assembly);
    feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
}

static void distributed_refmem_receipt_service_pending(tdma_service_service_t *owner)
{
    if (!s_feedback_ready || s_feedback_binding.mode != VDC_DPLL_CONTROL_MODE_MASTER ||
        s_feedback_binding.rate_mode != VDC_BOUNDARY_MODE_REFERENCE) return;
    for (uint32_t source = 0; source < s_feedback_binding.node_count; ++source)
        if (source != s_feedback_binding.local) receipt_finish_pending(owner, source);
}

static void distributed_refmem_receipt_receive(tdma_service_service_t *owner, uint32_t source,
    const uint8_t *mailbox, const tdma_flight_rx_view_t *view)
{
    if (!s_feedback_ready || !view || !s_feedback_admission ||
        view->admission_epoch != s_feedback_admission || source >= s_feedback_binding.node_count ||
        source == s_feedback_binding.local) return;
    distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
    if (state->assembly.state & BOUNDARY_ASSEMBLY_COMPLETE_PENDING) return;
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    feedback_load(state->words, &rx, sizeof(rx));
    const uint32_t seq = distributed_refmem_get_le32(mailbox + TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET);
    if (mailbox[4] != source || !seq || (uint16_t)seq != distributed_refmem_get_le16(mailbox + 6u) ||
        !(mailbox[5] & (1u << s_feedback_binding.local)) ||
        distributed_refmem_get_le16(mailbox + TDMA_PROCESS_IMAGE_CRC_OFFSET) !=
            tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET)) {
        state->assembly.state &= (uint8_t)~REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE;
        rx.reject_count = feedback_inc(rx.reject_count);
    } else if (mailbox[3] != TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS) {
        if (!(state->assembly.state & REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE)) return;
        state->assembly.state &= (uint8_t)~REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE;
        rx.reject_count = feedback_inc(rx.reject_count);
    } else {
        uint8_t complete[64];
        const refmem_sync_vdc_feedback_result_t result = refmem_sync_vdc_receipt_ack_push(
            &state->assembly, source, s_feedback_binding.local, s_feedback_binding.node_count, seq,
            mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET],
            mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_COUNT_OFFSET],
            mailbox + TDMA_PROCESS_IMAGE_VDC_FRAGMENT_DATA_OFFSET, osal_tick_ms(), complete);
        if (result == REFMEM_VDC_FEEDBACK_COMPLETE) {
            /* After assembly completes, first_sequence is no longer needed
             * for fragment joining. Reuse it as this pending admission token. */
            state->assembly.first_sequence = view->admission_epoch;
            state->assembly.state |= BOUNDARY_ASSEMBLY_COMPLETE_PENDING;
            receipt_finish_pending(owner, source);
            return;
        }
        if (result == REFMEM_VDC_FEEDBACK_DUPLICATE) rx.duplicate_count = feedback_inc(rx.duplicate_count);
        else if (result == REFMEM_VDC_FEEDBACK_EXPIRED || result == REFMEM_VDC_FEEDBACK_EXPIRED_RESTARTED)
            rx.timeout_count = feedback_inc(rx.timeout_count);
        else if (result != REFMEM_VDC_FEEDBACK_PROGRESS && result != REFMEM_VDC_FEEDBACK_RESTARTED)
            rx.reject_count = feedback_inc(rx.reject_count);
    }
    feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
}

static void distributed_refmem_feedback_receive(tdma_service_service_t *owner, uint32_t source,
    const uint8_t *mailbox, const tdma_flight_rx_view_t *view)
{
    if (s_feedback_binding.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER) {
        distributed_refmem_boundary_receive(owner, source, mailbox, view);
        return;
    }
    if (s_feedback_binding.mode == VDC_DPLL_CONTROL_MODE_MASTER &&
        s_feedback_binding.rate_mode == VDC_BOUNDARY_MODE_REFERENCE) {
        distributed_refmem_receipt_receive(owner, source, mailbox, view);
        return;
    }
    if (!s_feedback_ready || s_feedback_binding.rate_mode == VDC_BOUNDARY_MODE_REFERENCE ||
        s_feedback_binding.mode != VDC_DPLL_CONTROL_MODE_MASTER ||
        !s_feedback_admission || view->admission_epoch != s_feedback_admission ||
        source >= s_feedback_binding.node_count || source == s_feedback_binding.local ||
        mailbox[3] != TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS) return;
    distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    feedback_load(state->words, &rx, sizeof(rx));
    if (rx.schema != FEEDBACK_REFERENCE_PROOF_ARM) rx.schema = 1u;
    rx.source_slot = source;
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
            rx.retained && rx.schema != FEEDBACK_REFERENCE_PROOF_ARM &&
                (rx.record[0] == REFMEM_VDC_FEEDBACK_SCHEMA ||
                 rx.record[0] == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA ||
                 rx.record[0] == REFMEM_VDC_FEEDBACK_RATE_SCHEMA) &&
                rx.record[1] == source && rx.record[2] == s_feedback_binding.local
                ? rx.record : NULL, s_feedback_binding.node_count, source, s_feedback_binding.local);
        distributed_refmem_feedback_binding_t after;
        if (!feedback_read_binding(owner, &after) || !feedback_same_binding(&after, &s_feedback_binding)) return;
        if (order == REFMEM_VDC_FEEDBACK_ORDER_FIRST || order == REFMEM_VDC_FEEDBACK_ORDER_NEW_NAMESPACE ||
            order == REFMEM_VDC_FEEDBACK_ORDER_NEWER) {
            refmem_sync_vdc_feedback_record_t sample;
            if (!refmem_sync_vdc_feedback_decode(wire, after.node_count, source, after.local, &sample)) return;
            if (!feedback_record_binding(&sample, &after)) {
                rx.reject_count = feedback_inc(rx.reject_count);
                feedback_publish(state->words, &state->guard, &rx, sizeof(rx));
                return;
            }
            rx.schema = 1u; rx.sample = sample;
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
    if (rx.schema == FEEDBACK_REFERENCE_PROOF_ARM) {
        uint32_t epoch = 0u;
        uint8_t original[64];
        refmem_sync_vdc_feedback_record_t reference;
        rx.active = rx.retained && known &&
            tdma_runtime_owner_get_origin_reference_epoch(&epoch) &&
            reference_proof_current(&rx, source, &current, epoch) &&
            refmem_sync_vdc_receipt_ack_restore(rx.record, current.node_count, source, current.local, original) &&
            refmem_sync_vdc_feedback_decode(original, current.node_count, current.local, source, &reference) &&
            reference.model.control_session == current.session && reference.source_arm_epoch == epoch;
        rx.schema = 1u; rx.source_slot = source;
        memset(&rx.sample, 0, sizeof(rx.sample)); /* This getter never relabels proof as decoded feedback. */
        __atomic_thread_fence(__ATOMIC_ACQ_REL);
        if (guard != __atomic_load_n(&state->guard, __ATOMIC_ACQUIRE)) return false;
        *out = rx;
        return true;
    }
    rx.schema = 1u; rx.source_slot = source;
    const bool feedback_role = current.mode == VDC_DPLL_CONTROL_MODE_MASTER &&
        current.rate_mode != VDC_BOUNDARY_MODE_REFERENCE &&
        (rx.record[0] == REFMEM_VDC_FEEDBACK_SCHEMA ||
         rx.record[0] == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA ||
         rx.record[0] == REFMEM_VDC_FEEDBACK_RATE_SCHEMA);
    const bool command_role = current.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER &&
        current.rate_mode != VDC_BOUNDARY_MODE_REFERENCE &&
        source == current.target && rx.record[0] == REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA &&
        rx.command.control_session == current.session &&
        rx.command.flags == (current.rate_mode ? REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS :
            REFMEM_VDC_BOUNDARY_COMMAND_FLAGS);
    const bool reference_role = current.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER &&
        current.rate_mode == VDC_BOUNDARY_MODE_REFERENCE && source == current.target &&
        rx.record[0] == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA;
    rx.active = rx.active && known && (feedback_role || command_role || reference_role) &&
        source < current.node_count && source != current.local &&
        rx.ring_config_seq == current.ring_config_seq && rx.role_generation == current.role_generation &&
        rx.schedule_crc32 == current.schedule_crc32 && rx.clock_epoch_id == current.clock_epoch_id &&
        rx.clock_run_id == current.clock_run_id &&
        (command_role || feedback_record_binding(&rx.sample, &current));
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&state->guard, __ATOMIC_ACQUIRE)) return false;
    *out = rx;
    return true;
}

bool distributed_refmem_get_vdc_reference_proof(uint32_t source,
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
    uint32_t epoch = 0u;
    const bool retained = rx.schema == FEEDBACK_REFERENCE_PROOF_ARM &&
        rx.reference_proof[0] == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA;
    rx.active = retained && known && tdma_runtime_owner_get_origin_reference_epoch(&epoch) &&
        reference_proof_current(&rx, source, &current, epoch);
    if (retained) memcpy(rx.record, rx.reference_proof, sizeof(rx.record));
    else memset(rx.record, 0, sizeof(rx.record));
    rx.schema = 1u; rx.source_slot = source; rx.retained = retained;
    memset(&rx.sample, 0, sizeof(rx.sample));
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
    const bool target_valid = current.mode == VDC_DPLL_CONTROL_MODE_MASTER
        ? tx.target_slot < current.node_count && tx.target_slot != current.local
        : current.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER && tx.target_slot == current.target;
    tx.active = tx.active && known && target_valid &&
        tx.ring_config_seq == current.ring_config_seq && tx.role_generation == current.role_generation &&
        tx.source_slot == current.local &&
        tx.schedule_crc32 == current.schedule_crc32 && tx.clock_epoch_id == current.clock_epoch_id &&
        tx.clock_run_id == current.clock_run_id && tx.control_session == current.session;
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_feedback_tx_guard, __ATOMIC_ACQUIRE)) return false;
    *out = tx;
    return true;
}

bool distributed_refmem_copy_vdc_feedback_rx(uint32_t source,
    distributed_refmem_vdc_feedback_rx_snapshot_t *out)
{
    if (out == NULL || source >= REFMEM_SYNC_NODE_COUNT) return false;
    distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
    const uint32_t guard = __atomic_load_n(&state->guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    feedback_load(state->words, &rx, sizeof(rx));
    /* The old diagnostic matcher consumes decoded feedback only. An ACK's
     * otherwise-idle union carries raw publication proof, never sample. */
    if (rx.schema == FEEDBACK_REFERENCE_PROOF_ARM) return false;
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&state->guard, __ATOMIC_ACQUIRE)) return false;
    *out = rx;
    return true;
}

bool __attribute__((noinline)) distributed_refmem_copy_vdc_boundary_command(
    uint32_t source, distributed_refmem_vdc_boundary_view_t *view)
{
    if (view == NULL || source >= REFMEM_SYNC_NODE_COUNT) return false;
    distributed_refmem_feedback_source_t *state = &s_feedback_rx[source];
    const uint32_t guard = __atomic_load_n(&state->guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    const uint32_t header = __atomic_load_n(state->words +
        offsetof(distributed_refmem_vdc_feedback_rx_snapshot_t, record) / 4u, __ATOMIC_RELAXED);
    if ((header & 0xffu) != REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA) return false;
    distributed_refmem_vdc_boundary_view_t next;
    feedback_load(state->words + offsetof(distributed_refmem_vdc_feedback_rx_snapshot_t, command) / 4u,
        &next.command, sizeof(next.command));
#define BOUNDARY_COPY(field) next.field = __atomic_load_n(state->words + \
    offsetof(distributed_refmem_vdc_feedback_rx_snapshot_t, field) / 4u, __ATOMIC_RELAXED)
    BOUNDARY_COPY(active); BOUNDARY_COPY(ring_config_seq); BOUNDARY_COPY(role_generation);
    BOUNDARY_COPY(schedule_crc32); BOUNDARY_COPY(clock_epoch_id); BOUNDARY_COPY(clock_run_id);
    BOUNDARY_COPY(first_rx_ms); BOUNDARY_COPY(last_rx_ms);
#undef BOUNDARY_COPY
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&state->guard, __ATOMIC_ACQUIRE) ||
        next.command.schema_version != REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA ||
        next.command.source_slot != source) return false;
    *view = next;
    return true;
}
