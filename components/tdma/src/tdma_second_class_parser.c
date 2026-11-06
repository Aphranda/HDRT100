#include "tdma_second_class_parser.h"

#include <string.h>

#include "algo_wire.h"

typedef struct {
    bool used;
    uint8_t type;
    tdma_second_class_parser_fn parser;
    void *context;
    tdma_second_class_parser_count_t count;
} entry_t;

static entry_t s_entries[TDMA_SECOND_CLASS_PARSER_MAX];
static uint32_t s_unknown;
static tdma_t2_static_apply_fn s_t2_static_apply;
static void *s_t2_static_context;
static uint32_t s_t2_static_applied;
static uint32_t s_t2_static_target;
static uint32_t s_t2_static_config;
static uint32_t s_t2_static_value;
static tdma_t2_dynamic_apply_fn s_t2_dynamic_apply;
static void *s_t2_dynamic_context;
static uint32_t s_t2_dynamic_applied;
static uint32_t s_t2_dynamic_target;
static uint32_t s_t2_dynamic_run;
static uint32_t s_t2_dynamic_config;
static uint32_t s_t2_dynamic_value;

static bool t2_static_default_apply(uint32_t target_mask,
                                    uint32_t config_id,
                                    uint32_t value,
                                    void *context)
{
    (void)context;
    s_t2_static_target = target_mask;
    s_t2_static_config = config_id;
    s_t2_static_value = value;
    s_t2_static_applied++;
    return true;
}

static bool t2_dynamic_default_apply(uint32_t target_mask,
                                     uint32_t run_id,
                                     uint32_t config_id,
                                     uint32_t value,
                                     void *context)
{
    (void)context;
    s_t2_dynamic_target = target_mask;
    s_t2_dynamic_run = run_id;
    s_t2_dynamic_config = config_id;
    s_t2_dynamic_value = value;
    s_t2_dynamic_applied++;
    return true;
}

void tdma_t2_dynamic_set_apply(tdma_t2_dynamic_apply_fn apply, void *context)
{
    s_t2_dynamic_apply = apply;
    s_t2_dynamic_context = context;
}

uint32_t tdma_t2_dynamic_applied_count(void)
{
    return s_t2_dynamic_applied;
}

bool tdma_t2_dynamic_get_last(uint32_t *target_mask,
                              uint32_t *run_id,
                              uint32_t *config_id,
                              uint32_t *value)
{
    if (target_mask != NULL) {
        *target_mask = s_t2_dynamic_target;
    }
    if (run_id != NULL) {
        *run_id = s_t2_dynamic_run;
    }
    if (config_id != NULL) {
        *config_id = s_t2_dynamic_config;
    }
    if (value != NULL) {
        *value = s_t2_dynamic_value;
    }
    return s_t2_dynamic_applied != 0u;
}

void tdma_t2_static_set_apply(tdma_t2_static_apply_fn apply, void *context)
{
    s_t2_static_apply = apply;
    s_t2_static_context = context;
}

uint32_t tdma_t2_static_applied_count(void)
{
    return s_t2_static_applied;
}

bool tdma_t2_static_get_last(uint32_t *target_mask,
                             uint32_t *config_id,
                             uint32_t *value)
{
    if (target_mask != NULL) {
        *target_mask = s_t2_static_target;
    }
    if (config_id != NULL) {
        *config_id = s_t2_static_config;
    }
    if (value != NULL) {
        *value = s_t2_static_value;
    }
    return s_t2_static_applied != 0u;
}

static uint16_t get_u16(const uint8_t *p)
{
    return algo_le16_load(p);
}

static uint32_t get_u32(const uint8_t *p)
{
    return algo_le32_load(p);
}

void tdma_second_class_parser_reset(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_unknown = 0u;
    s_t2_static_applied = 0u;
    s_t2_static_target = 0u;
    s_t2_static_config = 0u;
    s_t2_static_value = 0u;
    s_t2_dynamic_applied = 0u;
    s_t2_dynamic_target = 0u;
    s_t2_dynamic_run = 0u;
    s_t2_dynamic_config = 0u;
    s_t2_dynamic_value = 0u;
}

bool tdma_second_class_parser_register(uint8_t type,
                                       tdma_second_class_parser_fn parser,
                                       void *context)
{
    if (parser == NULL || type == 0u) {
        return false;
    }
    for (uint32_t i = 0u; i < TDMA_SECOND_CLASS_PARSER_MAX; i++) {
        if (s_entries[i].used && s_entries[i].type == type) {
            return false;
        }
    }
    for (uint32_t i = 0u; i < TDMA_SECOND_CLASS_PARSER_MAX; i++) {
        if (!s_entries[i].used) {
            s_entries[i].used = true;
            s_entries[i].type = type;
            s_entries[i].parser = parser;
            s_entries[i].context = context;
            s_entries[i].count = (tdma_second_class_parser_count_t){0};
            return true;
        }
    }
    return false;
}

bool tdma_second_class_parser_dispatch(uint8_t type,
                                       const uint8_t *payload,
                                       size_t payload_size)
{
    for (uint32_t i = 0u; i < TDMA_SECOND_CLASS_PARSER_MAX; i++) {
        if (!s_entries[i].used || s_entries[i].type != type) {
            continue;
        }
        const bool accepted = s_entries[i].parser(
            payload, payload_size, s_entries[i].context);
        if (accepted) {
            s_entries[i].count.accepted++;
        } else {
            s_entries[i].count.rejected++;
        }
        return accepted;
    }
    s_unknown++;
    return false;
}

bool tdma_second_class_parser_get_count(
    uint8_t type, tdma_second_class_parser_count_t *count)
{
    if (count == NULL) {
        return false;
    }
    for (uint32_t i = 0u; i < TDMA_SECOND_CLASS_PARSER_MAX; i++) {
        if (s_entries[i].used && s_entries[i].type == type) {
            *count = s_entries[i].count;
            return true;
        }
    }
    return false;
}

uint32_t tdma_second_class_parser_unknown_count(void)
{
    return s_unknown;
}

uint32_t tdma_second_class_parser_registered_count(void)
{
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < TDMA_SECOND_CLASS_PARSER_MAX; i++) {
        if (s_entries[i].used) {
            count++;
        }
    }
    return count;
}

bool tdma_t2_static_parser(const uint8_t *payload,
                           size_t payload_size,
                           void *context)
{
    (void)context;
    if (payload == NULL || payload_size != TDMA_T2_STATIC_SIZE) {
        return false;
    }
    const uint16_t magic = get_u16(payload);
    const uint32_t target_mask = get_u32(payload + 4u);
    const uint32_t config_id = get_u32(payload + 8u);
    const uint32_t value = get_u32(payload + 12u);
    if (magic != TDMA_T2_STATIC_MAGIC || payload[2] != 1u ||
        target_mask == 0u) {
        return false;
    }
    const tdma_t2_static_apply_fn apply =
        s_t2_static_apply != NULL ? s_t2_static_apply
                                  : t2_static_default_apply;
    return apply(target_mask, config_id, value, s_t2_static_context);
}

bool tdma_t2_dynamic_parser(const uint8_t *payload,
                            size_t payload_size,
                            void *context)
{
    (void)context;
    if (payload == NULL || payload_size != TDMA_T2_DYNAMIC_SIZE) {
        return false;
    }
    if (get_u16(payload) != TDMA_T2_DYNAMIC_MAGIC ||
        payload[2] != 1u ||
        get_u32(payload + 4u) == 0u ||
        get_u32(payload + 8u) == 0u ||
        get_u32(payload + 20u) != 0u) {
        return false;
    }
    /* Envelope-first, execution second: the reserved word is already proven
     * zero, so the executor receives only validated fields. */
    const tdma_t2_dynamic_apply_fn apply =
        s_t2_dynamic_apply != NULL ? s_t2_dynamic_apply
                                   : t2_dynamic_default_apply;
    return apply(get_u32(payload + 4u), get_u32(payload + 8u),
                 get_u32(payload + 12u), get_u32(payload + 16u),
                 s_t2_dynamic_context);
}
