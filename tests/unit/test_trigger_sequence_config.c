#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trigger_sequence_config.h"

static trigger_sequence_store_t store, before;

#define UNCHANGED(call, expected) do { \
    before = store; \
    assert((call) == (expected)); \
    assert(memcmp(&before, &store, sizeof(store)) == 0); \
} while (0)

static void test_lifecycle(void)
{
    trigger_sequence_init(NULL);
    memset(&store, 0, sizeof(store));
    trigger_sequence_active_t zero_active;
    assert(trigger_sequence_get_plan(&store, NULL) == NULL);
    assert(trigger_sequence_get_active(&store, &zero_active) == TRIGGER_SEQUENCE_NO_ACTIVE_PLAN);
    trigger_sequence_init(&store);
    const trigger_sequence_params_t params = {8u, 2u, 5u, 1u};
    const uint32_t ids[] = {0u, 1u, 2u, 3u, 4u, 5u};
    trigger_sequence_check_t check;
    trigger_sequence_active_t active;
    trigger_sequence_state_t state;
    assert(store.active_slot == TRIGGER_SEQUENCE_NO_ACTIVE && !store.configured);
    assert(trigger_sequence_get_plan(&store, NULL) == NULL);
    assert(trigger_sequence_get_active(&store, &active) == TRIGGER_SEQUENCE_NO_ACTIVE_PLAN);
    UNCHANGED(trigger_sequence_write_plan(&store, "A", ids, 6u), TRIGGER_SEQUENCE_NOT_CONFIGURED);
    UNCHANGED(trigger_sequence_check(&store, "A", &check), TRIGGER_SEQUENCE_NOT_CONFIGURED);
    UNCHANGED(trigger_sequence_activate(&store, "A"), TRIGGER_SEQUENCE_NOT_CONFIGURED);
    assert(trigger_sequence_map_state(&store, 0u, &state) == TRIGGER_SEQUENCE_NOT_CONFIGURED);
    assert(trigger_sequence_configure(&store, &params) == TRIGGER_SEQUENCE_OK);
    assert(store.generation == 1u && store.state_count == 80u);
    assert(trigger_sequence_write_plan(&store, "Plan_a", ids, 6u) == TRIGGER_SEQUENCE_OK);
    assert(strcmp(trigger_sequence_get_plan(&store, "plan_A")->id, "PLAN_A") == 0);
    assert(trigger_sequence_get_active(&store, &active) == TRIGGER_SEQUENCE_NO_ACTIVE_PLAN);
    UNCHANGED(trigger_sequence_check(&store, "Plan_A", &check), TRIGGER_SEQUENCE_OK);
    assert(check.valid && check.missing_state && !check.duplicate_state);
    assert(check.state_range_ok && check.map_crc_ok && check.switch_range_ok &&
           check.pol_range_ok && check.freq_range_ok && check.wave_range_ok);
    assert(trigger_sequence_activate(&store, "plan_a") == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_get_active(&store, &active) == TRIGGER_SEQUENCE_OK && active.valid);
    assert(active.count == 6u && active.generation == 1u);
    UNCHANGED(trigger_sequence_activate(&store, "MISSING"), TRIGGER_SEQUENCE_PLAN_NOT_FOUND);
    UNCHANGED(trigger_sequence_check(&store, "MISSING", &check), TRIGGER_SEQUENCE_PLAN_NOT_FOUND);
    assert(trigger_sequence_get_entry(&store, NULL, 5u, &state) == TRIGGER_SEQUENCE_OK);
    assert(state.state_id == 5u && state.switch1_ch == 1u && state.pol == 1u && state.freq_idx == 0u);
    assert(trigger_sequence_get_entry(&store, NULL, 6u, &state) == TRIGGER_SEQUENCE_INDEX_RANGE);
    assert(trigger_sequence_map_state(&store, 80u, &state) == TRIGGER_SEQUENCE_STATE_RANGE);

    const uint32_t repeats[] = {7u, 7u, 3u};
    assert(trigger_sequence_write_plan(&store, "B", repeats, 3u) == TRIGGER_SEQUENCE_OK);
    UNCHANGED(trigger_sequence_check(&store, "B", &check), TRIGGER_SEQUENCE_OK);
    assert(check.duplicate_state && check.missing_state && check.valid);
    assert(strcmp(trigger_sequence_get_plan(&store, NULL)->id, "PLAN_A") == 0);
    const trigger_sequence_params_t rejected = {8u, 2u, UINT32_MAX, 1u};
    UNCHANGED(trigger_sequence_configure(&store, &rejected), TRIGGER_SEQUENCE_CAPACITY);
    const trigger_sequence_plan_t retained_b = *trigger_sequence_get_plan(&store, "B");
    assert(trigger_sequence_write_plan(&store, "plan_a", ids, 6u) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_get_active(&store, &active) == TRIGGER_SEQUENCE_NO_ACTIVE_PLAN);
    assert(memcmp(&retained_b, trigger_sequence_get_plan(&store, "B"), sizeof(retained_b)) == 0);
    assert(trigger_sequence_activate(&store, "PLAN_A") == TRIGGER_SEQUENCE_OK);

    before = store;
    assert(trigger_sequence_configure(&store, &params) == TRIGGER_SEQUENCE_OK);
    assert(store.generation == 2u && store.parameter_crc == before.parameter_crc);
    assert(memcmp(store.plans, before.plans, sizeof(store.plans)) == 0);
    UNCHANGED(trigger_sequence_check(&store, "PLAN_A", &check), TRIGGER_SEQUENCE_STALE_PLAN);
    UNCHANGED(trigger_sequence_activate(&store, "PLAN_A"), TRIGGER_SEQUENCE_STALE_PLAN);
    assert(trigger_sequence_get_entry(&store, "B", 0u, &state) == TRIGGER_SEQUENCE_STALE_PLAN);
    assert(trigger_sequence_get_active(&store, &active) == TRIGGER_SEQUENCE_STALE_PLAN);
    assert(!active.valid && active.generation == 1u && strcmp(active.id, "PLAN_A") == 0);
    const trigger_sequence_plan_t *alias = trigger_sequence_get_plan(&store, "PLAN_A");
    assert(trigger_sequence_write_plan(&store, alias->id, alias->state_ids + 1u, 5u) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_get_plan(&store, "PLAN_A")->state_ids[0] == 1u);
    assert(trigger_sequence_get_active(&store, &active) == TRIGGER_SEQUENCE_NO_ACTIVE_PLAN);
    assert(trigger_sequence_activate(&store, "PLAN_A") == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_set_frozen(&store, true) == TRIGGER_SEQUENCE_OK);
    UNCHANGED(trigger_sequence_configure(&store, &params), TRIGGER_SEQUENCE_FROZEN);
    UNCHANGED(trigger_sequence_write_plan(&store, "PLAN_A", ids, 6u), TRIGGER_SEQUENCE_FROZEN);
    UNCHANGED(trigger_sequence_activate(&store, "B"), TRIGGER_SEQUENCE_FROZEN);
    UNCHANGED(trigger_sequence_check(&store, NULL, &check), TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_get_active(&store, &active) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_set_frozen(&store, false) == TRIGGER_SEQUENCE_OK);
    store.generation = UINT32_MAX;
    UNCHANGED(trigger_sequence_configure(&store, &params), TRIGGER_SEQUENCE_GENERATION_EXHAUSTED);
}

static void test_bounds(void)
{
    trigger_sequence_init(&store);
    const trigger_sequence_params_t params = {8u, 2u, 8u, 2u};
    const trigger_sequence_params_t invalid[] = {
        {0u, 0u, 1u, 1u}, {9u, 0u, 1u, 1u}, {1u, 3u, 1u, 1u},
        {1u, 0u, 0u, 1u}, {1u, 0u, 1u, 0u},
    };
    for (unsigned i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        UNCHANGED(trigger_sequence_configure(&store, &invalid[i]), TRIGGER_SEQUENCE_INVALID_PARAMETERS);
    }
    const trigger_sequence_params_t too_large[] = {
        {8u, 2u, 8u, 3u}, {1u, 0u, 257u, 1u},
        {8u, 2u, UINT32_MAX, UINT32_MAX}, {1u, 0u, 1u, UINT32_MAX},
    };
    for (unsigned i = 0u; i < sizeof(too_large) / sizeof(too_large[0]); ++i) {
        UNCHANGED(trigger_sequence_configure(&store, &too_large[i]), TRIGGER_SEQUENCE_CAPACITY);
    }
    assert(trigger_sequence_configure(&store, &params) == TRIGGER_SEQUENCE_OK);
    uint32_t ids[TRIGGER_SEQUENCE_STATE_MAX];
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_STATE_MAX; ++i) ids[i] = i;
    const char *bad_names[] = {"", "0A", "A-B", "A B", "A.B", "A\x80", "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456"};
    for (unsigned i = 0u; i < sizeof(bad_names) / sizeof(bad_names[0]); ++i) {
        UNCHANGED(trigger_sequence_write_plan(&store, bad_names[i], ids, 1u), TRIGGER_SEQUENCE_INVALID_PLAN_ID);
    }
    UNCHANGED(trigger_sequence_write_plan(&store, NULL, ids, 1u), TRIGGER_SEQUENCE_INVALID_PLAN_ID);
    UNCHANGED(trigger_sequence_write_plan(&store, "A", NULL, 1u), TRIGGER_SEQUENCE_INVALID_ARGUMENT);
    UNCHANGED(trigger_sequence_write_plan(&store, "A", ids, 0u), TRIGGER_SEQUENCE_CAPACITY);
    UNCHANGED(trigger_sequence_write_plan(&store, "A", ids, 257u), TRIGGER_SEQUENCE_CAPACITY);
    UNCHANGED(trigger_sequence_write_plan(&store, "A", ids, UINT32_MAX), TRIGGER_SEQUENCE_CAPACITY);
    assert(trigger_sequence_write_plan(&store, "ABCDEFGHIJKLMNOPQRSTUVWXYZ12345", ids, 256u) == TRIGGER_SEQUENCE_OK);
    trigger_sequence_check_t check;
    UNCHANGED(trigger_sequence_check(&store, "ABCDEFGHIJKLMNOPQRSTUVWXYZ12345", &check), TRIGGER_SEQUENCE_OK);
    assert(check.valid && !check.duplicate_state && !check.missing_state);
    assert(trigger_sequence_activate(&store, "ABCDEFGHIJKLMNOPQRSTUVWXYZ12345") == TRIGGER_SEQUENCE_OK);
    for (uint32_t position = 0u; position < 256u; ++position) {
        ids[position] = 256u;
        UNCHANGED(trigger_sequence_write_plan(&store, "ABCDEFGHIJKLMNOPQRSTUVWXYZ12345", ids, 256u), TRIGGER_SEQUENCE_STATE_RANGE);
        ids[position] = position;
    }
    assert(trigger_sequence_write_plan(&store, "_", ids, 1u) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_write_plan(&store, "C", ids, 1u) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_write_plan(&store, "D", ids, 1u) == TRIGGER_SEQUENCE_OK);
    UNCHANGED(trigger_sequence_write_plan(&store, "E", ids, 1u), TRIGGER_SEQUENCE_CAPACITY);
    assert(trigger_sequence_write_plan(&store, "c", ids, 2u) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_get_plan(&store, "C")->count == 2u);
    assert(trigger_sequence_get_plan(&store, "D")->count == 1u);
    UNCHANGED(trigger_sequence_configure(&store, NULL), TRIGGER_SEQUENCE_INVALID_ARGUMENT);
    assert(trigger_sequence_set_frozen(NULL, true) == TRIGGER_SEQUENCE_INVALID_ARGUMENT);
    assert(trigger_sequence_configure(NULL, &params) == TRIGGER_SEQUENCE_INVALID_ARGUMENT);
    assert(trigger_sequence_check(&store, "C", NULL) == TRIGGER_SEQUENCE_INVALID_ARGUMENT);
    assert(trigger_sequence_check(NULL, "C", &check) == TRIGGER_SEQUENCE_INVALID_ARGUMENT);
    assert(!check.valid);

    const trigger_sequence_params_t minimum = {1u, 1u, 1u, 1u};
    assert(trigger_sequence_configure(&store, &minimum) == TRIGGER_SEQUENCE_OK);
    assert(store.state_count == 1u);
    UNCHANGED(trigger_sequence_activate(&store, "D"), TRIGGER_SEQUENCE_STALE_PLAN);
    assert(trigger_sequence_write_plan(&store, "D", ids, 1u) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_activate(&store, "D") == TRIGGER_SEQUENCE_OK);
    trigger_sequence_state_t state;
    assert(trigger_sequence_get_entry(&store, NULL, 0u, &state) == TRIGGER_SEQUENCE_OK);
    assert(state.switch1_ch == 1u && state.switch2_sel == 1u && state.pol == 1u);
}

static void test_integrity(void)
{
    trigger_sequence_init(&store);
    const trigger_sequence_params_t params = {1u, 0u, 2u, 1u};
    const uint32_t ids[] = {0u, 1u};
    assert(trigger_sequence_configure(&store, &params) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_write_plan(&store, "A", ids, 2u) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_write_plan(&store, "B", ids, 2u) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_get_plan(&store, "A")->crc == trigger_sequence_get_plan(&store, "B")->crc);
    const uint32_t reversed[] = {1u, 0u};
    assert(trigger_sequence_write_plan(&store, "B", reversed, 2u) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_get_plan(&store, "A")->crc != trigger_sequence_get_plan(&store, "B")->crc);
    assert(trigger_sequence_activate(&store, "A") == TRIGGER_SEQUENCE_OK);
    trigger_sequence_check_t check;
    trigger_sequence_active_t active;
    store.plans[0].crc ^= 1u;
    UNCHANGED(trigger_sequence_check(&store, "A", &check), TRIGGER_SEQUENCE_CRC_MISMATCH);
    assert(trigger_sequence_get_active(&store, &active) == TRIGGER_SEQUENCE_CRC_MISMATCH && !active.valid);
    store.plans[0].crc ^= 1u;
    store.map_crc ^= 1u;
    UNCHANGED(trigger_sequence_check(&store, "A", &check), TRIGGER_SEQUENCE_CRC_MISMATCH);
    assert(!check.map_crc_ok);
    store.map_crc ^= 1u;
    store.plans[0].state_ids[1] = 2u;
    UNCHANGED(trigger_sequence_check(&store, "A", &check), TRIGGER_SEQUENCE_STATE_RANGE);
    store.plans[0].state_ids[1] = 1u;
    store.plans[0].count = UINT32_MAX;
    UNCHANGED(trigger_sequence_check(&store, "A", &check), TRIGGER_SEQUENCE_CAPACITY);
    store.plans[0].count = 2u;
    store.params.wave_count = 0u;
    UNCHANGED(trigger_sequence_check(&store, "A", &check), TRIGGER_SEQUENCE_INVALID_PARAMETERS);
    assert(strcmp(trigger_sequence_result_name(TRIGGER_SEQUENCE_STALE_PLAN), "STALE_GENERATION") == 0);
    assert(strcmp(trigger_sequence_result_name((trigger_sequence_result_t)-1), "UNKNOWN") == 0);
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        test_lifecycle();
        test_bounds();
        test_integrity();
        puts("model lifecycle, bounds, freeze and atomicity passed");
        return 0;
    }
    assert(argc == 5);
    const trigger_sequence_params_t params = {
        (uint32_t)strtoul(argv[1], NULL, 10), (uint32_t)strtoul(argv[2], NULL, 10),
        (uint32_t)strtoul(argv[3], NULL, 10), (uint32_t)strtoul(argv[4], NULL, 10),
    };
    trigger_sequence_init(&store);
    assert(trigger_sequence_configure(&store, &params) == TRIGGER_SEQUENCE_OK);
    uint32_t ids[TRIGGER_SEQUENCE_STATE_MAX];
    for (uint32_t i = 0u; i < store.state_count; ++i) {
        trigger_sequence_state_t state;
        assert(trigger_sequence_map_state(&store, i, &state) == TRIGGER_SEQUENCE_OK);
        printf("MAP %u %u %u %u %u %u\n", state.state_id, state.switch1_ch,
               state.switch2_sel, state.pol, state.freq_idx, state.wave_idx);
        ids[i] = store.state_count - i - 1u;
    }
    assert(trigger_sequence_write_plan(&store, "oracle", ids, store.state_count) == TRIGGER_SEQUENCE_OK);
    printf("CRC %u %u %u\n", store.parameter_crc, store.map_crc,
           trigger_sequence_get_plan(&store, "ORACLE")->crc);
    return 0;
}
