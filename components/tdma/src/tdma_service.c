#include "tdma_service.h"
#include "tdma_service_timing.h"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/time.h"
#include "tdma_seat_mount_store.h"
#else
/* Host builds have no seat-mount flash store. Keep the same diagnostic entry
 * point as the device build so every call site stays unconditional. */
static inline void tdma_seat_mount_store_note_reject(uint32_t code) { (void)code; }
#endif

#include "tdma_service_internal.h"

static bool tdma_service_ring_control_lock(tdma_service_service_t *service);
static void tdma_service_ring_control_unlock(tdma_service_service_t *service);
static bool tdma_service_ring_retire_stopped(tdma_service_service_t *service);
static void tdma_service_ring_stop_locked(tdma_service_service_t *service);

uint64_t tdma_service_now_ns(void)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    return time_us_64() * 1000ull;
#else
    static uint64_t s_host_fake_time_ns;
    s_host_fake_time_ns += 100000ull;
    return s_host_fake_time_ns;
#endif
}

uint32_t tdma_service_elapsed_ns(uint64_t start_ns,
                                                uint64_t done_ns)
{
    if (done_ns <= start_ns) {
        return 0u;
    }
    const uint64_t elapsed_ns = done_ns - start_ns;
    return elapsed_ns > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ns;
}

uint32_t tdma_service_delta_ns(uint64_t end_ns,
                                              uint64_t start_ns)
{
    return end_ns > start_ns
               ? tdma_service_elapsed_ns(start_ns, end_ns)
               : 0u;
}

void tdma_service_split_u64(uint64_t value,
                                           uint32_t *lo,
                                           uint32_t *hi)
{
    if (lo != NULL) {
        *lo = (uint32_t)(value & 0xFFFFFFFFull);
    }
    if (hi != NULL) {
        *hi = (uint32_t)(value >> 32u);
    }
}

uint32_t tdma_service_load(const volatile uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void tdma_service_store_guard(volatile uint32_t *guard)
{
    (void)__atomic_add_fetch(guard, 1u, __ATOMIC_RELEASE);
}

void tdma_service_begin_intent_write(tdma_service_service_t *service)
{
    tdma_service_store_guard(&service->intent_guard);
}

void tdma_service_end_intent_write(tdma_service_service_t *service)
{
    tdma_service_store_guard(&service->intent_guard);
}

void tdma_service_begin_result_write(tdma_service_service_t *service)
{
    tdma_service_store_guard(&service->result_guard);
}

void tdma_service_end_result_write(tdma_service_service_t *service)
{
    tdma_service_store_guard(&service->result_guard);
}

static void tdma_service_set_default_timestamp(tdma_service_service_t *service)
{
    if (service == NULL) {
        return;
    }
    service->timestamp_source = (uint32_t)tdma_service_DEFAULT_TIMESTAMP_SOURCE;
    service->timestamp_resolution_ns =
        tdma_service_DEFAULT_TIMESTAMP_RESOLUTION_NS;
    service->timestamp_flags = tdma_service_DEFAULT_TIMESTAMP_FLAGS;
}

void tdma_service_publish_exec_timestamp(
    tdma_service_service_t *service,
    const tdma_service_exec_status_t *status)
{
    if (service == NULL) {
        return;
    }
    if (status != NULL &&
        status->timestamp_source != tdma_service_TIMESTAMP_SOURCE_NONE &&
        status->timestamp_resolution_ns != 0u) {
        uint32_t flags = status->timestamp_flags;
        const bool eligible_window =
            service->scheduled_window_valid != 0u &&
            service->scheduled_window_class ==
                TDMA_SERVICE_WINDOW_CLASS_VDC_OBSERVATION &&
            (service->payload_class ==
                 TDMA_SERVICE_PAYLOAD_CLASS_VDC_SYNC_SAMPLE ||
             service->payload_class ==
                 TDMA_SERVICE_PAYLOAD_CLASS_IDLE_BEACON);
        const bool eligible_timestamp =
            status->timestamp_source ==
                tdma_service_TIMESTAMP_SOURCE_HARDWARE_TICK &&
            status->timestamp_resolution_ns <=
                TDMA_SERVICE_TIMESTAMP_RESOLUTION_LIMIT_NS &&
            (flags & tdma_service_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u;
        if (!eligible_window || !eligible_timestamp) {
            flags &= ~tdma_service_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
        }
        service->timestamp_source = status->timestamp_source;
        service->timestamp_resolution_ns = status->timestamp_resolution_ns;
        service->timestamp_flags = flags;
        return;
    }
    tdma_service_set_default_timestamp(service);
}


bool tdma_service_init(tdma_service_service_t *service)
{
    if (service == NULL) {
        return false;
    }

    memset(service, 0, sizeof(*service));
    service->state = tdma_service_STATE_IDLE;
    service->owner_core = tdma_service_OWNER_CORE1;
    service->last_result = tdma_service_RESULT_NONE;
    service->active_traffic_class = UINT32_MAX;
    if (!tdma_payload_registry_init(&service->payload_registry,
                                    TDMA_SERVICE_SHORT_FRAME_MAX,
                                    TDMA_SERVICE_LONG_FRAME_MAX)) {
        return false;
    }
    if (!tdma_ring_runtime_init(&service->ring_runtime)) {
        return false;
    }
    if (!tdma_flight_fifo_init(&service->flight_fifo)) {
        return false;
    }
    if (!tdma_flight_engine_init(&service->flight_engine)) {
        return false;
    }
    if (!tdma_operating_profile_get(TDMA_OPERATING_PROFILE_DEFAULT_LEVEL,
                                    &service->operating_profile)) {
        return false;
    }
    tdma_service_set_default_timestamp(service);
    return true;
}

bool tdma_service_bind_ops(tdma_service_service_t *service,
                                   const tdma_service_ops_t *ops,
                                   void *ops_context)
{
    if (service == NULL || ops == NULL ||
        ops->transmit == NULL || ops->receive == NULL) {
        return false;
    }

    service->ops = ops;
    service->ops_context = ops_context;
    return true;
}

bool tdma_service_bind_traffic_scheduler(
    tdma_service_service_t *service,
    tdma_traffic_scheduler_t *scheduler)
{
    if (service == NULL || scheduler == NULL) {
        return false;
    }
    service->traffic_scheduler = scheduler;
    return true;
}

bool tdma_service_set_maintenance_gate(tdma_service_service_t *service,
                                       bool open)
{
    if (service == NULL) {
        return false;
    }
    __atomic_store_n(&service->maintenance_gate_open,
                     open ? 1u : 0u,
                     __ATOMIC_RELEASE);
    return true;
}

bool tdma_service_register_payload(tdma_service_service_t *service,
                                   const tdma_service_payload_binding_t *binding)
{
    return service != NULL &&
           tdma_payload_registry_register(&service->payload_registry, binding);
}

bool tdma_service_register_adapter_impl(tdma_service_service_t *service,
                                        uint32_t adapter_type,
                                        const tdma_ring_adapter_ops_t *ops,
                                        void *context)
{
    if (service == NULL || adapter_type == TDMA_ADAPTER_NONE ||
        ops == NULL || ops->start == NULL || ops->stop == NULL ||
        ops->service == NULL ||
        service->adapter_impl_count >= TDMA_SERVICE_ADAPTER_IMPL_MAX) {
        return false;
    }
    for (uint32_t i = 0u; i < service->adapter_impl_count; i++) {
        if (service->adapter_impls[i].adapter_type == adapter_type) {
            service->adapter_impls[i].ops = ops;
            service->adapter_impls[i].context = context;
            return true;
        }
    }
    service->adapter_impls[service->adapter_impl_count].adapter_type =
        adapter_type;
    service->adapter_impls[service->adapter_impl_count].ops = ops;
    service->adapter_impls[service->adapter_impl_count].context = context;
    service->adapter_impl_count++;
    return true;
}

/* Bind the registered ring adapter implementation whose adapter_type matches
 * the active foundation profile; unbind when the profile requests a transport
 * that is not registered. This keeps the ring runtime transport-agnostic and
 * lets future BISS-C / UART / RS485 adapters plug in without changing the
 * scheduler contract. */
static bool tdma_service_apply_profile_adapter(tdma_service_service_t *service,
                                               uint32_t adapter_type)
{
    for (uint32_t i = 0u; i < service->adapter_impl_count; i++) {
        if (service->adapter_impls[i].adapter_type == adapter_type) {
            return tdma_ring_runtime_bind_adapter(
                &service->ring_runtime,
                service->adapter_impls[i].ops,
                service->adapter_impls[i].context);
        }
    }
    tdma_ring_runtime_unbind_adapter(&service->ring_runtime);
    return service->ring_runtime.adapter_ops == NULL;
}

static void tdma_service_cancel_stopped_update(tdma_service_service_t *service)
{
    uint32_t expected = __atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE);
    if ((expected & TDMA_STOPPED_UPDATE_REQUESTED) != 0u) {
        (void)__atomic_compare_exchange_n(&service->stopped_update, &expected,
            0u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
}

static bool tdma_service_configure_ring_runtime_locked(
    tdma_service_service_t *service,
    const tdma_service_ring_runtime_config_t *config)
{
    if (config != NULL && config->enabled != 0u) {
        if (__atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) != 0u)
            return false;
    } else {
        tdma_service_cancel_stopped_update(service);
    }
    return tdma_ring_runtime_configure(&service->ring_runtime, config);
}

bool tdma_service_configure_ring_runtime(tdma_service_service_t *service,
    const tdma_service_ring_runtime_config_t *config)
{
    if (service == NULL || !tdma_service_ring_control_lock(service)) return false;
    const bool ok = tdma_service_configure_ring_runtime_locked(service, config);
    tdma_service_ring_control_unlock(service);
    return ok;
}

bool tdma_service_bind_ring_adapter(tdma_service_service_t *service,
                                    const tdma_ring_adapter_ops_t *ops,
                                    void *context)
{
    return service != NULL &&
           tdma_ring_runtime_bind_adapter(&service->ring_runtime,
                                          ops,
                                          context);
}

static bool tdma_service_configure_foundation_profile_locked(
    tdma_service_service_t *service,
    const tdma_foundation_profile_t *profile,
    uint32_t schedule_crc32)
{
    tdma_profile_result_t result = TDMA_PROFILE_BAD_ARGUMENT;
    if (service == NULL || profile == NULL ||
        !tdma_foundation_profile_validate(profile, &result) ||
        profile->resource.short_frame_capacity > TDMA_SERVICE_SHORT_FRAME_MAX ||
        profile->resource.long_frame_capacity > TDMA_SERVICE_LONG_FRAME_MAX ||
        schedule_crc32 == 0u) {
        return false;
    }
    /* A failed physical STOP retains the old adapter and its payload pools.
     * Reject before publishing or reconfiguring any replacement metadata. */
    if (!tdma_service_apply_profile_adapter(service, profile->resource.adapter_type)) {
        return false;
    }
    if (!tdma_payload_registry_configure(
            &service->payload_registry,
            profile->resource.payload_whitelist_mask,
            profile->resource.short_frame_capacity,
            profile->resource.long_frame_capacity)) {
        return false;
    }
    tdma_operating_profile_result_t operating_result;
    if (!tdma_operating_profile_validate(&service->operating_profile,
                                         &operating_result)) {
        return false;
    }
    tdma_foundation_profile_t scheduler_profile = *profile;
    scheduler_profile.resource.cycle_period_ns =
        service->operating_profile.cycle_period_ns;
    scheduler_profile.profile_crc32 =
        tdma_foundation_profile_crc32(&scheduler_profile);
    if (service->traffic_scheduler != NULL &&
        !tdma_traffic_scheduler_configure_closed(service->traffic_scheduler,
                                          &scheduler_profile)) {
        return false;
    }

    const uint32_t effective_schedule_crc32 =
        tdma_operating_profile_schedule_crc32(
            schedule_crc32, &service->operating_profile);
    const uint64_t feedback_timeout_ns =
        (uint64_t)service->operating_profile.cycle_period_ns *
        (uint64_t)profile->ring.node_count;
    if (effective_schedule_crc32 == 0u || feedback_timeout_ns > UINT32_MAX) {
        return false;
    }

    const tdma_service_ring_runtime_config_t ring = {
        .enabled = profile->enabled,
        .node_count = profile->ring.node_count,
        .local_slot_id = profile->ring.local_index,
        .reference_slot_id = profile->ring.reference_index,
        .up_group_id = profile->ring.up_group_id,
        .down_group_id = profile->ring.down_group_id,
        .flags = profile->ring.flags,
        .ring_profile_crc32 = profile->ring.profile_crc32,
        .schedule_crc32 = effective_schedule_crc32,
        .operating_profile_crc32 =
            service->operating_profile.profile_crc32,
        .baud_hz = service->operating_profile.baud_hz,
        .cycle_period_ns = service->operating_profile.cycle_period_ns,
        .loop_delay_ns = 0u,
        .loop_delay_tolerance_ns = 0u,
        .feedback_timeout_ns = (uint32_t)feedback_timeout_ns,
        .tx_dma_channel_id = profile->resource.tx_dma_channel_id,
        .rx_dma_channel_id = profile->resource.rx_dma_channel_id,
    };
    tdma_service_begin_intent_write(service);
    service->foundation_profile_crc32 = profile->profile_crc32;
    service->foundation_owner_instance_id = profile->owner_instance_id;
    service->adapter_type = profile->resource.adapter_type;
    service->pio_block_id = profile->resource.pio_block_id;
    service->up_state_machine_id = profile->resource.up_state_machine_id;
    service->down_state_machine_id = profile->resource.down_state_machine_id;
    service->tx_dma_channel_id = profile->resource.tx_dma_channel_id;
    service->rx_dma_channel_id = profile->resource.rx_dma_channel_id;
    service->core1_service_id = profile->resource.core1_service_id;
    service->short_frame_capacity = profile->resource.short_frame_capacity;
    service->long_frame_capacity = profile->resource.long_frame_capacity;
    service->payload_whitelist_mask = profile->resource.payload_whitelist_mask;
    service->io_claim_mask = profile->resource.io_claim_mask;
    service->ip_core_claim_mask = profile->resource.ip_core_claim_mask;
    tdma_service_end_intent_write(service);
    service->ring_base_schedule_crc32 = schedule_crc32;
    service->ring_staged_config = ring;
    /* Product links start explicitly after both boards have roles assigned.
     * Keep the adapter and all ISO1452 drivers stopped at boot/profile load. */
    tdma_service_ring_stop_locked(service);
    return true;
}

bool tdma_service_configure_foundation_profile(
    tdma_service_service_t *service,
    const tdma_foundation_profile_t *profile,
    uint32_t schedule_crc32)
{
    if (service == NULL || !tdma_service_ring_control_lock(service)) return false;
    tdma_ring_runtime_snapshot_t snapshot;
    const bool ok = tdma_service_ring_retire_stopped(service) &&
        tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) &&
        snapshot.enabled == 0u && snapshot.adapter_started == 0u &&
        snapshot.config_seq == snapshot.applied_config_seq &&
        tdma_service_load(&service->intent_seq) == service->completed_seq &&
        tdma_service_configure_foundation_profile_locked(service, profile,
                                                         schedule_crc32);
    tdma_service_ring_control_unlock(service);
    return ok;
}

static bool tdma_service_set_operating_profile_locked(
    tdma_service_service_t *service,
    const tdma_operating_profile_t *profile)
{
    tdma_operating_profile_result_t result;
    tdma_ring_runtime_snapshot_t snapshot;
    if (service == NULL ||
        !tdma_operating_profile_validate(profile, &result) ||
        !tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) ||
        snapshot.enabled != 0u) {
        return false;
    }

    tdma_service_ring_runtime_config_t staged = service->ring_staged_config;
    if (staged.enabled != 0u) {
        const uint32_t effective_schedule_crc32 =
            tdma_operating_profile_schedule_crc32(
                service->ring_base_schedule_crc32, profile);
        const uint64_t feedback_timeout_ns =
            (uint64_t)profile->cycle_period_ns *
            (uint64_t)staged.node_count;
        if (effective_schedule_crc32 == 0u ||
            feedback_timeout_ns > UINT32_MAX) {
            return false;
        }
        if (service->traffic_scheduler != NULL &&
            !tdma_traffic_scheduler_set_cycle_period(
                service->traffic_scheduler, profile->cycle_period_ns)) {
            return false;
        }
        staged.schedule_crc32 = effective_schedule_crc32;
        staged.operating_profile_crc32 = profile->profile_crc32;
        staged.baud_hz = profile->baud_hz;
        staged.cycle_period_ns = profile->cycle_period_ns;
        staged.feedback_timeout_ns = (uint32_t)feedback_timeout_ns;
    }
    service->operating_profile = *profile;
    service->ring_staged_config = staged;
    return true;
}

bool tdma_service_set_operating_profile(
    tdma_service_service_t *service,
    const tdma_operating_profile_t *profile)
{
    if (service == NULL || !tdma_service_ring_control_lock(service)) return false;
    const bool ok = tdma_service_ring_retire_stopped(service) &&
        tdma_service_set_operating_profile_locked(service, profile);
    tdma_service_ring_control_unlock(service);
    return ok;
}

bool tdma_service_set_loop_delay_ns(tdma_service_service_t *service,
                                    uint32_t loop_delay_ns,
                                    uint32_t tolerance_ns)
{
    tdma_ring_runtime_snapshot_t snapshot;
    if (service == NULL ||
        !tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) ||
        snapshot.enabled != 0u ||
        service->ring_staged_config.enabled == 0u ||
        loop_delay_ns > service->ring_staged_config.feedback_timeout_ns) {
        return false;
    }
    if (loop_delay_ns == 0u) {
        tolerance_ns = 0u;
    }
    service->ring_staged_config.loop_delay_ns = loop_delay_ns;
    service->ring_staged_config.loop_delay_tolerance_ns = tolerance_ns;
    return true;
}

bool tdma_service_set_ring_diagnostic_mode(tdma_service_service_t *service,
                                           bool enabled)
{
    tdma_ring_runtime_snapshot_t snapshot;
    if (service == NULL || !tdma_service_ring_control_lock(service)) return false;
    if (__atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) != 0u ||
        !tdma_service_ring_retire_stopped(service) ||
        !tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) ||
        snapshot.enabled != 0u || snapshot.adapter_started != 0u ||
        snapshot.config_seq != snapshot.applied_config_seq ||
        service->ring_staged_config.enabled == 0u) {
        tdma_service_ring_control_unlock(service);
        return false;
    }
    if (enabled) {
        service->ring_staged_config.flags |=
            TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE;
    } else {
        service->ring_staged_config.flags &=
            ~(TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE |
              TDMA_RING_FLAG_DIAGNOSTIC_BURST_MASK);
    }
    tdma_service_ring_control_unlock(service);
    return true;
}

/* STOP-only bootstrap authorization. It permits a no-stage ARM to keep the
 * flight engine active with fixed phase 1..31; it never writes calibration
 * and must be cleared before a calibrated product session. */
bool tdma_service_set_bringup_no_cal(tdma_service_service_t *service,
                                     bool enabled)
{
    tdma_ring_runtime_snapshot_t snapshot;
    if (service == NULL || !tdma_service_ring_control_lock(service)) return false;
    if (__atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) != 0u ||
        !tdma_service_ring_retire_stopped(service) ||
        !tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) ||
        snapshot.enabled != 0u || snapshot.adapter_started != 0u ||
        snapshot.config_seq != snapshot.applied_config_seq ||
        service->ring_staged_config.enabled == 0u) {
        tdma_service_ring_control_unlock(service);
        return false;
    }
    if (enabled) {
        service->ring_staged_config.flags |= TDMA_RING_FLAG_BRINGUP_NO_CAL;
    } else {
        service->ring_staged_config.flags &= ~TDMA_RING_FLAG_BRINGUP_NO_CAL;
    }
    tdma_service_ring_control_unlock(service);
    return true;
}

/* Gate policy switch for the reference node's self-excited emission.  This is
 * NOT the physical emission cadence budget (tdma_origin_cadence_t /
 * tdma_origin_cadence_calculate): it only relaxes the on-change publish gate so
 * the reference may publish the current process image on a ring return even
 * when the received frame carried no new segment. */
bool tdma_service_set_origin_cadence(tdma_service_service_t *service,
                                     bool enabled)
{
    tdma_ring_runtime_snapshot_t snapshot;
    if (service == NULL || !tdma_service_ring_control_lock(service)) return false;
    if (__atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) != 0u ||
        !tdma_service_ring_retire_stopped(service) ||
        !tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) ||
        snapshot.enabled != 0u || snapshot.adapter_started != 0u ||
        snapshot.config_seq != snapshot.applied_config_seq ||
        service->ring_staged_config.enabled == 0u) {
        tdma_service_ring_control_unlock(service);
        return false;
    }
    service->ring_staged_config.origin_cadence_enabled = enabled ? 1u : 0u;
    tdma_service_ring_control_unlock(service);
    return true;
}

bool tdma_service_set_ring_diagnostic_burst(tdma_service_service_t *service,
                                            uint32_t limit)
{
    if (service == NULL || limit > 2u ||
        !tdma_service_ring_control_lock(service)) return false;
    tdma_ring_runtime_snapshot_t snapshot;
    const bool ok =
        __atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) == 0u &&
        tdma_service_ring_retire_stopped(service) &&
        tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) &&
        snapshot.enabled == 0u && snapshot.adapter_started == 0u &&
        snapshot.config_seq == snapshot.applied_config_seq &&
        service->ring_staged_config.enabled != 0u &&
        (limit == 0u ||
         ((service->ring_staged_config.flags &
           TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) != 0u &&
          service->ring_staged_config.local_slot_id ==
              service->ring_staged_config.reference_slot_id));
    if (ok) {
        service->ring_staged_config.flags =
            (service->ring_staged_config.flags &
             ~TDMA_RING_FLAG_DIAGNOSTIC_BURST_MASK) |
            (limit << TDMA_RING_FLAG_DIAGNOSTIC_BURST_SHIFT);
    }
    tdma_service_ring_control_unlock(service);
    return ok;
}

bool tdma_service_set_ring_geometry_generation(tdma_service_service_t *service,
                                               uint32_t generation)
{
    if (service == NULL || !tdma_service_ring_control_lock(service)) return false;
    tdma_ring_runtime_snapshot_t snapshot;
    const bool ok =
        __atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) == 0u &&
        tdma_service_ring_retire_stopped(service) &&
        tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) &&
        snapshot.enabled == 0u && snapshot.adapter_started == 0u &&
        snapshot.config_seq == snapshot.applied_config_seq &&
        service->ring_staged_config.enabled != 0u;
    if (ok) service->ring_staged_config.geometry_generation = generation;
    tdma_service_ring_control_unlock(service);
    return ok;
}

bool tdma_service_stage_calibration(
    tdma_service_service_t *service,
    const tdma_ring_calibration_stage_t *stage)
{
    tdma_ring_runtime_reason_t reason = TDMA_RING_RUNTIME_REASON_NONE;
    tdma_ring_runtime_snapshot_t snapshot;
    if (service == NULL || stage == NULL ||
        !tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) ||
        snapshot.enabled != 0u ||
        stage->profile_crc32 !=
            service->ring_staged_config.operating_profile_crc32 ||
        stage->schedule_crc32 != service->ring_staged_config.schedule_crc32 ||
        !tdma_ring_runtime_validate_calibration_stage(
            stage, service->ring_staged_config.node_count, &reason)) {
        return false;
    }
    service->calibration_stage = *stage;
    service->calibration_gate_required = 1u;
    return true;
}

static bool tdma_service_calibration_stage_is_stopped(
    const tdma_service_service_t *service)
{
    tdma_ring_runtime_snapshot_t snapshot;
    return service != NULL &&
           tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) &&
           snapshot.enabled == 0u;
}

static bool tdma_service_calibration_header_is_valid(
    const tdma_service_service_t *service,
    const tdma_ring_calibration_stage_t *header)
{
    return service != NULL && header != NULL && header->enabled != 0u &&
           header->node_count >= 2u &&
           header->node_count <= TDMA_RING_NODE_MAX &&
           header->node_count <= TDMA_RING_CALIBRATION_LINK_MAX &&
           header->node_count == service->ring_staged_config.node_count &&
           (header->evidence_flags &
            TDMA_RING_CALIBRATION_REQUIRED_FLAGS) ==
               TDMA_RING_CALIBRATION_REQUIRED_FLAGS &&
           (header->evidence_flags &
            TDMA_RING_CALIBRATION_FLAG_DIAGNOSTIC_ONLY) == 0u &&
           header->profile_crc32 ==
               service->ring_staged_config.operating_profile_crc32 &&
           header->schedule_crc32 ==
               service->ring_staged_config.schedule_crc32 &&
           header->calibration_generation != 0u &&
           header->topology_generation != 0u &&
           header->topology_crc32 != 0u && header->profile_crc32 != 0u &&
           header->schedule_crc32 != 0u;
}

bool tdma_service_begin_calibration_stage(
    tdma_service_service_t *service,
    const tdma_ring_calibration_stage_t *header)
{
    if (!tdma_service_calibration_stage_is_stopped(service) ||
        !tdma_service_calibration_header_is_valid(service, header)) {
        return false;
    }
    memset(&service->calibration_stage, 0,
           sizeof(service->calibration_stage));
    service->calibration_stage.enabled = 1u;
    service->calibration_stage.node_count = header->node_count;
    service->calibration_stage.evidence_flags = header->evidence_flags;
    service->calibration_stage.calibration_generation =
        header->calibration_generation;
    service->calibration_stage.topology_generation =
        header->topology_generation;
    service->calibration_stage.topology_crc32 = header->topology_crc32;
    service->calibration_stage.profile_crc32 = header->profile_crc32;
    service->calibration_stage.schedule_crc32 = header->schedule_crc32;
    /* BEGIN closes ARM immediately.  ARM remains rejected until every
     * physical link has a valid replay budget. */
    service->calibration_gate_required = 1u;
    return true;
}

bool tdma_service_stage_calibration_link(
    tdma_service_service_t *service,
    const tdma_ring_calibration_link_t *link)
{
    if (!tdma_service_calibration_stage_is_stopped(service) || link == NULL ||
        service->calibration_gate_required == 0u ||
        service->calibration_stage.enabled == 0u ||
        link->valid == 0u ||
        (link->evidence_flags & TDMA_RING_CALIBRATION_REQUIRED_FLAGS) !=
            TDMA_RING_CALIBRATION_REQUIRED_FLAGS ||
        (link->evidence_flags &
         TDMA_RING_CALIBRATION_FLAG_DIAGNOSTIC_ONLY) != 0u ||
        link->link_index >= service->calibration_stage.node_count ||
        link->marker_source_node >= service->calibration_stage.node_count ||
        link->marker_destination_node >=
            service->calibration_stage.node_count ||
        link->data_source_node >= service->calibration_stage.node_count ||
        link->data_destination_node >=
            service->calibration_stage.node_count ||
        link->marker_source_node == link->marker_destination_node ||
        link->data_source_node != link->marker_destination_node ||
        link->data_destination_node != link->marker_source_node ||
        link->calibration_generation !=
            service->calibration_stage.calibration_generation ||
        link->topology_generation !=
            service->calibration_stage.topology_generation ||
        link->topology_crc32 != service->calibration_stage.topology_crc32 ||
        link->profile_crc32 != service->calibration_stage.profile_crc32 ||
        link->schedule_crc32 != service->calibration_stage.schedule_crc32 ||
        link->pio_persona == 0u || link->clkdiv_q16 == 0u ||
        link->clk_sys_hz == 0u || link->instruction_period_ns == 0u ||
        link->bit_cycles == 0u || link->marker_to_data_cycles == 0u ||
        link->codeword_cycles == 0u || link->link_budget_cycles == 0u ||
        link->sample_period_ns == 0u || link->link_base_delay_ns == 0u ||
        link->marker_phase_delay_cycles > 31u ||
        link->sck_phase_delay_cycles > 31u ||
        link->data_phase_delay_cycles > 31u ||
        !tdma_ring_runtime_validate_calibration_link_phase(link)) {
        return false;
    }
    const uint64_t required_cycles =
        (uint64_t)link->marker_to_data_cycles +
        link->forward_residence_cycles + link->rx_arm_lead_cycles +
        link->codeword_cycles + link->guard_cycles +
        link->loop_delay_cycles;
    if (required_cycles > link->link_budget_cycles) {
        return false;
    }
    service->calibration_stage.links[link->link_index] = *link;
    return true;
}

bool tdma_service_get_calibration_stage(
    const tdma_service_service_t *service,
    tdma_ring_calibration_stage_t *stage,
    bool *complete)
{
    if (service == NULL || stage == NULL) {
        return false;
    }
    *stage = service->calibration_stage;
    if (complete != NULL) {
        *complete = service->calibration_gate_required != 0u &&
                    tdma_ring_runtime_validate_calibration_stage(
                        stage, service->ring_staged_config.node_count, NULL);
    }
    return stage->enabled != 0u;
}

bool tdma_service_clear_calibration_stage(tdma_service_service_t *service)
{
    tdma_ring_runtime_snapshot_t snapshot;
    if (service == NULL ||
        !tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) ||
        snapshot.enabled != 0u) {
        return false;
    }
    memset(&service->calibration_stage, 0,
           sizeof(service->calibration_stage));
    service->calibration_gate_required = 0u;
    return true;
}

static bool tdma_service_ring_control_lock(tdma_service_service_t *service)
{
    uint32_t expected = 0u;
    return __atomic_compare_exchange_n(&service->ring_control_guard,
        &expected, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void tdma_service_ring_control_unlock(tdma_service_service_t *service)
{
    __atomic_store_n(&service->ring_control_guard, 0u, __ATOMIC_RELEASE);
}

/* Caller owns the Core0 control guard. Do not retire queue/recovery storage
 * until the matching physical STOP has been acknowledged by Core1. */
static bool tdma_service_ring_retire_stopped(tdma_service_service_t *service)
{
    if (service->ring_control_pending != TDMA_RING_CONTROL_RETIRE) {
        return true;
    }
    tdma_ring_runtime_snapshot_t snapshot;
    if (!tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) ||
        snapshot.enabled != 0u || snapshot.adapter_started != 0u ||
        snapshot.config_seq != service->ring_control_config_seq ||
        snapshot.applied_config_seq != service->ring_control_config_seq) {
        return false;
    }
    if (service->traffic_scheduler != NULL &&
        !tdma_traffic_scheduler_cancel_pending(service->traffic_scheduler, NULL)) {
        return false;
    }
    service->ring_control_pending = TDMA_RING_CONTROL_NONE;
    return true;
}

void tdma_service_core0_lifecycle_service(tdma_service_service_t *service)
{
    if (service == NULL || !tdma_service_ring_control_lock(service)) {
        return;
    }
    if (service->ring_control_pending == TDMA_RING_CONTROL_RETIRE) {
        (void)tdma_service_ring_retire_stopped(service);
    } else if (service->ring_control_pending == TDMA_RING_CONTROL_ARM) {
        tdma_ring_runtime_snapshot_t snapshot;
        if (tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot)) {
            if (snapshot.config_seq != service->ring_control_config_seq ||
                snapshot.enabled == 0u) {
                service->ring_control_pending = TDMA_RING_CONTROL_NONE;
            } else if (snapshot.adapter_started != 0u &&
                       snapshot.applied_config_seq == service->ring_control_config_seq) {
                if (service->traffic_scheduler != NULL) {
                    (void)tdma_traffic_scheduler_resume(service->traffic_scheduler);
                }
                service->ring_control_pending = TDMA_RING_CONTROL_NONE;
            }
        }
    }
    tdma_service_ring_control_unlock(service);
}

bool tdma_service_request_stopped_update(tdma_service_service_t *service,
    uint32_t token, uint32_t *generation)
{
    if (service == NULL || token == 0u ||
        (token & ~TDMA_STOPPED_UPDATE_TOKEN_MASK) != 0u ||
        !tdma_service_ring_control_lock(service)) return false;
    tdma_ring_runtime_snapshot_t snapshot;
    const bool ok = __atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) == 0u &&
        tdma_service_ring_retire_stopped(service) &&
        service->ring_control_pending == TDMA_RING_CONTROL_NONE &&
        tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) &&
        snapshot.enabled == 0u && snapshot.adapter_started == 0u &&
        snapshot.config_seq == snapshot.applied_config_seq;
    if (ok) {
        const uint32_t next = __atomic_add_fetch(&service->stopped_update_generation, 1u, __ATOMIC_RELAXED);
        if (generation != NULL) *generation = next;
        __atomic_store_n(&service->stopped_update,
            token | TDMA_STOPPED_UPDATE_REQUESTED, __ATOMIC_RELEASE);
    }
    tdma_service_ring_control_unlock(service);
    return ok;
}

bool tdma_service_update_event_tap(tdma_service_service_t *service,
    bool (*publish)(void *context), void *context)
{
    if (service == NULL || publish == NULL ||
        !tdma_service_ring_control_lock(service)) return false;
    tdma_ring_runtime_snapshot_t snapshot;
    /* Split so a bench readout names the blocking condition (temporary). */
    bool stopped = true;
    do {
        if (__atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) != 0u) {
            tdma_seat_mount_store_note_reject(111u); stopped = false; break;
        }
        if (!tdma_service_ring_retire_stopped(service)) {
            tdma_seat_mount_store_note_reject(112u); stopped = false; break;
        }
        if (service->ring_control_pending != TDMA_RING_CONTROL_NONE) {
            tdma_seat_mount_store_note_reject(113u); stopped = false; break;
        }
        if (!tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot)) {
            tdma_seat_mount_store_note_reject(114u); stopped = false; break;
        }
        if (snapshot.enabled != 0u || snapshot.adapter_started != 0u) {
            tdma_seat_mount_store_note_reject(115u); stopped = false; break;
        }
        if (snapshot.config_seq != snapshot.applied_config_seq) {
            tdma_seat_mount_store_note_reject(116u); stopped = false; break;
        }
        if (service->ring_staged_config.geometry_generation != 0u) {
            tdma_seat_mount_store_note_reject(117u); stopped = false; break;
        }
    } while (0);
    /* Publication is SRAM-only and bounded. Holding the control guard makes
     * the stopped proof indivisible from ARM/configure/STOP on Core0. */
    const bool ok = stopped && publish(context);
    tdma_service_ring_control_unlock(service);
    return ok;
}

bool tdma_service_update_stopped_metadata(tdma_service_service_t *service,
    bool (*publish)(void *context), void *context)
{
    return tdma_service_update_event_tap(service, publish, context);
}

bool tdma_service_run_stopped_maintenance(tdma_service_service_t *service,
    bool (*maintain)(void *context), void *context)
{
    if (service == NULL) { tdma_seat_mount_store_note_reject(123u); return false; }
    if (maintain == NULL) { tdma_seat_mount_store_note_reject(124u); return false; }
    if (!tdma_service_ring_control_lock(service)) {
        tdma_seat_mount_store_note_reject(125u); return false;
    }
    tdma_ring_runtime_snapshot_t snapshot;
    bool stopped = true;
    do {
        if (__atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) != 0u) {
            tdma_seat_mount_store_note_reject(131u); stopped = false; break;
        }
        if (!tdma_service_ring_retire_stopped(service)) {
            tdma_seat_mount_store_note_reject(132u); stopped = false; break;
        }
        if (service->ring_control_pending != TDMA_RING_CONTROL_NONE) {
            tdma_seat_mount_store_note_reject(133u); stopped = false; break;
        }
        if (!tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot)) {
            tdma_seat_mount_store_note_reject(134u); stopped = false; break;
        }
        if (snapshot.enabled != 0u || snapshot.adapter_started != 0u) {
            tdma_seat_mount_store_note_reject(135u); stopped = false; break;
        }
        if (snapshot.config_seq != snapshot.applied_config_seq) {
            tdma_seat_mount_store_note_reject(136u); stopped = false; break;
        }
        if (service->ring_staged_config.geometry_generation != 0u) {
            tdma_seat_mount_store_note_reject(137u); stopped = false; break;
        }
    } while (0);
    /* Separate from SRAM-only metadata publication: this non-realtime
     * Core0 operation can block on its existing FlashTransaction owner.
     * Core1's flash park acknowledgement never takes ring_control_guard. */
    const bool ok = stopped && maintain(context);
    tdma_service_ring_control_unlock(service);
    return ok;
}

bool tdma_service_get_stopped_update(tdma_service_service_t *service,
    uint32_t *token, uint32_t *generation, bool *applying)
{
    if (service == NULL || token == NULL || generation == NULL || applying == NULL ||
        !tdma_service_ring_control_lock(service)) return false;
    const uint32_t request = __atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE);
    *token = request & TDMA_STOPPED_UPDATE_TOKEN_MASK;
    *generation = __atomic_load_n(&service->stopped_update_generation, __ATOMIC_ACQUIRE);
    *applying = (request & TDMA_STOPPED_UPDATE_APPLYING) != 0u;
    tdma_service_ring_control_unlock(service);
    return true;
}

bool tdma_service_claim_stopped_update_core1(tdma_service_service_t *service,
    uint32_t *token, uint32_t *generation)
{
    if (service == NULL || token == NULL || generation == NULL) return false;
    uint32_t expected = __atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE);
    if ((expected & TDMA_STOPPED_UPDATE_REQUESTED) == 0u) return false;
    const uint32_t value = expected & TDMA_STOPPED_UPDATE_TOKEN_MASK;
    if (!__atomic_compare_exchange_n(&service->stopped_update, &expected,
            value | TDMA_STOPPED_UPDATE_APPLYING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return false;
    *token = value;
    *generation = __atomic_load_n(&service->stopped_update_generation, __ATOMIC_ACQUIRE);
    return true;
}

bool tdma_service_finish_stopped_update_core1(tdma_service_service_t *service,
    uint32_t token, uint32_t generation)
{
    if (service == NULL || token == 0u || (token & ~TDMA_STOPPED_UPDATE_TOKEN_MASK) != 0u ||
        generation != __atomic_load_n(&service->stopped_update_generation, __ATOMIC_ACQUIRE)) return false;
    uint32_t expected = token | TDMA_STOPPED_UPDATE_APPLYING;
    return __atomic_compare_exchange_n(&service->stopped_update, &expected,
        0u, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

bool tdma_service_ring_arm(tdma_service_service_t *service)
{
    if (service == NULL || !tdma_service_ring_control_lock(service)) {
        return false;
    }
    bool accepted = false;
    tdma_ring_runtime_snapshot_t snapshot;
    if (__atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) != 0u ||
        service->ring_staged_config.enabled == 0u ||
        !tdma_service_ring_retire_stopped(service) ||
        !tdma_ring_runtime_get_snapshot(&service->ring_runtime, &snapshot) ||
        snapshot.enabled != 0u || snapshot.adapter_started != 0u ||
        snapshot.applied_config_seq != snapshot.config_seq) {
        goto done;
    }
    if (service->calibration_gate_required != 0u &&
        (service->calibration_stage.profile_crc32 !=
             service->ring_staged_config.operating_profile_crc32 ||
         service->calibration_stage.schedule_crc32 !=
             service->ring_staged_config.schedule_crc32 ||
         !tdma_ring_runtime_validate_calibration_stage(
             &service->calibration_stage,
             service->ring_staged_config.node_count, NULL))) {
        goto done;
    }
    tdma_traffic_scheduler_close_admission(service->traffic_scheduler);
    if (!tdma_service_configure_ring_runtime_locked(
            service, &service->ring_staged_config)) {
        goto done;
    }
    /* The runtime owns this publication's selector. A later ARM must make
     * another explicit choice even when this physical ARM is rejected. */
    service->ring_staged_config.geometry_generation = 0u;
    service->ring_control_config_seq = service->ring_runtime.config_seq;
    service->ring_control_pending = TDMA_RING_CONTROL_ARM;
    accepted = true;
done:
    tdma_service_ring_control_unlock(service);
    return accepted;
}

bool tdma_service_ring_train_clock(tdma_service_service_t *service,
                                   uint32_t cycles)
{
    return service != NULL &&
           tdma_ring_runtime_train_clock(&service->ring_runtime, cycles);
}

bool tdma_service_ring_start(tdma_service_service_t *service)
{
    if (service == NULL || !tdma_service_ring_control_lock(service)) return false;
    /* Serialize the final DATA publication with Core0 STOP/ARM/configure.
     * Core1 is never blocked by this maintenance-side control lock. */
    const bool accepted =
        tdma_ring_runtime_set_data_enabled(&service->ring_runtime, true);
    tdma_service_ring_control_unlock(service);
    return accepted;
}

static void tdma_service_ring_stop_locked(tdma_service_service_t *service)
{
    tdma_traffic_scheduler_close_admission(service->traffic_scheduler);
    /* NULL is an unconditional disable request for this valid runtime.
     * Acceptance does not wait for physical stop or queue-lock ownership. */
    (void)tdma_service_configure_ring_runtime_locked(service, NULL);
    service->ring_control_config_seq = service->ring_runtime.config_seq;
    service->ring_control_pending = TDMA_RING_CONTROL_RETIRE;
}

bool tdma_service_ring_stop(tdma_service_service_t *service)
{
    if (service == NULL || !tdma_service_ring_control_lock(service)) {
        return false;
    }
    tdma_service_ring_stop_locked(service);
    tdma_service_ring_control_unlock(service);
    return true;
}

bool tdma_service_ring_stop_if_current(tdma_service_service_t *service,
    uint32_t config_seq, bool (*before_stop)(void *context), void *context,
    uint32_t *stop_config_seq)
{
    if (stop_config_seq != NULL) *stop_config_seq = 0u;
    if (service == NULL || before_stop == NULL ||
        stop_config_seq == NULL || !tdma_service_ring_control_lock(service)) return false;
    const bool current = service->ring_runtime.config_seq == config_seq &&
        service->ring_runtime.enabled != 0u;
    const bool accepted = current && before_stop(context);
    if (accepted) {
        tdma_service_ring_stop_locked(service);
        *stop_config_seq = service->ring_runtime.config_seq;
    }
    tdma_service_ring_control_unlock(service);
    return accepted;
}

bool tdma_service_get_flight_fifo_snapshot(
    const tdma_service_service_t *service,
    tdma_flight_fifo_snapshot_t *snapshot)
{
    if (service == NULL || snapshot == NULL) {
        return false;
    }
    return tdma_flight_fifo_get_snapshot(&service->flight_fifo, snapshot);
}

tdma_service_flight_fifo_reset_result_t tdma_service_reset_flight_fifo_checked(
    tdma_service_service_t *service)
{
    if (service == NULL) {
        return TDMA_SERVICE_FLIGHT_FIFO_RESET_INVALID;
    }
    if (!tdma_service_ring_control_lock(service)) {
        return TDMA_SERVICE_FLIGHT_FIFO_RESET_BUSY;
    }
    tdma_service_flight_fifo_reset_result_t result = TDMA_SERVICE_FLIGHT_FIFO_RESET_BUSY;
    tdma_ring_runtime_snapshot_t ring_snapshot;
    tdma_flight_engine_snapshot_t engine_snapshot;
    if (!tdma_ring_runtime_get_snapshot(&service->ring_runtime,
                                        &ring_snapshot) ||
        !tdma_flight_engine_get_snapshot(&service->flight_engine,
                                         &engine_snapshot)) {
        goto done;
    }
    if (ring_snapshot.enabled != 0u ||
        ring_snapshot.adapter_started != 0u ||
        ring_snapshot.config_seq != ring_snapshot.applied_config_seq ||
        engine_snapshot.active != 0u) {
        result = TDMA_SERVICE_FLIGHT_FIFO_RESET_NOT_STOPPED;
        goto done;
    }
    if (__atomic_load_n(&service->stopped_update, __ATOMIC_ACQUIRE) == 0u &&
        tdma_flight_fifo_reset_stopped(&service->flight_fifo)) {
        result = TDMA_SERVICE_FLIGHT_FIFO_RESET_OK;
    }
done:
    tdma_service_ring_control_unlock(service);
    return result;
}

bool tdma_service_reset_flight_fifo(tdma_service_service_t *service)
{
    return tdma_service_reset_flight_fifo_checked(service) ==
           TDMA_SERVICE_FLIGHT_FIFO_RESET_OK;
}

bool tdma_service_configure_flight_map(
    tdma_service_service_t *service,
    const tdma_process_image_map_t *map)
{
    return tdma_service_configure_flight_map_checked(service, map) ==
           TDMA_SERVICE_FLIGHT_MAP_OK;
}

tdma_service_flight_map_result_t tdma_service_configure_flight_map_checked(
    tdma_service_service_t *service, const tdma_process_image_map_t *map)
{
    if (service == NULL || map == NULL) {
        return TDMA_SERVICE_FLIGHT_MAP_INVALID;
    }
    tdma_ring_runtime_snapshot_t ring_snapshot;
    if (!tdma_ring_runtime_get_snapshot(&service->ring_runtime,
                                        &ring_snapshot)) {
        return TDMA_SERVICE_FLIGHT_MAP_SNAPSHOT_UNAVAILABLE;
    }
    if (ring_snapshot.enabled != 0u || ring_snapshot.adapter_started != 0u) {
        return TDMA_SERVICE_FLIGHT_MAP_RUNTIME_ACTIVE;
    }
    switch (tdma_flight_engine_configure_checked(&service->flight_engine, map)) {
    case TDMA_FLIGHT_MAP_CONFIG_OK:
        return TDMA_SERVICE_FLIGHT_MAP_OK;
    case TDMA_FLIGHT_MAP_CONFIG_BUSY:
        return TDMA_SERVICE_FLIGHT_MAP_BUSY;
    case TDMA_FLIGHT_MAP_CONFIG_ACTIVE:
        return TDMA_SERVICE_FLIGHT_MAP_ENGINE_ACTIVE;
    default:
        return TDMA_SERVICE_FLIGHT_MAP_INVALID;
    }
}

bool tdma_service_get_flight_engine_snapshot(
    const tdma_service_service_t *service,
    tdma_flight_engine_snapshot_t *snapshot)
{
    return service != NULL && snapshot != NULL &&
           tdma_flight_engine_get_snapshot(&service->flight_engine, snapshot);
}

bool tdma_service_publish_flight_tx(tdma_service_service_t *service,
                                    const uint8_t *data,
                                    size_t data_size,
                                    uint32_t generation,
                                    uint32_t sequence,
                                    uint32_t segment_mask)
{
    return service != NULL &&
           tdma_flight_fifo_core0_publish_tx(&service->flight_fifo,
                                             data,
                                             data_size,
                                             generation,
                                             sequence,
                                             segment_mask);
}

bool tdma_service_first_class_route(const tdma_service_service_t *service)
{
    return service != NULL && tdma_first_class_route(&service->first_class);
}

bool tdma_service_first_class_pending(const tdma_service_service_t *service)
{
    return service != NULL && tdma_first_class_pending(&service->first_class);
}

bool tdma_service_first_class_free(const tdma_service_service_t *service)
{
    return service != NULL && tdma_first_class_free(&service->first_class);
}

bool tdma_service_stage_first_class(tdma_service_service_t *service,
    const uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE])
{
    return service != NULL && tdma_first_class_route(&service->first_class) &&
        tdma_first_class_stage(&service->first_class, mailbox);
}

uint32_t tdma_service_core0_advance_flight_rx_admission_epoch(
    tdma_service_service_t *service)
{
    return service != NULL
        ? tdma_flight_fifo_core0_advance_rx_admission_epoch(&service->flight_fifo)
        : 0u;
}

bool tdma_service_acquire_flight_rx(tdma_service_service_t *service,
                                    tdma_flight_rx_view_t *view)
{
    return service != NULL &&
           tdma_flight_fifo_core0_acquire_rx(&service->flight_fifo, view);
}

bool tdma_service_release_flight_rx(tdma_service_service_t *service,
                                    uint32_t slot_index)
{
    return service != NULL &&
           tdma_flight_fifo_core0_release_rx(&service->flight_fifo,
                                             slot_index);
}

