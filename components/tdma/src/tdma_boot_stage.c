#include "tdma_boot_stage.h"

void tdma_boot_stage_decide(bool has_stage,
                            uint32_t stage_profile_crc32,
                            const tdma_boot_stage_level_t *levels,
                            size_t level_count,
                            tdma_boot_stage_plan_t *out)
{
    if (out == NULL) {
        return;
    }
    out->action = TDMA_BOOT_STAGE_ACTION_NONE;
    out->reason = TDMA_BOOT_STAGE_REASON_NO_STAGE;
    out->level = 0u;
    if (!has_stage) {
        return;
    }
    if (stage_profile_crc32 == 0u || levels == NULL || level_count == 0u) {
        out->action = TDMA_BOOT_STAGE_ACTION_REJECT;
        out->reason = TDMA_BOOT_STAGE_REASON_BAD_PROFILE;
        return;
    }
    for (size_t index = 0u; index < level_count; index++) {
        if (levels[index].level != 0u &&
            levels[index].profile_crc32 == stage_profile_crc32) {
            out->action = TDMA_BOOT_STAGE_ACTION_STAGE;
            out->reason = TDMA_BOOT_STAGE_REASON_NONE;
            out->level = levels[index].level;
            return;
        }
    }
    out->action = TDMA_BOOT_STAGE_ACTION_REJECT;
    out->reason = TDMA_BOOT_STAGE_REASON_NO_MATCH;
}
