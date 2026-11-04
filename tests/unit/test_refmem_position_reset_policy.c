#include "distributed_refmem.h"

#include <stdio.h>

static int expect(const char *name, int actual, int expected)
{
    if ((actual != 0) != (expected != 0)) {
        (void)printf("%s: expected %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %u got %u\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int s_submit_calls;
static bool s_carrier_ready;
static uint8_t s_last_type;
static uint8_t s_last_generation;
static uint8_t s_last_mask;
static uint32_t s_last_size;

static bool fake_submit(const uint8_t *payload, uint32_t payload_size,
                        uint8_t type, uint8_t generation, uint8_t target_mask)
{
    s_submit_calls++;
    s_last_type = type;
    s_last_generation = generation;
    s_last_mask = target_mask;
    s_last_size = payload_size;
    return payload != NULL && payload_size != 0u;
}

static bool fake_ready(void)
{
    return s_carrier_ready;
}

int main(void)
{
    int failed = 0;

    /* stopped ring: no running reset. */
    failed += expect("stopped", refmem_position_reset_admits(0u, 0u, 7u, 7u), 0);
    /* enabled but adapter not started. */
    failed += expect("adapter down", refmem_position_reset_admits(1u, 0u, 7u, 7u), 0);
    /* configuration not applied yet. */
    failed += expect("config pending", refmem_position_reset_admits(1u, 1u, 8u, 7u), 0);
    /* running and applied. */
    failed += expect("running", refmem_position_reset_admits(1u, 1u, 7u, 7u), 1);

    /* ring transport admission mirrors the same condition. */
    failed += expect("transport stopped", refmem_ring_transport_admits(0u, 0u, 7u, 7u), 0);
    failed += expect("transport adapter down", refmem_ring_transport_admits(1u, 0u, 7u, 7u), 0);
    failed += expect("transport config pending", refmem_ring_transport_admits(1u, 1u, 8u, 7u), 0);
    failed += expect("transport running", refmem_ring_transport_admits(1u, 1u, 7u, 7u), 1);

    /* control transport admission (CONSOLE-035) */
    failed += expect("admit none", refmem_control_transport_admits(0u, 0u), 0);
    failed += expect("admit ring", refmem_control_transport_admits(1u, 0u), 1);
    failed += expect("admit bootstrap", refmem_control_transport_admits(0u, 1u), 1);
    failed += expect("route ring",
                     refmem_control_transport_route(1u) ==
                         REFMEM_CONTROL_TRANSPORT_TDMA_RING, 1);
    failed += expect("route none",
                     refmem_control_transport_route(0u) ==
                         REFMEM_CONTROL_TRANSPORT_NONE, 1);

    /* no carrier registered: submit stays closed */
    {
        const uint8_t payload[4] = {1u, 2u, 3u, 4u};
        failed += expect("submit unregistered",
                         refmem_control_transport_submit(payload, 4u, 1u, 7u, 3u), 0);
        failed += expect("unregistered calls", s_submit_calls, 0);
    }

    /* invalid registrations are rejected */
    failed += expect("register null",
                     refmem_control_transport_register_bootstrap(NULL), 0);
    {
        refmem_bootstrap_transport_t partial = {0};
        partial.submit = fake_submit;
        failed += expect("register missing ready",
                         refmem_control_transport_register_bootstrap(&partial), 0);
    }

    /* registered but not handshaken: still closed */
    {
        refmem_bootstrap_transport_t transport = {0};
        transport.submit = fake_submit;
        transport.ready = fake_ready;
        failed += expect("register carrier",
                         refmem_control_transport_register_bootstrap(&transport), 1);
        failed += expect("not ready active",
                         refmem_control_transport_bootstrap_active(), 0);
        failed += expect("route not ready",
                         refmem_control_transport_route(0u) ==
                             REFMEM_CONTROL_TRANSPORT_NONE, 1);
        const uint8_t payload[4] = {9u, 8u, 7u, 6u};
        failed += expect("submit not ready",
                         refmem_control_transport_submit(payload, 4u, 2u, 5u, 1u), 0);
        failed += expect("not ready calls", s_submit_calls, 0);
    }

    /* handshake ready: bootstrap becomes the fallback carrier */
    s_carrier_ready = true;
    failed += expect("ready active",
                     refmem_control_transport_bootstrap_active(), 1);
    failed += expect("route bootstrap",
                     refmem_control_transport_route(0u) ==
                         REFMEM_CONTROL_TRANSPORT_BOOTSTRAP_SPI, 1);
    failed += expect("ring still wins",
                     refmem_control_transport_route(1u) ==
                         REFMEM_CONTROL_TRANSPORT_TDMA_RING, 1);
    {
        const uint8_t payload[3] = {4u, 5u, 6u};
        failed += expect("submit ready",
                         refmem_control_transport_submit(payload, 3u, 2u, 9u, 3u), 1);
        failed += expect("ready calls", s_submit_calls, 1);
        failed += expect_u32("submit type", s_last_type, 2u);
        failed += expect_u32("submit generation", s_last_generation, 9u);
        failed += expect_u32("submit mask", s_last_mask, 3u);
        failed += expect_u32("submit size", s_last_size, 3u);
    }

    /* unregister closes the fallback immediately */
    refmem_control_transport_unregister_bootstrap();
    failed += expect("unregistered active",
                     refmem_control_transport_bootstrap_active(), 0);
    {
        const uint8_t payload[2] = {1u, 1u};
        failed += expect("submit after unregister",
                         refmem_control_transport_submit(payload, 2u, 1u, 1u, 1u), 0);
    }

    if (failed != 0) {
        return 1;
    }
    (void)printf("refmem_position_reset_policy tests passed\n");
    return 0;
}
