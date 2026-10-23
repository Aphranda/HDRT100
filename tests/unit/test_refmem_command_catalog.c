#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "refmem_command_catalog.h"

int main(void)
{
    assert(refmem_command_catalog_count == 3u);

    const refmem_catalog_entry_t *board =
        refmem_command_catalog_find_by_id(REFMEM_CMD_ID_REFMEM_LOAD_BOARD);
    assert(board != NULL);
    assert(strcmp(board->name, "REFMEM_LOAD_BOARD") == 0);
    assert(board->param_words == 9u);
    assert(board->param_kind == REFMEM_CATALOG_PARAM_INLINE);

    const refmem_catalog_entry_t *by_name =
        refmem_command_catalog_find_by_name("REFMEM_LOAD_NODE");
    assert(by_name != NULL && by_name->id == REFMEM_CMD_ID_REFMEM_LOAD_NODE);

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
