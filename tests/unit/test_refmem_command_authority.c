/* N5.2b: target-side config authorization wiring.
 *
 * The dispatch is static, so this harness drives it through
 * distributed_refmem_node_control_service() with a crafted node-control record
 * and stubs for the surrounding control plane.  It pins the rule that the
 * authorization decision precedes the idle-window decision. */

#include "distributed_refmem.h"
#include "refmem_application_model.h"
#include "refmem_command.h"
#include "refmem_lease_table.h"
#include "refmem_vector_table.h"
#include "osal.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void distributed_refmem_node_control_service(void);
bool distributed_refmem_control_owner_slot(uint32_t *owner_slot);

static refmem_command_snapshot_t s_snapshot;
static uint32_t s_params[REFMEM_NODE_CONTROL_PARAM_WORDS];
static uint32_t s_local_slot;
static uint32_t s_owner_slot;
static bool s_owner_valid;
static bool s_window_open;
static bool s_stage_called;
static bool s_ack_called;
static uint32_t s_nack_reason;
static uint32_t s_nack_count;
static uint32_t s_node_status_state;
static uint32_t s_node_status_nack;
static uint32_t s_node_status_reason;
static uint32_t s_node_status_calls;

bool distributed_refmem_idle_window(void) { return s_window_open; }
uint32_t distributed_refmem_local_slot(void) { return s_local_slot; }
bool distributed_refmem_control_owner_slot(uint32_t *owner_slot)
{
    if (owner_slot != NULL) {
        *owner_slot = s_owner_slot;
    }
    return s_owner_valid;
}

bool distributed_refmem_node_control_read_params(
    uint32_t node_id,
    refmem_command_snapshot_t *snapshot,
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS],
    uint32_t *seq,
    uint32_t *crc32)
{
    (void)node_id;
    (void)seq;
    (void)crc32;
    *snapshot = s_snapshot;
    memcpy(params, s_params, sizeof(s_params));
    return true;
}

bool distributed_refmem_node_control_read(uint32_t node_id,
                                          refmem_command_snapshot_t *snapshot,
                                          uint32_t *seq,
                                          uint32_t *crc32)
{
    (void)node_id;
    (void)seq;
    (void)crc32;
    *snapshot = s_snapshot;
    return true;
}

bool distributed_refmem_post_command_replacing_complete(
    refmem_command_request_t *request, uint32_t issue_tick32)
{
    (void)request;
    (void)issue_tick32;
    return true;
}

refmem_command_take_result_t distributed_refmem_command_try_take(
    uint32_t target_node, uint32_t active_epoch, uint32_t active_run_id,
    uint32_t observed_payload_crc32, uint32_t evidence_index)
{
    (void)target_node;
    (void)active_epoch;
    (void)active_run_id;
    (void)observed_payload_crc32;
    (void)evidence_index;
    return REFMEM_COMMAND_TAKE_TAKEN;
}

bool distributed_refmem_command_ack(uint32_t target_node, uint32_t evidence_index)
{
    (void)target_node;
    (void)evidence_index;
    s_ack_called = true;
    return true;
}

bool distributed_refmem_command_nack(uint32_t target_node,
                                     refmem_command_reason_t reason,
                                     uint32_t evidence_index)
{
    (void)target_node;
    (void)evidence_index;
    s_nack_reason = (uint32_t)reason;
    s_nack_count++;
    return true;
}

bool distributed_refmem_node_control_ack(uint32_t node_id, uint32_t target_slot,
                                         uint32_t evidence_index)
{
    (void)node_id;
    (void)target_slot;
    (void)evidence_index;
    return true;
}

bool distributed_refmem_node_control_update_status(
    uint32_t node_id, uint32_t state, uint32_t ack_flags, uint32_t nack_flags,
    uint32_t busy_flags, uint32_t timeout_flags, uint32_t last_reason,
    uint32_t last_reason_slot, uint32_t evidence_index,
    uint32_t last_completed_seq)
{
    (void)node_id;
    (void)ack_flags;
    (void)busy_flags;
    (void)timeout_flags;
    (void)last_reason_slot;
    (void)evidence_index;
    (void)last_completed_seq;
    s_node_status_state = state;
    s_node_status_nack = nack_flags;
    s_node_status_reason = last_reason;
    s_node_status_calls++;
    return true;
}

void distributed_refmem_cmd_reply_arm(uint32_t command_seq, uint32_t origin_slot,
                                      uint32_t state, uint32_t reason)
{
    (void)command_seq;
    (void)origin_slot;
    (void)state;
    (void)reason;
}

bool refmem_application_model_stage_scpi_node_config(uint32_t node_id,
                                                     uint32_t instance_id,
                                                     uint32_t role_mask,
                                                     uint32_t persona_mask,
                                                     uint32_t enabled,
                                                     uint32_t required,
                                                     uint32_t load_order)
{
    (void)node_id;
    (void)instance_id;
    (void)role_mask;
    (void)persona_mask;
    (void)enabled;
    (void)required;
    (void)load_order;
    s_stage_called = true;
    return true;
}

bool refmem_application_model_stage_scpi_board_capability(
    uint32_t board_id, uint32_t board_uuid_crc32, uint32_t capability_mask,
    uint32_t io_constraint_mask, uint32_t ip_core_mask,
    uint32_t default_persona_mask, uint32_t hw_profile_crc32,
    uint32_t active_default_slot, uint32_t online_required)
{
    (void)board_id;
    (void)board_uuid_crc32;
    (void)capability_mask;
    (void)io_constraint_mask;
    (void)ip_core_mask;
    (void)default_persona_mask;
    (void)hw_profile_crc32;
    (void)active_default_slot;
    (void)online_required;
    s_stage_called = true;
    return true;
}

bool distributed_refmem_node_load_auto_enqueue(uint32_t instance_id)
{
    (void)instance_id;
    return true;
}

bool refmem_vector_table_validate_directory(const refmem_vector_table_t *table)
{
    (void)table;
    return true;
}

refmem_vector_table_t *distributed_refmem_vector_table(void) { return NULL; }

refmem_lease_result_t refmem_lease_table_take(uint32_t node_id,
                                              uint32_t owner_slot,
                                              uint32_t epoch,
                                              uint32_t now_ms,
                                              uint32_t ttl_ms)
{
    (void)node_id;
    (void)owner_slot;
    (void)epoch;
    (void)now_ms;
    (void)ttl_ms;
    return REFMEM_LEASE_OK;
}

refmem_lease_result_t refmem_lease_table_release(uint32_t node_id,
                                                 uint32_t owner_slot,
                                                 uint32_t epoch)
{
    (void)node_id;
    (void)owner_slot;
    (void)epoch;
    return REFMEM_LEASE_OK;
}

uint32_t osal_tick_ms(void) { return 0u; }

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n", name,
                     (unsigned long)expected, (unsigned long)actual);
        return 1;
    }
    return 0;
}

static void reset_state(uint32_t seq)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.command_seq = seq;
    s_snapshot.state = REFMEM_COMMAND_STATE_POSTED;
    s_snapshot.command_type = (uint32_t)REFMEM_COMMAND_TYPE_NODE_LOAD_STAGE;
    s_snapshot.source_node = 0u;
    s_snapshot.target_mask = 1u << s_local_slot;
    s_snapshot.required_mask = s_snapshot.target_mask;
    memset(s_params, 0, sizeof(s_params));
    s_stage_called = false;
    s_ack_called = false;
    s_nack_reason = 0u;
    s_nack_count = 0u;
    s_node_status_state = 0u;
    s_node_status_nack = 0u;
    s_node_status_reason = 0u;
    s_node_status_calls = 0u;
}

static int run_case(uint32_t seq)
{
    distributed_refmem_node_control_service();
    (void)seq;
    return 0;
}

int main(void)
{
    int failed = 0;

    s_local_slot = 1u;   /* target COM4 */
    s_owner_slot = 0u;   /* coordinator COM5 */
    s_owner_valid = true;
    s_window_open = true;

    /* 1. remote non-coordinator is rejected with 12 and never stages. */
    reset_state(1001u);
    s_snapshot.source_node = 2u;
    int rc = run_case(1001u);
    failed += rc;
    failed += expect_u32("non-coordinator nack count", s_nack_count, 1u);
    failed += expect_u32("non-coordinator reason", s_nack_reason,
                         REFMEM_COMMAND_REASON_PERMISSION_DENIED);
    failed += expect_u32("non-coordinator never stages", s_stage_called ? 1u : 0u, 0u);
    failed += expect_u32("non-coordinator node status updated", s_node_status_calls, 1u);
    failed += expect_u32("non-coordinator node state", s_node_status_state,
                         REFMEM_COMMAND_STATE_NACKED);
    failed += expect_u32("non-coordinator node reason", s_node_status_reason,
                         REFMEM_COMMAND_REASON_PERMISSION_DENIED);
    failed += expect_u32("non-coordinator node nack mask", s_node_status_nack,
                         1u << s_local_slot);

    /* 2. the local maintenance entry keeps the CP9 exception. */
    reset_state(1002u);
    s_snapshot.source_node = s_local_slot;
    rc = run_case(1002u);
    failed += rc;
    failed += expect_u32("local stages", s_stage_called ? 1u : 0u, 1u);
    failed += expect_u32("local acked", s_ack_called ? 1u : 0u, 1u);
    failed += expect_u32("local no nack", s_nack_count, 0u);

    /* 3. the derived coordinator is accepted. */
    reset_state(1003u);
    s_snapshot.source_node = s_owner_slot;
    rc = run_case(1003u);
    failed += rc;
    failed += expect_u32("owner stages", s_stage_called ? 1u : 0u, 1u);
    failed += expect_u32("owner acked", s_ack_called ? 1u : 0u, 1u);

    /* 4. without a derivable owner a remote origin fails closed. */
    reset_state(1004u);
    s_owner_valid = false;
    s_snapshot.source_node = 0u;
    rc = run_case(1004u);
    failed += rc;
    failed += expect_u32("no owner reason", s_nack_reason,
                         REFMEM_COMMAND_REASON_PERMISSION_DENIED);
    s_owner_valid = true;

    /* 5. authorization passes, but a closed window still denies with 7. */
    reset_state(1005u);
    s_window_open = false;
    s_snapshot.source_node = s_owner_slot;
    rc = run_case(1005u);
    failed += rc;
    failed += expect_u32("closed window reason", s_nack_reason,
                         REFMEM_COMMAND_REASON_RUN_STATE_DENIED);
    s_window_open = true;

    /* 6. non-config commands are not subject to the config authorization. */
    reset_state(1006u);
    s_snapshot.command_type = (uint32_t)REFMEM_COMMAND_TYPE_MAINTENANCE;
    s_snapshot.source_node = 2u;
    s_window_open = false;
    rc = run_case(1006u);
    failed += rc;
    failed += expect_u32("maintenance acked", s_ack_called ? 1u : 0u, 1u);
    failed += expect_u32("maintenance no nack", s_nack_count, 0u);
    s_window_open = true;

    if (failed != 0) {
        (void)printf("refmem_command_authority tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("refmem_command_authority tests passed\n");
    return 0;
}
