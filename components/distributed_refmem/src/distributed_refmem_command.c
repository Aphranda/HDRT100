#include "distributed_refmem_internal.h"

#include "refmem_application_model.h"
#include "refmem_control_route.h"
#include "refmem_lease_table.h"
#include "refmem_node_control.h"
#include "refmem_vector_table.h"
#include "osal.h"

#include <string.h>

static uint32_t s_node_control_last_command_seq;
static refmem_lease_result_t s_lease_last_result;
static uint32_t s_dispatch_reason;

static bool distributed_refmem_is_config_type(uint32_t type)
{
    switch ((refmem_command_type_t)type) {
    case REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE:
    case REFMEM_COMMAND_TYPE_NODE_LOAD_STAGE:
    case REFMEM_COMMAND_TYPE_TABLE_PACKAGE_STAGE:
    case REFMEM_COMMAND_TYPE_TABLE_PACKAGE_ACTIVATE:
    case REFMEM_COMMAND_TYPE_TDMA_RING_TOPOLOGY:
    case REFMEM_COMMAND_TYPE_TDMA_SEAT_ROTATION:
    case REFMEM_COMMAND_TYPE_TDMA_PERIOD:
    case REFMEM_COMMAND_TYPE_TDMA_LOAD_MASK:
    case REFMEM_COMMAND_TYPE_DPLL_ROLE:
    case REFMEM_COMMAND_TYPE_DPLL_TUNE:
    case REFMEM_COMMAND_TYPE_DPLL_OVERRIDE:
    case REFMEM_COMMAND_TYPE_VDC_REFERENCE_CONFIG:
    case REFMEM_COMMAND_TYPE_VDC_OUTPUT_DELAY:
    case REFMEM_COMMAND_TYPE_VDC_OUTPUT_TIMING:
    case REFMEM_COMMAND_TYPE_T2_CONFIGURE:
    case REFMEM_COMMAND_TYPE_T2_MOUNT:
    case REFMEM_COMMAND_TYPE_T2_UNMOUNT:
    case REFMEM_COMMAND_TYPE_VDC_STOP:
    case REFMEM_COMMAND_TYPE_VDC_START:
        return true;
    default:
        return false;
    }
}

/* The RefMem idle window only gates deployment-time table/vector staging.
 * Online domain configuration (TDMA seat/load, DPLL, VDC/T2 inside their own
 * stop windows) must not depend on it: TDMA is the non-stop bearer and the
 * target domain has its own stop boundary. */
static bool distributed_refmem_requires_idle_window(uint32_t type)
{
    switch ((refmem_command_type_t)type) {
    case REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE:
    case REFMEM_COMMAND_TYPE_NODE_LOAD_STAGE:
    case REFMEM_COMMAND_TYPE_TABLE_PACKAGE_STAGE:
    case REFMEM_COMMAND_TYPE_TABLE_PACKAGE_ACTIVATE:
    case REFMEM_COMMAND_TYPE_TDMA_RING_TOPOLOGY:
    case REFMEM_COMMAND_TYPE_TDMA_PERIOD:
        return true;
    default:
        return false;
    }
}

static uint32_t distributed_refmem_lease_reason(
    refmem_lease_result_t result)
{
    switch (result) {
    case REFMEM_LEASE_CONFLICT:
        return (uint32_t)REFMEM_COMMAND_REASON_RESOURCE_BUSY;
    case REFMEM_LEASE_STALE_EPOCH:
        return (uint32_t)REFMEM_COMMAND_REASON_EPOCH_MISMATCH;
    case REFMEM_LEASE_NOT_OWNER:
        return (uint32_t)REFMEM_COMMAND_REASON_PERMISSION_DENIED;
    case REFMEM_LEASE_RESULT_EXPIRED:
        return (uint32_t)REFMEM_COMMAND_REASON_TIMEOUT;
    default:
        return (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
    }
}

/* Catalog-driven local dispatch: the node control record is the intent, the
 * owner function below executes it.  RefMem only carries and aggregates. */
static bool distributed_refmem_dispatch_control(
    const refmem_command_snapshot_t *snapshot,
    const uint32_t *params,
    uint32_t local_slot)
{
    s_dispatch_reason = (uint32_t)REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
    if (distributed_refmem_is_config_type(snapshot->command_type)) {
        /* N5.2b: authorization precedes the window check.  A config command from
         * a non-coordinator remote origin is a terminal PERMISSION_DENIED and
         * must not oscillate with the idle window (7 vs 12).  The local entry
         * keeps the CP9 exception. */
        uint32_t control_owner = 0u;
        const bool owner_valid = distributed_refmem_control_owner_slot(&control_owner);
        if (refmem_control_origin_decide(snapshot->source_node, local_slot,
                                         control_owner, owner_valid) ==
            REFMEM_CONTROL_ORIGIN_REJECT) {
            s_dispatch_reason = (uint32_t)REFMEM_COMMAND_REASON_PERMISSION_DENIED;
            return false;
        }
        if (distributed_refmem_requires_idle_window(snapshot->command_type) &&
            !distributed_refmem_idle_window()) {
            s_dispatch_reason = (uint32_t)REFMEM_COMMAND_REASON_RUN_STATE_DENIED;
            return false;
        }
    }
    switch ((refmem_command_type_t)snapshot->command_type) {
    case REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE:
        return refmem_application_model_stage_scpi_board_capability(
            params[0], params[1], params[2], params[3], params[4], params[5],
            params[6], params[7], params[8]);
    case REFMEM_COMMAND_TYPE_NODE_LOAD_STAGE: {
        const bool staged = refmem_application_model_stage_scpi_node_config(
            params[0], params[1], params[2], params[3], params[4], params[5],
            params[6]);
        if (staged) {
            (void)distributed_refmem_node_load_auto_enqueue(params[1]);
        }
        return staged;
    }
    case REFMEM_COMMAND_TYPE_MAINTENANCE: {
        /* Node self-check: prove the local control record and the vector
         * directory are readable and valid, without changing any fact.  It is
         * the non-config control-plane probe used while the ring runs. */
        const uint32_t check_mask = params[0];
        if (check_mask > 3u) {
            return false;
        }
        if (check_mask == 0u || (check_mask & 1u) != 0u) {
            refmem_command_snapshot_t self;
            if (!distributed_refmem_node_control_read(
                    distributed_refmem_local_slot(), &self, NULL, NULL)) {
                return false;
            }
        }
        if (check_mask == 0u || (check_mask & 2u) != 0u) {
            if (!refmem_vector_table_validate_directory(
                    distributed_refmem_vector_table())) {
                return false;
            }
        }
        return true;
    }
    case REFMEM_COMMAND_TYPE_LEASE_TAKE: {
        const refmem_lease_result_t result = refmem_lease_table_take(
            distributed_refmem_local_slot(), snapshot->source_node, params[0],
            osal_tick_ms(), params[1]);
        s_lease_last_result = result;
        if (result != REFMEM_LEASE_OK) {
            s_dispatch_reason = distributed_refmem_lease_reason(result);
        }
        return result == REFMEM_LEASE_OK;
    }
    case REFMEM_COMMAND_TYPE_LEASE_RELEASE: {
        const refmem_lease_result_t result = refmem_lease_table_release(
            distributed_refmem_local_slot(), snapshot->source_node,
            params[0]);
        s_lease_last_result = result;
        if (result != REFMEM_LEASE_OK) {
            s_dispatch_reason = distributed_refmem_lease_reason(result);
        }
        return result == REFMEM_LEASE_OK;
    }
    default:
        return false;
    }
}

void distributed_refmem_node_control_service(void)
{
    refmem_command_snapshot_t snapshot;
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS];
    const uint32_t local_slot = distributed_refmem_local_slot();
    if (!distributed_refmem_node_control_read_params(
            local_slot, &snapshot, params, NULL, NULL)) {
        return;
    }
    if (snapshot.command_seq == 0u ||
        snapshot.command_seq == s_node_control_last_command_seq ||
        snapshot.state != REFMEM_COMMAND_STATE_POSTED) {
        return;
    }
    /* Accept local and remote issuers; the target mask is the authority.
     * (A record written by a remote source must still be consumed here.) */
    if ((snapshot.target_mask & (1u << local_slot)) == 0u) {
        return;
    }

    refmem_command_request_t request;
    memset(&request, 0, sizeof(request));
    request.command_seq = snapshot.command_seq;
    request.source_node = snapshot.source_node;
    request.source_instance = snapshot.source_instance;
    request.target_mask = snapshot.target_mask;
    request.required_mask = snapshot.required_mask;
    request.command_type = snapshot.command_type;
    request.command_class = snapshot.command_class;
    request.payload_kind = snapshot.payload_kind;
    request.payload_ref = snapshot.payload_ref;
    request.payload_size = snapshot.payload_size;
    request.payload_crc32 = snapshot.payload_crc32;
    request.issue_epoch = snapshot.issue_epoch;
    request.run_id = snapshot.run_id;
    request.timeout_us = snapshot.timeout_us;

    const bool posted =
        distributed_refmem_post_command_replacing_complete(&request,
                                                           osal_tick_ms());
    s_node_control_last_command_seq = snapshot.command_seq;
    if (!posted) {
        return;
    }
    const refmem_command_take_result_t take =
        distributed_refmem_command_try_take(local_slot, request.issue_epoch,
                                            request.run_id,
                                            request.payload_crc32, 0u);
    if (take == REFMEM_COMMAND_TAKE_TAKEN) {
        const bool executed =
            distributed_refmem_dispatch_control(&snapshot, params, local_slot);
        if (executed) {
            (void)distributed_refmem_command_ack(local_slot, 0u);
            (void)distributed_refmem_node_control_ack(local_slot, local_slot,
                                                      0u);
            distributed_refmem_cmd_reply_arm(
                snapshot.command_seq, snapshot.source_node,
                (uint32_t)REFMEM_COMMAND_STATE_ACKED,
                (uint32_t)REFMEM_COMMAND_REASON_NONE);
        } else {
            uint32_t reason = s_dispatch_reason;
            if (snapshot.command_type ==
                    (uint32_t)REFMEM_COMMAND_TYPE_LEASE_TAKE ||
                snapshot.command_type ==
                    (uint32_t)REFMEM_COMMAND_TYPE_LEASE_RELEASE) {
                reason = distributed_refmem_lease_reason(s_lease_last_result);
            }
            (void)distributed_refmem_command_nack(local_slot, reason, 0u);
            distributed_refmem_cmd_reply_arm(
                snapshot.command_seq, snapshot.source_node,
                (uint32_t)REFMEM_COMMAND_STATE_NACKED, reason);
        }
    }
}
