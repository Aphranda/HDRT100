#include "trigger_sequence_config.h"

#include <stddef.h>
#include <string.h>

#include "pota_types.h"

static uint32_t crc_word(uint32_t crc, uint32_t word)
{
    const uint8_t bytes[] = {
        (uint8_t)word, (uint8_t)(word >> 8),
        (uint8_t)(word >> 16), (uint8_t)(word >> 24),
    };
    return pota_crc32_update(crc, bytes, sizeof(bytes));
}

static bool normalize_id(const char *id, char *normalized)
{
    if (id == NULL) return false;
    for (uint32_t i = 0u; i <= TRIGGER_SEQUENCE_PLAN_ID_MAX; ++i) {
        const unsigned char c = (unsigned char)id[i];
        if (c == '\0') {
            normalized[i] = '\0';
            return i != 0u;
        }
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (i == TRIGGER_SEQUENCE_PLAN_ID_MAX ||
            !(letter || c == '_' || (i != 0u && c >= '0' && c <= '9'))) {
            return false;
        }
        normalized[i] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    return false;
}

static trigger_sequence_result_t parameter_count(
    const trigger_sequence_params_t *params, uint32_t *count)
{
    if (params->chan_count == 0u ||
        params->chan_count > TRIGGER_SEQUENCE_CHANNEL_MAX || params->pol > 2u ||
        params->freq_count == 0u || params->wave_count == 0u) {
        return TRIGGER_SEQUENCE_INVALID_PARAMETERS;
    }
    uint32_t total = params->chan_count * (params->pol == 2u ? 2u : 1u);
    if (params->freq_count > TRIGGER_SEQUENCE_STATE_MAX / total) {
        return TRIGGER_SEQUENCE_CAPACITY;
    }
    total *= params->freq_count;
    if (params->wave_count > TRIGGER_SEQUENCE_STATE_MAX / total) {
        return TRIGGER_SEQUENCE_CAPACITY;
    }
    *count = total * params->wave_count;
    return TRIGGER_SEQUENCE_OK;
}

static uint32_t parameter_crc(const trigger_sequence_params_t *params)
{
    uint32_t crc = crc_word(0u, params->chan_count);
    crc = crc_word(crc, params->pol);
    crc = crc_word(crc, params->freq_count);
    return crc_word(crc, params->wave_count);
}

static trigger_sequence_state_t expand_state(
    const trigger_sequence_params_t *params, uint32_t id)
{
    trigger_sequence_state_t state = {0};
    state.state_id = id;
    state.wave_idx = id % params->wave_count;
    id /= params->wave_count;
    state.freq_idx = id % params->freq_count;
    id /= params->freq_count;
    const uint32_t pol_count = params->pol == 2u ? 2u : 1u;
    state.pol = params->pol == 2u ? id % pol_count : params->pol;
    state.switch2_sel = state.pol;
    state.switch1_ch = id / pol_count + 1u;
    return state;
}

static uint32_t mapping_crc(const trigger_sequence_params_t *params, uint32_t count)
{
    uint32_t crc = 0u;
    for (uint32_t id = 0u; id < count; ++id) {
        const trigger_sequence_state_t row = expand_state(params, id);
        crc = crc_word(crc, row.state_id);
        crc = crc_word(crc, row.switch1_ch);
        crc = crc_word(crc, row.switch2_sel);
        crc = crc_word(crc, row.pol);
        crc = crc_word(crc, row.freq_idx);
        crc = crc_word(crc, row.wave_idx);
    }
    return crc;
}

static uint32_t plan_crc(const trigger_sequence_store_t *store,
                         const uint32_t *ids, uint32_t count)
{
    uint32_t crc = crc_word(0u, store->parameter_crc);
    crc = crc_word(crc, store->map_crc);
    crc = crc_word(crc, count);
    for (uint32_t i = 0u; i < count; ++i) crc = crc_word(crc, ids[i]);
    return crc;
}

static uint32_t find_slot(const trigger_sequence_store_t *store, const char *id)
{
    if (id == NULL) {
        return store->active_slot < TRIGGER_SEQUENCE_PLAN_MAX &&
               store->plans[store->active_slot].used
            ? store->active_slot : TRIGGER_SEQUENCE_NO_ACTIVE;
    }
    char normalized[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u];
    if (!normalize_id(id, normalized)) return TRIGGER_SEQUENCE_NO_ACTIVE;
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_PLAN_MAX; ++i) {
        if (store->plans[i].used && strcmp(store->plans[i].id, normalized) == 0) {
            return i;
        }
    }
    return TRIGGER_SEQUENCE_NO_ACTIVE;
}

void trigger_sequence_init(trigger_sequence_store_t *store)
{
    if (store == NULL) return;
    memset(store, 0, sizeof(*store));
    store->active_slot = TRIGGER_SEQUENCE_NO_ACTIVE;
}

trigger_sequence_result_t trigger_sequence_set_frozen(
    trigger_sequence_store_t *store, bool frozen)
{
    if (store == NULL) return TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    store->frozen = frozen;
    return TRIGGER_SEQUENCE_OK;
}

trigger_sequence_result_t trigger_sequence_configure(
    trigger_sequence_store_t *store, const trigger_sequence_params_t *params)
{
    if (store == NULL || params == NULL) return TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    if (store->frozen) return TRIGGER_SEQUENCE_FROZEN;
    uint32_t count = 0u;
    const trigger_sequence_result_t result = parameter_count(params, &count);
    if (result != TRIGGER_SEQUENCE_OK) return result;
    if (store->generation == UINT32_MAX) return TRIGGER_SEQUENCE_GENERATION_EXHAUSTED;
    const trigger_sequence_params_t next = *params;
    const uint32_t next_parameter_crc = parameter_crc(&next);
    const uint32_t next_map_crc = mapping_crc(&next, count);
    store->params = next;
    store->state_count = count;
    store->parameter_crc = next_parameter_crc;
    store->map_crc = next_map_crc;
    ++store->generation;
    store->configured = true;
    return TRIGGER_SEQUENCE_OK;
}

trigger_sequence_result_t trigger_sequence_write_plan(
    trigger_sequence_store_t *store, const char *id,
    const uint32_t *state_ids, uint32_t count)
{
    if (store == NULL || state_ids == NULL) return TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    if (store->frozen) return TRIGGER_SEQUENCE_FROZEN;
    if (!store->configured) return TRIGGER_SEQUENCE_NOT_CONFIGURED;
    char normalized[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u] = {0};
    if (!normalize_id(id, normalized)) return TRIGGER_SEQUENCE_INVALID_PLAN_ID;
    if (count == 0u || count > TRIGGER_SEQUENCE_STATE_MAX) return TRIGGER_SEQUENCE_CAPACITY;
    for (uint32_t i = 0u; i < count; ++i) {
        if (state_ids[i] >= store->state_count) return TRIGGER_SEQUENCE_STATE_RANGE;
    }
    uint32_t slot = find_slot(store, normalized);
    if (slot == TRIGGER_SEQUENCE_NO_ACTIVE) {
        for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_PLAN_MAX; ++i) {
            if (!store->plans[i].used) { slot = i; break; }
        }
    }
    if (slot == TRIGGER_SEQUENCE_NO_ACTIVE) return TRIGGER_SEQUENCE_CAPACITY;
    const uint32_t crc = plan_crc(store, state_ids, count);
    trigger_sequence_plan_t *plan = &store->plans[slot];
    /* Input may be this plan's own retained sequence (including a subrange). */
    memmove(plan->state_ids, state_ids, count * sizeof(*state_ids));
    memset(plan->state_ids + count, 0,
           (TRIGGER_SEQUENCE_STATE_MAX - count) * sizeof(*state_ids));
    memcpy(plan->id, normalized, sizeof(plan->id));
    plan->generation = store->generation;
    plan->count = count;
    plan->crc = crc;
    plan->used = true;
    if (store->active_slot == slot) store->active_slot = TRIGGER_SEQUENCE_NO_ACTIVE;
    return TRIGGER_SEQUENCE_OK;
}

const trigger_sequence_plan_t *trigger_sequence_get_plan(
    const trigger_sequence_store_t *store, const char *id)
{
    if (store == NULL) return NULL;
    const uint32_t slot = find_slot(store, id);
    return slot < TRIGGER_SEQUENCE_PLAN_MAX ? &store->plans[slot] : NULL;
}

trigger_sequence_result_t trigger_sequence_map_state(
    const trigger_sequence_store_t *store, uint32_t state_id,
    trigger_sequence_state_t *state)
{
    if (store == NULL || state == NULL) return TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    if (!store->configured) return TRIGGER_SEQUENCE_NOT_CONFIGURED;
    if (state_id >= store->state_count) return TRIGGER_SEQUENCE_STATE_RANGE;
    *state = expand_state(&store->params, state_id);
    return TRIGGER_SEQUENCE_OK;
}

trigger_sequence_result_t trigger_sequence_check(
    const trigger_sequence_store_t *store, const char *id,
    trigger_sequence_check_t *check)
{
    if (check == NULL) return TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    memset(check, 0, sizeof(*check));
    check->result = TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    if (store == NULL) return check->result;
    if (!store->configured) return check->result = TRIGGER_SEQUENCE_NOT_CONFIGURED;
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(store, id);
    if (plan == NULL) return check->result = TRIGGER_SEQUENCE_PLAN_NOT_FOUND;
    check->state_count = plan->count;
    check->sequence_crc = plan->crc;
    if (plan->generation != store->generation) return check->result = TRIGGER_SEQUENCE_STALE_PLAN;
    if (plan->count == 0u || plan->count > TRIGGER_SEQUENCE_STATE_MAX) {
        return check->result = TRIGGER_SEQUENCE_CAPACITY;
    }
    uint32_t count = 0u;
    if (parameter_count(&store->params, &count) != TRIGGER_SEQUENCE_OK ||
        count != store->state_count) {
        return check->result = TRIGGER_SEQUENCE_INVALID_PARAMETERS;
    }
    check->map_crc_ok = parameter_crc(&store->params) == store->parameter_crc &&
                       mapping_crc(&store->params, count) == store->map_crc;
    check->state_range_ok = true;
    check->switch_range_ok = true;
    check->pol_range_ok = true;
    check->freq_range_ok = true;
    check->wave_range_ok = true;
    bool seen[TRIGGER_SEQUENCE_STATE_MAX] = {false};
    uint32_t distinct = 0u;
    for (uint32_t i = 0u; i < plan->count; ++i) {
        const uint32_t state_id = plan->state_ids[i];
        if (state_id >= count) { check->state_range_ok = false; continue; }
        if (seen[state_id]) check->duplicate_state = true;
        else { seen[state_id] = true; ++distinct; }
        const trigger_sequence_state_t state = expand_state(&store->params, state_id);
        check->switch_range_ok = check->switch_range_ok && state.switch1_ch > 0u &&
            state.switch1_ch <= TRIGGER_SEQUENCE_CHANNEL_MAX && state.switch2_sel <= 1u;
        check->pol_range_ok = check->pol_range_ok && state.pol <= 1u;
        check->freq_range_ok = check->freq_range_ok && state.freq_idx < store->params.freq_count;
        check->wave_range_ok = check->wave_range_ok && state.wave_idx < store->params.wave_count;
    }
    check->missing_state = distinct != count;
    if (!check->state_range_ok) return check->result = TRIGGER_SEQUENCE_STATE_RANGE;
    if (!check->map_crc_ok || plan_crc(store, plan->state_ids, plan->count) != plan->crc) {
        return check->result = TRIGGER_SEQUENCE_CRC_MISMATCH;
    }
    check->valid = true;
    return check->result = TRIGGER_SEQUENCE_OK;
}

trigger_sequence_result_t trigger_sequence_activate(
    trigger_sequence_store_t *store, const char *id)
{
    if (store == NULL || id == NULL) return TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    if (store->frozen) return TRIGGER_SEQUENCE_FROZEN;
    trigger_sequence_check_t check;
    const trigger_sequence_result_t result = trigger_sequence_check(store, id, &check);
    if (result != TRIGGER_SEQUENCE_OK) return result;
    store->active_slot = find_slot(store, id);
    return TRIGGER_SEQUENCE_OK;
}

trigger_sequence_result_t trigger_sequence_get_active(
    const trigger_sequence_store_t *store, trigger_sequence_active_t *active)
{
    if (active == NULL) return TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    memset(active, 0, sizeof(*active));
    active->result = TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    if (store == NULL) return active->result;
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(store, NULL);
    if (plan == NULL) return active->result = TRIGGER_SEQUENCE_NO_ACTIVE_PLAN;
    memcpy(active->id, plan->id, sizeof(active->id));
    active->generation = plan->generation;
    active->crc = plan->crc;
    active->count = plan->count;
    trigger_sequence_check_t check;
    active->result = trigger_sequence_check(store, plan->id, &check);
    active->valid = check.valid;
    return active->result;
}

trigger_sequence_result_t trigger_sequence_get_entry(
    const trigger_sequence_store_t *store, const char *id,
    uint32_t index, trigger_sequence_state_t *state)
{
    if (store == NULL || state == NULL) return TRIGGER_SEQUENCE_INVALID_ARGUMENT;
    trigger_sequence_check_t check;
    const trigger_sequence_result_t result = trigger_sequence_check(store, id, &check);
    if (result != TRIGGER_SEQUENCE_OK) return result;
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(store, id);
    if (index >= plan->count) return TRIGGER_SEQUENCE_INDEX_RANGE;
    return trigger_sequence_map_state(store, plan->state_ids[index], state);
}

const char *trigger_sequence_result_name(trigger_sequence_result_t result)
{
    static const char *const names[] = {
        "NONE", "INVALID_ARGUMENT", "NOT_CONFIGURED", "INVALID_PARAMETERS",
        "CAPACITY", "INVALID_PLAN_ID", "PLAN_NOT_FOUND", "STATE_RANGE",
        "INDEX_RANGE", "STALE_GENERATION", "CRC_MISMATCH", "NO_ACTIVE_PLAN",
        "CONFIG_FROZEN", "GENERATION_EXHAUSTED",
    };
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "UNKNOWN";
}
