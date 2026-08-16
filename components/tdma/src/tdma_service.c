#include "tdma_service.h"

#include <string.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/time.h"
#endif

#define tdma_service_OWNER_CORE1 1u
#define tdma_service_ERROR_NO_OPS 100u
#define tdma_service_TIMESTAMP_RESOLUTION_NS 1000u
#define tdma_service_TIMESTAMP_SOURCE \
    tdma_service_TIMESTAMP_SOURCE_SOFTWARE_US
#define tdma_service_TIMESTAMP_FLAGS \
    tdma_service_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY
#define tdma_service_ERROR_WINDOW_MISSED 101u

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

static bool tdma_service_has_pending(const tdma_service_service_t *service)
{
    const uint32_t intent_seq = tdma_service_load(&service->intent_seq);
    const uint32_t completed_seq = tdma_service_load(&service->completed_seq);
    const uint32_t abort_seq = tdma_service_load(&service->abort_seq);
    return intent_seq > completed_seq && abort_seq < intent_seq;
}

static bool tdma_service_payload_registered(
    const tdma_service_service_t *service,
    uint32_t frame_class,
    uint32_t payload_class,
    size_t frame_size)
{
    if (service == NULL ||
        payload_class == TDMA_SERVICE_PAYLOAD_CLASS_NONE ||
        (frame_class != TDMA_SERVICE_FRAME_CLASS_SHORT &&
         frame_class != TDMA_SERVICE_FRAME_CLASS_LONG)) {
        return false;
    }

    for (uint32_t i = 0u; i < TDMA_SERVICE_PAYLOAD_REGISTRY_COUNT; i++) {
        const tdma_service_payload_binding_t *binding =
            &service->payload_binding[i];
        if (binding->used != 0u &&
            binding->payload_class == payload_class &&
            binding->frame_class == frame_class) {
            return frame_size <= binding->max_payload_size;
        }
    }
    return false;
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
    service->deadline_1e3ns = config->deadline_1e3ns;
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

bool tdma_service_register_payload(tdma_service_service_t *service,
                                   const tdma_service_payload_binding_t *binding)
{
    if (service == NULL || binding == NULL ||
        binding->payload_class == TDMA_SERVICE_PAYLOAD_CLASS_NONE ||
        (binding->frame_class != TDMA_SERVICE_FRAME_CLASS_SHORT &&
         binding->frame_class != TDMA_SERVICE_FRAME_CLASS_LONG) ||
        binding->max_payload_size > TDMA_SERVICE_FRAME_MAX) {
        return false;
    }
    if (binding->frame_class == TDMA_SERVICE_FRAME_CLASS_SHORT &&
        binding->max_payload_size > TDMA_SERVICE_SHORT_FRAME_MAX) {
        return false;
    }

    for (uint32_t i = 0u; i < TDMA_SERVICE_PAYLOAD_REGISTRY_COUNT; i++) {
        tdma_service_payload_binding_t *entry = &service->payload_binding[i];
        if (entry->used == 0u ||
            (entry->producer_id == binding->producer_id &&
             entry->consumer_id == binding->consumer_id &&
             entry->payload_class == binding->payload_class)) {
            *entry = *binding;
            entry->used = 1u;
            return true;
        }
    }
    return false;
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

void tdma_service_core1_service(tdma_service_service_t *service)
{
    if (service == NULL || service->state == tdma_service_STATE_UNINIT) {
        return;
    }

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

    uint8_t frame[tdma_service_FRAME_MAX];
    uint32_t intent_type;
    tdma_service_role_t role;
    tdma_service_pin_config_t pins;
    uint32_t baud_hz;
    uint32_t deadline_1e3ns;
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
        deadline_1e3ns = service->deadline_1e3ns;
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
            tdma_service_begin_result_write(service);
            service->armed = 1u;
            service->scheduled_window_wait_ns =
                tdma_service_delta_ns(scheduled_guard_start_ns, now_ns);
            service->scheduled_window_late_ns = 0u;
            service->last_result = tdma_service_RESULT_WAITING_FOR_WINDOW;
            service->state = tdma_service_STATE_ARMED;
            tdma_service_end_result_write(service);
            return;
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
            tdma_service_end_result_write(service);
            return;
        }
        while (now_ns < scheduled_window_start_ns) {
            now_ns = tdma_service_now_ns();
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
                                    deadline_1e3ns,
                                    &exec_status);
    } else if (intent_type == tdma_service_INTENT_RX_WINDOW) {
        ok = service->ops->receive(service->ops_context,
                                   frame,
                                   sizeof(frame),
                                   role,
                                   baud_hz,
                                   &pins,
                                   deadline_1e3ns,
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
    } else if (exec_status.result == tdma_service_EXEC_TIMEOUT) {
        service->timeout_count++;
        service->last_result = tdma_service_RESULT_TIMEOUT;
        service->state = tdma_service_STATE_ERROR;
    } else {
        service->overrun_count++;
        service->last_result = tdma_service_RESULT_OVERRUN;
        service->state = tdma_service_STATE_ERROR;
    }
    tdma_service_end_result_write(service);
}

bool tdma_service_get_snapshot(const tdma_service_service_t *service,
                                       tdma_service_snapshot_t *snapshot)
{
    if (service == NULL || snapshot == NULL) {
        return false;
    }

    uint32_t abort_seq;
    while (true) {
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
        snapshot->deadline_1e3ns = service->deadline_1e3ns;
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
        tdma_service_split_u64(service->submit_time_ns,
                                       &snapshot->submit_time_ns_lo,
                                       &snapshot->submit_time_ns_hi);
        const uint32_t seq_end = tdma_service_load(&service->intent_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            break;
        }
    }

    uint32_t result_frame_size = 0u;
    while (true) {
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
        snapshot->timestamp_source =
            (uint32_t)tdma_service_TIMESTAMP_SOURCE;
        snapshot->timestamp_resolution_ns =
            tdma_service_TIMESTAMP_RESOLUTION_NS;
        snapshot->timestamp_flags = tdma_service_TIMESTAMP_FLAGS;
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
        result_frame_size = service->result_frame_size;
        const uint32_t seq_end = tdma_service_load(&service->result_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            break;
        }
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

    return true;
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
