#ifndef TRIGGER_ACTION_TEMPLATE_STORE_H
#define TRIGGER_ACTION_TEMPLATE_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "trigger_action_template_table.h"

typedef enum {
    TRIGGER_ACTION_TEMPLATE_STORE_OK=0u,
    TRIGGER_ACTION_TEMPLATE_STORE_BUSY=32u,
    TRIGGER_ACTION_TEMPLATE_STORE_NOT_STOPPED,
    TRIGGER_ACTION_TEMPLATE_STORE_DEPLOYMENT,
    TRIGGER_ACTION_TEMPLATE_STORE_DIGEST,
    TRIGGER_ACTION_TEMPLATE_STORE_GENERATION,
    TRIGGER_ACTION_TEMPLATE_STORE_CHANGED,
} trigger_action_template_store_result_t;

typedef struct {
    uint32_t installed, binding_current, resources_loaded, generation, crc, last_generation;
    trigger_action_template_table_t table;
} trigger_action_template_store_snapshot_t;

uint32_t trigger_action_template_install(
    const trigger_action_template_table_t *table, uint32_t expected_crc);
uint32_t trigger_action_template_clear(void);
uint32_t trigger_action_template_activate_resources(void);
uint32_t trigger_action_template_release_resources(void);
uint32_t trigger_action_template_snapshot(
    trigger_action_template_store_snapshot_t *out);

#endif
