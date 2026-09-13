#include "diagnostics_tdma_record.h"

#include <string.h>
#include "ota_crc32.h"

#define RECORD_BITMAP_WORDS ((DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS + 31u) / 32u)
#define RECORD_HEADER_WORDS 16u
#define RECORD_SAMPLE_HEADER_WORDS 12u
#define RECORD_FOOTER_WORDS 16u

static struct {
    uint32_t state, cancel;
    diagnostics_tdma_record_status_t status;
    uint64_t trigger_us;
    uint32_t capacity, crc, next_slot;
    uint32_t previous[DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS];
} s_record;

static uint32_t record_state(void)
{
    return __atomic_load_n(&s_record.state, __ATOMIC_ACQUIRE);
}

static void record_publish(uint32_t state)
{
    __atomic_store_n(&s_record.state, state, __ATOMIC_RELEASE);
}

bool diagnostics_tdma_record_arm(uint32_t epoch, uint32_t interval_us,
                                 uint32_t samples)
{
    if (epoch == 0u || interval_us < DIAGNOSTICS_TDMA_RECORD_MIN_INTERVAL_US ||
        interval_us > DIAGNOSTICS_TDMA_RECORD_MAX_INTERVAL_US ||
        samples == 0u || samples > DIAGNOSTICS_TDMA_RECORD_MAX_SAMPLES) {
        return false;
    }
    uint32_t state = record_state();
    if (state != TDMA_RECORD_IDLE && state != TDMA_RECORD_SAVED &&
        state != TDMA_RECORD_FAILED) {
        return false;
    }
    if (!__atomic_compare_exchange_n(&s_record.state, &state,
            TDMA_RECORD_PREPARING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return false;
    }
    memset(&s_record.status, 0, sizeof(s_record.status));
    s_record.status.epoch = epoch;
    s_record.status.interval_us = interval_us;
    s_record.status.requested = samples;
    s_record.next_slot = 0u;
    s_record.trigger_us = 0u;
    s_record.crc = 0u;
    __atomic_store_n(&s_record.cancel, 0u, __ATOMIC_RELAXED);
    record_publish(TDMA_RECORD_REQUESTED);
    return true;
}

void diagnostics_tdma_record_start(uint64_t requested_us)
{
    if (record_state() == TDMA_RECORD_ARMED) {
        s_record.trigger_us = requested_us;
        record_publish(TDMA_RECORD_RUNNING);
    }
}

void diagnostics_tdma_record_cancel(void)
{
    __atomic_store_n(&s_record.cancel, 1u, __ATOMIC_RELEASE);
}

bool diagnostics_tdma_record_save(void)
{
    uint32_t expected = TDMA_RECORD_FROZEN;
    return __atomic_compare_exchange_n(&s_record.state, &expected,
        TDMA_RECORD_SAVE_REQUESTED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

bool diagnostics_tdma_record_status(diagnostics_tdma_record_status_t *out)
{
    const uint32_t state = record_state();
    if (out == NULL || state == TDMA_RECORD_RUNNING ||
        state == TDMA_RECORD_PREPARING || state == TDMA_RECORD_REQUESTED ||
        state == TDMA_RECORD_SAVE_REQUESTED || state == TDMA_RECORD_SAVING) {
        return false;
    }
    *out = s_record.status;
    out->state = state;
    return record_state() == state;
}

static bool record_append(const diagnostics_tdma_record_port_t *port,
                           const uint32_t *words, uint32_t count)
{
    const uint32_t bytes = count * (uint32_t)sizeof(uint32_t);
    if (s_record.status.bytes > s_record.capacity ||
        bytes > s_record.capacity - s_record.status.bytes ||
        !port->append(s_record.status.bytes, (const uint8_t *)words, bytes)) {
        return false;
    }
    s_record.crc = ota_crc32_update(s_record.crc, (const uint8_t *)words, bytes);
    s_record.status.bytes += bytes;
    return true;
}

static void record_finish(const diagnostics_tdma_record_port_t *port,
                           uint32_t reason)
{
    const uint64_t now = port->now_us();
    uint32_t footer[RECORD_FOOTER_WORDS] = {
        DIAGNOSTICS_TDMA_RECORD_END_MAGIC, RECORD_FOOTER_WORDS,
        s_record.status.requested, s_record.status.written,
        s_record.status.missed, reason,
        (uint32_t)s_record.trigger_us, (uint32_t)(s_record.trigger_us >> 32u),
        (uint32_t)now, (uint32_t)(now >> 32u), s_record.status.bytes,
        s_record.crc, s_record.status.epoch, 0u, 0u, 0u
    };
    s_record.status.reason = reason;
    if (!record_append(port, footer, RECORD_FOOTER_WORDS)) {
        s_record.status.reason = TDMA_RECORD_STORAGE_ERROR;
        record_publish(TDMA_RECORD_FAILED);
        return;
    }
    record_publish(TDMA_RECORD_FROZEN);
}

static bool record_sample(const diagnostics_tdma_record_port_t *port,
                           uint32_t slot, uint64_t target, uint32_t skipped)
{
    uint32_t current[DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS] = {0};
    uint32_t packet[RECORD_SAMPLE_HEADER_WORDS + RECORD_BITMAP_WORDS +
                    DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS] = {0};
    const uint64_t started = port->now_us();
    const uint32_t valid = port->snapshot(current);
    const uint64_t completed = port->now_us();
    uint32_t count = RECORD_SAMPLE_HEADER_WORDS + RECORD_BITMAP_WORDS;
    for (uint32_t i = 0u; i < DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS; ++i) {
        if (slot == UINT32_MAX || current[i] != s_record.previous[i]) {
            packet[RECORD_SAMPLE_HEADER_WORDS + i / 32u] |= 1u << (i % 32u);
            packet[count++] = current[i];
        }
    }
    packet[0] = DIAGNOSTICS_TDMA_RECORD_SAMPLE_MAGIC;
    packet[1] = count;
    packet[2] = slot;
    packet[3] = valid;
    packet[4] = (uint32_t)target;
    packet[5] = (uint32_t)(target >> 32u);
    packet[6] = (uint32_t)started;
    packet[7] = (uint32_t)(started >> 32u);
    packet[8] = (uint32_t)completed;
    packet[9] = (uint32_t)(completed >> 32u);
    packet[10] = skipped;
    /* Reserve an intact footer even when the delta payload fills the lease. */
    if (s_record.status.bytes > s_record.capacity ||
        (count + RECORD_FOOTER_WORDS) * sizeof(uint32_t) >
            s_record.capacity - s_record.status.bytes) {
        record_finish(port, TDMA_RECORD_OVERFLOW);
        return false;
    }
    if (!record_append(port, packet, count)) {
        record_finish(port, TDMA_RECORD_STORAGE_ERROR);
        return false;
    }
    memcpy(s_record.previous, current, sizeof(current));
    if (slot != UINT32_MAX) {
        s_record.status.written++;
    }
    return true;
}

void diagnostics_tdma_record_service(const diagnostics_tdma_record_port_t *port)
{
    const uint32_t state = record_state();
    if (state == TDMA_RECORD_REQUESTED) {
        if (!port->begin(s_record.status.epoch, &s_record.capacity)) {
            s_record.status.reason = TDMA_RECORD_STORAGE_ERROR;
            record_publish(TDMA_RECORD_FAILED);
            return;
        }
        uint64_t build = 0u, board = 0u;
        port->identity(&build, &board);
        uint32_t header[RECORD_HEADER_WORDS] = {
            DIAGNOSTICS_TDMA_RECORD_MAGIC, DIAGNOSTICS_TDMA_RECORD_SCHEMA,
            RECORD_HEADER_WORDS, DIAGNOSTICS_TDMA_RECORD_VALUE_WORDS,
            s_record.status.interval_us, s_record.status.requested,
            s_record.status.epoch, (uint32_t)build, (uint32_t)(build >> 32u),
            (uint32_t)board, (uint32_t)(board >> 32u), 1000000u,
            RECORD_BITMAP_WORDS, 0u, 0u, 0u
        };
        if (!record_append(port, header, RECORD_HEADER_WORDS)) {
            s_record.status.reason = TDMA_RECORD_STORAGE_ERROR;
            record_publish(TDMA_RECORD_FAILED);
        } else if (record_sample(port, UINT32_MAX, port->now_us(), 0u)) {
            record_publish(TDMA_RECORD_ARMED);
        }
        return;
    }
    if (state == TDMA_RECORD_ARMED || state == TDMA_RECORD_RUNNING) {
        if (__atomic_load_n(&s_record.cancel, __ATOMIC_ACQUIRE) != 0u) {
            record_publish(TDMA_RECORD_RUNNING);
            record_finish(port, TDMA_RECORD_CANCELLED);
            return;
        }
        if (state == TDMA_RECORD_ARMED) {
            return;
        }
        const uint64_t now = port->now_us();
        const uint64_t target = s_record.trigger_us +
            (uint64_t)s_record.next_slot * s_record.status.interval_us;
        if (now < target) {
            return;
        }
        const uint64_t elapsed_slots = (now - s_record.trigger_us) /
            s_record.status.interval_us;
        if (elapsed_slots >= s_record.status.requested) {
            s_record.status.missed += s_record.status.requested - s_record.next_slot;
            record_finish(port, TDMA_RECORD_COMPLETE);
            return;
        }
        const uint32_t slot = (uint32_t)elapsed_slots;
        const uint32_t skipped = slot - s_record.next_slot;
        s_record.status.missed += skipped;
        if (!record_sample(port, slot, s_record.trigger_us +
                (uint64_t)slot * s_record.status.interval_us, skipped)) {
            return;
        }
        s_record.next_slot = slot + 1u;
        if (s_record.next_slot == s_record.status.requested) {
            record_finish(port, TDMA_RECORD_COMPLETE);
        }
        return;
    }
    if (state == TDMA_RECORD_SAVE_REQUESTED) {
        if (!port->save(s_record.crc, &s_record.status.job_id)) {
            /* Keep the frozen RAM lease available for an explicit retry. */
            record_publish(TDMA_RECORD_FROZEN);
        } else {
            record_publish(TDMA_RECORD_SAVING);
        }
        return;
    }
    if (state == TDMA_RECORD_SAVING) {
        const int result = port->save_result(s_record.status.job_id);
        if (result != 0) {
            record_publish(result > 0 ? TDMA_RECORD_SAVED : TDMA_RECORD_FAILED);
        }
    }
}
