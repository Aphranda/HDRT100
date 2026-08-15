#include "refmem_realtime_tdma.h"

#include <string.h>

#define REFMEM_REALTIME_TDMA_OWNER_CORE1 1u
#define REFMEM_REALTIME_TDMA_ERROR_NO_OPS 100u

static uint32_t refmem_realtime_tdma_load(const volatile uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void refmem_realtime_tdma_begin_intent_write(refmem_realtime_tdma_service_t *service)
{
    service->intent_guard++;
}

static void refmem_realtime_tdma_end_intent_write(refmem_realtime_tdma_service_t *service)
{
    service->intent_guard++;
}

static void refmem_realtime_tdma_begin_result_write(refmem_realtime_tdma_service_t *service)
{
    service->result_guard++;
}

static void refmem_realtime_tdma_end_result_write(refmem_realtime_tdma_service_t *service)
{
    service->result_guard++;
}

static bool refmem_realtime_tdma_has_pending(const refmem_realtime_tdma_service_t *service)
{
    const uint32_t intent_seq = refmem_realtime_tdma_load(&service->intent_seq);
    const uint32_t completed_seq = refmem_realtime_tdma_load(&service->completed_seq);
    const uint32_t abort_seq = refmem_realtime_tdma_load(&service->abort_seq);
    return intent_seq > completed_seq && abort_seq < intent_seq;
}

static bool refmem_realtime_tdma_submit(refmem_realtime_tdma_service_t *service,
                                        const refmem_realtime_tdma_intent_config_t *config,
                                        refmem_realtime_tdma_intent_t intent)
{
    if (service == NULL || config == NULL ||
        (intent == REFMEM_REALTIME_TDMA_INTENT_TX_FRAME &&
         (config->frame == NULL || config->frame_size == 0u)) ||
        config->frame_size > REFMEM_REALTIME_TDMA_FRAME_MAX) {
        if (service != NULL) {
            refmem_realtime_tdma_begin_intent_write(service);
            service->reject_count++;
            refmem_realtime_tdma_end_intent_write(service);
        }
        return false;
    }

    if (refmem_realtime_tdma_has_pending(service)) {
        refmem_realtime_tdma_begin_intent_write(service);
        service->reject_count++;
        refmem_realtime_tdma_end_intent_write(service);
        return false;
    }

    refmem_realtime_tdma_begin_intent_write(service);
    service->intent_seq++;
    service->window_epoch = config->window_epoch;
    service->window_index = config->window_index;
    service->intent_type = (uint32_t)intent;
    service->role = (uint32_t)config->role;
    service->baud_hz = config->baud_hz;
    service->deadline_us = config->deadline_us;
    service->frame_size = (uint32_t)config->frame_size;
    if (config->frame_size != 0u && config->frame != NULL) {
        memcpy(service->frame, config->frame, config->frame_size);
    }
    refmem_realtime_tdma_end_intent_write(service);
    return true;
}

bool refmem_realtime_tdma_init(refmem_realtime_tdma_service_t *service)
{
    if (service == NULL) {
        return false;
    }

    memset(service, 0, sizeof(*service));
    service->state = REFMEM_REALTIME_TDMA_STATE_IDLE;
    service->owner_core = REFMEM_REALTIME_TDMA_OWNER_CORE1;
    service->last_result = REFMEM_REALTIME_TDMA_RESULT_NONE;
    return true;
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
    return true;
}

bool refmem_realtime_tdma_submit_tx(refmem_realtime_tdma_service_t *service,
                                    const refmem_realtime_tdma_intent_config_t *config)
{
    return refmem_realtime_tdma_submit(service, config, REFMEM_REALTIME_TDMA_INTENT_TX_FRAME);
}

bool refmem_realtime_tdma_submit_rx(refmem_realtime_tdma_service_t *service,
                                    const refmem_realtime_tdma_intent_config_t *config)
{
    return refmem_realtime_tdma_submit(service, config, REFMEM_REALTIME_TDMA_INTENT_RX_WINDOW);
}

void refmem_realtime_tdma_abort(refmem_realtime_tdma_service_t *service)
{
    if (service == NULL) {
        return;
    }

    refmem_realtime_tdma_begin_intent_write(service);
    service->abort_seq = service->intent_seq;
    refmem_realtime_tdma_end_intent_write(service);
}

void refmem_realtime_tdma_core1_service(refmem_realtime_tdma_service_t *service)
{
    if (service == NULL || service->state == REFMEM_REALTIME_TDMA_STATE_UNINIT) {
        return;
    }

    const uint32_t intent_seq = refmem_realtime_tdma_load(&service->intent_seq);
    const uint32_t abort_seq = refmem_realtime_tdma_load(&service->abort_seq);
    if (intent_seq <= service->completed_seq) {
        refmem_realtime_tdma_begin_result_write(service);
        service->service_count++;
        service->armed = 0u;
        if (service->state == REFMEM_REALTIME_TDMA_STATE_UNINIT) {
            service->state = REFMEM_REALTIME_TDMA_STATE_IDLE;
        }
        refmem_realtime_tdma_end_result_write(service);
        return;
    }

    refmem_realtime_tdma_begin_result_write(service);
    service->service_count++;
    if (intent_seq > service->completed_seq) {
        if (abort_seq >= intent_seq) {
            service->dropped_seq = intent_seq;
            service->completed_seq = intent_seq;
            service->armed = 0u;
            service->state = REFMEM_REALTIME_TDMA_STATE_IDLE;
            service->last_result = REFMEM_REALTIME_TDMA_RESULT_NONE;
        } else {
            service->armed = 1u;
            service->state = REFMEM_REALTIME_TDMA_STATE_ARMED;
        }
    }
    refmem_realtime_tdma_end_result_write(service);

    uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
    uint32_t intent_type;
    refmem_spi_physical_role_t role;
    uint32_t baud_hz;
    uint32_t deadline_us;
    size_t frame_size;
    while (true) {
        const uint32_t seq_begin = refmem_realtime_tdma_load(&service->intent_guard);
        if ((seq_begin & 1u) != 0u) {
            continue;
        }
        intent_type = service->intent_type;
        role = (refmem_spi_physical_role_t)service->role;
        baud_hz = service->baud_hz;
        deadline_us = service->deadline_us;
        frame_size = (size_t)service->frame_size;
        if (frame_size > sizeof(frame)) {
            frame_size = sizeof(frame);
        }
        if (frame_size != 0u) {
            memcpy(frame, service->frame, frame_size);
        }
        const uint32_t seq_end = refmem_realtime_tdma_load(&service->intent_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            break;
        }
    }

    refmem_realtime_tdma_exec_status_t exec_status = {
        .result = REFMEM_REALTIME_TDMA_EXEC_NONE,
        .error = 0u,
        .frame_size = frame_size,
    };
    bool ok = false;
    if (service->ops == NULL) {
        exec_status.result = REFMEM_REALTIME_TDMA_EXEC_ERROR;
        exec_status.error = REFMEM_REALTIME_TDMA_ERROR_NO_OPS;
    } else if (intent_type == REFMEM_REALTIME_TDMA_INTENT_TX_FRAME) {
        ok = service->ops->transmit(service->ops_context,
                                    frame,
                                    frame_size,
                                    role,
                                    baud_hz,
                                    deadline_us,
                                    &exec_status);
    } else if (intent_type == REFMEM_REALTIME_TDMA_INTENT_RX_WINDOW) {
        ok = service->ops->receive(service->ops_context,
                                   frame,
                                   sizeof(frame),
                                   role,
                                   baud_hz,
                                   deadline_us,
                                   &exec_status);
    } else {
        exec_status.result = REFMEM_REALTIME_TDMA_EXEC_ERROR;
        exec_status.error = REFMEM_REALTIME_TDMA_RESULT_BAD_ARGUMENT;
    }

    refmem_realtime_tdma_begin_result_write(service);
    service->completed_seq = intent_seq;
    service->armed = 0u;
    service->last_error = exec_status.error;
    service->result_frame_size = (uint32_t)exec_status.frame_size;
    if (ok &&
        exec_status.result == REFMEM_REALTIME_TDMA_EXEC_RX_OK &&
        exec_status.frame_size <= sizeof(service->result_frame)) {
        memcpy(service->result_frame, frame, exec_status.frame_size);
    }
    if (ok &&
        (exec_status.result == REFMEM_REALTIME_TDMA_EXEC_TX_OK ||
         exec_status.result == REFMEM_REALTIME_TDMA_EXEC_RX_OK)) {
        service->ready_count++;
        service->last_result = REFMEM_REALTIME_TDMA_RESULT_FRAME_READY;
        service->state = REFMEM_REALTIME_TDMA_STATE_DONE;
    } else if (exec_status.result == REFMEM_REALTIME_TDMA_EXEC_TIMEOUT) {
        service->timeout_count++;
        service->last_result = REFMEM_REALTIME_TDMA_RESULT_TIMEOUT;
        service->state = REFMEM_REALTIME_TDMA_STATE_ERROR;
    } else {
        service->overrun_count++;
        service->last_result = REFMEM_REALTIME_TDMA_RESULT_OVERRUN;
        service->state = REFMEM_REALTIME_TDMA_STATE_ERROR;
    }
    refmem_realtime_tdma_end_result_write(service);
}

bool refmem_realtime_tdma_get_snapshot(const refmem_realtime_tdma_service_t *service,
                                       refmem_realtime_tdma_snapshot_t *snapshot)
{
    if (service == NULL || snapshot == NULL) {
        return false;
    }

    uint32_t abort_seq;
    while (true) {
        const uint32_t seq_begin = refmem_realtime_tdma_load(&service->intent_guard);
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
        snapshot->deadline_us = service->deadline_us;
        snapshot->frame_size = service->frame_size;
        snapshot->reject_count = service->reject_count;
        const uint32_t seq_end = refmem_realtime_tdma_load(&service->intent_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            break;
        }
    }

    uint32_t result_frame_size = 0u;
    while (true) {
        const uint32_t seq_begin = refmem_realtime_tdma_load(&service->result_guard);
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
        result_frame_size = service->result_frame_size;
        const uint32_t seq_end = refmem_realtime_tdma_load(&service->result_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            break;
        }
    }

    if (snapshot->intent_seq > snapshot->completed_seq && abort_seq < snapshot->intent_seq) {
        snapshot->state = REFMEM_REALTIME_TDMA_STATE_PENDING;
        snapshot->armed = 1u;
        snapshot->last_result = REFMEM_REALTIME_TDMA_RESULT_ACCEPTED;
    } else if (snapshot->completed_seq == snapshot->intent_seq && result_frame_size != 0u) {
        snapshot->frame_size = result_frame_size;
    }

    return true;
}

bool refmem_realtime_tdma_get_result_frame(const refmem_realtime_tdma_service_t *service,
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
        const uint32_t seq_begin = refmem_realtime_tdma_load(&service->result_guard);
        if ((seq_begin & 1u) != 0u) {
            continue;
        }
        const size_t result_size = (size_t)service->result_frame_size;
        const uint32_t last_result = service->last_result;
        if (result_size == 0u || result_size > frame_capacity ||
            last_result != REFMEM_REALTIME_TDMA_RESULT_FRAME_READY) {
            const uint32_t seq_end = refmem_realtime_tdma_load(&service->result_guard);
            if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
                return false;
            }
            continue;
        }
        memcpy(frame, service->result_frame, result_size);
        *frame_size = result_size;
        const uint32_t seq_end = refmem_realtime_tdma_load(&service->result_guard);
        if (seq_begin == seq_end && (seq_end & 1u) == 0u) {
            return true;
        }
    }
}
