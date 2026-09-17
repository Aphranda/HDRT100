#include "trigger_sequence_link.h"
#include "refmem_application_model.h"
#include "refmem_realtime_contract.h"
#include "tdma_runtime_owner.h"
#include "sync_io_sequence.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static trigger_sequence_service_status_t owner;
static refmem_node_load_table_t loads;
static refmem_fb_instance_table_t instances;
static tdma_ring_runtime_snapshot_t ring;
static uint32_t tick, model_epoch = 7u, fire_count, ready_count, step_count, stop_count;
static bool transport_enabled, transport_accept = true, gateway_accept = true;
static bool probe_service_guard, probe_config_guard, snapshot_accept = true;
static bool probe_next_guard;
static bool reconfigure_in_snapshot, model_change_in_snapshot, restart_in_snapshot;
static uint32_t stop_on_owner_read;
static bool configuration_gate, stop_pending, action_busy;
static uint32_t service_guard_calls, config_guard_calls;
static bool (*guard)(void);
static const trigger_sequence_link_config_t config = {
    .enabled = true, .dut_slot = 2u, .vna_slot = 3u, .ready_input = 1u,
    .trigger_output_mask = 8u, .pulse_us = 10u, .timeout_ms = 100u,
};

void osal_critical_enter(void) {}
void osal_critical_exit(void) {}
uint32_t osal_tick_ms(void) { return tick; }
uint32_t refmem_realtime_contract_origin_model_epoch(void) { return model_epoch; }
const refmem_node_load_table_t *refmem_application_model_get_node_load_table(void) { return &loads; }
const refmem_fb_instance_table_t *refmem_application_model_get_fb_instance_table(void) { return &instances; }
bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *out)
{
    *out = ring;
    if (restart_in_snapshot) {
        restart_in_snapshot = false;
        owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE;
        trigger_sequence_link_service();
        ++owner.run_id;
        owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
        trigger_sequence_link_service();
    }
    if (reconfigure_in_snapshot) {
        reconfigure_in_snapshot = false;
        const tdma_ring_runtime_snapshot_t saved = ring;
        memset(&ring, 0, sizeof(ring));
        trigger_sequence_link_config_t next = config;
        next.ready_input = 2u;
        assert(trigger_sequence_link_configure(&next));
        ring = saved;
    }
    if (model_change_in_snapshot) { model_change_in_snapshot = false; ++model_epoch; }
    return snapshot_accept;
}
bool tdma_runtime_owner_set_local_return_delivery(bool enabled)
{
    if (!transport_accept || ring.enabled || ring.adapter_started) return false;
    transport_enabled = enabled;
    return true;
}
bool trigger_sequence_service_stop_pending(void) { return stop_pending; }
bool trigger_sequence_service_is_active(void) { return owner.state != TRIGGER_SEQUENCE_SERVICE_IDLE; }
bool trigger_sequence_service_configuration_begin(void)
{
    if (configuration_gate || trigger_sequence_service_is_active()) return false;
    configuration_gate = true;
    return true;
}
void trigger_sequence_service_configuration_end(void)
{ assert(configuration_gate); configuration_gate = false; }
void trigger_sequence_service_get_status(trigger_sequence_service_status_t *out)
{
    if (probe_service_guard) {
        /* Real service() has already taken its writer lock. A preempting
         * START reader must still authorize the unchanged frozen binding. */
        ++service_guard_calls;
        assert(guard != NULL && guard());
    }
    *out = owner;
    if (stop_on_owner_read && --stop_on_owner_read == 0u) stop_pending = true;
    if (probe_next_guard) {
        probe_next_guard = false;
        assert(trigger_sequence_link_next() == TRIGGER_SEQUENCE_SERVICE_OK);
    }
}
trigger_sequence_service_result_t trigger_sequence_service_set_gateway_locked(
    const trigger_sequence_gateway_config_t *gateway, bool (*start_guard)(void))
{
    assert(configuration_gate);
    (void)gateway;
    if (!gateway_accept) return TRIGGER_SEQUENCE_SERVICE_IO_CONFIG;
    if (probe_config_guard) {
        /* Configuration is not yet published. Even a ready TDMA snapshot
         * cannot authorize START using the preceding published binding. */
        const tdma_ring_runtime_snapshot_t saved = ring;
        ring.enabled = ring.adapter_started = ring.data_enabled = ring.up_running = 1u;
        ++config_guard_calls;
        assert(guard != NULL && !guard());
        ring = saved;
    }
    guard = start_guard;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}
trigger_sequence_service_result_t trigger_sequence_service_stop(void)
{ ++stop_count; owner.state = TRIGGER_SEQUENCE_SERVICE_STOPPING; return TRIGGER_SEQUENCE_SERVICE_OK; }
static bool valid_action(uint32_t run, uint32_t generation, uint32_t step)
{
    return owner.state == TRIGGER_SEQUENCE_SERVICE_READY && owner.run_id == run &&
           owner.generation == generation && owner.completed == step &&
           owner.accepted == owner.completed;
}
trigger_sequence_service_result_t trigger_sequence_service_gateway_fire(uint32_t run, uint32_t generation, uint32_t step)
{
    if (action_busy || stop_pending) return TRIGGER_SEQUENCE_SERVICE_BUSY;
    if (!valid_action(run, generation, step)) return TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    ++fire_count; ++owner.gateway_trigger_count;
    owner.gateway_waiting = owner.gateway_pulse_busy = true;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}
trigger_sequence_service_result_t trigger_sequence_service_gateway_ready(
    uint32_t run, uint32_t generation, uint32_t step)
{
    if (!valid_action(run, generation, step) || !owner.gateway_waiting)
        return TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    ++ready_count;
    ++owner.gateway_ready_count;
    owner.gateway_waiting = false;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}
trigger_sequence_service_result_t trigger_sequence_service_cycle_step(uint32_t run, uint32_t generation, uint32_t step)
{
    if (!valid_action(run, generation, step) || owner.gateway_waiting || owner.gateway_pulse_busy)
        return TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    ++step_count; ++owner.accepted; owner.state = TRIGGER_SEQUENCE_SERVICE_RUNNING;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}
trigger_sequence_service_result_t trigger_sequence_service_cycle_finish(uint32_t run, uint32_t generation, uint32_t step)
{
    if (!valid_action(run, generation, step) || owner.gateway_waiting || owner.gateway_pulse_busy)
        return TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    owner.finished = true;
    return trigger_sequence_service_stop();
}
trigger_sequence_service_result_t trigger_sequence_service_counter_rearm(uint32_t run, uint32_t generation, uint32_t step)
{
    if (!valid_action(run, generation, step) || owner.gateway_waiting || owner.gateway_pulse_busy)
        return TRIGGER_SEQUENCE_SERVICE_NOT_READY;
    ++owner.counter_rearm_count;
    owner.counter_busy = false;
    return TRIGGER_SEQUENCE_SERVICE_OK;
}

static void fixture(void)
{
    loads.load_count = 2u;
    loads.load[0] = (refmem_node_load_entry_t){.node_id = 2u, .instance_id = 5u,
        .role_mask = REFMEM_APP_ROLE_LINK_SWITCHER, .enabled = 1u};
    loads.load[1] = (refmem_node_load_entry_t){.node_id = 3u, .instance_id = 7u,
        .role_mask = REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER, .enabled = 1u};
    instances.instance_count = 2u;
    instances.instance[0] = (refmem_fb_instance_entry_t){.instance_id = 5u,
        .fb_type = REFMEM_APP_FB_LINK_SWITCHER, .enable_condition = 1u};
    instances.instance[1] = (refmem_fb_instance_entry_t){.instance_id = 7u,
        .fb_type = REFMEM_APP_FB_INSTRUMENT_CONTROLLER, .enable_condition = 1u};
}
static void start(void)
{
    assert(trigger_sequence_link_configure(&config));
    assert(transport_enabled && guard != NULL && !guard());
    ring.enabled = ring.adapter_started = ring.data_enabled = ring.up_running = 1u;
    ring.local_slot_id = 0u;
    assert(guard());
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    owner.run_id = 11u; owner.generation = 19u;
    owner.count = 8u;
    owner.accepted = owner.completed = 0u; /* START applies first state, no NEXT yet. */
    trigger_sequence_link_service();
}

static void start_published_view(void)
{
    assert(trigger_sequence_link_configure(&config));
    ring.enabled = ring.adapter_started = ring.data_enabled = ring.up_running = 1u;
    assert(guard());
    probe_service_guard = true;
    trigger_sequence_link_service();
    probe_service_guard = false;
    assert(service_guard_calls == 1u && guard());
    /* All preexisting readiness and model/role gates remain mandatory. */
    snapshot_accept = false; assert(!guard()); snapshot_accept = true;
    ring.enabled = 0u; assert(!guard()); ring.enabled = 1u;
    ring.adapter_started = 0u; assert(!guard()); ring.adapter_started = 1u;
    ring.data_enabled = 0u; assert(!guard()); ring.data_enabled = 1u;
    ring.up_running = 0u; assert(!guard()); ring.up_running = 1u;
    instances.instance[0].enable_condition = 0u; assert(!guard());
    instances.instance[0].enable_condition = 1u;
    loads.load[1].enabled = 0u; assert(!guard()); loads.load[1].enabled = 1u;
    ++model_epoch; assert(!guard()); --model_epoch;
    assert(guard());
}

static void start_configuration_publication(void)
{
    assert(trigger_sequence_link_configure(&config));
    trigger_sequence_link_status_t first, after;
    trigger_sequence_link_get_status(&first);
    trigger_sequence_link_config_t next = config;
    next.ready_input = 2u;
    probe_config_guard = true;
    assert(trigger_sequence_link_configure(&next));
    probe_config_guard = false;
    trigger_sequence_link_get_status(&after);
    assert(config_guard_calls == 1u && after.binding_epoch == first.binding_epoch + 1u);
    assert(after.config.ready_input == 2u && after.model_epoch == model_epoch);
    ring.enabled = ring.adapter_started = ring.data_enabled = ring.up_running = 1u;
    assert(guard());
    reconfigure_in_snapshot = true;
    assert(!guard()); /* Binding replaced after the reader copied its view. */
    assert(guard());
    model_change_in_snapshot = true;
    assert(!guard()); /* Model changed during ring readback. */
}
typedef uint8_t fragments_t[TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT][TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE];
static void outgoing(fragments_t f)
{
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT; ++i)
        assert(trigger_sequence_link_tx_fragment(f[i]));
}
static void receive(fragments_t f)
{
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT; ++i)
        trigger_sequence_link_rx_fragment(0u, f[i]);
    (void)trigger_sequence_link_service();
}
static trigger_sequence_link_message_t decode(fragments_t f)
{
    trigger_sequence_link_rx_t rx; trigger_sequence_link_rx_init(&rx);
    trigger_sequence_link_message_t message = {0};
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT; ++i)
        assert(trigger_sequence_link_rx_feed(&rx, f[i], &message) ==
            (i + 1u == TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT ? TRIGGER_SEQUENCE_LINK_RX_MESSAGE : TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE));
    return message;
}
static void inject(trigger_sequence_link_message_t message, uint16_t token)
{
    trigger_sequence_link_tx_t tx = {0};
    assert(trigger_sequence_link_tx_begin(&tx, &message, token));
    uint8_t f[10];
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT; ++i) {
        assert(trigger_sequence_link_tx_next(&tx, f));
        trigger_sequence_link_rx_fragment(0u, f);
    }
    (void)trigger_sequence_link_service();
}
static void workflow(void)
{
    start();
    fragments_t link, ready, next;
    outgoing(link);
    trigger_sequence_link_message_t message = decode(link);
    assert(message.kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED && message.step_ordinal == 0u);
    assert(message.exchange_id != 0u);
    assert(message.source_slot == 2u && message.target_slot == 3u);
    for (uint32_t i = 0u; i < 20u; ++i) trigger_sequence_link_service();
    assert(fire_count == 0u && step_count == 0u); /* Publishing is not physical RX. */
    for (uint32_t i = 0u; i < 6u; ++i) trigger_sequence_link_rx_fragment(1u, link[i]);
    assert(fire_count == 0u); /* Same-board milestone rejects remote physical source. */
    for (uint32_t i = 0u; i < 5u; ++i) trigger_sequence_link_rx_fragment(0u, link[i]);
    assert(fire_count == 0u);
    trigger_sequence_link_rx_fragment(0u, link[5]);
    assert(fire_count == 0u); /* Transport cannot run the IO FSM. */
    assert(trigger_sequence_link_service());
    assert(fire_count == 1u);
    receive(link); assert(fire_count == 1u);
    ++owner.gateway_ready_count;
    trigger_sequence_link_service();
    outgoing(ready); assert(decode(ready).kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
    owner.gateway_pulse_busy = false;
    trigger_sequence_link_service();
    outgoing(ready); assert(decode(ready).kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
    owner.gateway_waiting = false;
    trigger_sequence_link_service(); outgoing(ready);
    assert(decode(ready).kind == TRIGGER_SEQUENCE_LINK_READY_NEXT);
    assert(step_count == 0u);
    receive(ready); assert(step_count == 1u);
    receive(ready); assert(step_count == 1u);
    trigger_sequence_link_service(); assert(fire_count == 1u);
    ++owner.completed; owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    trigger_sequence_link_service(); outgoing(next);
    assert(decode(next).step_ordinal == 1u && decode(next).kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
    receive(link); assert(fire_count == 1u && step_count == 1u);
    receive(next); assert(fire_count == 2u && step_count == 1u);
}
static void software_next(void)
{
    assert(trigger_sequence_link_next() == TRIGGER_SEQUENCE_SERVICE_NOT_READY);
    trigger_sequence_link_config_t manual = config;
    manual.ready_input = 0u;
    assert(trigger_sequence_link_configure(&manual));
    ring.enabled = ring.adapter_started = ring.data_enabled = ring.up_running = 1u;
    ring.local_slot_id = 0u;
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    owner.run_id = 11u; owner.generation = 19u; owner.count = 8u;
    owner.accepted = owner.completed = 0u;
    trigger_sequence_link_service();
    fragments_t link, ready;
    outgoing(link);
    assert(trigger_sequence_link_next() == TRIGGER_SEQUENCE_SERVICE_NOT_READY);
    receive(link);
    assert(fire_count == 1u && owner.gateway_waiting);
    tick = manual.timeout_ms + 1u;
    trigger_sequence_link_service();
    trigger_sequence_link_status_t status;
    trigger_sequence_link_get_status(&status);
    assert(status.phase == 3u && status.error == 0u && stop_count == 0u);
    assert(trigger_sequence_link_next() == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(ready_count == 1u && owner.gateway_ready_count == 1u);
    assert(trigger_sequence_link_next() == TRIGGER_SEQUENCE_SERVICE_NOT_READY);
    owner.gateway_pulse_busy = false;
    trigger_sequence_link_service();
    outgoing(ready);
    assert(decode(ready).kind == TRIGGER_SEQUENCE_LINK_READY_NEXT);
    receive(ready);
    assert(step_count == 1u);
}
static void external_next_rejected(void)
{
    start();
    fragments_t link;
    outgoing(link);
    receive(link);
    assert(fire_count == 1u && owner.gateway_waiting);
    assert(trigger_sequence_link_next() == TRIGGER_SEQUENCE_SERVICE_NOT_READY);
    assert(ready_count == 0u && owner.gateway_ready_count == 0u);
}
static void stale_messages(void)
{
    start(); fragments_t f; outgoing(f);
    const trigger_sequence_link_message_t correct = decode(f);
    for (uint32_t field = 0u; field < 7u; ++field) {
        trigger_sequence_link_message_t bad = correct;
        if (field == 0u) --bad.run_id;
        if (field == 1u) ++bad.generation;
        if (field == 2u) ++bad.binding_epoch;
        if (field == 3u) ++bad.step_ordinal;
        if (field == 4u) { bad.source_slot = 3u; bad.target_slot = 2u; }
        if (field == 5u) bad.kind = TRIGGER_SEQUENCE_LINK_READY_NEXT;
        if (field == 6u) ++bad.exchange_id;
        inject(bad, (uint16_t)(100u + field));
        assert(fire_count == 0u && step_count == 0u);
    }
    receive(f); assert(fire_count == 1u);
}
static void stopped(void)
{
    start(); fragments_t f; outgoing(f);
    owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE;
    trigger_sequence_link_service();
    receive(f); uint8_t fragment[10];
    assert(!trigger_sequence_link_tx_fragment(fragment));
    assert(fire_count == 0u && step_count == 0u);
    owner.run_id++; owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    trigger_sequence_link_service();
    receive(f); assert(fire_count == 0u);
    outgoing(f); receive(f); assert(fire_count == 1u);
}
static void paused(void)
{
    start(); fragments_t f, old; outgoing(f); memcpy(old, f, sizeof(old)); receive(f);
    const uint32_t old_exchange = decode(old).exchange_id;
    owner.state = TRIGGER_SEQUENCE_SERVICE_PAUSED;
    trigger_sequence_link_service(); tick += 1000u;
    trigger_sequence_link_service(); receive(f);
    assert(fire_count == 1u && stop_count == 0u);
    uint8_t fragment[10]; assert(!trigger_sequence_link_tx_fragment(fragment));
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    owner.gateway_waiting = owner.gateway_pulse_busy = false;
    trigger_sequence_link_service(); outgoing(f);
    assert(decode(f).step_ordinal == 0u);
    assert(decode(f).exchange_id != old_exchange);
    assert(fire_count == 1u);
    receive(old); assert(fire_count == 1u);
    receive(f); assert(fire_count == 2u && step_count == 0u);
}
static void timeout(void)
{
    start(); fragments_t f; outgoing(f);
    tick = 101u; trigger_sequence_link_service();
    trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
    assert(status.error != 0u && stop_count == 1u);
    receive(f); assert(fire_count == 0u && step_count == 0u);
    uint8_t fragment[10]; assert(!trigger_sequence_link_tx_fragment(fragment));
    owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE;
    trigger_sequence_link_service();
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    ++owner.run_id;
    owner.accepted = owner.completed = 0u;
    tick = 102u;
    trigger_sequence_link_service();
    fragments_t restarted; outgoing(restarted);
    assert(decode(restarted).exchange_id != decode(f).exchange_id);
    receive(f); assert(fire_count == 0u && step_count == 0u);
    receive(restarted); assert(fire_count == 1u && step_count == 0u);
}
static void model_changed(void)
{
    start(); ++model_epoch; trigger_sequence_link_service();
    trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
    assert(status.error != 0u && stop_count == 1u);
}
static void configure_rejections(void)
{
    trigger_sequence_link_config_t bad = config;
    instances.instance[0].enable_condition = 0u;
    assert(!trigger_sequence_link_configure(&config) && !transport_enabled);
    instances.instance[0].enable_condition = 1u;
    bad.trigger_output_mask = 3u;
    assert(!trigger_sequence_link_configure(&bad) && !transport_enabled);
    transport_accept = false;
    assert(!trigger_sequence_link_configure(&config) && !transport_enabled);
    transport_accept = true;
    ring.enabled = 1u;
    assert(!trigger_sequence_link_configure(&config) && !transport_enabled);
    ring.enabled = 0u;
    owner.state = TRIGGER_SEQUENCE_SERVICE_PAUSED;
    assert(!trigger_sequence_link_configure(&config) && !transport_enabled);
}
static void configure_rollback(void)
{
    gateway_accept = false;
    assert(!trigger_sequence_link_configure(&config));
    assert(!transport_enabled); /* A rejected config must not partially mutate transport. */
}
static void first_settle(void)
{
    assert(trigger_sequence_link_configure(&config));
    ring.enabled = ring.adapter_started = ring.data_enabled = ring.up_running = 1u;
    ring.local_slot_id = 0u;
    owner.state = TRIGGER_SEQUENCE_SERVICE_RUNNING;
    owner.run_id = 11u; owner.generation = 19u;
    owner.accepted = 1u; owner.completed = 0u;
    trigger_sequence_link_service();
    uint8_t fragment[10];
    assert(!trigger_sequence_link_tx_fragment(fragment));
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY; owner.completed = 1u;
    trigger_sequence_link_service();
    fragments_t f; outgoing(f);
    assert(decode(f).step_ordinal == 1u);
    assert(fire_count == 0u);
    receive(f); assert(fire_count == 1u);
}
static void repetitions(uint32_t repeat_count)
{
    owner.repeat_count = repeat_count;
    start();
    const uint32_t measurements = repeat_count != 0u ? repeat_count * 8u : 24u;
    fragments_t link, ready;
    for (uint32_t i = 0u; i < measurements; ++i) {
        outgoing(link);
        assert(decode(link).kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
        assert(decode(link).step_ordinal == i);
        assert(fire_count == i);
        receive(link); receive(link);
        assert(fire_count == i + 1u);
        owner.gateway_pulse_busy = owner.gateway_waiting = false;
        ++owner.gateway_ready_count;
        trigger_sequence_link_service(); outgoing(ready);
        assert(decode(ready).kind == TRIGGER_SEQUENCE_LINK_READY_NEXT);
        assert(decode(ready).step_ordinal == i);
        assert(step_count == i);
        receive(ready); receive(ready);
        const bool final = repeat_count != 0u && i + 1u == measurements;
        if (final) {
            assert(step_count == measurements - 1u && stop_count == 1u && owner.finished);
            uint8_t fragment[10];
            assert(!trigger_sequence_link_tx_fragment(fragment));
            owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE;
            tick += 1000u;
            trigger_sequence_link_service(); receive(link); receive(ready);
            trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
            assert(status.phase == 8u && status.error == 0u);
            assert(fire_count == measurements && step_count == measurements - 1u && stop_count == 1u);
        } else {
            assert(step_count == i + 1u && stop_count == 0u);
            ++owner.completed; owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
            trigger_sequence_link_service();
        }
    }
    if (repeat_count == 0u) {
        outgoing(link); assert(decode(link).step_ordinal == measurements);
        assert(step_count == measurements && stop_count == 0u);
    }
}

static void counter_start_threshold(uint32_t repeat, uint32_t count, uint32_t threshold)
{
    loads.load[2] = (refmem_node_load_entry_t){.node_id = 4u, .instance_id = 2u,
        .role_mask = REFMEM_APP_ROLE_PULSE_DISTRIBUTOR, .enabled = 1u};
    loads.load_count = 3u;
    instances.instance[2] = (refmem_fb_instance_entry_t){.instance_id = 2u,
        .fb_type = REFMEM_APP_FB_PULSE_COUNTER, .enable_condition = 1u};
    instances.instance_count = 3u;
    trigger_sequence_link_config_t third = config;
    third.counter_enabled = true; third.counter_slot = 4u;
    third.counter_input = 2u; third.counter_threshold = threshold;
    assert(trigger_sequence_link_configure(&third));
    ring.enabled = ring.adapter_started = ring.data_enabled = ring.up_running = 1u;
    assert(guard());
    owner.run_id = 11u; owner.generation = 19u; owner.count = count;
    owner.repeat_count = repeat; owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    trigger_sequence_link_service();
    uint8_t fragment[10]; assert(!trigger_sequence_link_tx_fragment(fragment));
    tick = third.timeout_ms * 3u;
    trigger_sequence_link_service();
    trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
    assert(status.phase == 9u && status.error == 0u && fire_count == 0u);
    assert(trigger_sequence_link_next() == TRIGGER_SEQUENCE_SERVICE_NOT_READY);
}
static void counter_start(uint32_t repeat, uint32_t count)
{ counter_start_threshold(repeat, count, 10u); }
static void counter_request(void)
{
    fragments_t f;
    trigger_sequence_link_service(); outgoing(f);
    trigger_sequence_link_message_t message = decode(f);
    assert(message.kind == TRIGGER_SEQUENCE_LINK_COUNTER_NEXT);
    assert(message.source_slot == 4u && message.target_slot == 2u);
    const uint32_t before = step_count;
    receive(f); receive(f);
    if (before != step_count) {
        ++owner.completed; owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
        trigger_sequence_link_service();
    }
}
static void counter_sample(void)
{
    fragments_t f; outgoing(f);
    assert(decode(f).kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED);
    receive(f); receive(f);
    owner.gateway_waiting = owner.gateway_pulse_busy = false;
    ++owner.gateway_ready_count;
    trigger_sequence_link_service(); outgoing(f);
    assert(decode(f).kind == TRIGGER_SEQUENCE_LINK_READY_NEXT);
    receive(f); receive(f);
    if (owner.state == TRIGGER_SEQUENCE_SERVICE_RUNNING) {
        ++owner.completed; owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    }
    trigger_sequence_link_service();
}
static void counter_rounds(uint32_t repeat, uint32_t count)
{
    counter_start(repeat, count);
    const uint32_t rounds = repeat ? repeat : 10u;
    for (uint32_t position = 1u; position <= rounds; ++position) {
        owner.counter_events = position * 10u;
        counter_request();
        for (uint32_t i = 0u; i < count; ++i) {
            /* Partial pulses during sampling are retained for next position. */
            owner.counter_events = position * 10u + 3u;
            counter_sample();
            assert(fire_count == (position - 1u) * count + i + 1u);
        }
        trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
        assert(status.counter_consumed == position && status.counter_partial == 3u);
        assert(status.phase == (repeat && position == rounds ? 8u : 9u));
    }
    assert(step_count == rounds * count - 1u);
    assert(stop_count == (repeat ? 1u : 0u));
    trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
    assert(status.history_total == rounds * count);
    const uint32_t retained = status.history_total < TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY ?
        status.history_total : TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY;
    assert(status.history_retained == retained);
    trigger_sequence_link_history_t record;
    for (uint32_t ordinal = 1u; ordinal <= status.history_total; ++ordinal) {
        const bool present = ordinal > status.history_total - retained;
        assert(trigger_sequence_link_get_history(ordinal, &record) == present);
        if (!present) continue;
        assert(record.ordinal == ordinal && record.position == (ordinal - 1u) / count + 1u);
        assert(record.sequence_index == (ordinal - 1u) % count);
        assert(record.threshold_pulses == record.position * 10u);
        assert(record.observed_pulses >= record.threshold_pulses &&
            record.observed_pulses < record.threshold_pulses + 10u);
        assert(record.outcome_flags == 7u);
    }
    assert(!trigger_sequence_link_get_history(0u, &record));
    assert(!trigger_sequence_link_get_history(status.history_total + 1u, &record));
}
static void counter_busy(bool before_return)
{
    counter_start(1u, 8u);
    owner.counter_events = 12u; /* Core0 may first observe threshold plus partial. */
    trigger_sequence_link_service();
    fragments_t f; outgoing(f);
    if (!before_return) { receive(f); outgoing(f); receive(f); }
    owner.counter_events = 19u;
    trigger_sequence_link_service();
    trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
    assert(status.error == 0u && status.counter_partial == 9u);
    owner.counter_events = 20u;
    if (before_return) receive(f); else trigger_sequence_link_service();
    trigger_sequence_link_get_status(&status);
    assert(status.phase == 7u && status.error == 5u && status.counter_fault_events == 20u);
    assert(stop_count == 1u && step_count == 0u);
}
static void counter_pause(void)
{
    counter_start(1u, 8u);
    owner.counter_events = 3u; trigger_sequence_link_service();
    owner.state = TRIGGER_SEQUENCE_SERVICE_PAUSED; trigger_sequence_link_service();
    tick += 1000u; trigger_sequence_link_service();
    owner.counter_events = 5u; trigger_sequence_link_service();
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY; trigger_sequence_link_service();
    trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
    assert(status.phase == 9u && status.counter_partial == 5u && !fire_count);
    owner.counter_events = 10u; counter_request();
    owner.state = TRIGGER_SEQUENCE_SERVICE_PAUSED; trigger_sequence_link_service();
    trigger_sequence_link_get_status(&status);
    assert(status.phase == 7u && status.error == 4u && !fire_count);
}
static void counter_stale(void)
{
    counter_start(1u, 8u);
    owner.counter_events = 10u; trigger_sequence_link_service();
    fragments_t f; outgoing(f);
    trigger_sequence_link_message_t good = decode(f);
    for (uint32_t field = 0u; field < 7u; ++field) {
        trigger_sequence_link_message_t bad = good;
        if (field == 0u) ++bad.run_id;
        if (field == 1u) ++bad.generation;
        if (field == 2u) ++bad.binding_epoch;
        if (field == 3u) ++bad.step_ordinal;
        if (field == 4u) bad.source_slot = 3u;
        if (field == 5u) bad.target_slot = 3u;
        if (field == 6u) ++bad.exchange_id;
        inject(bad, (uint16_t)(200u + field));
        assert(!fire_count && !step_count);
    }
    receive(f); outgoing(f); receive(f);
    assert(fire_count == 1u);
}
static void software_next_contention(void)
{
    trigger_sequence_link_config_t manual = config; manual.ready_input = 0u;
    assert(trigger_sequence_link_configure(&manual));
    ring.enabled = ring.adapter_started = ring.data_enabled = ring.up_running = 1u;
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    owner.run_id = 11u; owner.generation = 19u; owner.count = 8u;
    trigger_sequence_link_service(); fragments_t f; outgoing(f); receive(f);
    probe_next_guard = true;
    trigger_sequence_link_service();
    assert(ready_count == 1u && !probe_next_guard);
}
static void counter_config_rejections(void)
{
    counter_start(1u, 8u);
    owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE;
    ring.enabled = ring.adapter_started = 0u;
    trigger_sequence_link_status_t before, after;
    trigger_sequence_link_get_status(&before);
    for (uint32_t test = 0u; test < 10u; ++test) {
        trigger_sequence_link_config_t bad = before.config;
        instances.instance[2].enable_condition = 1u;
        if (test == 0u) bad.counter_slot = bad.dut_slot;
        if (test == 1u) bad.counter_slot = bad.vna_slot;
        if (test == 2u) bad.counter_slot = REFMEM_APP_MODEL_NODE_COUNT;
        if (test == 3u) bad.counter_input = 0u;
        if (test == 4u) bad.counter_input = bad.ready_input;
        if (test == 5u) bad.counter_threshold = 0u;
        if (test == 6u) bad.enabled = false;
        if (test == 7u) instances.instance[2].enable_condition = 0u;
        if (test == 8u) bad.counter_threshold = SYNC_IO_SEQUENCE_COUNTER_LIMIT;
        if (test == 9u) bad.counter_threshold = UINT32_MAX;
        assert(!trigger_sequence_link_configure(&bad));
        trigger_sequence_link_get_status(&after);
        assert(after.binding_epoch == before.binding_epoch && transport_enabled);
    }
    trigger_sequence_link_config_t maximum = before.config;
    maximum.counter_threshold = SYNC_IO_SEQUENCE_COUNTER_LIMIT - 1u;
    assert(trigger_sequence_link_configure(&maximum));
    trigger_sequence_link_get_status(&after);
    assert(after.config.counter_threshold == SYNC_IO_SEQUENCE_COUNTER_LIMIT - 1u);
}
static void counter_fault_restart(void)
{
    counter_start(1u, 8u);
    owner.counter_events = 10u; counter_request();
    owner.counter_events = 20u; owner.state = TRIGGER_SEQUENCE_SERVICE_FAULT;
    owner.backend_fault = SYNC_IO_SEQUENCE_FAULT_COUNTER_BUSY;
    trigger_sequence_link_service();
    trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
    assert(status.phase == 7u && status.error == 5u && status.counter_fault_events == 20u);
    trigger_sequence_link_history_t record;
    assert(trigger_sequence_link_get_history(1u, &record) && record.outcome_flags == 3u);
    owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE; trigger_sequence_link_service();
    assert(trigger_sequence_link_get_history(1u, &record));
    owner.counter_events = 0u; ++owner.run_id; owner.backend_fault = 0u;
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY; trigger_sequence_link_service();
    trigger_sequence_link_get_status(&status);
    assert(status.phase == 9u && status.error == 0u && status.counter_consumed == 0u);
    assert(!trigger_sequence_link_get_history(1u, &record));
}
static void counter_rearm_boundary(void)
{
    counter_start(2u, 1u);
    owner.counter_events = 10u; counter_request();
    fragments_t f; outgoing(f); receive(f);
    owner.gateway_waiting = owner.gateway_pulse_busy = false;
    ++owner.gateway_ready_count; trigger_sequence_link_service();
    outgoing(f); receive(f); /* rearm accepted; no link service sees ack yet */
    owner.counter_events = 20u; owner.counter_busy = true;
    trigger_sequence_link_service(); outgoing(f);
    assert(decode(f).kind == TRIGGER_SEQUENCE_LINK_COUNTER_NEXT);
    trigger_sequence_link_status_t status; trigger_sequence_link_get_status(&status);
    assert(status.counter_consumed == 2u && status.error == 0u);
    receive(f); ++owner.completed; owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    trigger_sequence_link_service(); counter_sample();
    assert(fire_count == 2u && stop_count == 1u);
}
static void counter_resume_boundary(void)
{
    counter_start_threshold(1u, 1u, 1000u);
    owner.counter_events = 999u;
    owner.state = TRIGGER_SEQUENCE_SERVICE_PAUSED;
    trigger_sequence_link_service();
    trigger_sequence_link_status_t status;
    trigger_sequence_link_get_status(&status);
    assert(status.phase == 6u && status.counter_partial == 999u && !status.error);
    /* CONTINUE and the next edge finish on Core1 before Core0 sees either. */
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    owner.counter_events = 1000u;
    owner.counter_busy = true;
    counter_request();
    trigger_sequence_link_get_status(&status);
    assert(status.counter_consumed == 1u && status.counter_partial == 0u && !status.error);
    assert(!stop_count && !fire_count);
    counter_sample();
    assert(fire_count == 1u && stop_count == 1u && owner.finished);
}
static void counter_pause_after_rearm(void)
{
    counter_start(2u, 1u);
    owner.counter_events = 10u;
    counter_request();
    fragments_t f;
    outgoing(f); receive(f);
    owner.gateway_waiting = owner.gateway_pulse_busy = false;
    ++owner.gateway_ready_count;
    trigger_sequence_link_service();
    outgoing(f); receive(f); /* Core1 rearm ack, Core0 still WAIT_REARM */
    trigger_sequence_link_status_t status;
    trigger_sequence_link_get_status(&status);
    assert(status.phase == 11u && owner.counter_rearm_count == 1u);
    owner.counter_events = 13u;
    owner.state = TRIGGER_SEQUENCE_SERVICE_PAUSING;
    trigger_sequence_link_service();
    trigger_sequence_link_get_status(&status);
    assert(status.phase == 6u && !status.error && status.counter_partial == 3u);
    assert(fire_count == 1u && !stop_count);
    owner.state = TRIGGER_SEQUENCE_SERVICE_PAUSED;
    owner.counter_events = 19u;
    trigger_sequence_link_service();
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    trigger_sequence_link_service();
    owner.counter_events = 20u;
    owner.counter_busy = true;
    counter_request(); counter_sample();
    assert(fire_count == 2u && step_count == 1u && stop_count == 1u);
}
static void counter_stopped_final_edges(bool finite)
{
    counter_start(1u, 1u);
    owner.counter_events = 10u;
    counter_request();
    if (finite) counter_sample();
    else assert(trigger_sequence_service_stop() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_link_status_t before, after;
    trigger_sequence_link_get_status(&before);
    /* STOP's final direct PIO snapshot includes edges not yet serviced by
     * Core0. It must refresh telemetry without accepting another position. */
    owner.counter_events = 13u;
    trigger_sequence_link_service();
    trigger_sequence_link_get_status(&after);
    assert(after.counter_events == 13u && after.counter_partial == 3u);
    owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE;
    owner.counter_events = 17u;
    trigger_sequence_link_service();
    trigger_sequence_link_get_status(&after);
    assert(after.counter_events == 17u && after.counter_partial == 7u);
    assert(after.counter_consumed == 1u && !after.error);
    assert(after.history_total == before.history_total && after.history_retained == before.history_retained);
    assert(after.phase == (finite ? 8u : 1u));
    assert(fire_count == (finite ? 1u : 0u) && stop_count == 1u);
    trigger_sequence_link_history_t record;
    assert(trigger_sequence_link_get_history(1u, &record));
    assert(record.observed_pulses == 10u && record.outcome_flags == (finite ? 7u : 3u));
    /* Stale owner publication must not roll back the terminal count. */
    owner.counter_events = 16u;
    trigger_sequence_link_service();
    trigger_sequence_link_get_status(&after);
    assert(after.counter_events == 17u);
}
/* These tests deliberately separate the Core0 delivery turn from Core1. */
static void receive_transport_only(fragments_t f)
{
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT; ++i)
        trigger_sequence_link_rx_fragment(0u, f[i]);
}
static void encode_message(trigger_sequence_link_message_t message, uint16_t token, fragments_t f)
{
    trigger_sequence_link_tx_t tx = {0};
    assert(trigger_sequence_link_tx_begin(&tx, &message, token));
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT; ++i)
        assert(trigger_sequence_link_tx_next(&tx, f[i]));
}
static void transport_deferred(void)
{
    start(); fragments_t f; outgoing(f);
    receive_transport_only(f);
    trigger_sequence_link_status_t before, after;
    trigger_sequence_link_get_status(&before);
    assert(!fire_count && !step_count && before.phase == 2u && !before.rx_messages);
    assert(trigger_sequence_link_service());
    assert(fire_count == 1u);
    owner.gateway_waiting = owner.gateway_pulse_busy = false;
    ++owner.gateway_ready_count;
    assert(!trigger_sequence_link_service());
    outgoing(f); receive_transport_only(f);
    assert(step_count == 0u);
    assert(trigger_sequence_link_service());
    trigger_sequence_link_get_status(&after);
    assert(step_count == 1u && after.rx_messages == 2u);
}
static void transport_stop_pending(void)
{
    start(); fragments_t f; outgoing(f);
    receive_transport_only(f);
    stop_pending = true;
    uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE];
    assert(!trigger_sequence_link_tx_fragment(fragment));
    receive_transport_only(f);
    assert(!trigger_sequence_link_service());
    assert(!fire_count && !step_count);
    owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE;
    stop_pending = false;
    trigger_sequence_link_service();
    owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
    ++owner.run_id;
    trigger_sequence_link_service();
    receive_transport_only(f);
    assert(!trigger_sequence_link_service() && !fire_count);
    outgoing(f); receive_transport_only(f);
    assert(trigger_sequence_link_service() && fire_count == 1u);
}
static void transport_busy_retry(bool stop)
{
    start(); fragments_t f; outgoing(f); receive_transport_only(f);
    action_busy = true;
    assert(!trigger_sequence_link_service() && !fire_count && !stop_count);
    trigger_sequence_link_status_t status;
    trigger_sequence_link_get_status(&status);
    assert(status.rx_messages == 1u && status.phase == 2u && !status.error);
    if (stop) stop_pending = true;
    action_busy = false;
    assert(trigger_sequence_link_service() == !stop);
    assert(fire_count == (stop ? 0u : 1u));
    trigger_sequence_link_get_status(&status);
    assert(status.rx_messages == 1u && !status.error);
    assert(!trigger_sequence_link_service());
    assert(fire_count == (stop ? 0u : 1u));
}
static void transport_mailbox_full(bool restart)
{
    start(); fragments_t original, f; outgoing(original);
    const trigger_sequence_link_message_t good = decode(original);
    for (uint32_t i = 0u; i <= TRIGGER_SEQUENCE_LINK_MAILBOX_CAPACITY; ++i) {
        trigger_sequence_link_message_t message = good;
        /* Differing exchange identities are complete transport events but
         * cannot execute. Capacity exhaustion must still be observable. */
        message.exchange_id += i;
        encode_message(message, (uint16_t)(100u + i), f);
        receive_transport_only(f);
    }
    assert(!fire_count && !stop_count);
    if (restart) {
        owner.state = TRIGGER_SEQUENCE_SERVICE_IDLE;
        trigger_sequence_link_service();
        owner.state = TRIGGER_SEQUENCE_SERVICE_READY;
        ++owner.run_id;
        trigger_sequence_link_service();
        outgoing(f); receive_transport_only(f);
        assert(trigger_sequence_link_service());
        assert(fire_count == 1u && !stop_count);
    } else {
        assert(trigger_sequence_link_service());
        trigger_sequence_link_status_t status;
        trigger_sequence_link_get_status(&status);
        assert(status.phase == 7u && status.error == 7u);
        assert(stop_count == 1u && !fire_count && !step_count);
        uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE];
        assert(!trigger_sequence_link_tx_fragment(fragment));
    }
}
static void transport_stop_at_action(void)
{
    start(); fragments_t f; outgoing(f); receive_transport_only(f);
    /* First get_status is the service observation; second is immediately
     * before the action. STOP wins at the owner command acceptance point. */
    stop_on_owner_read = 2u;
    assert(!trigger_sequence_link_service());
    assert(stop_pending && !fire_count && !step_count);
    assert(!trigger_sequence_link_service());
    trigger_sequence_link_status_t status;
    trigger_sequence_link_get_status(&status);
    assert(status.phase == 1u && !status.error);
}
static void transport_publish_after_discard(void)
{
    start(); fragments_t old, fresh; outgoing(old);
    for (uint32_t i = 0u; i + 1u < TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT; ++i)
        trigger_sequence_link_rx_fragment(0u, old[i]);
    /* Core0 has checked the old owner, then Core1 STOP/restart discards the
     * inbox before the old producer publishes its final assembled message. */
    restart_in_snapshot = true;
    trigger_sequence_link_rx_fragment(0u, old[TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT - 1u]);
    assert(!restart_in_snapshot && !trigger_sequence_link_service());
    assert(!fire_count && !stop_count);
    outgoing(fresh); receive_transport_only(fresh);
    assert(trigger_sequence_link_service() && fire_count == 1u);
}
static void runtime_frozen_model(void)
{
    start(); fragments_t f; outgoing(f);
    /* A model writer must bump the atomic epoch. RUN uses the frozen binding,
     * never traverses these mutable Core0 table pointers on Core1. */
    loads.load_count = instances.instance_count = 0u;
    receive_transport_only(f);
    assert(trigger_sequence_link_service() && fire_count == 1u);
    ++model_epoch;
    assert(trigger_sequence_link_service() && stop_count == 1u);
}
int main(int argc, char **argv)
{
    assert(argc == 2); fixture();
    if (!strcmp(argv[1], "transport_stop_at_action")) transport_stop_at_action();
    else if (!strcmp(argv[1], "transport_publish_after_discard")) transport_publish_after_discard();
    else if (!strcmp(argv[1], "transport_deferred")) transport_deferred();
    else if (!strcmp(argv[1], "transport_stop")) transport_stop_pending();
    else if (!strcmp(argv[1], "transport_busy")) transport_busy_retry(false);
    else if (!strcmp(argv[1], "transport_busy_stop")) transport_busy_retry(true);
    else if (!strcmp(argv[1], "transport_full")) transport_mailbox_full(false);
    else if (!strcmp(argv[1], "transport_full_restart")) transport_mailbox_full(true);
    else if (!strcmp(argv[1], "runtime_frozen_model")) runtime_frozen_model();
    else if (!strcmp(argv[1], "workflow")) workflow();
    else if (!strcmp(argv[1], "stale")) stale_messages();
    else if (!strcmp(argv[1], "stop")) stopped();
    else if (!strcmp(argv[1], "pause")) paused();
    else if (!strcmp(argv[1], "timeout")) timeout();
    else if (!strcmp(argv[1], "model")) model_changed();
    else if (!strcmp(argv[1], "config")) configure_rejections();
    else if (!strcmp(argv[1], "rollback")) configure_rollback();
    else if (!strcmp(argv[1], "first_settle")) first_settle();
    else if (!strcmp(argv[1], "once")) repetitions(1u);
    else if (!strcmp(argv[1], "twice")) repetitions(2u);
    else if (!strcmp(argv[1], "continuous")) repetitions(0u);
    else if (!strcmp(argv[1], "start_view")) start_published_view();
    else if (!strcmp(argv[1], "start_publication")) start_configuration_publication();
    else if (!strcmp(argv[1], "software_next")) software_next();
    else if (!strcmp(argv[1], "external_next")) external_next_rejected();
    else if (!strcmp(argv[1], "counter_once")) counter_rounds(1u, 8u);
    else if (!strcmp(argv[1], "counter_twice")) counter_rounds(2u, 8u);
    else if (!strcmp(argv[1], "counter_single")) counter_rounds(2u, 1u);
    else if (!strcmp(argv[1], "counter_continuous")) counter_rounds(0u, 8u);
    else if (!strcmp(argv[1], "counter_busy_rx")) counter_busy(true);
    else if (!strcmp(argv[1], "counter_busy")) counter_busy(false);
    else if (!strcmp(argv[1], "counter_pause")) counter_pause();
    else if (!strcmp(argv[1], "counter_stale")) counter_stale();
    else if (!strcmp(argv[1], "software_next_contention")) software_next_contention();
    else if (!strcmp(argv[1], "counter_config")) counter_config_rejections();
    else if (!strcmp(argv[1], "counter_fault_restart")) counter_fault_restart();
    else if (!strcmp(argv[1], "counter_rearm_boundary")) counter_rearm_boundary();
    else if (!strcmp(argv[1], "counter_resume_boundary")) counter_resume_boundary();
    else if (!strcmp(argv[1], "counter_pause_after_rearm")) counter_pause_after_rearm();
    else if (!strcmp(argv[1], "counter_stop_final_edges")) counter_stopped_final_edges(false);
    else if (!strcmp(argv[1], "counter_finish_final_edges")) counter_stopped_final_edges(true);
    else assert(0);
    puts("sequence link orchestrator passed"); return 0;
}
