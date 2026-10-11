"""TDMA-SEAT-01/03 invariants under randomized traffic.

The seat's stability claim rests on bounded evidence: every publication
opportunity must land in exactly one counted bucket, the M:N rotation must hold
its ratio, and the declared wait budget must never be exceeded silently. The
hand-written cases in test_tdma_seat_rotation.py pin down known situations; this
file drives the real origin fast path with randomized sequences so that the
identities, not the examples, are what gets checked.

Two properties are exercised:
  A. healthy path  - opportunity conservation, M:N fairness, budget bounds;
  B. fault recovery - a transient publish failure delays a record but never loses
     it: once the fault clears, the record boards on the next opportunity.
"""
import os
import shutil
import subprocess

from test_t2_freeze_completion import HARNESS as REFMEM_HARNESS
from test_t2_freeze_completion import INC as REFMEM_INC
from test_trigger_grid_preview import ROOT, compile_grid
from test_tdma_seat_rotation import FUNCTIONS, HARNESS
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ORIGIN = ROOT / "components/tdma/src/tdma_pio_spi_ring_origin.inc"

# Everything the hand-written harness sets up (stubs, fixtures, helpers), minus
# its own main(), so a randomized driver can replace it.
PREFIX = HARNESS.split("\nint main(void)")[0]

RANDOM_MAIN = r'''
/* The imported hand-written harness carries helpers this driver does not all
 * use (its own step()), so unused-function warnings are expected here. */
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stdio.h>
#include <stdlib.h>

/* Deterministic LCG: the same seed must reproduce the same sequence. */
static uint32_t s_rng;
static uint32_t rnd(void) { s_rng = s_rng * 1103515245u + 12345u; return s_rng >> 8u; }

/* Soft step: reports whether the fast path claimed the opportunity. */
static bool step_soft(bool *handled)
{
    bool claimed = false;
    const bool ok = tdma_pio_spi_ring_origin_publish_priority(&a, &claimed);
    if (claimed) boundary();
    *handled = claimed;
    return ok;
}

static uint32_t seats_of(void) { return a.origin.seat_priority_count + a.origin.seat_first_class_count; }

int main(void)
{
    a.config.local_slot_id = 0u; a.config.node_count = 4u;
    a.priority_tx_provider = provider;
    a.phys_origin.ready = ready; a.phys_origin.publish = publish;
    a.first_class = &first; a.overlay_preparation = &station;
    s_offer = TDMA_PRIORITY_TX_EMPTY;

    uint64_t total_opportunities = 0u, total_seats = 0u, total_fc = 0u;

    /* ---- A. healthy path: conservation, fairness, budget ---- */
    const uint32_t ratios[][2] = {{1u, 1u}, {1u, 2u}, {2u, 1u}, {3u, 1u}, {1u, 3u}, {5u, 3u}};
    for (uint32_t r = 0u; r < sizeof(ratios) / sizeof(ratios[0]); ++r) {
        for (uint32_t seed = 1u; seed <= 32u; ++seed) {
            s_rng = seed * 2654435761u + r;
            a.seat_rotation_priority_share = ratios[r][0];
            a.seat_rotation_first_class_share = ratios[r][1];
            a.origin.seat_opportunity_count = 0u;
            a.origin.seat_priority_count = 0u;
            a.origin.seat_first_class_count = 0u;
            a.origin.seat_first_class_deferred_count = 0u;
            a.origin.seat_first_class_reject_count = 0u;
            a.origin.seat_first_class_wait = 0u;
            a.origin.seat_first_class_wait_max = 0u;
            a.origin.seat_first_class_wait_violation_count = 0u;
            tdma_first_class_retire(&first, true);
            s_offer = TDMA_PRIORITY_TX_EMPTY;
            uint32_t staged = 0u, budget_crossings = 0u;
            const uint32_t opportunities = 400u;
            for (uint32_t i = 0u; i < opportunities; ++i) {
                /* Both seats always have news: the rotation alone decides. */
                if (!tdma_first_class_pending(&first)) {
                    stage_first((uint8_t)(i & 0xffu));
                    ++staged;
                }
                priority_event((uint8_t)(i & 0xffu));
                bool handled = false;
                const bool ok = step_soft(&handled);
                assert(handled && ok);
                /* The declared budget is a bound, never a starvation. */
                assert(a.origin.seat_first_class_wait <=
                       TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES);
                if (a.origin.seat_first_class_wait ==
                    TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES)
                    ++budget_crossings;
            }
            /* Conservation: with a healthy publish side every claimed
             * opportunity ends in exactly one seat. */
            assert(a.origin.seat_opportunity_count == opportunities);
            assert(a.origin.seat_first_class_deferred_count == 0u);
            assert(a.origin.seat_first_class_reject_count == 0u);
            assert(seats_of() == a.origin.seat_opportunity_count);
            assert(a.origin.seat_first_class_wait_max <=
                   TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES);
            assert(a.origin.seat_first_class_wait_violation_count == budget_crossings);
            /* You can only board what was staged. */
            assert(staged >= a.origin.seat_first_class_count);
            /* M:N fairness: the first class takes its configured share (rounded
             * up by at most one slot per cycle) and the priority seat the rest. */
            total_opportunities += a.origin.seat_opportunity_count;
            total_seats += seats_of();
            total_fc += a.origin.seat_first_class_count;
            const uint32_t p = ratios[r][0], f = ratios[r][1];
            const uint64_t ideal = (uint64_t)opportunities * f / (p + f);
            const int64_t drift = (int64_t)a.origin.seat_first_class_count - (int64_t)ideal;
            assert(drift >= -1 && drift <= 1);
            assert(a.origin.seat_priority_count == opportunities - a.origin.seat_first_class_count);
        }
    }

    /* ---- A2. mixed randomized traffic: conservation still holds when news is
     * sometimes absent and the provider sometimes has nothing to give ---- */
    for (uint32_t seed = 1u; seed <= 32u; ++seed) {
        s_rng = seed * 2246822519u;
        a.seat_rotation_priority_share = (seed % 3u) ? 1u : 2u;
        a.seat_rotation_first_class_share = (seed % 4u) ? 1u : 3u;
        a.origin.seat_opportunity_count = 0u;
        a.origin.seat_priority_count = 0u;
        a.origin.seat_first_class_count = 0u;
        a.origin.seat_first_class_deferred_count = 0u;
        a.origin.seat_first_class_reject_count = 0u;
        a.origin.seat_first_class_wait = 0u;
        a.origin.seat_first_class_wait_max = 0u;
        tdma_first_class_retire(&first, true);
        s_offer = TDMA_PRIORITY_TX_EMPTY;
        uint32_t claimed = 0u;
        for (uint32_t i = 0u; i < 300u; ++i) {
            const uint32_t r = rnd() % 100u;
            if (r < 50u && !tdma_first_class_pending(&first))
                stage_first((uint8_t)(i & 0xffu));
            if (r >= 20u) priority_event((uint8_t)(i & 0xffu));
            if (r >= 90u) s_offer = TDMA_PRIORITY_TX_EMPTY;
            bool handled = false;
            assert(step_soft(&handled));
            /* `handled` is route ownership, not a spent opportunity: with the
             * provider enabled this fast path owns local mailbox publication on
             * every call, whether or not anyone has news to send. */
            assert(handled);
            if (handled) ++claimed;
            assert(a.origin.seat_first_class_wait <=
                   TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES);
            assert(a.origin.seat_first_class_wait_max <=
                   TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES);
        }
        /* Conservation is on opportunities: each one that the fast path claimed
         * ends in exactly one seat, a deferral or a refusal - never in nothing. */
        assert(a.origin.seat_opportunity_count ==
               seats_of() + a.origin.seat_first_class_deferred_count +
               a.origin.seat_first_class_reject_count);
        assert(a.origin.seat_opportunity_count <= claimed);
    }

    /* ---- B. fault recovery: a transient publish failure delays, never loses ---- */
    for (uint32_t seed = 1u; seed <= 16u; ++seed) {
        s_rng = seed * 40503u;
        a.seat_rotation_priority_share = 1u;
        a.seat_rotation_first_class_share = 1u;
        a.origin.seat_opportunity_count = 0u;
        a.origin.seat_priority_count = 0u;
        a.origin.seat_first_class_count = 0u;
        a.origin.seat_first_class_wait = 0u;
        tdma_first_class_retire(&first, true);
        s_offer = TDMA_PRIORITY_TX_EMPTY;
        s_publish_ok = false;                 /* the publish side is refusing */
        stage_first((uint8_t)seed);
        assert(tdma_first_class_pending(&first));
        uint32_t refused = 0u;
        for (uint32_t i = 0u; i < 40u; ++i) {
            bool handled = false;
            (void)step_soft(&handled);
            assert(handled);
            if (tdma_first_class_pending(&first)) ++refused;
        }
        assert(refused > 0u);                 /* the fault really was exercised */
        s_publish_ok = true;                  /* ... and now it clears */
        bool boarded = false;
        for (uint32_t i = 0u; i < 8u && !boarded; ++i) {
            bool handled = false;
            assert(step_soft(&handled) && handled);
            boarded = !tdma_first_class_pending(&first);
        }
        assert(boarded);                      /* delayed, never dropped */
        assert(a.origin.seat_first_class_count >= 1u);
        assert(a.origin.seat_first_class_reject_count == 0u);
    }
    /* A digest the test compares across runs: the driver is seeded, so the same
     * binary must report the same numbers. */
    printf("opportunities=%llu seats=%llu first_class=%llu\n",
           (unsigned long long)total_opportunities, (unsigned long long)total_seats,
           (unsigned long long)total_fc);
    return 0;
}
'''


def _build(tmp_path, name):
    source = ORIGIN.read_text(encoding="utf-8")
    bodies = "".join(
        f"{kind} {function}({arguments}) {{" + c_definition_body(source, function) + "}\n"
        for kind, function, arguments in FUNCTIONS)
    # The imported harness defines helpers (its own step()) that this driver does
    # not use; the diagnostic switch must precede those definitions to apply.
    guard = '#pragma GCC diagnostic ignored "-Wunused-function"\n'
    return compile_grid(tmp_path, name, guard + PREFIX + bodies + RANDOM_MAIN)


def _run(tmp_path, name):
    executable = _build(tmp_path, name)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr


def test_seat_rotation_invariants_under_randomized_traffic(tmp_path):
    """Opportunity conservation, M:N fairness and the wait budget, over 6 ratios
    x 32 seeds x 400 opportunities, plus fault-recovery liveness."""
    _run(tmp_path, "seat-random-invariants")


def test_randomized_harness_is_deterministic(tmp_path):
    """The driver is seeded, so two builds of the same source must report the same
    digest: a failure is reproducible, and the invariants above are not satisfied
    by chance."""
    def digest(name):
        executable = _build(tmp_path, name)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout.strip()

    first, second = digest("seat-random-repeat-a"), digest("seat-random-repeat-b")
    assert first and first == second, (first, second)
    # A weak sanity check that the loops actually ran.
    assert int(first.split()[0].split("=")[1]) > 0


# --- bounded receive preservation: randomized accounting invariants ---------

REFMEM_PREFIX = REFMEM_HARNESS.split("\nint main(void)")[0].replace(
    '"INC"', '"' + REFMEM_INC.as_posix() + '"')

REFMEM_RANDOM_MAIN = r'''
#pragma GCC diagnostic ignored "-Wunused-function"
#include <stdio.h>

static uint32_t s_rng;
static uint32_t rnd(void) { s_rng = s_rng * 1103515245u + 12345u; return s_rng >> 8u; }

/* Entries still queued per source (the FIFO is per source, so a global "count"
 * can never answer this). */
static uint32_t still_held(void)
{
    uint32_t total = 0u;
    for (uint32_t slot = 0u; slot < DISTRIBUTED_REFMEM_NODE_COUNT; ++slot) {
        const uint32_t head = s_position_priority_hold_head[slot];
        const uint32_t tail = s_position_priority_hold_tail[slot];
        assert(tail >= head);
        assert(tail - head <= DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH);
        total += tail - head;
    }
    return total;
}

int main(void)
{
    for (uint32_t seed = 1u; seed <= 24u; ++seed) {
        s_rng = seed * 2654435761u;
        for (uint32_t slot = 0u; slot < DISTRIBUTED_REFMEM_NODE_COUNT; ++slot) {
            s_position_priority_hold_head[slot] = 0u;
            s_position_priority_hold_tail[slot] = 0u;
        }
        memset(s_position_rx_mailboxes, 0, sizeof(s_position_rx_mailboxes));
        memset(s_position_freeze_latches, 0, sizeof(s_position_freeze_latches));
        s_position_priority_hold_count = 0u;
        s_position_priority_hold_dropped_count = 0u;
        s_position_priority_drain_count = 0u;
        s_position_priority_hold_expired_count = 0u;
        s_position_priority_hold_delivered_count = 0u;
        s_position_priority_accept_count = 0u;
        s_position_priority_duplicate_count = 0u;
        s_position_priority_reject_count = 0u;
        s_position_priority_decode_reject_count = 0u;

        tdma_priority_rx_record_t previous = record(
            TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 1u, 1u, false, 1u);
        bool have_previous = false;
        uint32_t t2_deliveries = 0u;
        for (uint32_t i = 0u; i < 400u; ++i) {
            const uint32_t r = rnd() % 100u;
            tdma_priority_rx_record_t incoming;
            if (r < 70u) {
                incoming = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS,
                                  (uint32_t)(1u + rnd() % 8u), (uint32_t)(1u + rnd() % 40u),
                                  (r % 5u) == 0u, rnd() % 200u);
            } else if (r < 85u) {
                incoming = record(TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS,
                                  1u, 1u, false, 5u);
            } else if (have_previous) {
                incoming = previous;                  /* wire repeat */
            } else {
                incoming = record(TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS, 1u, 1u, false, 1u);
            }
            if ((r % 23u) == 0u)                      /* undecodable slice */
                incoming.mailbox[TDMA_PROCESS_IMAGE_REFMEM_OFFSET] = 0x1Fu;
            previous = incoming;
            have_previous = true;

            const bool is_t2 =
                incoming.mailbox[3] == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS;
            const uint32_t before_accept = s_position_priority_accept_count;
            const uint32_t before_duplicate = s_position_priority_duplicate_count;
            const uint32_t before_reject = s_position_priority_reject_count;
            const uint32_t before_decode = s_position_priority_decode_reject_count;
            const uint32_t before_enqueued = s_position_priority_hold_count;
            const uint32_t before_dropped = s_position_priority_hold_dropped_count;
            const uint32_t held_before = still_held();
            if (is_t2) ++t2_deliveries;

            distributed_refmem_position_priority_rx(&incoming);

            /* Accounting after every single delivery. */
            const uint32_t accepted = s_position_priority_accept_count - before_accept;
            const uint32_t duplicated = s_position_priority_duplicate_count - before_duplicate;
            const uint32_t refused = s_position_priority_reject_count - before_reject;
            const uint32_t undecodable = s_position_priority_decode_reject_count - before_decode;
            const uint32_t enqueued = s_position_priority_hold_count - before_enqueued;
            const uint32_t dropped = s_position_priority_hold_dropped_count - before_dropped;
            const uint32_t held_now = still_held();
            if (!is_t2) {
                /* Another class is not ours: it must not touch any T2 fact. */
                assert(accepted == 0u && duplicated == 0u && refused == 0u &&
                       undecodable == 0u && enqueued == 0u && dropped == 0u);
            } else {
                /* Exactly one outcome per delivery, always counted. */
                assert(accepted + duplicated + refused + undecodable == 1u);
                assert(enqueued + dropped <= refused);
                /* A drop happens only when the queue was already full, and an
                 * enqueue only when it was not. */
                assert(dropped == 0u || held_before == DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH);
                assert(enqueued == 0u || held_before < DISTRIBUTED_REFMEM_POSITION_HOLD_DEPTH);
                assert(held_now == held_before + enqueued);
            }
            /* The queue never loses an entry silently: everything enqueued is
             * drained, expired, or still queued. */
            if (s_position_priority_hold_count !=
                s_position_priority_drain_count +
                s_position_priority_hold_expired_count +
                s_position_priority_hold_delivered_count + held_now) {
                printf("MISMATCH seed=%u i=%u is_t2=%d hold=%u drain=%u expired=%u "
                       "delivered=%u dropped=%u head=%u tail=%u held_now=%u "
                       "held_before=%u accept=%u dup=%u reject=%u decode=%u\n",
                       seed, i, (int)is_t2, s_position_priority_hold_count,
                       s_position_priority_drain_count,
                       s_position_priority_hold_expired_count,
                       s_position_priority_hold_delivered_count,
                       s_position_priority_hold_dropped_count,
                       s_position_priority_hold_head[0], s_position_priority_hold_tail[0],
                       held_now, held_before, s_position_priority_accept_count,
                       s_position_priority_duplicate_count, s_position_priority_reject_count,
                       s_position_priority_decode_reject_count);
                return 1;
            }

            /* Random consumption, the way the T2 phase does it: take a
             * descriptor, drain a held entry, both, or neither. Doing nothing is
             * what ages a hold out against the retry budget. */
            trigger_position_descriptor_t d;
            const uint32_t consumption = rnd() % 4u;
            if (consumption == 1u || consumption == 3u) (void)take(&d);
            if (consumption >= 2u) {
                const uint32_t drained_before = s_position_priority_drain_count;
                if (distributed_refmem_position_priority_drain(0u)) {
                    assert(s_position_priority_drain_count == drained_before + 1u);
                    /* A drained entry must be readable, never dropped on the
                     * floor between the queue and the consumer. */
                    assert(take(&d));
                } else {
                    /* drain() is false both for an empty queue and for a busy
                     * consumer, so a failure count is NOT implied; what must
                     * hold is that the refusal consumed nothing, which the queue
                     * identity below checks. */
                    (void)s_position_priority_drain_fail_count;
                }
            }
            assert(s_position_priority_hold_count ==
                   s_position_priority_drain_count +
                   s_position_priority_hold_expired_count + s_position_priority_hold_delivered_count + still_held());
        }
        /* End of run: every T2 delivery ended in exactly one bucket. */
        assert(s_position_priority_accept_count + s_position_priority_duplicate_count +
               s_position_priority_reject_count + s_position_priority_decode_reject_count ==
               t2_deliveries);
        assert(s_position_priority_hold_count == s_position_priority_drain_count +
               s_position_priority_hold_expired_count + s_position_priority_hold_delivered_count + still_held());
        assert(s_position_priority_hold_count <= t2_deliveries);
        printf("seed=%u t2=%u accept=%u duplicate=%u reject=%u decode=%u enqueued=%u "
               "drained=%u expired=%u dropped=%u held=%u\n",
               seed, t2_deliveries, s_position_priority_accept_count,
               s_position_priority_duplicate_count, s_position_priority_reject_count,
               s_position_priority_decode_reject_count, s_position_priority_hold_count,
               s_position_priority_drain_count, s_position_priority_hold_expired_count,
               s_position_priority_hold_dropped_count, still_held());
    }
    return 0;
}
'''


def _build_refmem(tmp_path, name):
    """The queue implementation is included as the .inc it is; touching the
    component's .c would drag the whole flash/refmem dependency tree in. The
    queue also carries entry points this driver does not exercise, so the
    unused-function diagnostic must be off before the include."""
    guard = '#pragma GCC diagnostic ignored "-Wunused-function"\n'
    return compile_grid(tmp_path, name, guard + REFMEM_PREFIX + REFMEM_RANDOM_MAIN)


def test_receive_preservation_accounting_under_randomized_traffic(tmp_path):
    """Randomized T2 traffic against the bounded receive queue: every delivery
    lands in exactly one bucket, a drop only when the queue was full, and the
    queue identity (enqueued == drained + expired + still held) holds after every
    single delivery."""
    executable = _build_refmem(tmp_path, "refmem-random-invariants")
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, result.stdout + result.stderr
    lines = [line for line in result.stdout.splitlines() if line.startswith("seed=")]
    assert len(lines) == 24, result.stdout
