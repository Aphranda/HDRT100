#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "refmem_function_catalog.h"

int main(void)
{
    assert(refmem_function_catalog_count == 5u);

    const refmem_function_entry_t *tdma =
        refmem_function_find_by_id(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE);
    assert(tdma != NULL);
    assert(strcmp(tdma->name, "FUNC_TDMA_ONLINE") == 0);
    assert(tdma->arg_words == 4u);
    assert(tdma->step_count == 2u);
    assert(tdma->failure_policy == REFMEM_FUNCTION_ABORT_REMAINING);
    assert(tdma->steps[0].command_id == 21u && tdma->steps[0].arg_base == 0u &&
           tdma->steps[0].arg_words == 3u);
    assert(tdma->steps[1].command_id == 23u && tdma->steps[1].arg_base == 3u &&
           tdma->steps[1].arg_words == 1u);

    const refmem_function_entry_t *by_name =
        refmem_function_find_by_name("FUNC_T2_RECONFIG");
    assert(by_name != NULL && by_name->id == REFMEM_FUNC_ID_FUNC_T2_RECONFIG);
    assert(by_name->arg_words == 13u);
    assert(by_name->steps[0].command_id == 32u);
    assert(by_name->steps[1].command_id == 30u);
    assert(by_name->steps[1].arg_words == 13u);

    const refmem_function_entry_t *clock =
        refmem_function_find_by_id(REFMEM_FUNC_ID_FUNC_CLOCK_RECONFIG);
    assert(clock != NULL && clock->arg_words == 7u && clock->step_count == 2u);
    assert(clock->steps[0].command_id == 24u && clock->steps[0].arg_words == 2u);
    assert(clock->steps[1].command_id == 25u && clock->steps[1].arg_base == 2u);

    const refmem_function_entry_t *vdc =
        refmem_function_find_by_id(REFMEM_FUNC_ID_FUNC_VDC_RECONFIG);
    assert(vdc != NULL && vdc->arg_words == 9u && vdc->step_count == 4u);
    assert(vdc->steps[0].command_id == 33u && vdc->steps[0].arg_words == 0u);
    assert(vdc->steps[1].command_id == 27u && vdc->steps[1].arg_base == 0u &&
           vdc->steps[1].arg_words == 5u);
    assert(vdc->steps[2].command_id == 28u && vdc->steps[2].arg_base == 5u &&
           vdc->steps[2].arg_words == 1u);
    assert(vdc->steps[3].command_id == 29u && vdc->steps[3].arg_base == 6u &&
           vdc->steps[3].arg_words == 3u);

    assert(refmem_function_find_by_id(999u) == NULL);
    assert(refmem_function_find_by_name("NOPE") == NULL);
    assert(refmem_function_find_by_name(NULL) == NULL);

    /* Every step slice stays inside the function argument window. */
    for (uint32_t i = 0u; i < refmem_function_catalog_count; ++i) {
        const refmem_function_entry_t *e = &refmem_function_catalog[i];
        assert(e->step_count <= REFMEM_FUNCTION_MAX_STEPS);
        assert(e->arg_words <= REFMEM_FUNCTION_ARG_MAX);
        for (uint32_t s = 0u; s < e->step_count; ++s) {
            assert((uint32_t)e->steps[s].arg_base +
                       e->steps[s].arg_words <= e->arg_words);
        }
    }

    printf("refmem_function_catalog: lookup/schema OK\n");
    return 0;
}

