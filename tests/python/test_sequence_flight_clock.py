"""Run the production flight publisher against a controlled hardware clock."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define BOARD_SYS_CLOCK_HZ 250000000u
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT 4u
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE 32u
#define TDMA_PROCESS_IMAGE_REFMEM_OFFSET 8u
#define TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET 20u
#define TDMA_PROCESS_IMAGE_CRC_OFFSET 30u
#define TRIGGER_SEQUENCE_LINK_CONTROL_OPCODE 9u
typedef struct { unsigned unused; } tdma_service_service_t;
typedef struct {
    uint32_t enabled, adapter_started, data_enabled, node_count;
    uint32_t local_slot_id, feedback_timeout_ns;
} tdma_ring_runtime_snapshot_t;
typedef struct { uint32_t tx_ready_count; } tdma_flight_fifo_snapshot_t;
static struct {
    uint32_t publish_interval_ms;
    uint64_t last_publish_ticks;
    uint32_t next_seq32, active_mask, tx_reject_count, tx_publish_count, last_error;
    uint8_t tx_image[128];
} s_tdma_flight_sync;
static uint32_t s_service_count;
static uint64_t hardware_ticks;
static bool clock_valid = true, fifo_valid = true;
static uint32_t pending, fifo_reads, mailbox_builds, fragment_reads, publications;

static bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz, uint64_t *out)
{
    assert(hz == BOARD_SYS_CLOCK_HZ);
    if (!clock_valid) return false;
    *out = hardware_ticks;
    return true;
}
static bool tdma_service_get_flight_fifo_snapshot(
    tdma_service_service_t *owner, tdma_flight_fifo_snapshot_t *out)
{
    assert(owner);
    ++fifo_reads;
    out->tx_ready_count = pending;
    return fifo_valid;
}
static bool distributed_refmem_tdma_flight_build_compact_mailbox(
    uint8_t slot, uint8_t mask, uint32_t seq, uint32_t services,
    uint8_t *frame, size_t size)
{
    assert(slot == 0u && mask == 1u && seq != 0u && services == s_service_count);
    ++mailbox_builds;
    memset(frame, 0, size);
    return true;
}
static bool trigger_sequence_link_tx_fragment(uint8_t *out)
{
    assert(out);
    ++fragment_reads;
    return true;
}
static uint16_t tdma_process_image_crc16_ccitt(const uint8_t *frame, size_t size)
{ assert(frame && size == TDMA_PROCESS_IMAGE_CRC_OFFSET); return 123u; }
static void distributed_refmem_put_le16(uint8_t *out, uint16_t value)
{ out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8u); }
static void distributed_refmem_tdma_flight_sync_store_mailbox(
    uint32_t slot, const uint8_t *frame, size_t size)
{ assert(slot == 0u && frame && size == DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE); }
static size_t tdma_flight_payload_size(uint32_t nodes)
{ return nodes * DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE; }
static uint32_t distributed_refmem_flight_publish_mask_for_slot(uint32_t slot)
{ return 1u << slot; }
static bool tdma_service_publish_flight_tx(tdma_service_service_t *owner,
    const uint8_t *image, size_t size, uint32_t seq, uint32_t generation, uint32_t mask)
{
    assert(owner && image == s_tdma_flight_sync.tx_image && size == 32u);
    assert(seq == generation && mask == 1u);
    ++publications;
    return true;
}
'''

MAIN = r'''
static void assert_no_work(void)
{
    assert(mailbox_builds == 0u && fragment_reads == 0u && publications == 0u);
    assert(s_tdma_flight_sync.next_seq32 == 1u);
    assert(s_tdma_flight_sync.tx_publish_count == 0u);
    assert(s_tdma_flight_sync.tx_reject_count == 0u);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *mode = argv[1];
    tdma_service_service_t owner = {0};
    tdma_ring_runtime_snapshot_t ring = {
        .enabled = 1u, .adapter_started = 1u, .data_enabled = 1u, .node_count = 1u,
    };
    const uint64_t ticks_per_ms = BOARD_SYS_CLOCK_HZ / 1000u;
    s_tdma_flight_sync.publish_interval_ms = 10u;
    s_tdma_flight_sync.next_seq32 = 1u;
    s_tdma_flight_sync.active_mask = 1u;
    uint64_t interval = 10u * ticks_per_ms;
    uint64_t origin = 500u * ticks_per_ms;
    if (strcmp(mode, "low32_wrap") == 0) origin = UINT32_MAX - 10u;
    if (strcmp(mode, "feedback_ceil") == 0) {
        ring.feedback_timeout_ns = UINT32_MAX;
        interval = 4295u * ticks_per_ms;
    }
    if (strcmp(mode, "large_interval") == 0) {
        s_tdma_flight_sync.publish_interval_ms = UINT32_MAX;
        interval = (uint64_t)UINT32_MAX * ticks_per_ms;
    }
    s_tdma_flight_sync.last_publish_ticks = origin;
    hardware_ticks = origin + interval - 1u;
    distributed_refmem_tdma_flight_sync_publish(&owner, &ring);
    assert_no_work();
    assert(fifo_reads == 0u && s_tdma_flight_sync.last_publish_ticks == origin);
    hardware_ticks = origin + interval;
    if (strcmp(mode, "invalid_clock") == 0) clock_valid = false;
    if (strcmp(mode, "regression") == 0) hardware_ticks = origin - 1u;
    if (strcmp(mode, "invalid_fifo") == 0) fifo_valid = false;
    if (strcmp(mode, "pending_fifo") == 0) pending = 1u;
    distributed_refmem_tdma_flight_sync_publish(&owner, &ring);
    if (!clock_valid || hardware_ticks < origin) {
        assert_no_work();
        assert(fifo_reads == 0u && s_tdma_flight_sync.last_error == 8u);
        assert(s_tdma_flight_sync.last_publish_ticks == origin);
        return 0;
    }
    if (!fifo_valid || pending) {
        assert_no_work();
        assert(fifo_reads == 1u && s_tdma_flight_sync.last_publish_ticks == origin);
        fifo_valid = true;
        pending = 0u;
        /* Backpressure does not consume the release or the sequence number. */
        distributed_refmem_tdma_flight_sync_publish(&owner, &ring);
    }
    assert(publications == 1u && fragment_reads == 1u && mailbox_builds == 1u);
    assert(s_tdma_flight_sync.tx_publish_count == 1u);
    assert(s_tdma_flight_sync.last_publish_ticks == hardware_ticks);
    assert(s_tdma_flight_sync.next_seq32 == 2u);
    hardware_ticks += interval - 1u;
    distributed_refmem_tdma_flight_sync_publish(&owner, &ring);
    assert(publications == 1u);
    ++hardware_ticks;
    distributed_refmem_tdma_flight_sync_publish(&owner, &ring);
    assert(publications == 2u && s_tdma_flight_sync.next_seq32 == 3u);
    return 0;
}
'''


@pytest.fixture(scope="module")
def flight_clock_executable(tmp_path_factory):
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(
        encoding="utf-8")
    start = source.index("static void distributed_refmem_tdma_flight_sync_publish(")
    end = source.index("\nstatic void distributed_refmem_tdma_flight_sync_receive(", start)
    production_function = source[start:end]
    directory = tmp_path_factory.mktemp("sequence-flight-clock")
    harness = directory / "flight-clock.c"
    harness.write_text(PREFIX + production_function + MAIN, encoding="utf-8")
    executable = directory / ("flight-clock.exe" if os.name == "nt" else "flight-clock")
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    built = subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(harness), "-o", str(executable)],
                           capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    return executable


@pytest.mark.parametrize("case", [
    "cadence", "low32_wrap", "feedback_ceil", "large_interval",
    "invalid_clock", "regression", "invalid_fifo", "pending_fifo",
])
def test_flight_publish_uses_hardware_clock(flight_clock_executable, case):
    result = subprocess.run([str(flight_clock_executable), case],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
