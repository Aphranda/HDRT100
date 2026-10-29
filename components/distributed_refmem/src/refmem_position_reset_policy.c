/* N8.5 running-safe reset admission policy.  Split out so the topology
 * decision is host testable without the TDMA service; the reset itself still
 * lives in distributed_refmem_plane.c. */
#include "distributed_refmem.h"

bool refmem_position_reset_admits(uint32_t ring_enabled,
                                  uint32_t adapter_started,
                                  uint32_t config_seq,
                                  uint32_t applied_config_seq)
{
    return ring_enabled != 0u && adapter_started != 0u &&
           config_seq == applied_config_seq;
}
