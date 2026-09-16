#include "trigger_sequence_link.h"
#include "refmem_application_model.h"
#include "refmem_realtime_contract.h"
#include "tdma_runtime_owner.h"
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
static bool reconfigure_in_snapshot, model_change_in_snapshot;
static bool configuration_gate;
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
    start();
    fragments_t link, ready;
    outgoing(link);
    assert(trigger_sequence_link_next() == TRIGGER_SEQUENCE_SERVICE_NOT_READY);
    receive(link);
    assert(fire_count == 1u && owner.gateway_waiting);
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
int main(int argc, char **argv)
{
    assert(argc == 2); fixture();
    if (!strcmp(argv[1], "workflow")) workflow();
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
    else assert(0);
    puts("sequence link orchestrator passed"); return 0;
}
