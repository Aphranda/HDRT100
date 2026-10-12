#include "app_realtime_profile.h"
#include <stddef.h>
#include <string.h>

#define PROFILE_PHASE(name, start, end, wcet) \
    [APP_REALTIME_PHASE_##name] = {start, end, wcet},
static const app_realtime_phase_contract_t catalog_base[APP_REALTIME_PHASE_COUNT] = {
    APP_REALTIME_PHASE_TABLE(PROFILE_PHASE)
};
#undef PROFILE_PHASE

#define PROFILE_OVERRIDE_NONE 0xffffffffu
#define PROFILE_PHASE_COUNT ((uint32_t)APP_REALTIME_PHASE_COUNT)

/* Override double buffer. A writer always fills the slot that is not active, so
 * a core1 reader that sampled the active index and then copied the table can
 * never observe a half-written one. Core0 is the only writer (the SCPI task and
 * the boot restore), and both commit around a stopped realtime boundary. */
static app_realtime_profile_table_t s_override_slots[2];
static volatile uint32_t s_override_active = PROFILE_OVERRIDE_NONE;

static app_realtime_profile_table_t s_staging;
static bool s_staging_loaded;
static bool s_staging_dirty;

static bool catalog_supported(uint32_t cycle_cycles)
{
    return cycle_cycles == PROJECT_CORE1_PROFILE_1500US_CYCLES ||
        cycle_cycles == PROJECT_CORE1_PROFILE_5MS_CYCLES ||
        cycle_cycles == PROJECT_CORE1_PROFILE_10MS_CYCLES ||
        cycle_cycles == PROJECT_CORE1_PROFILE_15MS_CYCLES;
}

static void rejection_set(app_realtime_profile_rejection_t *rejection,
                          app_realtime_profile_reject_t code, uint32_t phase_id)
{
    if (rejection == NULL) {
        return;
    }
    rejection->code = code;
    rejection->phase_id = phase_id;
}

const char *app_realtime_profile_reject_name(app_realtime_profile_reject_t reject)
{
    switch (reject) {
    case APP_REALTIME_PROFILE_REJECT_OK: return "OK";
    case APP_REALTIME_PROFILE_REJECT_NULL: return "NO_TABLE";
    case APP_REALTIME_PROFILE_REJECT_VERSION: return "VERSION";
    case APP_REALTIME_PROFILE_REJECT_ORDER: return "ORDER";
    case APP_REALTIME_PROFILE_REJECT_EMPTY_PHASE: return "EMPTY_PHASE";
    case APP_REALTIME_PROFILE_REJECT_WCET: return "WCET";
    case APP_REALTIME_PROFILE_REJECT_GUARD_WCET: return "GUARD_WCET";
    case APP_REALTIME_PROFILE_REJECT_CYCLE_RANGE: return "CYCLE_RANGE";
    case APP_REALTIME_PROFILE_REJECT_CYCLE_TAIL: return "CYCLE_TAIL";
    case APP_REALTIME_PROFILE_REJECT_TDMA_FLOOR: return "TDMA_FLOOR";
    case APP_REALTIME_PROFILE_REJECT_DPLL_HEADROOM: return "DPLL_HEADROOM";
    case APP_REALTIME_PROFILE_REJECT_PERSIST: return "NO_PERSIST";
    case APP_REALTIME_PROFILE_REJECT_PUBLISH: return "NO_PUBLISH";
    default: return "UNKNOWN";
    }
}

bool app_realtime_profile_table_default(app_realtime_profile_table_t *table)
{
    if (table == NULL) {
        return false;
    }
    table->version = APP_REALTIME_PROFILE_TABLE_VERSION;
    table->cycle_cycles = PROJECT_CORE1_CYCLE_CYCLES;
    memcpy(table->phase, catalog_base, sizeof(table->phase));
    return true;
}

static uint32_t override_active_slot(void)
{
    return __atomic_load_n(&s_override_active, __ATOMIC_ACQUIRE);
}

bool app_realtime_profile_override_active(void)
{
    return override_active_slot() != PROFILE_OVERRIDE_NONE;
}

bool app_realtime_profile_table_effective(app_realtime_profile_table_t *table)
{
    if (table == NULL) {
        return false;
    }
    const uint32_t active = override_active_slot();
    if (active == PROFILE_OVERRIDE_NONE) {
        return app_realtime_profile_table_default(table);
    }
    *table = s_override_slots[active];
    return true;
}

bool app_realtime_profile_table_validate(const app_realtime_profile_table_t *table,
    app_realtime_profile_rejection_t *rejection)
{
    rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_OK, PROFILE_PHASE_COUNT);
    if (table == NULL) {
        rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_NULL,
                      PROFILE_PHASE_COUNT);
        return false;
    }
    if (table->version != APP_REALTIME_PROFILE_TABLE_VERSION) {
        rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_VERSION,
                      PROFILE_PHASE_COUNT);
        return false;
    }
    /* Every phase owns [start, end) and each window continues the previous one,
     * so the table covers the cycle without gaps or overlaps. */
    uint32_t expected_start = 0u;
    for (uint32_t i = 0u; i < PROFILE_PHASE_COUNT; ++i) {
        const app_realtime_phase_contract_t *phase = &table->phase[i];
        if (phase->start_cycle != expected_start) {
            rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_ORDER, i);
            return false;
        }
        if (phase->end_cycle <= phase->start_cycle) {
            rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_EMPTY_PHASE, i);
            return false;
        }
        if (phase->wcet_cycles > phase->end_cycle - phase->start_cycle) {
            rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_WCET, i);
            return false;
        }
        expected_start = phase->end_cycle;
    }
    if (table->phase[APP_REALTIME_PHASE_GUARD].wcet_cycles != 0u) {
        rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_GUARD_WCET,
                      (uint32_t)APP_REALTIME_PHASE_GUARD);
        return false;
    }
    if (table->cycle_cycles == 0u ||
        table->cycle_cycles > APP_REALTIME_PROFILE_CYCLE_MAX_CYCLES) {
        rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_CYCLE_RANGE,
                      PROFILE_PHASE_COUNT);
        return false;
    }
    if (expected_start != table->cycle_cycles) {
        rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_CYCLE_TAIL,
                      PROFILE_PHASE_COUNT);
        return false;
    }
    if (table->phase[APP_REALTIME_PHASE_TDMA].wcet_cycles <
        PROJECT_CORE1_PHASE_TDMA_WCET_FLOOR_CYCLES) {
        rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_TDMA_FLOOR,
                      (uint32_t)APP_REALTIME_PHASE_TDMA);
        return false;
    }
    const app_realtime_phase_contract_t *dpll = &table->phase[APP_REALTIME_PHASE_DPLL];
    if (dpll->end_cycle - dpll->start_cycle - dpll->wcet_cycles <
        APP_REALTIME_PROFILE_DPLL_HEADROOM_CYCLES) {
        rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_DPLL_HEADROOM,
                      (uint32_t)APP_REALTIME_PHASE_DPLL);
        return false;
    }
    return true;
}

static bool table_is_compiled_default(const app_realtime_profile_table_t *table)
{
    app_realtime_profile_table_t fallback;
    if (!app_realtime_profile_table_default(&fallback)) {
        return false;
    }
    return memcmp(table, &fallback, sizeof(fallback)) == 0;
}

bool app_realtime_profile_override_install(const app_realtime_profile_table_t *table,
    app_realtime_profile_rejection_t *rejection)
{
    if (!app_realtime_profile_table_validate(table, rejection)) {
        return false;
    }
    /* An override that is numerically the compiled default is the default:
     * clearing it keeps the catalog profiles reachable. */
    if (table_is_compiled_default(table)) {
        return app_realtime_profile_override_clear();
    }
    const uint32_t active = override_active_slot();
    const uint32_t slot = (active == PROFILE_OVERRIDE_NONE) ? 0u : (1u - active);
    s_override_slots[slot] = *table;
    __atomic_store_n(&s_override_active, slot, __ATOMIC_RELEASE);
    return true;
}

bool app_realtime_profile_override_clear(void)
{
    __atomic_store_n(&s_override_active, PROFILE_OVERRIDE_NONE, __ATOMIC_RELEASE);
    return true;
}

bool app_realtime_profile_supported(uint32_t cycle_cycles)
{
    const uint32_t active = override_active_slot();
    if (active != PROFILE_OVERRIDE_NONE) {
        return cycle_cycles == s_override_slots[active].cycle_cycles;
    }
    return catalog_supported(cycle_cycles);
}

bool app_realtime_profile_phase(uint32_t cycle_cycles,
    app_realtime_phase_id_t phase_id, app_realtime_phase_contract_t *phase)
{
    if (phase == NULL || (uint32_t)phase_id >= PROFILE_PHASE_COUNT) {
        return false;
    }
    const uint32_t active = override_active_slot();
    if (active != PROFILE_OVERRIDE_NONE) {
        const app_realtime_profile_table_t *table = &s_override_slots[active];
        if (cycle_cycles != table->cycle_cycles) {
            return false;
        }
        *phase = table->phase[phase_id];
        return true;
    }
    if (!catalog_supported(cycle_cycles)) {
        return false;
    }
    const uint32_t extra = cycle_cycles - PROJECT_CORE1_CYCLE_CYCLES;
    *phase = catalog_base[phase_id];
    if (phase_id == APP_REALTIME_PHASE_TDMA) {
        phase->wcet_cycles += extra;
    } else {
        phase->start_cycle += extra;
    }
    phase->end_cycle += extra;
    return true;
}

bool app_realtime_profile_install(app_realtime_schedule_snapshot_t *schedule,
    uint32_t cycle_cycles, uint32_t generation)
{
    if (schedule == NULL || !app_realtime_profile_supported(cycle_cycles)) {
        return false;
    }
    app_realtime_phase_contract_t phases[APP_REALTIME_PHASE_COUNT];
    for (uint32_t i = 0u; i < PROFILE_PHASE_COUNT; ++i) {
        if (!app_realtime_profile_phase(cycle_cycles,
                (app_realtime_phase_id_t)i, &phases[i])) {
            return false;
        }
    }
    for (uint32_t i = 0u; i < PROFILE_PHASE_COUNT; ++i) {
        schedule->phase_start_cycle[i] = phases[i].start_cycle;
        schedule->phase_end_cycle[i] = phases[i].end_cycle;
        schedule->phase_wcet_cycles[i] = phases[i].wcet_cycles;
    }
    schedule->cycle_cycles = cycle_cycles;
    schedule->profile_generation = generation;
    return true;
}

static void staging_sync_cycle(void)
{
    s_staging.cycle_cycles = s_staging.phase[APP_REALTIME_PHASE_COUNT - 1].end_cycle;
}

bool app_realtime_profile_stage_begin(void)
{
    if (!app_realtime_profile_table_effective(&s_staging)) {
        return false;
    }
    s_staging_loaded = true;
    s_staging_dirty = false;
    return true;
}

bool app_realtime_profile_stage_from_schedule(
    const app_realtime_schedule_snapshot_t *schedule)
{
    if (schedule == NULL || schedule->phase_count != PROFILE_PHASE_COUNT) {
        return false;
    }
    app_realtime_profile_table_t table = {
        .version = APP_REALTIME_PROFILE_TABLE_VERSION,
        .cycle_cycles = schedule->cycle_cycles};
    for (uint32_t i = 0u; i < PROFILE_PHASE_COUNT; ++i) {
        table.phase[i] = (app_realtime_phase_contract_t){
            schedule->phase_start_cycle[i], schedule->phase_end_cycle[i],
            schedule->phase_wcet_cycles[i]};
    }
    if (!app_realtime_profile_table_validate(&table, NULL)) {
        return false;
    }
    s_staging = table;
    s_staging_loaded = true;
    s_staging_dirty = false;
    return true;
}

bool app_realtime_profile_stage_phase(uint32_t phase_id, uint32_t start_cycle,
    uint32_t end_cycle, uint32_t wcet_cycles)
{
    if (!s_staging_loaded || phase_id >= PROFILE_PHASE_COUNT) {
        return false;
    }
    app_realtime_phase_contract_t *phase = &s_staging.phase[phase_id];
    phase->start_cycle = start_cycle;
    phase->end_cycle = end_cycle;
    phase->wcet_cycles = wcet_cycles;
    staging_sync_cycle();
    s_staging_dirty = true;
    return true;
}

bool app_realtime_profile_stage_cycle(uint32_t cycle_cycles)
{
    if (!s_staging_loaded) {
        return false;
    }
    s_staging.phase[APP_REALTIME_PHASE_COUNT - 1].end_cycle = cycle_cycles;
    staging_sync_cycle();
    s_staging_dirty = true;
    return true;
}

bool app_realtime_profile_stage_read(app_realtime_profile_table_t *table)
{
    if (!s_staging_loaded || table == NULL) {
        return false;
    }
    *table = s_staging;
    return true;
}

bool app_realtime_profile_stage_dirty(void)
{
    return s_staging_loaded && s_staging_dirty;
}

bool app_realtime_profile_stage_validate(app_realtime_profile_rejection_t *rejection)
{
    if (!s_staging_loaded) {
        rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_NULL,
                      PROFILE_PHASE_COUNT);
        return false;
    }
    return app_realtime_profile_table_validate(&s_staging, rejection);
}

bool app_realtime_profile_stage_apply(app_realtime_profile_rejection_t *rejection)
{
    if (!s_staging_loaded) {
        rejection_set(rejection, APP_REALTIME_PROFILE_REJECT_NULL,
                      PROFILE_PHASE_COUNT);
        return false;
    }
    if (!app_realtime_profile_table_validate(&s_staging, rejection)) {
        return false;
    }
    if (!app_realtime_profile_override_install(&s_staging, rejection)) {
        return false;
    }
    s_staging_dirty = false;
    return true;
}

bool app_realtime_profile_stage_discard(void)
{
    s_staging_loaded = false;
    s_staging_dirty = false;
    memset(&s_staging, 0, sizeof(s_staging));
    return true;
}
