#include "tdma_boot_sequence.h"

#include <stddef.h>

static const tdma_boot_step_t k_start_ref[] = {
    TDMA_BOOT_STEP_STAGE_PROFILE,
    TDMA_BOOT_STEP_APPLY_PROFILE,
    TDMA_BOOT_STEP_SET_BRINGUP_TOPOLOGY,
    TDMA_BOOT_STEP_SET_BRINGUP_NO_CAL,
    TDMA_BOOT_STEP_RING_ARM,
    TDMA_BOOT_STEP_RING_START,
};

static const tdma_boot_step_t k_listen[] = {
    TDMA_BOOT_STEP_STAGE_PROFILE,
    TDMA_BOOT_STEP_APPLY_PROFILE,
    TDMA_BOOT_STEP_SET_BRINGUP_TOPOLOGY,
    TDMA_BOOT_STEP_SET_BRINGUP_NO_CAL,
    TDMA_BOOT_STEP_RING_ARM,
    TDMA_BOOT_STEP_RING_LISTEN,
};

static const tdma_boot_step_t k_reject[] = {
    TDMA_BOOT_STEP_STOP,
};

static size_t copy_steps(const tdma_boot_step_t *steps,
                         size_t count,
                         tdma_boot_step_t *out,
                         size_t capacity)
{
    if (out == NULL || capacity < count) {
        return 0u;
    }
    for (size_t i = 0u; i < count; i++) {
        out[i] = steps[i];
    }
    return count;
}

size_t tdma_boot_sequence_build(const tdma_boot_plan_t *plan,
                                tdma_boot_step_t *out,
                                size_t capacity)
{
    if (plan == NULL || out == NULL) {
        return 0u;
    }
    switch (plan->action) {
    case TDMA_BOOT_ACTION_START_REF:
        return copy_steps(k_start_ref,
                          sizeof(k_start_ref) / sizeof(k_start_ref[0]),
                          out, capacity);
    case TDMA_BOOT_ACTION_LISTEN:
        return copy_steps(k_listen, sizeof(k_listen) / sizeof(k_listen[0]),
                          out, capacity);
    case TDMA_BOOT_ACTION_REJECT:
        return copy_steps(k_reject, sizeof(k_reject) / sizeof(k_reject[0]),
                          out, capacity);
    case TDMA_BOOT_ACTION_NONE:
    default:
        return 0u;
    }
}

const char *tdma_boot_step_name(tdma_boot_step_t step)
{
    switch (step) {
    case TDMA_BOOT_STEP_SET_BRINGUP_NO_CAL:
        return "SET_BRINGUP_NO_CAL";
    case TDMA_BOOT_STEP_SET_BRINGUP_TOPOLOGY:
        return "SET_BRINGUP_TOPOLOGY";
    case TDMA_BOOT_STEP_STAGE_PROFILE:
        return "STAGE_PROFILE";
    case TDMA_BOOT_STEP_APPLY_PROFILE:
        return "APPLY_PROFILE";
    case TDMA_BOOT_STEP_RING_ARM:
        return "RING_ARM";
    case TDMA_BOOT_STEP_RING_START:
        return "RING_START";
    case TDMA_BOOT_STEP_RING_LISTEN:
        return "RING_LISTEN";
    case TDMA_BOOT_STEP_STOP:
        return "STOP";
    case TDMA_BOOT_STEP_NONE:
    default:
        return "NONE";
    }
}
