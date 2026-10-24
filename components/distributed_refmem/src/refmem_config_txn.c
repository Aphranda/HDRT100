#include "distributed_refmem.h"

#include "refmem_application_model.h"

static volatile uint32_t s_idle_window = 1u;
static volatile uint32_t s_config_txn_active;

void distributed_refmem_set_idle_window(bool open)
{
    s_idle_window = open ? 1u : 0u;
}

bool distributed_refmem_idle_window(void)
{
    return s_idle_window != 0u;
}

bool distributed_refmem_config_transaction_begin(void)
{
    if (s_config_txn_active != 0u) {
        return false;
    }
    distributed_refmem_set_idle_window(true);
    s_config_txn_active = 1u;
    return true;
}

bool distributed_refmem_config_transaction_commit(void)
{
    if (s_config_txn_active == 0u) {
        return false;
    }
    /* Node/board stages are validated by their own dispatch; the prepared
     * table views are the only staged object with a commit/discard pair.  A
     * table commit failure must not silently half-apply: discard it, but the
     * transaction still closes so a node-only stage can commit. */
    if (!refmem_application_model_commit_prepared_table_views()) {
        refmem_application_model_discard_prepared_table_views();
    }
    distributed_refmem_set_idle_window(false);
    s_config_txn_active = 0u;
    return true;
}

void distributed_refmem_config_transaction_abort(void)
{
    if (s_config_txn_active == 0u) {
        return;
    }
    refmem_application_model_discard_prepared_table_views();
    distributed_refmem_set_idle_window(true);
    s_config_txn_active = 0u;
}

bool distributed_refmem_config_transaction_active(void)
{
    return s_config_txn_active != 0u;
}
