#include "refmem_bootstrap_transport.h"

#include <stdio.h>
#include <string.h>

/* The owner module calls the physical adapter API by name; this host test
 * substitutes fake implementations so the STOP/ring gates and the ownership
 * bookkeeping can be exercised without PIO hardware. */
static int s_arm_calls;
static int s_disarm_calls;
static int s_snapshot_calls;
static bool s_arm_result;
static refmem_spi_physical_role_t s_last_role;
static uint32_t s_last_baud;
static refmem_spi_physical_pin_config_t s_last_pins;

bool refmem_spi_physical_adapter_arm(refmem_spi_physical_adapter_t *adapter,
                                     refmem_spi_physical_role_t role,
                                     uint32_t baud_hz,
                                     const refmem_spi_physical_pin_config_t *pins)
{
    s_arm_calls++;
    s_last_role = role;
    s_last_baud = baud_hz;
    if (pins != NULL) {
        s_last_pins = *pins;
    }
    if (adapter != NULL) {
        adapter->role = role;
        adapter->baud_hz = baud_hz;
        if (pins != NULL) {
            adapter->pins = *pins;
        }
        adapter->armed = s_arm_result;
    }
    return s_arm_result;
}

void refmem_spi_physical_adapter_disarm(refmem_spi_physical_adapter_t *adapter)
{
    s_disarm_calls++;
    if (adapter != NULL) {
        adapter->armed = false;
    }
}

bool refmem_spi_physical_adapter_get_snapshot(
    const refmem_spi_physical_adapter_t *adapter,
    refmem_spi_physical_snapshot_t *snapshot)
{
    s_snapshot_calls++;
    if (adapter == NULL || snapshot == NULL) {
        return false;
    }
    snapshot->armed = adapter->armed ? 1u : 0u;
    snapshot->role = (uint32_t)adapter->role;
    snapshot->baud_hz = adapter->baud_hz;
    snapshot->rx_pin = adapter->pins.rx_pin;
    return true;
}

/* The owner's frame wrappers are not exercised here, but linking the owner
 * still needs the adapter symbols they reference. */
bool refmem_spi_physical_adapter_transmit(refmem_spi_physical_adapter_t *adapter,
                                          const uint8_t *frame,
                                          size_t frame_size)
{
    (void)adapter;
    (void)frame;
    (void)frame_size;
    return false;
}

bool refmem_spi_physical_adapter_receive_begin(
    refmem_spi_physical_adapter_t *adapter, size_t frame_capacity,
    uint32_t timeout_ms)
{
    (void)adapter;
    (void)frame_capacity;
    (void)timeout_ms;
    return false;
}

refmem_spi_physical_rx_poll_result_t refmem_spi_physical_adapter_receive_poll(
    refmem_spi_physical_adapter_t *adapter, uint8_t *frame,
    size_t frame_capacity, size_t *frame_size)
{
    (void)adapter;
    (void)frame;
    (void)frame_capacity;
    (void)frame_size;
    return REFMEM_SPI_PHYSICAL_RX_POLL_ERROR;
}

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

int main(void)
{
    int failed = 0;
    refmem_bootstrap_arm_request_t request;
    memset(&request, 0, sizeof(request));
    request.role = REFMEM_SPI_PHYSICAL_ROLE_MASTER;
    request.baud_hz = 1000000u;

    failed += expect("initial disarm state",
                     refmem_bootstrap_transport_armed(), 0);
    failed += expect("initial not-stopped rejects",
                     refmem_bootstrap_transport_reject_not_stopped(), 0);

    /* NULL request is an adapter error, not a silent accept. */
    failed += expect("null request",
                     refmem_bootstrap_transport_arm(NULL) ==
                         REFMEM_BOOTSTRAP_ARM_ADAPTER_ERROR, 1);
    failed += expect("null request error count",
                     refmem_bootstrap_transport_arm_error_count(), 1);

    /* Not stopped: refused before touching the adapter. */
    request.system_stopped = false;
    request.ring_ready = false;
    failed += expect("not stopped",
                     refmem_bootstrap_transport_arm(&request) ==
                         REFMEM_BOOTSTRAP_ARM_NOT_STOPPED, 1);
    failed += expect("not stopped counter",
                     refmem_bootstrap_transport_reject_not_stopped(), 1);
    failed += expect("not stopped no arm call", s_arm_calls, 0);

    /* Ring still carrying commands: refused (ring keeps priority). */
    request.system_stopped = true;
    request.ring_ready = true;
    failed += expect("ring ready",
                     refmem_bootstrap_transport_arm(&request) ==
                         REFMEM_BOOTSTRAP_ARM_RING_READY, 1);
    failed += expect("ring ready counter",
                     refmem_bootstrap_transport_reject_ring_ready(), 1);
    failed += expect("ring ready no arm call", s_arm_calls, 0);

    /* Adapter refuses: owner stays disarmed and counts the error. */
    request.ring_ready = false;
    s_arm_result = false;
    failed += expect("adapter error",
                     refmem_bootstrap_transport_arm(&request) ==
                         REFMEM_BOOTSTRAP_ARM_ADAPTER_ERROR, 1);
    failed += expect("adapter error armed",
                     refmem_bootstrap_transport_armed(), 0);
    failed += expect("adapter error counter",
                     refmem_bootstrap_transport_arm_error_count(), 2);
    failed += expect("adapter called once", s_arm_calls, 1);

    /* Success: owner records arm and forwards role/baud/pins. */
    refmem_spi_physical_pin_config_t pins = {
        .rx_pin = 16u,
        .csn_pin = 17u,
        .sck_pin = 18u,
        .tx_pin = 19u,
    };
    request.pins = &pins;
    s_arm_result = true;
    failed += expect("arm ok",
                     refmem_bootstrap_transport_arm(&request) ==
                         REFMEM_BOOTSTRAP_ARM_OK, 1);
    failed += expect("armed after ok",
                     refmem_bootstrap_transport_armed(), 1);
    failed += expect("arm calls", s_arm_calls, 2);
    failed += expect_u32("role forwarded", (uint32_t)s_last_role,
                         (uint32_t)REFMEM_SPI_PHYSICAL_ROLE_MASTER);
    failed += expect_u32("baud forwarded", s_last_baud, 1000000u);
    failed += expect_u32("rx pin forwarded", s_last_pins.rx_pin, 16u);

    /* Snapshot is served by the owner's adapter. */
    refmem_spi_physical_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    failed += expect("snapshot",
                     refmem_bootstrap_transport_snapshot(&snapshot), 1);
    failed += expect_u32("snapshot armed", snapshot.armed, 1u);
    failed += expect_u32("snapshot role", snapshot.role,
                         (uint32_t)REFMEM_SPI_PHYSICAL_ROLE_MASTER);

    /* Disarm clears ownership state and calls the adapter once. */
    refmem_bootstrap_transport_disarm();
    failed += expect("disarmed", refmem_bootstrap_transport_armed(), 0);
    failed += expect("disarm calls", s_disarm_calls, 1);
    failed += expect("adapter accessor",
                     refmem_bootstrap_transport_adapter() != NULL, 1);

    if (failed != 0) {
        return 1;
    }
    (void)printf("refmem_bootstrap_transport tests passed\n");
    return 0;
}
