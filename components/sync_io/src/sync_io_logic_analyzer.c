#include "sync_io_logic_analyzer.h"

#include <string.h>

_Static_assert(SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS > 0u,
               "logic analyzer workspace must hold at least one record");

static bool sync_io_logic_analyzer_source_mask_valid(uint32_t source_mask)
{
    const sync_io_persona_descriptor_t *descriptor =
        sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_LOGIC_ANALYZER);
    return sync_io_persona_descriptor_valid(descriptor) &&
           (descriptor->flags & SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD) != 0u &&
           descriptor->gpio_write_mask == 0u &&
           source_mask != 0u &&
           (source_mask & ~descriptor->gpio_read_mask) == 0u;
}

static bool sync_io_logic_analyzer_mask_valid(uint32_t mask,
                                              uint32_t source_mask)
{
    return (mask & ~source_mask) == 0u;
}

bool sync_io_logic_analyzer_config_valid(
    const sync_io_logic_analyzer_config_t *config)
{
    if (config == NULL ||
        config->contract_version != SYNC_IO_LOGIC_ANALYZER_CONTRACT_VERSION ||
        config->mode <= SYNC_IO_LOGIC_ANALYZER_MODE_INVALID ||
        config->mode >= SYNC_IO_LOGIC_ANALYZER_MODE_COUNT ||
        !sync_io_logic_analyzer_source_mask_valid(config->source_mask) ||
        config->max_records == 0u ||
        config->max_records > SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS ||
        config->timeout_us == 0u ||
        config->overwrite_oldest > 1u ||
        config->expected_profile_generation == 0u ||
        config->expected_persona_generation == 0u ||
        config->debug_continue_budget >
            SYNC_IO_LOGIC_ANALYZER_DEBUG_CONTINUE_LIMIT) {
        return false;
    }

    if (config->mode == SYNC_IO_LOGIC_ANALYZER_MODE_EDGE_TIMESTAMP) {
        if (config->sample_period_ns != 0u) {
            return false;
        }
    } else if (config->sample_period_ns == 0u) {
        return false;
    }

    const bool triggered =
        config->mode == SYNC_IO_LOGIC_ANALYZER_MODE_TRIGGERED_CAPTURE;
    if (!triggered) {
        return config->trigger.type == SYNC_IO_LOGIC_ANALYZER_TRIGGER_NONE &&
               config->pre_trigger_records == 0u &&
               config->post_trigger_records == 0u;
    }

    if (config->trigger.type <= SYNC_IO_LOGIC_ANALYZER_TRIGGER_NONE ||
        config->trigger.type >= SYNC_IO_LOGIC_ANALYZER_TRIGGER_COUNT ||
        config->pre_trigger_records > config->max_records ||
        config->post_trigger_records >
            config->max_records - config->pre_trigger_records ||
        config->trigger.source_mask == 0u ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.source_mask,
                                           config->source_mask) ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.level_mask,
                                           config->trigger.source_mask) ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.edge_mask,
                                           config->trigger.source_mask) ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.pattern_mask,
                                           config->trigger.source_mask) ||
        !sync_io_logic_analyzer_mask_valid(config->trigger.pattern_value,
                                           config->trigger.pattern_mask)) {
        return false;
    }

    if (config->trigger.type == SYNC_IO_LOGIC_ANALYZER_TRIGGER_LEVEL) {
        return config->trigger.level_mask != 0u;
    }
    if (config->trigger.type == SYNC_IO_LOGIC_ANALYZER_TRIGGER_EDGE) {
        return config->trigger.edge_mask != 0u;
    }
    return config->trigger.pattern_mask != 0u;
}

sync_io_logic_analyzer_gate_action_t sync_io_logic_analyzer_gate_action(
    sync_io_logic_analyzer_gate_reason_t reason,
    bool product_mode,
    uint32_t debug_continue_count,
    uint32_t debug_continue_budget)
{
    if (reason <= SYNC_IO_LOGIC_ANALYZER_GATE_NONE ||
        reason >= SYNC_IO_LOGIC_ANALYZER_GATE_COUNT) {
        return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_REJECT;
    }
    const bool hard_stop =
        reason == SYNC_IO_LOGIC_ANALYZER_GATE_DMA_BOUNDS ||
        reason == SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_MEMORY ||
        reason == SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_FLASH ||
        reason == SYNC_IO_LOGIC_ANALYZER_GATE_UNCONTROLLED_GPIO;
    if (hard_stop) {
        return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_HARD_STOP;
    }
    if (product_mode) {
        return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_REJECT;
    }
    if (debug_continue_count >= debug_continue_budget) {
        return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_ROUND_END;
    }
    return SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_CONTINUE;
}

bool sync_io_logic_analyzer_snapshot_read(
    const sync_io_logic_analyzer_snapshot_t *source,
    sync_io_logic_analyzer_snapshot_payload_t *snapshot)
{
    if (source == NULL || snapshot == NULL) {
        return false;
    }

    for (uint32_t attempt = 0u;
         attempt < SYNC_IO_LOGIC_ANALYZER_SNAPSHOT_READ_ATTEMPTS;
         attempt++) {
        const uint32_t before =
            __atomic_load_n(&source->sequence_lock, __ATOMIC_ACQUIRE);
        if ((before & 1u) != 0u) {
            continue;
        }
        memcpy(snapshot, &source->payload, sizeof(*snapshot));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t after =
            __atomic_load_n(&source->sequence_lock, __ATOMIC_RELAXED);
        if (before == after && (after & 1u) == 0u) {
            return true;
        }
    }
    return false;
}
