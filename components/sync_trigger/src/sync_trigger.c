#include "sync_trigger.h"

#include <string.h>

#include "board_config.h"
#include "hardware/pio.h"
#include "osal.h"
#include "resource_arbiter.h"
#include "storage_manager.h"
#include "sync_io.h"
#include "trigger_fb.h"

#define SYNC_TRIGGER_QUEUE_LENGTH  16u
#define TRIG_TRACE_DOMAIN_TRIGGER  2u
#define TRIG_TRACE_SEVERITY_INFO   1u
#define TRIG_TRACE_SEVERITY_WARN   2u
#define TRIG_TRACE_SEVERITY_ERROR  3u

typedef enum {
    TRIG_TRACE_EVENT_AO_INIT        = 20u,
    TRIG_TRACE_EVENT_QUEUE_POST     = 21u,
    TRIG_TRACE_EVENT_QUEUE_FULL     = 22u,
    TRIG_TRACE_EVENT_QUEUE_NULL     = 23u,
    TRIG_TRACE_EVENT_EXECUTE        = 30u,
    TRIG_TRACE_EVENT_STATE_CHANGE   = 31u,
    TRIG_TRACE_EVENT_ERROR_CHANGE   = 32u,
    TRIG_TRACE_EVENT_DMA_ROLLOVER   = 33u,
    TRIG_TRACE_EVENT_ENC_Z_PULSE    = 34u,
    TRIG_TRACE_EVENT_EVENT_IGNORED  = 35u,
    TRIG_TRACE_EVENT_SOURCE_CONFIG  = 36u,
    TRIG_TRACE_EVENT_EDGE_CONFIG    = 37u,
    TRIG_TRACE_EVENT_GATE_CONFIG    = 38u,
    TRIG_TRACE_EVENT_SAFE_CONFIG    = 39u,
    TRIG_TRACE_EVENT_RESOURCE_BUSY  = 40u,
    TRIG_TRACE_EVENT_IO_ARM_FAILED  = 41u,
    TRIG_TRACE_EVENT_IO_LOST        = 42u,
    TRIG_TRACE_EVENT_RUNTIME_SAMPLE = 43u,
    TRIG_TRACE_EVENT_RESOURCE_SNAPSHOT = 44u,
} trig_trace_event_id_t;

typedef struct {
    trigger_vector_t vector;
    trig_event_t     queue[SYNC_TRIGGER_QUEUE_LENGTH];
    uint32_t         queue_head;
    uint32_t         queue_tail;
    uint32_t         queue_count;
} sync_trigger_ao_t;

typedef struct {
    trig_state_t state;
    uint32_t     error_code;
    uint32_t     seq_index;
    uint32_t     trigger_count;
    uint64_t     rollover_count;
    uint32_t     enc_count;
    uint32_t     enc_rev_count;
    uint32_t     missed_count;
    uint32_t     trigger_source_pin;
    trig_edge_t  edge;
    bool         gate_enabled;
    trig_safe_state_t safe_state;
} sync_trigger_trace_sample_t;

static sync_trigger_ao_t s_ao;

static uint32_t ao_trace_pack_u16(uint32_t high, uint32_t low)
{
    return ((high & 0xFFFFu) << 16) | (low & 0xFFFFu);
}

static void ao_trace_sample(sync_trigger_trace_sample_t *sample)
{
    if (sample == NULL) {
        return;
    }

    sample->state = s_ao.vector.state;
    sample->error_code = s_ao.vector.error_code;
    sample->seq_index = s_ao.vector.seq_index;
    sample->trigger_count = s_ao.vector.trigger_count;
    sample->rollover_count = s_ao.vector.rollover_count;
    sample->enc_count = s_ao.vector.enc_count;
    sample->enc_rev_count = s_ao.vector.enc_rev_count;
    sample->missed_count = s_ao.vector.missed_count;
    sample->trigger_source_pin = s_ao.vector.trigger_source_pin;
    sample->edge = s_ao.vector.edge;
    sample->gate_enabled = s_ao.vector.gate_enabled;
    sample->safe_state = s_ao.vector.safe_state;
}

static void ao_trace_config_changes(const trig_event_t *event,
                                    const sync_trigger_trace_sample_t *before)
{
    if (event == NULL || before == NULL) {
        return;
    }

    if (event->type == TRIG_EVENT_SET_SOURCE_PIN &&
        before->trigger_source_pin != s_ao.vector.trigger_source_pin) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_SOURCE_CONFIG,
                                    TRIG_TRACE_SEVERITY_INFO,
                                    before->trigger_source_pin,
                                    s_ao.vector.trigger_source_pin);
    }

    if (event->type == TRIG_EVENT_SET_EDGE &&
        before->edge != s_ao.vector.edge) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_EDGE_CONFIG,
                                    TRIG_TRACE_SEVERITY_INFO,
                                    (uint32_t)before->edge,
                                    (uint32_t)s_ao.vector.edge);
    }

    if (event->type == TRIG_EVENT_SET_GATE &&
        before->gate_enabled != s_ao.vector.gate_enabled) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_GATE_CONFIG,
                                    TRIG_TRACE_SEVERITY_INFO,
                                    before->gate_enabled ? 1u : 0u,
                                    s_ao.vector.gate_enabled ? 1u : 0u);
    }

    if (event->type == TRIG_EVENT_SET_SAFE_STATE &&
        before->safe_state != s_ao.vector.safe_state) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_SAFE_CONFIG,
                                    TRIG_TRACE_SEVERITY_INFO,
                                    (uint32_t)before->safe_state,
                                    (uint32_t)s_ao.vector.safe_state);
    }
}

static uint32_t ao_trace_pack_resource_snapshot(uint32_t requested_resources,
                                                resource_arbiter_mode_t mode)
{
    return (requested_resources & 0xFFFFu) |
           (((uint32_t)mode & 0xFFu) << 16);
}

static uint32_t ao_trace_resources_for_state(trig_state_t state)
{
    switch (state) {
    case TRIG_STATE_ENC_CONFIGURED:
    case TRIG_STATE_ENC_ARMED:
        return RESOURCE_ARBITER_RESOURCE_PIO1;
    case TRIG_STATE_BISS_CONFIGURED:
    case TRIG_STATE_BISS_ARMED:
        return RESOURCE_ARBITER_RESOURCE_PIO2 |
               RESOURCE_ARBITER_RESOURCE_AUX;
    case TRIG_STATE_SEQ_CONFIGURED:
    case TRIG_STATE_SEQ_ARMED:
        return RESOURCE_ARBITER_RESOURCE_PIO1 |
               RESOURCE_ARBITER_RESOURCE_DMA;
    default:
        return 0u;
    }
}

static void ao_trace_resource_snapshot(uint32_t requested_resources,
                                       uint8_t severity)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                TRIG_TRACE_EVENT_RESOURCE_SNAPSHOT,
                                severity,
                                ao_trace_pack_resource_snapshot(requested_resources,
                                                                snapshot.mode),
                                snapshot.active_resources);
}

static void ao_trace_error_details(const trig_event_t *event,
                                   const sync_trigger_trace_sample_t *before)
{
    if (event == NULL || before == NULL || before->error_code == s_ao.vector.error_code) {
        return;
    }

    if (event->type == TRIG_EVENT_ARM &&
        s_ao.vector.error_code == TRIG_ERROR_RESOURCE_CONFLICT) {
        const uint32_t requested_resources = ao_trace_resources_for_state(before->state);
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_RESOURCE_BUSY,
                                    TRIG_TRACE_SEVERITY_ERROR,
                                    (uint32_t)before->state,
                                    (uint32_t)s_ao.vector.state);
        ao_trace_resource_snapshot(requested_resources,
                                   TRIG_TRACE_SEVERITY_ERROR);
    }

    if (event->type == TRIG_EVENT_ARM &&
        s_ao.vector.error_code == TRIG_ERROR_IO_ARM_FAILED) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_IO_ARM_FAILED,
                                    TRIG_TRACE_SEVERITY_ERROR,
                                    (uint32_t)before->state,
                                    (uint32_t)s_ao.vector.state);
    }

    if (event->type == TRIG_EVENT_DMA_ROLLOVER &&
        s_ao.vector.error_code == TRIG_ERROR_IO_LOST) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_IO_LOST,
                                    TRIG_TRACE_SEVERITY_ERROR,
                                    (uint32_t)before->state,
                                    (uint32_t)s_ao.vector.state);
    }
}

static void ao_trace_after_execute(const trig_event_t *event,
                                   const sync_trigger_trace_sample_t *before)
{
    if (event == NULL || before == NULL) {
        return;
    }

    const uint32_t event_type = (uint32_t)event->type;
    const bool state_changed = before->state != s_ao.vector.state;
    const bool error_changed = before->error_code != s_ao.vector.error_code;
    const bool rollover_changed = before->rollover_count != s_ao.vector.rollover_count;
    const bool enc_z_changed = before->enc_rev_count != s_ao.vector.enc_rev_count;
    const bool progress_changed = before->seq_index != s_ao.vector.seq_index ||
                                  before->trigger_count != s_ao.vector.trigger_count ||
                                  before->enc_count != s_ao.vector.enc_count;
    const bool config_changed = before->trigger_source_pin != s_ao.vector.trigger_source_pin ||
                                before->edge != s_ao.vector.edge ||
                                before->gate_enabled != s_ao.vector.gate_enabled ||
                                before->safe_state != s_ao.vector.safe_state;
    const uint8_t severity = (s_ao.vector.state == TRIG_STATE_FAULT ||
                              s_ao.vector.error_code != 0u) ?
                                 TRIG_TRACE_SEVERITY_ERROR :
                                 TRIG_TRACE_SEVERITY_INFO;

    if (event->type != TRIG_EVENT_DMA_ROLLOVER &&
        event->type != TRIG_EVENT_RUNTIME_SAMPLE &&
        event->type != TRIG_EVENT_ENC_Z_PULSE) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_EXECUTE,
                                    severity,
                                    event_type,
                                    ao_trace_pack_u16((uint32_t)s_ao.vector.state,
                                                       (uint32_t)before->state));
    }

    if (state_changed) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_STATE_CHANGE,
                                    severity,
                                    event_type,
                                    ao_trace_pack_u16((uint32_t)s_ao.vector.state,
                                                       (uint32_t)before->state));
    }

    if (error_changed) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_ERROR_CHANGE,
                                    s_ao.vector.error_code != 0u ?
                                        TRIG_TRACE_SEVERITY_ERROR :
                                        TRIG_TRACE_SEVERITY_INFO,
                                    event_type,
                                    s_ao.vector.error_code);
    }
    ao_trace_config_changes(event, before);
    ao_trace_error_details(event, before);

    if ((event->type == TRIG_EVENT_ARM ||
         event->type == TRIG_EVENT_RUNTIME_SAMPLE) &&
        (s_ao.vector.state == TRIG_STATE_SEQ_ARMED ||
         s_ao.vector.state == TRIG_STATE_ENC_ARMED ||
         s_ao.vector.state == TRIG_STATE_BISS_ARMED) &&
        (event->type == TRIG_EVENT_ARM ||
         progress_changed || rollover_changed || enc_z_changed || error_changed)) {
        const uint32_t state_edge_gate =
            ((uint32_t)s_ao.vector.state & 0xFFu) |
            (((uint32_t)s_ao.vector.edge & 0xFFu) << 8) |
            (s_ao.vector.gate_enabled ? (1u << 16) : 0u);
        const uint32_t progress =
            ((s_ao.vector.seq_index & 0xFFFFu) << 16) |
            (s_ao.vector.trigger_count & 0xFFFFu);
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_RUNTIME_SAMPLE,
                                    severity,
                                    state_edge_gate,
                                    progress);
    }

    if (event->type == TRIG_EVENT_ARM &&
        s_ao.vector.error_code == TRIG_ERROR_NONE &&
        (s_ao.vector.state == TRIG_STATE_SEQ_ARMED ||
         s_ao.vector.state == TRIG_STATE_ENC_ARMED ||
         s_ao.vector.state == TRIG_STATE_BISS_ARMED)) {
        const uint32_t requested_resources = ao_trace_resources_for_state(s_ao.vector.state);
        ao_trace_resource_snapshot(requested_resources,
                                   TRIG_TRACE_SEVERITY_INFO);
    }

    if ((event->type == TRIG_EVENT_ARM ||
         event->type == TRIG_EVENT_RUNTIME_SAMPLE) &&
        s_ao.vector.state == TRIG_STATE_SEQ_ARMED) {
        sync_io_seq_step_trace_runtime_sample(false);
        sync_io_trace_aux_status_sample(event->type == TRIG_EVENT_ARM);
    }

    if ((event->type == TRIG_EVENT_ARM ||
         event->type == TRIG_EVENT_RUNTIME_SAMPLE) &&
        s_ao.vector.state == TRIG_STATE_ENC_ARMED) {
        sync_io_enc_count_trace_runtime_sample(false);
        sync_io_trace_aux_status_sample(event->type == TRIG_EVENT_ARM);
    }

    if ((event->type == TRIG_EVENT_ARM ||
         event->type == TRIG_EVENT_RUNTIME_SAMPLE) &&
        s_ao.vector.state == TRIG_STATE_BISS_ARMED) {
        sync_io_trace_aux_status_sample(event->type == TRIG_EVENT_ARM);
    }

    if (event->type == TRIG_EVENT_DMA_ROLLOVER && rollover_changed) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_DMA_ROLLOVER,
                                    severity,
                                    (uint32_t)s_ao.vector.rollover_count,
                                    s_ao.vector.seq_index);
    }

    if (event->type == TRIG_EVENT_ENC_Z_PULSE && enc_z_changed) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_ENC_Z_PULSE,
                                    TRIG_TRACE_SEVERITY_INFO,
                                    s_ao.vector.enc_rev_count,
                                    s_ao.vector.enc_count);
    }

    if (!state_changed &&
        !error_changed &&
        !rollover_changed &&
        !enc_z_changed &&
        !progress_changed &&
        !config_changed &&
        event->type != TRIG_EVENT_DMA_ROLLOVER &&
        event->type != TRIG_EVENT_RUNTIME_SAMPLE &&
        event->type != TRIG_EVENT_ENC_Z_PULSE) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_EVENT_IGNORED,
                                    TRIG_TRACE_SEVERITY_WARN,
                                    event_type,
                                    ao_trace_pack_u16((uint32_t)s_ao.vector.state,
                                                       before->missed_count));
    }
}

static void ao_execute_traced(const trig_event_t *event)
{
    sync_trigger_trace_sample_t before;
    ao_trace_sample(&before);
    trigger_fb_execute(&s_ao.vector, event);
    ao_trace_after_execute(event, &before);
}

/* ── 事件队列 ── */

static bool ao_enqueue(const trig_event_t *event)
{
    bool posted = false;
    uint32_t queue_count = 0u;

    osal_critical_enter();
    if (s_ao.queue_count >= SYNC_TRIGGER_QUEUE_LENGTH) {
        s_ao.vector.missed_count++;
        queue_count = s_ao.queue_count;
        osal_critical_exit();
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_QUEUE_FULL,
                                    TRIG_TRACE_SEVERITY_WARN,
                                    event != NULL ? (uint32_t)event->type : 0xFFFFFFFFu,
                                    queue_count);
        return false;
    }

    s_ao.queue[s_ao.queue_tail] = *event;
    s_ao.queue_tail = (s_ao.queue_tail + 1u) % SYNC_TRIGGER_QUEUE_LENGTH;
    s_ao.queue_count++;
    queue_count = s_ao.queue_count;
    posted = true;
    osal_critical_exit();

    storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                TRIG_TRACE_EVENT_QUEUE_POST,
                                TRIG_TRACE_SEVERITY_INFO,
                                (uint32_t)event->type,
                                queue_count);
    return posted;
}

static bool ao_dequeue(trig_event_t *event)
{
    bool received = false;

    osal_critical_enter();
    if (s_ao.queue_count == 0u) {
        goto exit;
    }

    *event = s_ao.queue[s_ao.queue_head];
    s_ao.queue_head = (s_ao.queue_head + 1u) % SYNC_TRIGGER_QUEUE_LENGTH;
    s_ao.queue_count--;
    received = true;

exit:
    osal_critical_exit();
    return received;
}

/* ── sync_io 状态同步 ── */

static void ao_refresh_from_io(void)
{
    sync_io_status_t status;
    sync_io_get_status(&status);

    osal_critical_enter();
    s_ao.vector.io_initialized = status.initialized;
    s_ao.vector.capture_running = status.capture_running;
    s_ao.vector.sync_clock_running = status.sync_clock_running;
    s_ao.vector.dropped_capture_words = status.dropped_capture_words;
    osal_critical_exit();

    resource_arbiter_publish_trigger_activity(status.capture_running,
                                              status.sync_clock_running);
}

/* ── 公共接口 ── */

bool sync_trigger_init(void)
{
    memset(&s_ao, 0, sizeof(s_ao));
    trigger_fb_init(&s_ao.vector);
    ao_refresh_from_io();
    storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                TRIG_TRACE_EVENT_AO_INIT,
                                TRIG_TRACE_SEVERITY_INFO,
                                (uint32_t)s_ao.vector.state,
                                0u);
    return true;
}

bool sync_trigger_post_event(const sync_trigger_event_t *event)
{
    if (event == NULL) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_QUEUE_NULL,
                                    TRIG_TRACE_SEVERITY_WARN,
                                    0u,
                                    s_ao.queue_count);
        return false;
    }

    trig_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = (trig_event_type_t)event->type;
    e.payload.value = event->value;

    return ao_enqueue(&e);
}

bool sync_trigger_post(const trig_event_t *event)
{
    if (event == NULL) {
        storage_manager_trace_event(TRIG_TRACE_DOMAIN_TRIGGER,
                                    TRIG_TRACE_EVENT_QUEUE_NULL,
                                    TRIG_TRACE_SEVERITY_WARN,
                                    0u,
                                    s_ao.queue_count);
        return false;
    }

    return ao_enqueue(event);
}

void sync_trigger_service(void)
{
    trig_event_t event;

    if (!ao_dequeue(&event)) {
#if PROJECT_USE_MULTICORE
        if (s_ao.vector.state != TRIG_STATE_SEQ_ARMED &&
            s_ao.vector.state != TRIG_STATE_ENC_ARMED &&
            s_ao.vector.state != TRIG_STATE_BISS_ARMED) {
            return;
        }
#endif
        /* 无事件时仍同步 ARM 态 PIO 状态 */
        if (s_ao.vector.state == TRIG_STATE_SEQ_ARMED ||
            s_ao.vector.state == TRIG_STATE_ENC_ARMED ||
            s_ao.vector.state == TRIG_STATE_BISS_ARMED) {
            trig_event_t svc_event;
            memset(&svc_event, 0, sizeof(svc_event));
            svc_event.type = TRIG_EVENT_RUNTIME_SAMPLE;
            ao_execute_traced(&svc_event);
        }

        /* ENC_ARMED: 检查 PIO IRQ0 (Z 脉冲=圈数+1) */
        if (s_ao.vector.state == TRIG_STATE_ENC_ARMED &&
            s_ao.vector.enc_z_enabled) {
            if (pio_interrupt_get(BOARD_SYNC_PIO_WAVE, 0)) {
                pio_interrupt_clear(BOARD_SYNC_PIO_WAVE, 0);
                trig_event_t z_event;
                memset(&z_event, 0, sizeof(z_event));
                z_event.type = TRIG_EVENT_ENC_Z_PULSE;
                ao_execute_traced(&z_event);
            }
        }
        ao_refresh_from_io();
        return;
    }

    ao_execute_traced(&event);
    ao_refresh_from_io();
}

void sync_trigger_get_summary(sync_trigger_summary_t *summary)
{
    if (summary == NULL) {
        return;
    }

    osal_critical_enter();
    *summary = s_ao.vector;
    osal_critical_exit();
}

void sync_trigger_get_vector(trigger_vector_t *vector)
{
    if (vector == NULL) {
        return;
    }

    osal_critical_enter();
    *vector = s_ao.vector;
    osal_critical_exit();
}
