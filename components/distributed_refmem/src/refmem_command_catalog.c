#include "refmem_command_catalog.h"

#include <string.h>

const refmem_catalog_entry_t *refmem_command_catalog_find_by_id(uint16_t id)
{
    for (uint32_t i = 0u; i < refmem_command_catalog_count; i++) {
        if (refmem_command_catalog[i].id == id) {
            return &refmem_command_catalog[i];
        }
    }
    return NULL;
}

const refmem_catalog_entry_t *refmem_command_catalog_find_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (uint32_t i = 0u; i < refmem_command_catalog_count; i++) {
        if (strcmp(refmem_command_catalog[i].name, name) == 0) {
            return &refmem_command_catalog[i];
        }
    }
    return NULL;
}

bool refmem_command_catalog_validate_params(const refmem_catalog_entry_t *entry,
                                            uint32_t param_words,
                                            uint32_t param_kind)
{
    if (entry == NULL || param_words > 16u) {
        return false;
    }
    if (entry->param_kind != (uint8_t)param_kind) {
        return false;
    }
    return param_words == entry->param_words;
}
