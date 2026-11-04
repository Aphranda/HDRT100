#ifndef REFMEM_BOOTSTRAP_HELLO_H
#define REFMEM_BOOTSTRAP_HELLO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Wire binding for the bootstrap relay probe: a REFMEM HELLO frame.  The
 * probe is a liveness/reachability marker, not a capability exchange; the
 * payload is intentionally zero-filled.  Readiness therefore means "heard a
 * valid HELLO from the expected peer", not "the peer agreed on capabilities". */
bool refmem_bootstrap_hello_build(uint8_t source_slot, uint8_t target_slot,
                                  uint32_t seq, uint8_t *frame,
                                  size_t frame_capacity, size_t *frame_size);
bool refmem_bootstrap_hello_parse(const uint8_t *frame, size_t frame_size,
                                  uint8_t *source_slot, bool *is_ack);

#endif /* REFMEM_BOOTSTRAP_HELLO_H */
