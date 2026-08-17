#include "refmem_realtime_tdma.h"

#include <string.h>

static tdma_service_role_t refmem_realtime_tdma_to_service_role(
    refmem_spi_physical_role_t role)
{
    switch (role) {
    case REFMEM_SPI_PHYSICAL_ROLE_MASTER:
        return TDMA_SERVICE_ROLE_MASTER;
    case REFMEM_SPI_PHYSICAL_ROLE_SLAVE:
        return TDMA_SERVICE_ROLE_SLAVE;
    case REFMEM_SPI_PHYSICAL_ROLE_DISABLED:
    default:
        return TDMA_SERVICE_ROLE_DISABLED;
    }
}

static refmem_spi_physical_role_t refmem_realtime_tdma_from_service_role(
    tdma_service_role_t role)
{
    switch (role) {
    case TDMA_SERVICE_ROLE_MASTER:
        return REFMEM_SPI_PHYSICAL_ROLE_MASTER;
    case TDMA_SERVICE_ROLE_SLAVE:
        return REFMEM_SPI_PHYSICAL_ROLE_SLAVE;
    case TDMA_SERVICE_ROLE_DISABLED:
    default:
        return REFMEM_SPI_PHYSICAL_ROLE_DISABLED;
    }
}

static tdma_service_pin_config_t refmem_realtime_tdma_to_service_pins(
    const refmem_spi_physical_pin_config_t *pins)
{
    tdma_service_pin_config_t mapped = {0};
    if (pins != NULL) {
        mapped.rx_pin = pins->rx_pin;
        mapped.csn_pin = pins->csn_pin;
        mapped.sck_pin = pins->sck_pin;
        mapped.tx_pin = pins->tx_pin;
    }
    return mapped;
}

static refmem_spi_physical_pin_config_t refmem_realtime_tdma_from_service_pins(
    const tdma_service_pin_config_t *pins)
{
    refmem_spi_physical_pin_config_t mapped = {0};
    if (pins != NULL) {
        mapped.rx_pin = pins->rx_pin;
        mapped.csn_pin = pins->csn_pin;
        mapped.sck_pin = pins->sck_pin;
        mapped.tx_pin = pins->tx_pin;
    }
    return mapped;
}

static bool refmem_realtime_tdma_transmit_bridge(
    void *context,
    const uint8_t *frame,
    size_t frame_size,
    tdma_service_role_t role,
    uint32_t baud_hz,
    const tdma_service_pin_config_t *pins,
    uint32_t deadline_1e3ns,
    tdma_service_exec_status_t *status)
{
    refmem_realtime_tdma_service_t *service =
        (refmem_realtime_tdma_service_t *)context;
    if (service == NULL || service->ops == NULL ||
        service->ops->transmit == NULL) {
        return false;
    }
    const refmem_spi_physical_pin_config_t refmem_pins =
        refmem_realtime_tdma_from_service_pins(pins);
    return service->ops->transmit(service->ops_context,
                                  frame,
                                  frame_size,
                                  refmem_realtime_tdma_from_service_role(role),
                                  baud_hz,
                                  &refmem_pins,
                                  deadline_1e3ns,
                                  status);
}

static bool refmem_realtime_tdma_receive_bridge(
    void *context,
    uint8_t *frame,
    size_t frame_capacity,
    tdma_service_role_t role,
    uint32_t baud_hz,
    const tdma_service_pin_config_t *pins,
    uint32_t deadline_1e3ns,
    tdma_service_exec_status_t *status)
{
    refmem_realtime_tdma_service_t *service =
        (refmem_realtime_tdma_service_t *)context;
    if (service == NULL || service->ops == NULL ||
        service->ops->receive == NULL) {
        return false;
    }
    const refmem_spi_physical_pin_config_t refmem_pins =
        refmem_realtime_tdma_from_service_pins(pins);
    const size_t refmem_capacity =
        frame_capacity > REFMEM_REALTIME_TDMA_FRAME_MAX
            ? REFMEM_REALTIME_TDMA_FRAME_MAX
            : frame_capacity;
    return service->ops->receive(service->ops_context,
                                 frame,
                                 refmem_capacity,
                                 refmem_realtime_tdma_from_service_role(role),
                                 baud_hz,
                                 &refmem_pins,
                                 deadline_1e3ns,
                                 status);
}

static const tdma_service_ops_t s_refmem_realtime_tdma_bridge_ops = {
    .transmit = refmem_realtime_tdma_transmit_bridge,
    .receive = refmem_realtime_tdma_receive_bridge,
};

static tdma_service_intent_config_t refmem_realtime_tdma_to_service_config(
    const refmem_realtime_tdma_intent_config_t *config)
{
    tdma_service_intent_config_t mapped = {0};
    if (config != NULL) {
        mapped.window_epoch = config->window_epoch;
        mapped.window_index = config->window_index;
        mapped.deadline_1e3ns = config->deadline_1e3ns;
        mapped.role = refmem_realtime_tdma_to_service_role(config->role);
        mapped.baud_hz = config->baud_hz;
        mapped.pins = refmem_realtime_tdma_to_service_pins(&config->pins);
        mapped.frame_class = TDMA_SERVICE_FRAME_CLASS_SHORT;
        mapped.payload_class = TDMA_SERVICE_PAYLOAD_CLASS_REFMEM_DELTA;
        mapped.scheduled_window_valid = config->vdc_window_plan_valid;
        mapped.scheduled_window_class = config->vdc_window_class;
        mapped.schedule_crc32 = config->vdc_schedule_crc32;
        mapped.scheduled_window_start_ns = config->vdc_window_start_ns;
        mapped.scheduled_window_end_ns = config->vdc_window_end_ns;
        mapped.scheduled_guard_start_ns = config->vdc_guard_start_ns;
        mapped.scheduled_guard_end_ns = config->vdc_guard_end_ns;
        mapped.frame = config->frame;
        mapped.frame_size = config->frame_size;
    }
    return mapped;
}

static void refmem_realtime_tdma_from_service_snapshot(
    const tdma_service_snapshot_t *source,
    refmem_realtime_tdma_snapshot_t *target)
{
    memset(target, 0, sizeof(*target));
    target->state = source->state;
    target->owner_core = source->owner_core;
    target->armed = source->armed;
    target->service_count = source->service_count;
    target->intent_seq = source->intent_seq;
    target->completed_seq = source->completed_seq;
    target->dropped_seq = source->dropped_seq;
    target->window_epoch = source->window_epoch;
    target->window_index = source->window_index;
    target->intent_type = source->intent_type;
    target->role = source->role;
    target->baud_hz = source->baud_hz;
    target->rx_pin = source->rx_pin;
    target->csn_pin = source->csn_pin;
    target->sck_pin = source->sck_pin;
    target->tx_pin = source->tx_pin;
    target->deadline_1e3ns = source->deadline_1e3ns;
    target->frame_size = source->frame_size;
    target->ready_count = source->ready_count;
    target->timeout_count = source->timeout_count;
    target->overrun_count = source->overrun_count;
    target->reject_count = source->reject_count;
    target->last_result = source->last_result;
    target->last_error = source->last_error;
    target->timestamp_source = source->timestamp_source;
    target->timestamp_resolution_ns = source->timestamp_resolution_ns;
    target->timestamp_flags = source->timestamp_flags;
    target->vdc_window_plan_valid = source->scheduled_window_valid;
    target->vdc_window_class = source->scheduled_window_class;
    target->vdc_schedule_crc32 = source->schedule_crc32;
    target->vdc_window_miss_count = source->scheduled_window_miss_count;
    target->vdc_window_wait_ns = source->scheduled_window_wait_ns;
    target->vdc_window_late_ns = source->scheduled_window_late_ns;
    target->vdc_window_start_ns_lo = source->scheduled_window_start_ns_lo;
    target->vdc_window_start_ns_hi = source->scheduled_window_start_ns_hi;
    target->vdc_window_end_ns_lo = source->scheduled_window_end_ns_lo;
    target->vdc_window_end_ns_hi = source->scheduled_window_end_ns_hi;
    target->vdc_guard_start_ns_lo = source->scheduled_guard_start_ns_lo;
    target->vdc_guard_start_ns_hi = source->scheduled_guard_start_ns_hi;
    target->vdc_guard_end_ns_lo = source->scheduled_guard_end_ns_lo;
    target->vdc_guard_end_ns_hi = source->scheduled_guard_end_ns_hi;
    target->submit_time_ns_lo = source->submit_time_ns_lo;
    target->submit_time_ns_hi = source->submit_time_ns_hi;
    target->core1_arm_time_ns_lo = source->core1_arm_time_ns_lo;
    target->core1_arm_time_ns_hi = source->core1_arm_time_ns_hi;
    target->core1_start_time_ns_lo = source->core1_start_time_ns_lo;
    target->core1_start_time_ns_hi = source->core1_start_time_ns_hi;
    target->core1_done_time_ns_lo = source->core1_done_time_ns_lo;
    target->core1_done_time_ns_hi = source->core1_done_time_ns_hi;
    target->core1_elapsed_ns = source->core1_elapsed_ns;
    target->foundation_profile_crc32 = source->foundation_profile_crc32;
    target->foundation_owner_instance_id = source->foundation_owner_instance_id;
    target->adapter_type = source->adapter_type;
    target->payload_whitelist_mask = source->payload_whitelist_mask;
    target->ring_enabled = source->ring_enabled;
    target->ring_config_seq = source->ring_config_seq;
    target->ring_config_reject_count = source->ring_config_reject_count;
    target->ring_node_count = source->ring_node_count;
    target->ring_local_slot_id = source->ring_local_slot_id;
    target->ring_reference_slot_id = source->ring_reference_slot_id;
    target->ring_up_group_id = source->ring_up_group_id;
    target->ring_down_group_id = source->ring_down_group_id;
    target->ring_profile_crc32 = source->ring_profile_crc32;
    target->ring_schedule_crc32 = source->ring_schedule_crc32;
    target->ring_up_running = source->ring_up_running;
    target->ring_down_running = source->ring_down_running;
    target->ring_seq = source->ring_seq;
    target->ring_last_error = source->ring_last_error;
    target->simultaneous_feedback_loop_evidence =
        source->simultaneous_feedback_loop_evidence;
    target->payload_registry_config_seq = source->payload_registry_config_seq;
    target->payload_registry_registration_seq =
        source->payload_registry_registration_seq;
    target->payload_registry_used_count = source->payload_registry_used_count;
    target->payload_registry_admitted_count =
        source->payload_registry_admitted_count;
    target->payload_registry_reject_count = source->payload_registry_reject_count;
    target->payload_registry_last_result = source->payload_registry_last_result;
    target->payload_registry_last_payload_class =
        source->payload_registry_last_payload_class;
}

bool refmem_realtime_tdma_init(refmem_realtime_tdma_service_t *service)
{
    if (service == NULL) {
        return false;
    }

    memset(service, 0, sizeof(*service));
    if (!tdma_service_init(&service->scheduler)) {
        return false;
    }

    return refmem_tdma_payload_register(&service->scheduler);
}

bool refmem_realtime_tdma_bind_ops(refmem_realtime_tdma_service_t *service,
                                   const refmem_realtime_tdma_ops_t *ops,
                                   void *ops_context)
{
    if (service == NULL || ops == NULL ||
        ops->transmit == NULL || ops->receive == NULL) {
        return false;
    }
    service->ops = ops;
    service->ops_context = ops_context;
    return tdma_service_bind_ops(&service->scheduler,
                                 &s_refmem_realtime_tdma_bridge_ops,
                                 service);
}

bool refmem_realtime_tdma_configure_foundation_profile(
    refmem_realtime_tdma_service_t *service,
    const tdma_foundation_profile_t *profile,
    uint32_t schedule_crc32)
{
    return service != NULL &&
           tdma_service_configure_foundation_profile(&service->scheduler,
                                                     profile,
                                                     schedule_crc32);
}

bool refmem_realtime_tdma_submit_tx(
    refmem_realtime_tdma_service_t *service,
    const refmem_realtime_tdma_intent_config_t *config)
{
    if (service == NULL || config == NULL) {
        return false;
    }
    const tdma_service_intent_config_t mapped =
        refmem_realtime_tdma_to_service_config(config);
    return tdma_service_submit_tx(&service->scheduler, &mapped);
}

bool refmem_realtime_tdma_submit_rx(
    refmem_realtime_tdma_service_t *service,
    const refmem_realtime_tdma_intent_config_t *config)
{
    if (service == NULL || config == NULL) {
        return false;
    }
    const tdma_service_intent_config_t mapped =
        refmem_realtime_tdma_to_service_config(config);
    return tdma_service_submit_rx(&service->scheduler, &mapped);
}

void refmem_realtime_tdma_abort(refmem_realtime_tdma_service_t *service)
{
    if (service == NULL) {
        return;
    }
    tdma_service_abort(&service->scheduler);
}

void refmem_realtime_tdma_core1_service(refmem_realtime_tdma_service_t *service)
{
    if (service == NULL) {
        return;
    }
    tdma_service_core1_service(&service->scheduler);
}

bool refmem_realtime_tdma_get_snapshot(
    const refmem_realtime_tdma_service_t *service,
    refmem_realtime_tdma_snapshot_t *snapshot)
{
    if (service == NULL || snapshot == NULL) {
        return false;
    }
    tdma_service_snapshot_t source;
    if (!tdma_service_get_snapshot(&service->scheduler, &source)) {
        return false;
    }
    refmem_realtime_tdma_from_service_snapshot(&source, snapshot);
    return true;
}

bool refmem_realtime_tdma_get_result_frame(
    const refmem_realtime_tdma_service_t *service,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size)
{
    if (service == NULL) {
        if (frame_size != NULL) {
            *frame_size = 0u;
        }
        return false;
    }
    return tdma_service_get_result_frame(&service->scheduler,
                                         frame,
                                         frame_capacity,
                                         frame_size);
}
