#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "refmem_command_catalog.h"

int main(void)
{
    assert(refmem_command_catalog_count == 21u);

    const refmem_catalog_entry_t *board =
        refmem_command_catalog_find_by_id(REFMEM_CMD_ID_REFMEM_LOAD_BOARD);
    assert(board != NULL);
    assert(strcmp(board->name, "REFMEM_LOAD_BOARD") == 0);
    assert(board->param_words == 9u);
    assert(board->param_kind == REFMEM_CATALOG_PARAM_INLINE);

    const refmem_catalog_entry_t *by_name =
        refmem_command_catalog_find_by_name("REFMEM_LOAD_NODE");
    assert(by_name != NULL && by_name->id == REFMEM_CMD_ID_REFMEM_LOAD_NODE);

    const refmem_catalog_entry_t *self_check =
        refmem_command_catalog_find_by_id(
            REFMEM_CMD_ID_REFMEM_MAINTENANCE_SELF_CHECK);
    assert(self_check != NULL);
    assert(self_check->command_type == 13u);
    assert(self_check->param_words == 1u);

    /* N6 online domain configuration: each row maps to its own command_type;
     * the TDMA non-stop rows stay inline and the deployment-only rows are
     * still present for the STOP/deployment window. */
    const struct {
        uint16_t id;
        const char *name;
        uint8_t command_type;
        uint8_t owner;
        uint8_t param_words;
    } n6_rows[] = {
        {REFMEM_CMD_ID_TDMA_RING_TOPOLOGY, "TDMA_RING_TOPOLOGY", 20u, 1u, 2u},
        {REFMEM_CMD_ID_TDMA_SEAT_ROTATION, "TDMA_SEAT_ROTATION", 21u, 1u, 3u},
        {REFMEM_CMD_ID_TDMA_PERIOD, "TDMA_PERIOD", 22u, 1u, 1u},
        {REFMEM_CMD_ID_TDMA_LOAD_MASK, "TDMA_LOAD_MASK", 23u, 1u, 1u},
        {REFMEM_CMD_ID_DPLL_ROLE, "DPLL_ROLE", 24u, 4u, 2u},
        {REFMEM_CMD_ID_DPLL_TUNE, "DPLL_TUNE", 25u, 4u, 5u},
        {REFMEM_CMD_ID_DPLL_OVERRIDE, "DPLL_OVERRIDE", 26u, 4u, 1u},
        {REFMEM_CMD_ID_VDC_REFERENCE_CONFIG, "VDC_REFERENCE_CONFIG", 27u, 4u, 5u},
        {REFMEM_CMD_ID_VDC_OUTPUT_DELAY, "VDC_OUTPUT_DELAY", 28u, 4u, 1u},
        {REFMEM_CMD_ID_VDC_OUTPUT_TIMING, "VDC_OUTPUT_TIMING", 29u, 4u, 3u},
        {REFMEM_CMD_ID_T2_CONFIGURE, "T2_CONFIGURE", 30u, 2u, 13u},
        {REFMEM_CMD_ID_T2_MOUNT, "T2_MOUNT", 31u, 2u, 3u},
        {REFMEM_CMD_ID_T2_UNMOUNT, "T2_UNMOUNT", 32u, 2u, 0u},
        {REFMEM_CMD_ID_VDC_STOP, "VDC_STOP", 33u, 4u, 0u},
        {REFMEM_CMD_ID_VDC_START, "VDC_START", 34u, 4u, 0u},
    };
    for (size_t i = 0u; i < sizeof(n6_rows) / sizeof(n6_rows[0]); ++i) {
        const refmem_catalog_entry_t *entry =
            refmem_command_catalog_find_by_id(n6_rows[i].id);
        assert(entry != NULL);
        assert(entry->command_type == n6_rows[i].command_type);
        assert(entry->owner == n6_rows[i].owner);
        assert(entry->param_words == n6_rows[i].param_words);
        assert(strcmp(entry->name, n6_rows[i].name) == 0);
        assert(refmem_command_catalog_validate_params(
            entry, n6_rows[i].param_words, REFMEM_CATALOG_PARAM_INLINE));
    }

    assert(refmem_command_catalog_find_by_id(999u) == NULL);
    assert(refmem_command_catalog_find_by_name("NOPE") == NULL);
    assert(refmem_command_catalog_find_by_name(NULL) == NULL);

    assert(refmem_command_catalog_validate_params(
        board, 9u, REFMEM_CATALOG_PARAM_INLINE));
    assert(!refmem_command_catalog_validate_params(
        board, 8u, REFMEM_CATALOG_PARAM_INLINE));
    assert(!refmem_command_catalog_validate_params(
        board, 9u, REFMEM_CATALOG_PARAM_REF));
    assert(!refmem_command_catalog_validate_params(NULL, 0u,
                                                   REFMEM_CATALOG_PARAM_INLINE));

    printf("refmem_command_catalog: lookup/validate OK\n");
    return 0;
}
