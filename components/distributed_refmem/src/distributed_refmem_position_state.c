#include "distributed_refmem_internal.h"

static distributed_refmem_position_state_t s_position_state = {
    .drain_ms = DISTRIBUTED_REFMEM_POSITION_DRAIN_DEFAULT_MS,
};

distributed_refmem_position_state_t *distributed_refmem_position_state(void)
{
    return &s_position_state;
}
