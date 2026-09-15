"""Run actual FIFO operations across preemption and release/publication races."""
import subprocess

from test_vdc_command_owner import ROOT, compile_executable


def test_core0_borrow_and_copy_exclusion(tmp_path):
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "tdma_flight_fifo.h"
static tdma_flight_fifo_t fifo;
static tdma_flight_rx_view_t held;
static void (*copy_interrupt)(void);
static bool release_interrupt;
static uint32_t copy_races, release_races;
static const uint8_t old_data[] = {1, 3, 5, 7};
static const uint8_t new_data[] = {2, 4, 6, 8};
static void after_atomic_store(const volatile void *address, uint64_t value);
static void *fifo_copy(void *dst, const void *src, size_t n)
{
    const size_t half = n / 2;
    memcpy(dst, src, half);
    if (copy_interrupt != NULL) {
        void (*callback)(void) = copy_interrupt;
        copy_interrupt = NULL;
        ++copy_races;
        callback();
    }
    memcpy((uint8_t *)dst + half, (const uint8_t *)src + half, n - half);
    return dst;
}
#define memcpy fifo_copy
/* The original atomic store still executes; inject after its visible effect. */
#define __atomic_store_n(p, v, m) do { \
    __atomic_store_n(p, v, m); after_atomic_store((p), (uint64_t)(v)); \
} while (0)
#include "FIFO_SOURCE"
#undef __atomic_store_n
#undef memcpy

static void reject_competing_core0_operations(void)
{
    tdma_flight_fifo_t before = fifo;
    tdma_flight_rx_view_t another;
    assert(!tdma_flight_fifo_reset_stopped(&fifo));
    assert(!tdma_flight_fifo_core0_publish_tx(&fifo, new_data, sizeof(new_data), 2, 2, 1));
    assert(!tdma_flight_fifo_core0_acquire_rx(&fifo, &another));
    assert(another.data == NULL && another.slot_index == UINT32_MAX);
    assert(memcmp(&before, &fifo, sizeof(fifo)) == 0);
}
static void after_atomic_store(const volatile void *address, uint64_t value)
{
    if (release_interrupt && address == &fifo.rx_slots[held.slot_index].owner &&
        value == TDMA_FLIGHT_RX_OWNER_FREE) {
        release_interrupt = false;
        ++release_races;
        /* Core1 reuses the just-freed slot before Core0 release unlocks. */
        assert(tdma_flight_fifo_core1_publish_rx(&fifo, new_data, sizeof(new_data),
                                                9, 99, 1, 999, 0));
        assert(fifo.rx_slots[held.slot_index].owner == TDMA_FLIGHT_RX_OWNER_CORE0_PARSE);
        reject_competing_core0_operations();
    }
}
int main(void)
{
    assert(tdma_flight_fifo_init(&fifo));
    copy_interrupt = reject_competing_core0_operations;
    assert(tdma_flight_fifo_core0_publish_tx(&fifo, old_data, sizeof(old_data), 1, 1, 1));
    assert(copy_races == 1 && fifo.core0_guard == 0);
    tdma_flight_tx_view_t tx;
    assert(tdma_flight_fifo_core1_acquire_tx(&fifo, &tx));
    assert(tx.sequence == 1 && memcmp(tx.data, old_data, sizeof(old_data)) == 0);
    tdma_flight_fifo_core1_release_tx(&fifo);

    assert(tdma_flight_fifo_core1_publish_rx(&fifo, old_data, sizeof(old_data), 1, 1, 1, 1, 0));
    /* Unacquired queued slots cannot be released. */
    assert(!tdma_flight_fifo_core0_release_rx(&fifo, 0));
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &held));
    reject_competing_core0_operations();
    tdma_flight_fifo_t before = fifo;
    assert(!tdma_flight_fifo_core0_release_rx(&fifo,
            (held.slot_index + 1) % TDMA_FLIGHT_RX_FRAME_SLOT_COUNT));
    assert(memcmp(&before, &fifo, sizeof(fifo)) == 0);
    assert(memcmp(held.data, old_data, sizeof(old_data)) == 0);
    /* Core1 continues while Core0 owns a view, with other free slots. */
    assert(tdma_flight_fifo_core1_publish_rx(&fifo, new_data, sizeof(new_data), 2, 2, 1, 2, 0));
    assert(memcmp(held.data, old_data, sizeof(old_data)) == 0);
    assert(tdma_flight_fifo_core0_release_rx(&fifo, held.slot_index));
    assert(!tdma_flight_fifo_core0_release_rx(&fifo, held.slot_index));
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &held));
    assert(held.sequence == 2 && memcmp(held.data, new_data, sizeof(new_data)) == 0);
    assert(tdma_flight_fifo_core0_release_rx(&fifo, held.slot_index));
    assert(tdma_flight_fifo_reset_stopped(&fifo));

    assert(tdma_flight_fifo_core1_publish_rx(&fifo, old_data, sizeof(old_data), 3, 3, 1, 3, 0));
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &held));
    release_interrupt = true;
    assert(tdma_flight_fifo_core0_release_rx(&fifo, held.slot_index));
    assert(release_races == 1 && fifo.core0_guard == 0);
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &held));
    assert(held.sequence == 99 && memcmp(held.data, new_data, sizeof(new_data)) == 0);
    assert(tdma_flight_fifo_core0_release_rx(&fifo, held.slot_index));
    assert(tdma_flight_fifo_reset_stopped(&fifo));
    assert(!tdma_flight_fifo_core0_acquire_rx(&fifo, &held));
    assert(fifo.core0_guard == 0);
    assert(!tdma_flight_fifo_core0_publish_tx(&fifo, NULL, 1, 1, 1, 1));
    assert(fifo.core0_guard == 0); /* Failed copy request cannot retain the lock. */
    return 0;
}
'''.replace("FIFO_SOURCE", (ROOT / "components/tdma/src/tdma_flight_fifo.c").as_posix())
    executable = compile_executable(tmp_path, "fifo-core0-access", harness)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
