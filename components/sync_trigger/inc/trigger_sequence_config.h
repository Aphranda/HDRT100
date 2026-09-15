#ifndef TRIGGER_SEQUENCE_CONFIG_H
#define TRIGGER_SEQUENCE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define TRIGGER_SEQUENCE_STATE_MAX 256u
#define TRIGGER_SEQUENCE_PLAN_MAX 4u
#define TRIGGER_SEQUENCE_PLAN_ID_MAX 31u
#define TRIGGER_SEQUENCE_CHANNEL_MAX 8u
#define TRIGGER_SEQUENCE_CRC_SCHEMA 1u
#define TRIGGER_SEQUENCE_NO_ACTIVE UINT32_MAX

typedef enum {
    TRIGGER_SEQUENCE_OK = 0,
    TRIGGER_SEQUENCE_INVALID_ARGUMENT,
    TRIGGER_SEQUENCE_NOT_CONFIGURED,
    TRIGGER_SEQUENCE_INVALID_PARAMETERS,
    TRIGGER_SEQUENCE_CAPACITY,
    TRIGGER_SEQUENCE_INVALID_PLAN_ID,
    TRIGGER_SEQUENCE_PLAN_NOT_FOUND,
    TRIGGER_SEQUENCE_STATE_RANGE,
    TRIGGER_SEQUENCE_INDEX_RANGE,
    TRIGGER_SEQUENCE_STALE_PLAN,
    TRIGGER_SEQUENCE_CRC_MISMATCH,
    TRIGGER_SEQUENCE_NO_ACTIVE_PLAN,
    TRIGGER_SEQUENCE_FROZEN,
    TRIGGER_SEQUENCE_GENERATION_EXHAUSTED,
} trigger_sequence_result_t;

typedef struct {
    uint32_t chan_count;
    uint32_t pol;
    uint32_t freq_count;
    uint32_t wave_count;
} trigger_sequence_params_t;

typedef struct {
    uint32_t state_id;
    uint32_t switch1_ch;
    uint32_t switch2_sel;
    uint32_t pol;
    uint32_t freq_idx;
    uint32_t wave_idx;
} trigger_sequence_state_t;

typedef struct {
    char id[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u];
    uint32_t generation;
    uint32_t count;
    uint32_t crc;
    uint32_t state_ids[TRIGGER_SEQUENCE_STATE_MAX];
    bool used;
} trigger_sequence_plan_t;

typedef struct {
    trigger_sequence_params_t params;
    uint32_t generation;
    uint32_t state_count;
    uint32_t parameter_crc;
    uint32_t map_crc;
    uint32_t active_slot;
    bool configured;
    bool frozen;
    trigger_sequence_plan_t plans[TRIGGER_SEQUENCE_PLAN_MAX];
} trigger_sequence_store_t;

typedef struct {
    uint32_t state_count;
    uint32_t sequence_crc;
    bool state_range_ok;
    bool map_crc_ok;
    bool switch_range_ok;
    bool pol_range_ok;
    bool freq_range_ok;
    bool wave_range_ok;
    bool duplicate_state;
    bool missing_state;
    bool valid;
    trigger_sequence_result_t result;
} trigger_sequence_check_t;

typedef struct {
    char id[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u];
    uint32_t generation;
    uint32_t crc;
    uint32_t count;
    bool valid;
    trigger_sequence_result_t result;
} trigger_sequence_active_t;

/* Core0 single-owner model. No locking or cross-core publication is provided.
 * Call init before use (zero initialization alone is not the lifecycle API);
 * the owner alone changes the freeze flag.
 * IDs are ASCII identifiers, case-insensitive, stored in uppercase.
 * CRC schema 1 uses LE32 words: parameters=(chan,pol,freq,wave);
 * map=all state rows in struct field order; sequence=(parameter_crc,map_crc,
 * count,state_ids...). Neither C padding nor names/generations enter a CRC. */
void trigger_sequence_init(trigger_sequence_store_t *store);
trigger_sequence_result_t trigger_sequence_set_frozen(
    trigger_sequence_store_t *store, bool frozen);
trigger_sequence_result_t trigger_sequence_configure(
    trigger_sequence_store_t *store, const trigger_sequence_params_t *params);
trigger_sequence_result_t trigger_sequence_write_plan(
    trigger_sequence_store_t *store, const char *id,
    const uint32_t *state_ids, uint32_t count);
const trigger_sequence_plan_t *trigger_sequence_get_plan(
    const trigger_sequence_store_t *store, const char *id);
/* get_plan returns a borrowed pointer confined to this owner. Reacquire after
 * any mutation and never publish it across cores. NULL id selects the active
 * plan for get_plan/check/get_entry; stale plans remain inspectable by get_plan.
 * Query output objects must not overlap the store. */
trigger_sequence_result_t trigger_sequence_check(
    const trigger_sequence_store_t *store, const char *id,
    trigger_sequence_check_t *check);
trigger_sequence_result_t trigger_sequence_activate(
    trigger_sequence_store_t *store, const char *id);
trigger_sequence_result_t trigger_sequence_get_active(
    const trigger_sequence_store_t *store, trigger_sequence_active_t *active);
trigger_sequence_result_t trigger_sequence_map_state(
    const trigger_sequence_store_t *store, uint32_t state_id,
    trigger_sequence_state_t *state);
trigger_sequence_result_t trigger_sequence_get_entry(
    const trigger_sequence_store_t *store, const char *id,
    uint32_t index, trigger_sequence_state_t *state);
const char *trigger_sequence_result_name(trigger_sequence_result_t result);

#endif
