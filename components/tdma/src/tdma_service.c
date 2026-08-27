#include "tdma_service.h"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/time.h"
#endif

#define tdma_service_OWNER_CORE1 1u
#define tdma_service_ERROR_NO_OPS 100u
#define tdma_service_DEFAULT_TIMESTAMP_RESOLUTION_NS 1000u
#define tdma_service_DEFAULT_TIMESTAMP_SOURCE \
    tdma_service_TIMESTAMP_SOURCE_SOFTWARE_US
#define tdma_service_DEFAULT_TIMESTAMP_FLAGS \
    tdma_service_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY
#define tdma_service_ERROR_WINDOW_MISSED 101u
#define tdma_service_WINDOW_ARM_AHEAD_NS 2000000u
#define TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT 64u

static uint64_t tdma_service_now_ns(void)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    return time_us_64() * 1000ull;
#else
    static uint64_t s_host_fake_time_ns;
    s_host_fake_time_ns += 100000ull;
    return s_host_fake_time_ns;
#endif
}

static uint32_t tdma_service_elapsed_ns(uint64_t start_ns,
                                                uint64_t done_ns)
{
    if (done_ns <= start_ns) {
        return 0u;
    }
    const uint64_t elapsed_ns = done_ns - start_ns;
    return elapsed_ns > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_ns;
}

static uint32_t tdma_service_delta_ns(uint64_t end_ns,
                                              uint64_t start_ns)
{
    return end_ns > start_ns
               ? tdma_service_elapsed_ns(start_ns, end_ns)
               : 0u;
}

static uint64_t tdma_service_wait_until_ns(uint64_t target_ns)
{
    uint64_t now_ns = tdma_service_now_ns();
    while (now_ns < target_ns) {
        now_ns = tdma_service_now_ns();
    }
    return now_ns;
}

static void tdma_service_split_u64(uint64_t value,
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

static uint32_t tdma_service_load(const volatile uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void tdma_service_store_guard(volatile uint32_t *guard)
{
    (void)__atomic_add_fetch(guard, 1u, __ATOMIC_RELEASE);
}

static void tdma_service_begin_intent_write(tdma_service_service_t *service)
{
    tdma_service_store_guard(&service->intent_guard);
}

static void tdma_service_end_intent_write(tdma_service_service_t *service)
{
    tdma_service_store_guard(&service->intent_guard);
}

static void tdma_service_begin_result_write(tdma_service_service_t *service)
{
    tdma_service_store_guard(&service->result_guard);
}

static void tdma_service_end_result_write(tdma_service_service_t *service)
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

static void tdma_service_publish_exec_timestamp(
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

static bool tdma_service_has_pending(const tdma_service_service_t *service)
{
    const uint32_t intent_seq = tdma_service_load(&service->intent_seq);
    const uint32_t completed_seq = tdma_service_load(&service->completed_seq);
    const uint32_t abort_seq = tdma_service_load(&service->abort_seq);
    return intent_seq > completed_seq && abort_seq < intent_seq;
}

static bool tdma_service_payload_registered(
    tdma_service_service_t *service,
    uint32_t frame_class,
    uint32_t payload_class,
    size_t frame_size)
{
    return service != NULL &&
           tdma_payload_registry_admit(&service->payload_registry,
                                       frame_class,
                                       payload_class,
                                       frame_size);
}

static uint32_t tdma_service_estimated_duration_ns(
    const tdma_service_intent_config_t *config,
    tdma_service_intent_t intent)
{
    if (config == NULL) {
        return 0u;
    }
    uint64_t duration_ns = (uint64_t)config->deadline_us * 1000ull;
    if (intent == tdma_service_INTENT_TX_FRAME && config->baud_hz != 0u &&
        config->frame_size != 0u) {
        const uint64_t wire_ns =
            (((uint64_t)config->frame_size * 8ull * 1000000000ull) +
             config->baud_hz - 1u) /
            config->baud_hz;
        if (wire_ns > duration_ns) {
            duration_ns = wire_ns;
        }
    }
    return duration_ns > UINT32_MAX ? UINT32_MAX : (uint32_t)duration_ns;
}

static bool tdma_service_enqueue_scheduled(
    tdma_service_service_t *service,
    const tdma_service_intent_config_t *config,
    tdma_service_intent_t intent)
{
    if (service == NULL || service->traffic_scheduler == NULL ||
        config == NULL) {
        return false;
    }
    const tdma_traffic_request_t request = {
        .intent_type = (uint32_t)intent,
        .role = (uint32_t)config->role,
        .baud_hz = config->baud_hz,
        .rx_pin = config->pins.rx_pin,
        .csn_pin = config->pins.csn_pin,
        .sck_pin = config->pins.sck_pin,
        .tx_pin = config->pins.tx_pin,
        .deadline_us = config->deadline_us,
        .frame_class = config->frame_class,
        .payload_class = config->payload_class,
        .window_epoch = config->window_epoch,
        .window_index = config->window_index,
        .scheduled_window_valid = config->scheduled_window_valid,
        .scheduled_window_class = config->scheduled_window_class,
        .schedule_crc32 = config->schedule_crc32,
        .scheduled_window_start_ns = config->scheduled_window_start_ns,
        .scheduled_window_end_ns = config->scheduled_window_end_ns,
        .scheduled_guard_start_ns = config->scheduled_guard_start_ns,
        .scheduled_guard_end_ns = config->scheduled_guard_end_ns,
        .enqueue_time_ns = tdma_service_now_ns(),
        .estimated_duration_ns =
            tdma_service_estimated_duration_ns(config, intent),
        .frame_size = config->frame_size,
        .frame = config->frame,
    };
    const tdma_traffic_scheduler_result_t result =
        tdma_traffic_scheduler_enqueue(service->traffic_scheduler, &request);
    if (result != TDMA_TRAFFIC_SCHEDULER_OK &&
        result != TDMA_TRAFFIC_SCHEDULER_DROPPED_OLDEST) {
        tdma_service_begin_intent_write(service);
        service->reject_count++;
        tdma_service_end_intent_write(service);
        return false;
    }

    tdma_traffic_scheduler_snapshot_t scheduler_snapshot;
    if (!tdma_traffic_scheduler_get_snapshot(service->traffic_scheduler,
                                              &scheduler_snapshot)) {
        return false;
    }
    tdma_service_begin_intent_write(service);
    service->scheduler_submit_seq = scheduler_snapshot.enqueue_seq;
    service->submit_time_ns = request.enqueue_time_ns;
    tdma_service_end_intent_write(service);
    return true;
}

static bool tdma_service_submit(tdma_service_service_t *service,
                                        const tdma_service_intent_config_t *config,
                                        tdma_service_intent_t intent)
{
    if (service == NULL || config == NULL ||
        (intent == tdma_service_INTENT_TX_FRAME &&
         (config->frame == NULL || config->frame_size == 0u)) ||
        config->frame_size > tdma_service_FRAME_MAX) {
        if (service != NULL) {
            tdma_service_begin_intent_write(service);
            service->reject_count++;
            tdma_service_end_intent_write(service);
        }
        return false;
    }

    if (!tdma_service_payload_registered(service,
                                         config->frame_class,
                                         config->payload_class,
                                         config->frame_size)) {
        tdma_service_begin_intent_write(service);
        service->reject_count++;
        tdma_service_end_intent_write(service);
        return false;
    }

    if (service->traffic_scheduler != NULL) {
        return tdma_service_enqueue_scheduled(service, config, intent);
    }

    if (tdma_service_has_pending(service)) {
        tdma_service_begin_intent_write(service);
        service->reject_count++;
        tdma_service_end_intent_write(service);
        return false;
    }

    tdma_service_begin_intent_write(service);
    service->intent_seq++;
    service->window_epoch = config->window_epoch;
    service->window_index = config->window_index;
    service->intent_type = (uint32_t)intent;
    service->role = (uint32_t)config->role;
    service->baud_hz = config->baud_hz;
    service->rx_pin = config->pins.rx_pin;
    service->csn_pin = config->pins.csn_pin;
    service->sck_pin = config->pins.sck_pin;
    service->tx_pin = config->pins.tx_pin;
    service->deadline_us = config->deadline_us;
    service->frame_class = config->frame_class;
    service->payload_class = config->payload_class;
    service->scheduled_window_valid = config->scheduled_window_valid;
    service->scheduled_window_class = config->scheduled_window_class;
    service->schedule_crc32 = config->schedule_crc32;
    service->scheduled_window_start_ns = config->scheduled_window_start_ns;
    service->scheduled_window_end_ns = config->scheduled_window_end_ns;
    service->scheduled_guard_start_ns = config->scheduled_guard_start_ns;
    service->scheduled_guard_end_ns = config->scheduled_guard_end_ns;
    service->frame_size = (uint32_t)config->frame_size;
    service->submit_time_ns = tdma_service_now_ns();
    if (config->frame_size != 0u && config->frame != NULL) {
        memcpy(service->frame, config->frame, config->frame_size);
    }
    tdma_service_end_intent_write(service);
    return true;
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
static void tdma_service_apply_profile_adapter(tdma_service_service_t *service,
                                               uint32_t adapter_type)
{
    for (uint32_t i = 0u; i < service->adapter_impl_count; i++) {
        if (service->adapter_impls[i].adapter_type == adapter_type) {
            (void)tdma_ring_runtime_bind_adapter(
                &service->ring_runtime,
                service->adapter_impls[i].ops,
                service->adapter_impls[i].context);
            return;
        }
    }
    tdma_ring_runtime_unbind_adapter(&service->ring_runtime);
}

bool tdma_service_configure_ring_runtime(
    tdma_service_service_t *service,
    const tdma_service_ring_runtime_config_t *config)
{
    return service != NULL &&
           tdma_ring_runtime_configure(&service->ring_runtime, config);
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

bool tdma_service_configure_foundation_profile(
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
        !tdma_traffic_scheduler_configure(service->traffic_scheduler,
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
    tdma_service_apply_profile_adapter(service, profile->resource.adapter_type);
    /* Product links start explicitly after both boards have roles assigned.
     * Keep the adapter and all ISO1452 drivers stopped at boot/profile load. */
    return tdma_service_ring_stop(service);
}

bool tdma_service_set_operating_profile(
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

bool tdma_service_ring_arm(tdma_service_service_t *service)
{
    if (service == NULL || service->ring_staged_config.enabled == 0u) {
        return false;
    }
    if (service->calibration_gate_required != 0u &&
        (service->calibration_stage.profile_crc32 !=
             service->ring_staged_config.operating_profile_crc32 ||
         service->calibration_stage.schedule_crc32 !=
             service->ring_staged_config.schedule_crc32 ||
         !tdma_ring_runtime_validate_calibration_stage(
             &service->calibration_stage,
             service->ring_staged_config.node_count, NULL))) {
        return false;
    }
    if (!tdma_service_configure_ring_runtime(
            service, &service->ring_staged_config)) {
        return false;
    }
    if (service->traffic_scheduler == NULL ||
        tdma_traffic_scheduler_resume(service->traffic_scheduler)) {
        return true;
    }
    (void)tdma_service_configure_ring_runtime(service, NULL);
    return false;
}

bool tdma_service_ring_train_clock(tdma_service_service_t *service,
                                   uint32_t cycles)
{
    return service != NULL &&
           tdma_ring_runtime_train_clock(&service->ring_runtime, cycles);
}

bool tdma_service_ring_start(tdma_service_service_t *service)
{
    return service != NULL &&
           tdma_ring_runtime_set_data_enabled(&service->ring_runtime, true);
}

bool tdma_service_ring_stop(tdma_service_service_t *service)
{
    if (service == NULL ||
        !tdma_service_configure_ring_runtime(service, NULL)) {
        return false;
    }
    return service->traffic_scheduler == NULL ||
           tdma_traffic_scheduler_suspend(service->traffic_scheduler, NULL);
}

bool tdma_service_submit_tx(tdma_service_service_t *service,
                                    const tdma_service_intent_config_t *config)
{
    return tdma_service_submit(service, config, tdma_service_INTENT_TX_FRAME);
}

bool tdma_service_submit_rx(tdma_service_service_t *service,
                                    const tdma_service_intent_config_t *config)
{
    return tdma_service_submit(service, config, tdma_service_INTENT_RX_WINDOW);
}

void tdma_service_abort(tdma_service_service_t *service)
{
    if (service == NULL) {
        return;
    }

    tdma_service_begin_intent_write(service);
    service->abort_seq = service->intent_seq;
    tdma_service_end_intent_write(service);
}

static bool tdma_service_dispatch_next_scheduled(
    tdma_service_service_t *service)
{
    if (service == NULL || service->traffic_scheduler == NULL ||
        tdma_service_has_pending(service)) {
        return false;
    }
    tdma_traffic_dispatch_t dispatch;
    const tdma_traffic_scheduler_result_t result =
        tdma_traffic_scheduler_select(service->traffic_scheduler,
                                      tdma_service_now_ns(),
                                      tdma_service_load(
                                          &service->maintenance_gate_open) != 0u,
                                      &dispatch);
    if (result != TDMA_TRAFFIC_SCHEDULER_OK) {
        return false;
    }

    tdma_service_begin_intent_write(service);
    service->intent_seq++;
    service->window_epoch = dispatch.request.window_epoch;
    service->window_index = dispatch.request.window_index;
    service->intent_type = dispatch.request.intent_type;
    service->role = dispatch.request.role;
    service->baud_hz = dispatch.request.baud_hz;
    service->rx_pin = dispatch.request.rx_pin;
    service->csn_pin = dispatch.request.csn_pin;
    service->sck_pin = dispatch.request.sck_pin;
    service->tx_pin = dispatch.request.tx_pin;
    service->deadline_us = dispatch.request.deadline_us;
    service->frame_class = dispatch.request.frame_class;
    service->payload_class = dispatch.request.payload_class;
    service->scheduled_window_valid =
        dispatch.request.scheduled_window_valid;
    service->scheduled_window_class =
        dispatch.request.scheduled_window_class;
    service->schedule_crc32 = dispatch.request.schedule_crc32;
    service->scheduled_window_start_ns =
        dispatch.request.scheduled_window_start_ns;
    service->scheduled_window_end_ns =
        dispatch.request.scheduled_window_end_ns;
    service->scheduled_guard_start_ns =
        dispatch.request.scheduled_guard_start_ns;
    service->scheduled_guard_end_ns =
        dispatch.request.scheduled_guard_end_ns;
    service->frame_size = (uint32_t)dispatch.request.frame_size;
    service->submit_time_ns = dispatch.request.enqueue_time_ns;
    service->active_traffic_class = dispatch.traffic_class;
    service->active_scheduler_sequence = dispatch.sequence;
    if (dispatch.request.frame_size != 0u) {
        memcpy(service->frame,
               dispatch.frame,
               dispatch.request.frame_size);
    }
    tdma_service_end_intent_write(service);
    return true;
}

static void tdma_service_complete_scheduled(
    tdma_service_service_t *service,
    tdma_traffic_completion_t completion)
{
    if (service == NULL || service->traffic_scheduler == NULL ||
        service->active_traffic_class >= TDMA_TRAFFIC_CLASS_COUNT) {
        return;
    }
    const uint32_t traffic_class = service->active_traffic_class;
    service->traffic_class_last_result[traffic_class] = service->last_result;
    service->traffic_class_last_error[traffic_class] = service->last_error;
    service->traffic_class_timestamp_source[traffic_class] =
        service->timestamp_source;
    service->traffic_class_timestamp_resolution_ns[traffic_class] =
        service->timestamp_resolution_ns;
    service->traffic_class_timestamp_flags[traffic_class] =
        service->timestamp_flags;
    const uint32_t frame_size =
        service->last_result == tdma_service_RESULT_FRAME_READY &&
                service->intent_type == tdma_service_INTENT_RX_WINDOW &&
                service->result_frame_size <= tdma_service_FRAME_MAX
            ? service->result_frame_size
            : 0u;
    service->traffic_class_result_frame_size[traffic_class] = frame_size;
    if (frame_size != 0u) {
        memcpy(service->traffic_class_result_frame[traffic_class],
               service->result_frame,
               frame_size);
    }
    (void)tdma_traffic_scheduler_complete(service->traffic_scheduler,
                                          traffic_class,
                                          completion);
}

void tdma_service_core1_service(tdma_service_service_t *service)
{
    if (service == NULL || service->state == tdma_service_STATE_UNINIT) {
        return;
    }

    tdma_ring_runtime_service(&service->ring_runtime);

    (void)tdma_service_dispatch_next_scheduled(service);

    const uint32_t intent_seq = tdma_service_load(&service->intent_seq);
    const uint32_t abort_seq = tdma_service_load(&service->abort_seq);
    if (intent_seq <= service->completed_seq) {
        tdma_service_begin_result_write(service);
        service->service_count++;
        service->armed = 0u;
        if (service->state == tdma_service_STATE_UNINIT) {
            service->state = tdma_service_STATE_IDLE;
        }
        tdma_service_end_result_write(service);
        return;
    }

    tdma_service_begin_result_write(service);
    service->service_count++;
    if (intent_seq > service->completed_seq) {
        if (abort_seq >= intent_seq) {
            tdma_service_complete_scheduled(service,
                                            TDMA_TRAFFIC_COMPLETION_DROP);
            service->dropped_seq = intent_seq;
            service->completed_seq = intent_seq;
            service->armed = 0u;
            service->state = tdma_service_STATE_IDLE;
            service->last_result = tdma_service_RESULT_NONE;
        } else {
            if (service->timing_intent_seq != intent_seq) {
                service->timing_intent_seq = intent_seq;
                service->core1_arm_time_ns = tdma_service_now_ns();
                service->core1_start_time_ns = 0ull;
                service->core1_done_time_ns = 0ull;
                service->core1_elapsed_ns = 0u;
            }
            service->armed = 1u;
            service->state = tdma_service_STATE_ARMED;
        }
    }
    tdma_service_end_result_write(service);
    if (abort_seq >= intent_seq) {
        return;
    }

    uint8_t frame[tdma_service_FRAME_MAX];
    uint32_t intent_type;
    tdma_service_role_t role;
    tdma_service_pin_config_t pins;
    uint32_t baud_hz;
    uint32_t deadline_us;
    uint32_t scheduled_window_valid;
    uint64_t scheduled_window_start_ns;
    uint64_t scheduled_window_end_ns;
    uint64_t scheduled_guard_start_ns;
    uint64_t scheduled_guard_end_ns;
    size_t frame_size;
    while (true) {
        const uint32_t seq_begin = tdma_service_load(&service->intent_guard);
        if ((seq_begin & 1u) != 0u) {
            continue;
        }
        intent_type = service->intent_type;
        role = (tdma_service_role_t)service->role;
        pins.rx_pin = service->rx_pin;
        pins.csn_pin = service->csn_pin;
        pins.sck_pin = service->sck_pin;
        pins.tx_pin = service->tx_pin;
        baud_hz = service->baud_hz;
        deadline_us = service->deadline_us;
        scheduled_window_valid = service->scheduled_window_valid;
        scheduled_window_start_ns = service->scheduled_window_start_ns;
        scheduled_window_end_ns = service->scheduled_window_end_ns;
        scheduled_guard_start_ns = service->scheduled_guard_start_ns;
        scheduled_guard_end_ns = service->scheduled_guard_end_ns;
        frame_size = (size_t)service->frame_size;
        if (frame_size > sizeof(frame)) {
            frame_size = sizeof(frame);
        }
        if (frame_size != 0u) {
            memcpy(frame, service->frame, frame_size);
        }
        const uint32_t seq_end = tdma_service_load(&service->intent_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            break;
        }
    }

    if (scheduled_window_valid != 0u) {
        uint64_t now_ns = tdma_service_now_ns();
        if (now_ns < scheduled_guard_start_ns) {
            const uint32_t wait_ns =
                tdma_service_delta_ns(scheduled_guard_start_ns, now_ns);
            if (wait_ns <= tdma_service_WINDOW_ARM_AHEAD_NS) {
                now_ns = tdma_service_wait_until_ns(scheduled_guard_start_ns);
            } else {
                tdma_service_begin_result_write(service);
                service->armed = 1u;
                service->scheduled_window_wait_ns = wait_ns;
                service->scheduled_window_late_ns = 0u;
                service->last_result = tdma_service_RESULT_WAITING_FOR_WINDOW;
                service->state = tdma_service_STATE_ARMED;
                tdma_service_end_result_write(service);
                return;
            }
        }
        if (now_ns < scheduled_window_start_ns) {
            now_ns = tdma_service_wait_until_ns(scheduled_window_start_ns);
        }
        if (now_ns > scheduled_guard_end_ns || now_ns > scheduled_window_end_ns) {
            tdma_service_begin_result_write(service);
            service->completed_seq = intent_seq;
            service->armed = 0u;
            service->scheduled_window_miss_count++;
            service->scheduled_window_wait_ns = 0u;
            service->scheduled_window_late_ns =
                tdma_service_delta_ns(now_ns, scheduled_window_start_ns);
            service->last_error = tdma_service_ERROR_WINDOW_MISSED;
            service->last_result = tdma_service_RESULT_WINDOW_MISSED;
            service->state = tdma_service_STATE_ERROR;
            tdma_service_complete_scheduled(
                service, TDMA_TRAFFIC_COMPLETION_WINDOW_MISSED);
            tdma_service_end_result_write(service);
            return;
        }
        tdma_service_begin_result_write(service);
        service->scheduled_window_wait_ns = 0u;
        service->scheduled_window_late_ns =
            tdma_service_delta_ns(now_ns, scheduled_window_start_ns);
        tdma_service_end_result_write(service);
    }

    tdma_service_exec_status_t exec_status = {
        .result = tdma_service_EXEC_NONE,
        .error = 0u,
        .frame_size = frame_size,
    };
    uint64_t core1_start_time_ns = service->core1_start_time_ns;
    if (core1_start_time_ns == 0ull) {
        core1_start_time_ns = tdma_service_now_ns();
        tdma_service_begin_result_write(service);
        service->core1_start_time_ns = core1_start_time_ns;
        tdma_service_end_result_write(service);
    }

    bool ok = false;
    if (service->ops == NULL) {
        exec_status.result = tdma_service_EXEC_ERROR;
        exec_status.error = tdma_service_ERROR_NO_OPS;
    } else if (intent_type == tdma_service_INTENT_TX_FRAME) {
        ok = service->ops->transmit(service->ops_context,
                                    frame,
                                    frame_size,
                                    role,
                                    baud_hz,
                                    &pins,
                                    deadline_us,
                                    &exec_status);
    } else if (intent_type == tdma_service_INTENT_RX_WINDOW) {
        ok = service->ops->receive(service->ops_context,
                                   frame,
                                   sizeof(frame),
                                   role,
                                   baud_hz,
                                   &pins,
                                   deadline_us,
                                   &exec_status);
    } else {
        exec_status.result = tdma_service_EXEC_ERROR;
        exec_status.error = tdma_service_RESULT_BAD_ARGUMENT;
    }

    if (exec_status.result == tdma_service_EXEC_PENDING) {
        tdma_service_begin_result_write(service);
        service->armed = 1u;
        service->last_error = exec_status.error;
        service->last_result = tdma_service_RESULT_ACCEPTED;
        service->state = tdma_service_STATE_ARMED;
        tdma_service_end_result_write(service);
        return;
    }

    const uint64_t core1_done_time_ns = tdma_service_now_ns();
    tdma_service_begin_result_write(service);
    service->completed_seq = intent_seq;
    service->armed = 0u;
    service->last_error = exec_status.error;
    service->result_frame_size = (uint32_t)exec_status.frame_size;
    tdma_service_publish_exec_timestamp(service, &exec_status);
    service->core1_done_time_ns = core1_done_time_ns;
    service->core1_elapsed_ns =
        tdma_service_elapsed_ns(core1_start_time_ns, core1_done_time_ns);
    if (ok &&
        exec_status.result == tdma_service_EXEC_RX_OK &&
        exec_status.frame_size <= sizeof(service->result_frame)) {
        memcpy(service->result_frame, frame, exec_status.frame_size);
    }
    if (ok &&
        (exec_status.result == tdma_service_EXEC_TX_OK ||
         exec_status.result == tdma_service_EXEC_RX_OK)) {
        service->ready_count++;
        service->last_result = tdma_service_RESULT_FRAME_READY;
        service->state = tdma_service_STATE_DONE;
        tdma_service_complete_scheduled(
            service,
            service->scheduled_window_late_ns != 0u
                ? TDMA_TRAFFIC_COMPLETION_LATE
                : TDMA_TRAFFIC_COMPLETION_SENT);
    } else if (exec_status.result == tdma_service_EXEC_TIMEOUT) {
        service->timeout_count++;
        service->last_result = tdma_service_RESULT_TIMEOUT;
        service->state = tdma_service_STATE_ERROR;
        tdma_service_complete_scheduled(service,
                                        TDMA_TRAFFIC_COMPLETION_RETRY);
    } else {
        service->overrun_count++;
        service->last_result = tdma_service_RESULT_OVERRUN;
        service->state = tdma_service_STATE_ERROR;
        tdma_service_complete_scheduled(
            service, TDMA_TRAFFIC_COMPLETION_ADAPTER_ERROR);
    }
    tdma_service_end_result_write(service);
}

bool tdma_service_get_snapshot(const tdma_service_service_t *service,
                                       tdma_service_snapshot_t *snapshot)
{
    if (service == NULL || snapshot == NULL) {
        return false;
    }

    memset(snapshot, 0, sizeof(*snapshot));

    uint32_t abort_seq = 0u;
    bool intent_copied = false;
    for (uint32_t attempt = 0u;
         attempt < TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT;
         attempt++) {
        const uint32_t seq_begin = tdma_service_load(&service->intent_guard);
        if ((seq_begin & 1u) != 0u) {
            continue;
        }
        snapshot->intent_seq = service->intent_seq;
        abort_seq = service->abort_seq;
        snapshot->window_epoch = service->window_epoch;
        snapshot->window_index = service->window_index;
        snapshot->intent_type = service->intent_type;
        snapshot->role = service->role;
        snapshot->baud_hz = service->baud_hz;
        snapshot->rx_pin = service->rx_pin;
        snapshot->csn_pin = service->csn_pin;
        snapshot->sck_pin = service->sck_pin;
        snapshot->tx_pin = service->tx_pin;
        snapshot->deadline_us = service->deadline_us;
        snapshot->frame_class = service->frame_class;
        snapshot->payload_class = service->payload_class;
        snapshot->scheduled_window_valid = service->scheduled_window_valid;
        snapshot->scheduled_window_class = service->scheduled_window_class;
        snapshot->schedule_crc32 = service->schedule_crc32;
        tdma_service_split_u64(service->scheduled_window_start_ns,
                                       &snapshot->scheduled_window_start_ns_lo,
                                       &snapshot->scheduled_window_start_ns_hi);
        tdma_service_split_u64(service->scheduled_window_end_ns,
                                       &snapshot->scheduled_window_end_ns_lo,
                                       &snapshot->scheduled_window_end_ns_hi);
        tdma_service_split_u64(service->scheduled_guard_start_ns,
                                       &snapshot->scheduled_guard_start_ns_lo,
                                       &snapshot->scheduled_guard_start_ns_hi);
        tdma_service_split_u64(service->scheduled_guard_end_ns,
                                       &snapshot->scheduled_guard_end_ns_lo,
                                       &snapshot->scheduled_guard_end_ns_hi);
        snapshot->frame_size = service->frame_size;
        snapshot->reject_count = service->reject_count;
        snapshot->traffic_scheduler_enqueue_seq =
            service->scheduler_submit_seq;
        snapshot->foundation_profile_crc32 = service->foundation_profile_crc32;
        snapshot->foundation_owner_instance_id = service->foundation_owner_instance_id;
        snapshot->adapter_type = service->adapter_type;
        snapshot->pio_block_id = service->pio_block_id;
        snapshot->up_state_machine_id = service->up_state_machine_id;
        snapshot->down_state_machine_id = service->down_state_machine_id;
        snapshot->tx_dma_channel_id = service->tx_dma_channel_id;
        snapshot->rx_dma_channel_id = service->rx_dma_channel_id;
        snapshot->core1_service_id = service->core1_service_id;
        snapshot->short_frame_capacity = service->short_frame_capacity;
        snapshot->long_frame_capacity = service->long_frame_capacity;
        snapshot->payload_whitelist_mask = service->payload_whitelist_mask;
        snapshot->io_claim_mask = service->io_claim_mask;
        snapshot->ip_core_claim_mask = service->ip_core_claim_mask;
        tdma_service_split_u64(service->submit_time_ns,
                                       &snapshot->submit_time_ns_lo,
                                       &snapshot->submit_time_ns_hi);
        const uint32_t seq_end = tdma_service_load(&service->intent_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            intent_copied = true;
            break;
        }
    }
    if (!intent_copied) {
        return false;
    }

    uint32_t result_frame_size = 0u;
    bool result_copied = false;
    for (uint32_t attempt = 0u;
         attempt < TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT;
         attempt++) {
        const uint32_t seq_begin = tdma_service_load(&service->result_guard);
        if ((seq_begin & 1u) != 0u) {
            continue;
        }
        snapshot->state = service->state;
        snapshot->owner_core = service->owner_core;
        snapshot->armed = service->armed;
        snapshot->service_count = service->service_count;
        snapshot->completed_seq = service->completed_seq;
        snapshot->dropped_seq = service->dropped_seq;
        snapshot->ready_count = service->ready_count;
        snapshot->timeout_count = service->timeout_count;
        snapshot->overrun_count = service->overrun_count;
        snapshot->last_result = service->last_result;
        snapshot->last_error = service->last_error;
        snapshot->timestamp_source = service->timestamp_source;
        snapshot->timestamp_resolution_ns = service->timestamp_resolution_ns;
        snapshot->timestamp_flags = service->timestamp_flags;
        snapshot->scheduled_window_miss_count = service->scheduled_window_miss_count;
        snapshot->scheduled_window_wait_ns = service->scheduled_window_wait_ns;
        snapshot->scheduled_window_late_ns = service->scheduled_window_late_ns;
        tdma_service_split_u64(service->core1_arm_time_ns,
                                       &snapshot->core1_arm_time_ns_lo,
                                       &snapshot->core1_arm_time_ns_hi);
        tdma_service_split_u64(service->core1_start_time_ns,
                                       &snapshot->core1_start_time_ns_lo,
                                       &snapshot->core1_start_time_ns_hi);
        tdma_service_split_u64(service->core1_done_time_ns,
                                       &snapshot->core1_done_time_ns_lo,
                                       &snapshot->core1_done_time_ns_hi);
        snapshot->core1_elapsed_ns = service->core1_elapsed_ns;
        for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
            snapshot->traffic_class_last_result[i] =
                service->traffic_class_last_result[i];
            snapshot->traffic_class_last_error[i] =
                service->traffic_class_last_error[i];
            snapshot->traffic_class_timestamp_source[i] =
                service->traffic_class_timestamp_source[i];
            snapshot->traffic_class_timestamp_resolution_ns[i] =
                service->traffic_class_timestamp_resolution_ns[i];
            snapshot->traffic_class_timestamp_flags[i] =
                service->traffic_class_timestamp_flags[i];
            snapshot->traffic_class_result_frame_size[i] =
                service->traffic_class_result_frame_size[i];
        }
        result_frame_size = service->result_frame_size;
        const uint32_t seq_end = tdma_service_load(&service->result_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            result_copied = true;
            break;
        }
    }
    if (!result_copied) {
        return false;
    }

    if (snapshot->intent_seq > snapshot->completed_seq && abort_seq < snapshot->intent_seq) {
        snapshot->state = tdma_service_STATE_PENDING;
        snapshot->armed = 1u;
        if (snapshot->last_result != tdma_service_RESULT_WAITING_FOR_WINDOW) {
            snapshot->last_result = tdma_service_RESULT_ACCEPTED;
        }
    } else if (snapshot->completed_seq == snapshot->intent_seq && result_frame_size != 0u) {
        snapshot->frame_size = result_frame_size;
    }

    tdma_payload_registry_snapshot_t registry_snapshot;
    if (!tdma_payload_registry_get_snapshot(&service->payload_registry,
                                            &registry_snapshot)) {
        return false;
    }
    snapshot->payload_registry_config_seq = registry_snapshot.config_seq;
    snapshot->payload_registry_registration_seq =
        registry_snapshot.registration_seq;
    snapshot->payload_registry_used_count = registry_snapshot.used_count;
    snapshot->payload_registry_admitted_count = registry_snapshot.admitted_count;
    snapshot->payload_registry_reject_count = registry_snapshot.reject_count;
    snapshot->payload_registry_last_result = registry_snapshot.last_result;
    snapshot->payload_registry_last_payload_class =
        registry_snapshot.last_payload_class;

    tdma_ring_runtime_snapshot_t ring_snapshot;
    if (!tdma_ring_runtime_get_snapshot(&service->ring_runtime, &ring_snapshot)) {
        return false;
    }
    snapshot->ring_enabled = ring_snapshot.enabled;
    snapshot->ring_config_seq = ring_snapshot.config_seq;
    snapshot->ring_config_reject_count = ring_snapshot.config_reject_count;
    snapshot->ring_service_seq = ring_snapshot.service_seq;
    snapshot->ring_node_count = ring_snapshot.node_count;
    snapshot->ring_local_slot_id = ring_snapshot.local_slot_id;
    snapshot->ring_reference_slot_id = ring_snapshot.reference_slot_id;
    snapshot->ring_up_group_id = ring_snapshot.up_group_id;
    snapshot->ring_down_group_id = ring_snapshot.down_group_id;
    snapshot->ring_flags = ring_snapshot.flags;
    snapshot->ring_up_configured = ring_snapshot.up_configured;
    snapshot->ring_down_configured = ring_snapshot.down_configured;
    snapshot->ring_up_running = ring_snapshot.up_running;
    snapshot->ring_down_running = ring_snapshot.down_running;
    snapshot->ring_seq = ring_snapshot.ring_seq;
    snapshot->ring_last_error = ring_snapshot.last_reason;
    snapshot->simultaneous_feedback_loop_evidence =
        ring_snapshot.simultaneous_feedback_loop_evidence;
    snapshot->ring_profile_crc32 = ring_snapshot.ring_profile_crc32;
    snapshot->ring_schedule_crc32 = ring_snapshot.schedule_crc32;
    snapshot->ring_feedback_timeout_ns = ring_snapshot.feedback_timeout_ns;
    snapshot->ring_loop_delay_ns = ring_snapshot.loop_delay_ns;
    snapshot->ring_loop_delay_tolerance_ns =
        ring_snapshot.loop_delay_tolerance_ns;
    snapshot->ring_adapter_started = ring_snapshot.adapter_started;
    snapshot->ring_adapter_start_count = ring_snapshot.adapter_start_count;
    snapshot->ring_adapter_stop_count = ring_snapshot.adapter_stop_count;
    snapshot->ring_adapter_service_count = ring_snapshot.adapter_service_count;
    snapshot->ring_adapter_last_error = ring_snapshot.adapter_last_error;
    snapshot->ring_adapter_tx_count = ring_snapshot.adapter_tx_count;
    snapshot->ring_adapter_rx_count = ring_snapshot.adapter_rx_count;
    snapshot->ring_adapter_rx_bad_count = ring_snapshot.adapter_rx_bad_count;
    snapshot->ring_adapter_rx_transport_bad_count =
        ring_snapshot.adapter_rx_transport_bad_count;
    snapshot->ring_adapter_rx_schedule_bad_count =
        ring_snapshot.adapter_rx_schedule_bad_count;
    snapshot->ring_adapter_rx_profile_bad_count =
        ring_snapshot.adapter_rx_profile_bad_count;
    snapshot->ring_adapter_last_bad_transport_result =
        ring_snapshot.adapter_last_bad_transport_result;
    snapshot->ring_adapter_last_bad_sequence =
        ring_snapshot.adapter_last_bad_sequence;
    snapshot->ring_adapter_last_bad_schedule_crc32 =
        ring_snapshot.adapter_last_bad_schedule_crc32;
    snapshot->ring_adapter_last_bad_profile_crc32 =
        ring_snapshot.adapter_last_bad_profile_crc32;
    snapshot->ring_adapter_last_bad_header_diff_count =
        ring_snapshot.adapter_last_bad_header_diff_count;
    snapshot->ring_adapter_last_bad_header_first_diff_offset =
        ring_snapshot.adapter_last_bad_header_first_diff_offset;
    snapshot->ring_adapter_last_bad_header_expected_byte =
        ring_snapshot.adapter_last_bad_header_expected_byte;
    snapshot->ring_adapter_last_bad_header_observed_byte =
        ring_snapshot.adapter_last_bad_header_observed_byte;
    snapshot->ring_up_tx_sequence = ring_snapshot.up_tx_sequence;
    snapshot->ring_down_rx_sequence = ring_snapshot.down_rx_sequence;
    snapshot->ring_up_tx_frame_crc32 = ring_snapshot.up_tx_frame_crc32;
    snapshot->ring_down_rx_frame_crc32 = ring_snapshot.down_rx_frame_crc32;
    snapshot->ring_timestamp_resolution_ns =
        ring_snapshot.timestamp_resolution_ns;
    snapshot->ring_timestamp_flags = ring_snapshot.timestamp_flags;
    snapshot->ring_idle_beacon_tx_count = ring_snapshot.idle_beacon_tx_count;
    snapshot->ring_idle_beacon_rx_count = ring_snapshot.idle_beacon_rx_count;
    snapshot->ring_feedback_round_trip_ns =
        ring_snapshot.feedback_round_trip_ns;
    tdma_service_split_u64(ring_snapshot.reference_tx_timestamp_ns,
                           &snapshot->ring_reference_tx_timestamp_ns_lo,
                           &snapshot->ring_reference_tx_timestamp_ns_hi);
    tdma_service_split_u64(ring_snapshot.feedback_rx_timestamp_ns,
                           &snapshot->ring_feedback_rx_timestamp_ns_lo,
                           &snapshot->ring_feedback_rx_timestamp_ns_hi);
    snapshot->ring_clock_observation_valid =
        ring_snapshot.clock_observation.valid;
    snapshot->ring_clock_observation_node_count =
        ring_snapshot.clock_observation.node_count;
    snapshot->ring_clock_observation_source_node =
        ring_snapshot.clock_observation.source_node;
    snapshot->ring_clock_observation_reference_node =
        ring_snapshot.clock_observation.reference_node;
    snapshot->ring_clock_observation_sequence =
        ring_snapshot.clock_observation.correlated_sequence;
    snapshot->ring_clock_observation_frame_crc32 =
        ring_snapshot.clock_observation.frame_crc32;
    snapshot->ring_clock_observation_schedule_crc32 =
        ring_snapshot.clock_observation.schedule_crc32;
    snapshot->ring_clock_observation_resolution_ns =
        ring_snapshot.clock_observation.timestamp_resolution_ns;
    snapshot->ring_clock_observation_flags =
        ring_snapshot.clock_observation.timestamp_flags;
    snapshot->ring_clock_observation_correlated =
        ring_snapshot.clock_observation.correlated_frame_evidence;
    tdma_service_split_u64(
        ring_snapshot.clock_observation.reference_tx_timestamp_ns,
        &snapshot->ring_clock_reference_tx_timestamp_ns_lo,
        &snapshot->ring_clock_reference_tx_timestamp_ns_hi);
    tdma_service_split_u64(
        ring_snapshot.clock_observation.local_rx_timestamp_ns,
        &snapshot->ring_clock_local_rx_timestamp_ns_lo,
        &snapshot->ring_clock_local_rx_timestamp_ns_hi);

    if (service->traffic_scheduler != NULL) {
        tdma_traffic_scheduler_snapshot_t scheduler_snapshot;
        if (!tdma_traffic_scheduler_get_snapshot(service->traffic_scheduler,
                                                  &scheduler_snapshot)) {
            return false;
        }
        snapshot->traffic_scheduler_configured =
            scheduler_snapshot.configured;
        snapshot->traffic_scheduler_enqueue_seq =
            scheduler_snapshot.enqueue_seq;
        snapshot->traffic_scheduler_dispatch_seq =
            scheduler_snapshot.dispatch_seq;
        snapshot->traffic_scheduler_fault_latched =
            scheduler_snapshot.fault_latched;
        snapshot->traffic_scheduler_last_result =
            scheduler_snapshot.last_result;
        snapshot->traffic_scheduler_last_class =
            scheduler_snapshot.last_traffic_class;
        uint32_t queued_count = 0u;
        for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
            queued_count += scheduler_snapshot.traffic[i].current_depth;
            snapshot->traffic_scheduler_completed_seq[i] =
                scheduler_snapshot.traffic[i].last_completed_sequence;
        }
        snapshot->traffic_scheduler_queued_count = queued_count;
        snapshot->intent_seq = scheduler_snapshot.enqueue_seq;
        snapshot->completed_seq = 0u;
        for (uint32_t i = 0u; i < TDMA_TRAFFIC_CLASS_COUNT; i++) {
            if (snapshot->traffic_scheduler_completed_seq[i] >
                snapshot->completed_seq) {
                snapshot->completed_seq =
                    snapshot->traffic_scheduler_completed_seq[i];
            }
        }
        if (snapshot->traffic_scheduler_queued_count != 0u ||
            service->intent_seq > service->completed_seq) {
            snapshot->state = tdma_service_STATE_PENDING;
            snapshot->armed = 1u;
            snapshot->last_result = tdma_service_RESULT_ACCEPTED;
        }
    }

    return true;
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

bool tdma_service_reset_flight_fifo(tdma_service_service_t *service)
{
    if (service == NULL) {
        return false;
    }
    tdma_ring_runtime_snapshot_t ring_snapshot;
    tdma_flight_engine_snapshot_t engine_snapshot;
    if (!tdma_ring_runtime_get_snapshot(&service->ring_runtime,
                                        &ring_snapshot) ||
        !tdma_flight_engine_get_snapshot(&service->flight_engine,
                                         &engine_snapshot) ||
        ring_snapshot.enabled != 0u ||
        ring_snapshot.adapter_started != 0u ||
        engine_snapshot.active != 0u) {
        return false;
    }
    return tdma_flight_fifo_reset_stopped(&service->flight_fifo);
}

bool tdma_service_configure_flight_map(
    tdma_service_service_t *service,
    const tdma_process_image_map_t *map)
{
    if (service == NULL || map == NULL) {
        return false;
    }
    tdma_ring_runtime_snapshot_t ring_snapshot;
    if (!tdma_ring_runtime_get_snapshot(&service->ring_runtime,
                                        &ring_snapshot) ||
        ring_snapshot.enabled != 0u || ring_snapshot.adapter_started != 0u) {
        return false;
    }
    return tdma_flight_engine_configure(&service->flight_engine, map);
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

bool tdma_service_get_result_frame(const tdma_service_service_t *service,
                                           uint8_t *frame,
                                           size_t frame_capacity,
                                           size_t *frame_size)
{
    if (frame_size != NULL) {
        *frame_size = 0u;
    }
    if (service == NULL || frame == NULL || frame_size == NULL || frame_capacity == 0u) {
        return false;
    }

    while (true) {
        const uint32_t seq_begin = tdma_service_load(&service->result_guard);
        if ((seq_begin & 1u) != 0u) {
            continue;
        }
        const size_t result_size = (size_t)service->result_frame_size;
        const uint32_t last_result = service->last_result;
        if (result_size == 0u || result_size > frame_capacity ||
            last_result != tdma_service_RESULT_FRAME_READY) {
            const uint32_t seq_end = tdma_service_load(&service->result_guard);
            if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
                return false;
            }
            continue;
        }
        memcpy(frame, service->result_frame, result_size);
        *frame_size = result_size;
        const uint32_t seq_end = tdma_service_load(&service->result_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            return true;
        }
    }
}

bool tdma_service_get_class_result_frame(
    const tdma_service_service_t *service,
    uint32_t traffic_class,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size)
{
    if (frame_size != NULL) {
        *frame_size = 0u;
    }
    if (service == NULL || traffic_class >= TDMA_TRAFFIC_CLASS_COUNT ||
        frame == NULL || frame_size == NULL || frame_capacity == 0u) {
        return false;
    }
    while (true) {
        const uint32_t seq_begin = tdma_service_load(&service->result_guard);
        if ((seq_begin & 1u) != 0u) {
            continue;
        }
        const size_t result_size =
            service->traffic_class_result_frame_size[traffic_class];
        const uint32_t last_result =
            service->traffic_class_last_result[traffic_class];
        if (result_size == 0u || result_size > frame_capacity ||
            last_result != tdma_service_RESULT_FRAME_READY) {
            const uint32_t seq_end =
                tdma_service_load(&service->result_guard);
            if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
                return false;
            }
            continue;
        }
        memcpy(frame,
               service->traffic_class_result_frame[traffic_class],
               result_size);
        *frame_size = result_size;
        const uint32_t seq_end = tdma_service_load(&service->result_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            return true;
        }
    }
}
