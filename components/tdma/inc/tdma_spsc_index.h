#ifndef TDMA_SPSC_INDEX_H
#define TDMA_SPSC_INDEX_H

#include <stdbool.h>
#include <stdint.h>

/* Single-producer/single-consumer ring index arithmetic.
 *
 * The storage lives in the caller; this helper only owns the head/tail
 * invariant so it can be host-tested.  `head` is the consumer index and `tail`
 * the producer index; one slot is always left free to distinguish full from
 * empty.  The runtime pairs these helpers with release/acquire atomics. */
static inline uint32_t tdma_spsc_next(uint32_t index, uint32_t capacity)
{
    const uint32_t next = index + 1u;
    return next >= capacity ? 0u : next;
}

static inline bool tdma_spsc_empty(uint32_t head, uint32_t tail)
{
    return head == tail;
}

static inline bool tdma_spsc_full(uint32_t head, uint32_t tail,
                                  uint32_t capacity)
{
    return tdma_spsc_next(tail, capacity) == head;
}

static inline uint32_t tdma_spsc_depth(uint32_t head, uint32_t tail,
                                       uint32_t capacity)
{
    if (capacity == 0u || head >= capacity || tail >= capacity) {
        return 0u;
    }
    return tail >= head ? tail - head : capacity - head + tail;
}

#endif /* TDMA_SPSC_INDEX_H */
