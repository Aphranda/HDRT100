"""Exercise the real FIFO, including a consumer rebind inside RX memcpy.

The epoch fences FIFO publication, not upstream DMA or wire record lifetime.
STOP reset is tested only after all borrowed views have been released.
"""
import subprocess

from test_vdc_command_owner import ROOT, compile_executable


def test_rx_publication_admission_epoch(tmp_path):
    harness = r'''
#include <assert.h>
#include <string.h>
#include "tdma_flight_fifo.h"
static void (*during_copy)(void);
static void *rx_test_memcpy(void *dst, const void *src, size_t n)
{
    const size_t split = n / 2;
    memcpy(dst, src, split);
    if (during_copy != NULL) {
        void (*callback)(void) = during_copy;
        during_copy = NULL;
        callback();
    }
    memcpy((unsigned char *)dst + split, (const unsigned char *)src + split,
           n - split);
    return dst;
}
#define memcpy rx_test_memcpy
#include "FIFO_SOURCE"
#undef memcpy

static tdma_flight_fifo_t fifo;
static uint32_t expected_epoch;
static const uint8_t payload[] = {3, 1, 4, 1, 5, 9};
static bool publish(uint32_t sequence)
{
    return tdma_flight_fifo_core1_publish_rx(&fifo, payload, sizeof(payload),
                                             17, sequence, 3, 1000, 7);
}
static void rebind_during_copy(void)
{
    /* Publish has started, but has not made its head visible to Core0. */
    assert(fifo.rx_head == fifo.rx_tail);
    expected_epoch = tdma_flight_fifo_core0_advance_rx_admission_epoch(&fifo);
    assert(expected_epoch != 0);
}
static void check_payload(const tdma_flight_rx_view_t *view)
{
    assert(view->data_size == sizeof(payload));
    assert(memcmp(view->data, payload, sizeof(payload)) == 0);
    assert(view->generation == 17 && view->segment_mask == 3);
    assert(view->timestamp_ns == 1000 && view->quality_flags == 7);
}
int main(void)
{
    tdma_flight_rx_view_t view, held;
    assert(tdma_flight_fifo_core0_advance_rx_admission_epoch(NULL) == 0);
    assert(tdma_flight_fifo_init(&fifo));
    /* Unbound data still travels, but carries no command admission grant. */
    assert(publish(1));
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &view));
    assert(view.admission_epoch == 0);
    check_payload(&view);
    assert(tdma_flight_fifo_core0_release_rx(&fifo, view.slot_index));
    expected_epoch = tdma_flight_fifo_core0_advance_rx_admission_epoch(&fifo);
    assert(expected_epoch == 1);

    assert(publish(2));
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &held));
    for (unsigned i = 0; i < TDMA_FLIGHT_RX_FRAME_SLOT_COUNT - 1; ++i)
        assert(publish(3 + i));
    assert(!publish(99)); /* Held slot still belongs to the consumer. */
    tdma_flight_fifo_t before = fifo;
    expected_epoch = tdma_flight_fifo_core0_advance_rx_admission_epoch(&fifo);
    assert(expected_epoch == 2);
    before.rx_admission_epoch = expected_epoch;
    assert(memcmp(&before, &fifo, sizeof(fifo)) == 0);
    assert(held.admission_epoch == 1 && held.admission_epoch != expected_epoch);
    check_payload(&held);
    assert(tdma_flight_fifo_core0_release_rx(&fifo, held.slot_index));
    for (unsigned i = 0; i < TDMA_FLIGHT_RX_FRAME_SLOT_COUNT - 1; ++i) {
        assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &view));
        assert(view.admission_epoch == 1 && view.admission_epoch != expected_epoch);
        check_payload(&view);
        assert(tdma_flight_fifo_core0_release_rx(&fifo, view.slot_index));
    }

    during_copy = rebind_during_copy;
    assert(publish(UINT32_MAX));
    assert(expected_epoch == 3 && during_copy == NULL);
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &view));
    assert(view.sequence == UINT32_MAX && view.admission_epoch == 2);
    check_payload(&view);
    assert(tdma_flight_fifo_core0_release_rx(&fifo, view.slot_index));
    assert(publish(0)); /* Transport wrap does not change local admission. */
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &view));
    assert(view.sequence == 0 && view.admission_epoch == expected_epoch);
    assert(tdma_flight_fifo_core0_release_rx(&fifo, view.slot_index));

    /* Empty queue cursor near wrap: neither cursor nor map generation is tag. */
    fifo.rx_head = fifo.rx_tail = UINT32_MAX;
    assert(publish(8) && publish(9));
    assert(fifo.rx_head == 1);
    for (unsigned i = 0; i < 2; ++i) {
        assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &view));
        assert(view.admission_epoch == expected_epoch && view.sequence == 8 + i);
        assert(tdma_flight_fifo_core0_release_rx(&fifo, view.slot_index));
    }
    assert(publish(10));
    const uint32_t published = fifo.rx_publish_count;
    assert(tdma_flight_fifo_reset_stopped(&fifo));
    assert(fifo.rx_admission_epoch == expected_epoch);
    assert(fifo.rx_publish_count == published);
    assert(!tdma_flight_fifo_core0_acquire_rx(&fifo, &view));
    assert(publish(11));
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &view));
    assert(view.admission_epoch == expected_epoch);
    assert(tdma_flight_fifo_core0_release_rx(&fifo, view.slot_index));

    fifo.rx_admission_epoch = UINT32_MAX - 1;
    assert(tdma_flight_fifo_core0_advance_rx_admission_epoch(&fifo) == UINT32_MAX);
    assert(tdma_flight_fifo_core0_advance_rx_admission_epoch(&fifo) == 0);
    assert(tdma_flight_fifo_core0_advance_rx_admission_epoch(&fifo) == 0);
    assert(fifo.rx_admission_epoch == UINT32_MAX);
    assert(publish(12)); /* Exhaustion never stops ordinary data. */
    assert(tdma_flight_fifo_core0_acquire_rx(&fifo, &view));
    assert(view.admission_epoch == UINT32_MAX);
    assert(tdma_flight_fifo_core0_release_rx(&fifo, view.slot_index));
    assert(tdma_flight_fifo_reset_stopped(&fifo));
    assert(tdma_flight_fifo_core0_advance_rx_admission_epoch(&fifo) == 0);
    return 0;
}
'''.replace("FIFO_SOURCE", (ROOT / "components/tdma/src/tdma_flight_fifo.c").as_posix())
    executable = compile_executable(tmp_path, "rx-admission", harness)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
