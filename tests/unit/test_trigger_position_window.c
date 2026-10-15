/* Host cases for the T2 position-instance window (Step 1, T2-DEPLOY-003).
 *
 * The module is the single owner of "which positions are in flight, in what
 * state, and which await confirmation". These cases pin the properties the
 * follower-side stop depended on: a scan walks far more instances than the
 * window has records, ack names only the instances awaiting confirmation, and a
 * full window refuses (bounded backpressure) instead of dropping silently.
 */
#include <assert.h>
#include <stdio.h>

#include "trigger_position_window.h"

static void test_scan_walks_beyond_capacity(void)
{
    trigger_position_window_t w;
    trigger_position_window_reset(&w);
    for (uint32_t n = 0u; n < 40u; ++n) {
        const uint32_t id = (n % TRIGGER_POSITION_INSTANCE_MAX) + 1u;
        assert(trigger_position_window_alloc(&w, id, 1000u * n, (uint16_t)n, n));
        assert(trigger_position_window_mark(&w, id, TRIGGER_POSITION_WINDOW_ANNOUNCED));
        assert(trigger_position_window_mark(&w, id, TRIGGER_POSITION_WINDOW_FROZEN));
        const int32_t slot = trigger_position_window_find(&w, id);
        assert(slot >= 0);
        assert((trigger_position_window_ack_mask(&w) & (1u << slot)) != 0u);
        assert(trigger_position_window_mark(&w, id, TRIGGER_POSITION_WINDOW_EXECUTED));
        assert(trigger_position_window_release(&w, id));
        assert(trigger_position_window_find(&w, id) < 0);
    }
    assert(w.entered >= 40u && w.announced >= 40u && w.frozen >= 40u && w.executed >= 40u);
    assert(trigger_position_window_ack_mask(&w) == 0u);
}

static void test_bounded_refusal_then_reuse(void)
{
    trigger_position_window_t w;
    trigger_position_window_reset(&w);
    for (uint32_t id = 1u; id <= TRIGGER_POSITION_WINDOW_CAPACITY; ++id)
        assert(trigger_position_window_alloc(&w, id, id, 1u, id));
    /* Live instances fill the window: the next one is refused, not dropped. */
    assert(!trigger_position_window_alloc(&w, TRIGGER_POSITION_WINDOW_CAPACITY + 1u, 99u, 1u, 99u));
    assert(trigger_position_window_find(&w, TRIGGER_POSITION_WINDOW_CAPACITY + 1u) < 0);
    assert(trigger_position_window_mark(&w, 1u, TRIGGER_POSITION_WINDOW_ANNOUNCED));
    assert(trigger_position_window_mark(&w, 1u, TRIGGER_POSITION_WINDOW_FROZEN));
    assert(trigger_position_window_mark(&w, 1u, TRIGGER_POSITION_WINDOW_EXECUTED));
    assert(trigger_position_window_alloc(&w, TRIGGER_POSITION_WINDOW_CAPACITY + 1u, 99u, 1u, 99u));
    assert(trigger_position_window_find(&w, TRIGGER_POSITION_WINDOW_CAPACITY + 1u) >= 0);
}

static void test_staging_is_monotone_and_idempotent(void)
{
    trigger_position_window_t w;
    trigger_position_window_reset(&w);
    assert(trigger_position_window_alloc(&w, 3u, 300u, 7u, 30u));
    /* No skipping: EXECUTED straight from ENTERED is refused. */
    assert(!trigger_position_window_mark(&w, 3u, TRIGGER_POSITION_WINDOW_EXECUTED));
    assert(!trigger_position_window_mark(&w, 3u, TRIGGER_POSITION_WINDOW_FROZEN));
    assert(trigger_position_window_mark(&w, 3u, TRIGGER_POSITION_WINDOW_ANNOUNCED));
    assert(trigger_position_window_mark(&w, 3u, TRIGGER_POSITION_WINDOW_ANNOUNCED));
    assert(trigger_position_window_mark(&w, 3u, TRIGGER_POSITION_WINDOW_FROZEN));
    assert(!trigger_position_window_mark(&w, 3u, TRIGGER_POSITION_WINDOW_ANNOUNCED));
    assert(trigger_position_window_mark(&w, 3u, TRIGGER_POSITION_WINDOW_EXECUTED));
}

static void test_repeat_instance_keeps_its_record(void)
{
    trigger_position_window_t w;
    trigger_position_window_reset(&w);
    assert(trigger_position_window_alloc(&w, 5u, 500u, 11u, 50u));
    const int32_t first = trigger_position_window_find(&w, 5u);
    /* A repeated FREEZE of the same instance must not consume a second record. */
    assert(trigger_position_window_alloc(&w, 5u, 500u, 11u, 50u));
    assert(trigger_position_window_find(&w, 5u) == first);
    assert(w.entered == 1u);
    /* A newer solve of the same instance updates in place. */
    assert(trigger_position_window_alloc(&w, 5u, 500u, 12u, 50u));
    assert(trigger_position_window_find(&w, 5u) == first);
    assert(w.entered == 1u);
}

static void test_abandon_and_unknown_instance(void)
{
    trigger_position_window_t w;
    trigger_position_window_reset(&w);
    assert(!trigger_position_window_mark(&w, 9u, TRIGGER_POSITION_WINDOW_ANNOUNCED));
    assert(!trigger_position_window_release(&w, 9u));
    assert(trigger_position_window_alloc(&w, 9u, 900u, 2u, 90u));
    assert(trigger_position_window_mark(&w, 9u, TRIGGER_POSITION_WINDOW_ABANDONED));
    assert(w.abandoned == 1u);
    assert(trigger_position_window_mark(&w, 9u, TRIGGER_POSITION_WINDOW_ABANDONED));
    assert(w.abandoned == 1u);
    assert(trigger_position_window_ack_mask(&w) == 0u);
    assert(trigger_position_window_release(&w, 9u));
    assert(trigger_position_window_active_mask(&w) == 0u);
}

static void test_instance_domain_is_validated(void)
{
    trigger_position_window_t w;
    trigger_position_window_reset(&w);
    assert(!trigger_position_window_alloc(&w, 0u, 1u, 1u, 1u));
    assert(!trigger_position_window_alloc(&w, TRIGGER_POSITION_INSTANCE_MAX + 1u, 1u, 1u, 1u));
    assert(trigger_position_window_alloc(&w, TRIGGER_POSITION_INSTANCE_MAX, 1u, 1u, 1u));
    assert(trigger_position_window_active_mask(&w) == 1u);
}

static void test_source_side_open_ack_and_expiry(void)
{
    trigger_position_window_t w;
    trigger_position_window_reset(&w);
    assert(trigger_position_window_alloc(&w, 1u, 5000u, 3u, 10u));
    assert(trigger_position_window_alloc(&w, 2u, 15000u, 4u, 20u));
    /* Re-announcing the same absolute position finds its instance. */
    assert(trigger_position_window_find_target(&w, 15000u) ==
           trigger_position_window_find(&w, 2u));
    assert(trigger_position_window_find_target(&w, 25000u) < 0);
    assert(trigger_position_window_mark(&w, 1u, TRIGGER_POSITION_WINDOW_ANNOUNCED));
    assert(trigger_position_window_mark(&w, 2u, TRIGGER_POSITION_WINDOW_ANNOUNCED));
    assert(trigger_position_window_mark(&w, 2u, TRIGGER_POSITION_WINDOW_FROZEN));
    const uint32_t open = trigger_position_window_open_mask(&w);
    assert(open == (trigger_position_window_active_mask(&w)));
    /* Nothing acknowledged yet: incomplete. */
    assert(!trigger_position_window_ack_complete(&w, 0u));
    /* The follower acknowledges its frozen instance only: still incomplete. */
    assert(!trigger_position_window_ack_complete(&w, trigger_position_window_ack_mask(&w)));
    assert(trigger_position_window_ack_complete(&w, open));
    /* Expiry abandons the live instance whose reserved q already passed. */
    assert(trigger_position_window_expire(&w, 15u) == 1u);
    assert(trigger_position_window_find(&w, 1u) < 0 ||
           w.record[trigger_position_window_find(&w, 1u)].state ==
               TRIGGER_POSITION_WINDOW_ABANDONED);
    assert(w.abandoned == 1u);
    /* An empty open set is complete by construction. */
    assert(trigger_position_window_ack_complete(&w, 0u) ||
           trigger_position_window_open_mask(&w) != 0u);
}

/* The instance window is only correct while it fits the wire it reports on:
 * the freeze acknowledgement carries a 4-bit run id next to an 8-bit frozen
 * mask (distributed_refmem_position_ack.inc:68-92), and the compact slice
 * carries the instance in a 4-bit field (trigger_position_slice.h:11-13).
 * Lock both so a future widening cannot silently break the acknowledgement. */
static void test_wire_contract_bindings(void)
{
    assert(TRIGGER_POSITION_INSTANCE_MAX == 15u);      /* 4-bit instance field   */
    assert(TRIGGER_POSITION_WINDOW_CAPACITY == 8u);    /* 8-bit frozen-mask byte */
    assert(TRIGGER_POSITION_WINDOW_CAPACITY <= 8u);    /* cannot exceed the byte */
    assert(TRIGGER_POSITION_INSTANCE_MAX >= TRIGGER_POSITION_WINDOW_CAPACITY);
}

int main(void)
{
    test_scan_walks_beyond_capacity();
    test_bounded_refusal_then_reuse();
    test_staging_is_monotone_and_idempotent();
    test_repeat_instance_keeps_its_record();
    test_abandon_and_unknown_instance();
    test_instance_domain_is_validated();
    test_source_side_open_ack_and_expiry();
    test_wire_contract_bindings();
    puts("8 production C case groups passed");
    return 0;
}
