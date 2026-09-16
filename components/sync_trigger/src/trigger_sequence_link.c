#include "trigger_sequence_link.h"
#include <string.h>
#include "osal.h"
#include "refmem_application_model.h"
#include "refmem_realtime_contract.h"
#include "tdma_runtime_owner.h"

enum { LINK_OFF, LINK_WAIT_START, LINK_WAIT_APPLIED, LINK_WAIT_READY,
       LINK_WAIT_RETURN, LINK_WAIT_STEP, LINK_PAUSED, LINK_FAULT, LINK_DONE };
enum { LINK_OK, LINK_CONFIG_CHANGED, LINK_TIMEOUT, LINK_PROTOCOL, LINK_OWNER };
static trigger_sequence_link_status_t s_link;
static trigger_sequence_link_tx_t s_tx;
/* Same-board milestone accepts one physical stream; no per-role RX buffers. */
static trigger_sequence_link_rx_t s_rx;
static uint16_t s_token;
static uint32_t s_phase_at, s_ready_baseline;
static bool s_tx_enabled;
static uint32_t s_guard;
static uint32_t s_configuring;
static trigger_sequence_link_status_t s_published;

static bool take(void)
{ return __atomic_exchange_n(&s_guard, 1u, __ATOMIC_ACQUIRE) == 0u; }
static void publish(void)
{
    osal_critical_enter();
    s_published = s_link;
    osal_critical_exit();
}
static void release(void)
{
    publish();
    __atomic_store_n(&s_guard, 0u, __ATOMIC_RELEASE);
}

static bool role_present(uint32_t slot, uint32_t role, uint32_t type)
{
    const refmem_node_load_table_t *loads = refmem_application_model_get_node_load_table();
    const refmem_fb_instance_table_t *instances = refmem_application_model_get_fb_instance_table();
    uint32_t found = 0u;
    for (uint32_t i = 0; i < loads->load_count; ++i) {
        const refmem_node_load_entry_t *l = &loads->load[i];
        if (!l->enabled || l->node_id != slot || !(l->role_mask & role)) continue;
        bool valid = false;
        for (uint32_t j = 0; j < instances->instance_count; ++j) {
            const refmem_fb_instance_entry_t *fb = &instances->instance[j];
            if (fb->instance_id == l->instance_id && fb->fb_type == type && fb->enable_condition)
                valid = true;
        }
        if (!valid || ++found != 1u) return false;
    }
    return found == 1u;
}

static bool binding_model_valid(const trigger_sequence_link_config_t *config, uint32_t model_epoch)
{
    return model_epoch == refmem_realtime_contract_origin_model_epoch() &&
        role_present(config->dut_slot, REFMEM_APP_ROLE_LINK_SWITCHER, REFMEM_APP_FB_LINK_SWITCHER) &&
        role_present(config->vna_slot, REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER,
                     REFMEM_APP_FB_INSTRUMENT_CONTROLLER);
}

static bool model_valid(void)
{ return binding_model_valid(&s_link.config, s_link.model_epoch); }

static bool start_guard(void)
{
    /* Runtime service may be preempted with s_guard held. START only needs
     * the frozen binding, published synchronously before configure returns.
     * USB and RS485 can enter SCPI from different Core0 tasks: reject an
     * in-progress configuration, without claiming a guard-to-enqueue lock. */
    if (__atomic_load_n(&s_configuring, __ATOMIC_ACQUIRE)) return false;
    trigger_sequence_link_config_t config;
    uint32_t model_epoch, binding_epoch;
    osal_critical_enter();
    config = s_published.config;
    model_epoch = s_published.model_epoch;
    binding_epoch = s_published.binding_epoch;
    osal_critical_exit();
    tdma_ring_runtime_snapshot_t ring;
    bool valid = config.enabled && binding_model_valid(&config, model_epoch) &&
        tdma_runtime_owner_get_ring_snapshot(&ring) && ring.enabled &&
        ring.adapter_started && ring.data_enabled && ring.up_running;
    osal_critical_enter();
    valid = valid && binding_epoch == s_published.binding_epoch;
    osal_critical_exit();
    return valid && !__atomic_load_n(&s_configuring, __ATOMIC_ACQUIRE) &&
        model_epoch == refmem_realtime_contract_origin_model_epoch();
}

static bool configure(const trigger_sequence_link_config_t *config)
{
    if (!config || trigger_sequence_service_is_active() ||
        s_link.binding_epoch == UINT32_MAX) return false;
    if (config->enabled &&
        (config->dut_slot >= REFMEM_APP_MODEL_NODE_COUNT ||
         config->vna_slot >= REFMEM_APP_MODEL_NODE_COUNT || config->dut_slot == config->vna_slot ||
         config->timeout_ms == 0u || config->timeout_ms > INT32_MAX ||
         !role_present(config->dut_slot, REFMEM_APP_ROLE_LINK_SWITCHER, REFMEM_APP_FB_LINK_SWITCHER) ||
         !role_present(config->vna_slot, REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER,
                       REFMEM_APP_FB_INSTRUMENT_CONTROLLER))) return false;
    trigger_sequence_gateway_config_t gateway = {
        .enabled = config->enabled, .ready_input = config->ready_input,
        .falling = config->falling, .trigger_output_mask = config->trigger_output_mask,
        .pulse_us = config->pulse_us
    };
    /* Validate before changing the transport's stopped configuration. */
    if (config->enabled && (config->ready_input < 1u || config->ready_input > 4u ||
        config->trigger_output_mask == 0u || config->trigger_output_mask > 15u ||
        (config->trigger_output_mask & (config->trigger_output_mask - 1u)) != 0u ||
        config->pulse_us == 0u || config->pulse_us > UINT32_MAX / 10u)) return false;
    if (!tdma_runtime_owner_set_local_return_delivery(config->enabled)) return false;
    if (trigger_sequence_service_set_gateway_locked(&gateway, start_guard) !=
        TRIGGER_SEQUENCE_SERVICE_OK) {
        (void)tdma_runtime_owner_set_local_return_delivery(s_link.config.enabled);
        return false;
    }
    uint32_t epoch = s_link.binding_epoch + 1u;
    memset(&s_link, 0, sizeof(s_link));
    s_link.config = *config;
    s_link.binding_epoch = epoch;
    s_link.model_epoch = refmem_realtime_contract_origin_model_epoch();
    s_link.phase = config->enabled ? LINK_WAIT_START : LINK_OFF;
    s_tx_enabled = false;
    memset(&s_rx, 0, sizeof(s_rx));
    return true;
}

static void fail(uint32_t error)
{
    s_link.error = error;
    s_link.phase = LINK_FAULT;
    s_tx_enabled = false;
    (void)trigger_sequence_service_stop();
}

static void publish_message(uint32_t kind)
{
    if (kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED && ++s_link.exchange_id == 0u)
        ++s_link.exchange_id;
    trigger_sequence_link_message_t message = {
        .kind = kind, .run_id = s_link.run_id, .generation = s_link.generation,
        .binding_epoch = s_link.binding_epoch, .step_ordinal = s_link.step,
        .source_slot = kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED ? s_link.config.dut_slot : s_link.config.vna_slot,
        .target_slot = kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED ? s_link.config.vna_slot : s_link.config.dut_slot,
        .exchange_id = s_link.exchange_id
    };
    if (++s_token == 0u) ++s_token; /* Assembly hint only; full identity guards execution. */
    if (!trigger_sequence_link_tx_begin(&s_tx, &message, s_token)) { fail(LINK_PROTOCOL); return; }
    s_tx_enabled = true;
    s_phase_at = osal_tick_ms();
}

static void service(void)
{
    if (!s_link.config.enabled) return;
    trigger_sequence_service_status_t owner;
    trigger_sequence_service_get_status(&owner);
    s_link.triggers = owner.gateway_trigger_count;
    s_link.ready = owner.gateway_ready_count;
    s_link.completed = owner.completed;
    if (owner.state == TRIGGER_SEQUENCE_SERVICE_IDLE || owner.state == TRIGGER_SEQUENCE_SERVICE_STOPPING ||
        owner.state == TRIGGER_SEQUENCE_SERVICE_FAULT) {
        s_tx_enabled = false;
        if (s_link.phase != LINK_FAULT && s_link.phase != LINK_DONE) s_link.phase = LINK_WAIT_START;
        return;
    }
    if (!model_valid()) { fail(LINK_CONFIG_CHANGED); return; }
    if (owner.state == TRIGGER_SEQUENCE_SERVICE_PAUSING || owner.state == TRIGGER_SEQUENCE_SERVICE_PAUSED) {
        s_tx_enabled = false;
        s_link.phase = LINK_PAUSED;
        return;
    }
    if (owner.state == TRIGGER_SEQUENCE_SERVICE_STARTING) return;
    if (owner.run_id != s_link.run_id || s_link.phase == LINK_PAUSED ||
        s_link.phase == LINK_WAIT_START || s_link.phase == LINK_FAULT) {
        if (owner.state != TRIGGER_SEQUENCE_SERVICE_READY || owner.accepted != owner.completed) return;
        s_link.error = LINK_OK;
        s_link.run_id = owner.run_id;
        s_link.generation = owner.generation;
        s_link.repeat_count = owner.repeat_count;
        s_link.step = owner.completed;
        memset(&s_rx, 0, sizeof(s_rx));
        s_link.phase = LINK_WAIT_APPLIED;
        publish_message(TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
    }
    if (s_link.phase == LINK_WAIT_READY && owner.gateway_ready_count > s_ready_baseline &&
        !owner.gateway_pulse_busy && !owner.gateway_waiting) {
        s_link.phase = LINK_WAIT_RETURN;
        publish_message(TRIGGER_SEQUENCE_LINK_READY_NEXT);
    } else if (s_link.phase == LINK_WAIT_STEP && owner.state == TRIGGER_SEQUENCE_SERVICE_READY &&
               owner.completed == s_link.step + 1u && owner.accepted == owner.completed) {
        s_link.step = owner.completed;
        s_link.phase = LINK_WAIT_APPLIED;
        publish_message(TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
    }
    if ((uint32_t)(osal_tick_ms() - s_phase_at) > s_link.config.timeout_ms) fail(LINK_TIMEOUT);
}

static bool tx_fragment(uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE])
{
    if (!s_tx_enabled || !trigger_sequence_service_is_active()) return false;
    if (!trigger_sequence_link_tx_next(&s_tx, fragment)) return false;
    ++s_link.tx_fragments;
    return true;
}

static void rx_fragment(uint32_t physical_source,
    const uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE])
{
    if (!s_link.config.enabled || !s_tx_enabled || physical_source >= REFMEM_APP_MODEL_NODE_COUNT) return;
    tdma_ring_runtime_snapshot_t ring;
    /* First milestone has both roles on this board. Remote delivery requires
     * an explicit active route binding, never an inferred physical slot. */
    if (!tdma_runtime_owner_get_ring_snapshot(&ring) || physical_source != ring.local_slot_id) return;
    trigger_sequence_link_message_t message;
    trigger_sequence_link_rx_result_t result = trigger_sequence_link_rx_feed(&s_rx, fragment, &message);
    if (result == TRIGGER_SEQUENCE_LINK_RX_REJECTED) { ++s_link.rejected; return; }
    if (result != TRIGGER_SEQUENCE_LINK_RX_MESSAGE) return;
    ++s_link.rx_messages;
    if (!model_valid() || message.run_id != s_link.run_id || message.generation != s_link.generation ||
        message.binding_epoch != s_link.binding_epoch || message.step_ordinal != s_link.step ||
        message.exchange_id != s_link.exchange_id) {
        ++s_link.rejected; return;
    }
    trigger_sequence_service_status_t owner;
    trigger_sequence_service_get_status(&owner);
    if (message.kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED && s_link.phase == LINK_WAIT_APPLIED &&
        message.source_slot == s_link.config.dut_slot && message.target_slot == s_link.config.vna_slot) {
        if (trigger_sequence_service_gateway_fire(message.run_id, message.generation, message.step_ordinal) ==
            TRIGGER_SEQUENCE_SERVICE_OK) {
            s_ready_baseline = owner.gateway_ready_count;
            s_link.phase = LINK_WAIT_READY;
            s_phase_at = osal_tick_ms();
        } else fail(LINK_OWNER);
    } else if (message.kind == TRIGGER_SEQUENCE_LINK_READY_NEXT && s_link.phase == LINK_WAIT_RETURN &&
               message.source_slot == s_link.config.vna_slot && message.target_slot == s_link.config.dut_slot) {
        if (s_link.repeat_count != 0u && (uint64_t)s_link.step + 1u >=
            (uint64_t)owner.count * s_link.repeat_count) {
            s_link.phase = LINK_DONE;
            s_tx_enabled = false;
            if (trigger_sequence_service_cycle_finish(message.run_id, message.generation, message.step_ordinal) !=
                TRIGGER_SEQUENCE_SERVICE_OK) fail(LINK_OWNER);
            return;
        }
        if (trigger_sequence_service_cycle_step(message.run_id, message.generation, message.step_ordinal) ==
            TRIGGER_SEQUENCE_SERVICE_OK) {
            s_link.phase = LINK_WAIT_STEP;
            s_phase_at = osal_tick_ms();
        } else fail(LINK_OWNER);
    } else ++s_link.rejected;
}

void trigger_sequence_link_get_status(trigger_sequence_link_status_t *status)
{
    if (!status) return;
    osal_critical_enter();
    *status = s_published;
    osal_critical_exit();
}
bool trigger_sequence_link_configure(const trigger_sequence_link_config_t *config)
{
    if (!trigger_sequence_service_configuration_begin()) return false;
    if (!take()) {
        trigger_sequence_service_configuration_end();
        return false;
    }
    __atomic_store_n(&s_configuring, 1u, __ATOMIC_RELEASE);
    bool result = configure(config);
    publish();
    /* Clear before releasing the sole-writer lock, so a subsequent
     * configure cannot have its in-progress flag cleared by this call. */
    __atomic_store_n(&s_configuring, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_guard, 0u, __ATOMIC_RELEASE);
    trigger_sequence_service_configuration_end();
    return result;
}
trigger_sequence_service_result_t trigger_sequence_link_next(void)
{
    if (!take()) return TRIGGER_SEQUENCE_SERVICE_BUSY;
    trigger_sequence_service_result_t result = TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    if (s_link.config.enabled && s_link.phase == LINK_WAIT_READY && s_link.error == LINK_OK)
        result = trigger_sequence_service_gateway_ready(
            s_link.run_id, s_link.generation, s_link.step);
    release();
    return result;
}
void trigger_sequence_link_service(void)
{ if (take()) { service(); release(); } }
bool trigger_sequence_link_tx_fragment(uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE])
{
    if (!take()) return false;
    bool result = tx_fragment(fragment);
    release();
    return result;
}
void trigger_sequence_link_rx_fragment(uint32_t physical_source,
    const uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE])
{ if (take()) { rx_fragment(physical_source, fragment); release(); } }
