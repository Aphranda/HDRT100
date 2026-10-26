#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "distributed_refmem.h"
#include "refmem_function.h"

/* Default hooks reference these symbols; the test always installs its own
 * hooks, but the linker still needs the firmware entry points. */
static uint32_t s_test_now;

uint32_t osal_tick_ms(void)
{
    return s_test_now;
}

bool distributed_refmem_control_submit(uint16_t command_id,
                                       uint32_t target_mask,
                                       uint32_t required_mask,
                                       const uint32_t *params,
                                       uint16_t param_words,
                                       uint32_t *command_seq)
{
    (void)command_id;
    (void)target_mask;
    (void)required_mask;
    (void)params;
    (void)param_words;
    (void)command_seq;
    return false;
}

bool distributed_refmem_node_control_read(uint32_t node_id,
                                          refmem_command_snapshot_t *snapshot,
                                          uint32_t *seq, uint32_t *crc32)
{
    (void)node_id;
    (void)snapshot;
    (void)seq;
    (void)crc32;
    return false;
}

uint32_t distributed_refmem_local_slot(void)
{
    return 0u;
}

static uint32_t s_next_seq = 1u;
static uint16_t s_last_cmd;
static uint32_t s_last_params[16];
static uint32_t s_last_words;
static uint32_t s_submit_count;
static uint32_t s_poll_state;
static uint32_t s_poll_reason;
static uint32_t s_poll_ack;
static uint32_t s_poll_nack;
static uint32_t s_last_target_mask;
static uint32_t s_last_required_mask;

static bool submit_hook(uint16_t command_id, uint32_t target_mask,
                        uint32_t required_mask, const uint32_t *params,
                        uint16_t param_words, uint32_t *command_seq)
{
    s_last_target_mask = target_mask;
    s_last_required_mask = required_mask;
    s_last_cmd = command_id;
    s_last_words = param_words;
    memset(s_last_params, 0, sizeof(s_last_params));
    if (params != NULL) {
        memcpy(s_last_params, params, param_words * sizeof(uint32_t));
    }
    *command_seq = s_next_seq++;
    ++s_submit_count;
    return true;
}

static bool poll_hook(uint32_t command_seq, uint32_t *state,
                      uint32_t *last_reason, uint32_t *ack_mask,
                      uint32_t *nack_mask)
{
    (void)command_seq;
    *state = s_poll_state;
    *last_reason = s_poll_reason;
    *ack_mask = s_poll_ack;
    *nack_mask = s_poll_nack;
    return true;
}

static void reset_sim(void)
{
    s_test_now = 0u;
    s_next_seq = 1u;
    s_last_cmd = 0u;
    s_last_words = 0u;
    memset(s_last_params, 0, sizeof(s_last_params));
    s_submit_count = 0u;
    s_poll_state = (uint32_t)REFMEM_COMMAND_STATE_POSTED;
    s_poll_reason = 0u;
    s_poll_ack = 0u;
    s_poll_nack = 0u;
    s_last_target_mask = 0u;
    s_last_required_mask = 0u;
    refmem_function_init();
    refmem_function_set_hooks(submit_hook, poll_hook);
}

int main(void)
{
    refmem_function_snapshot_t status;
    uint32_t seq = 0u;
    const uint32_t tdma_args[4] = {3u, 1u, 0u, 0x5u};

    reset_sim();
    assert(refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0x01u, 0x01u,
                               tdma_args, 4u, &seq));
    assert(refmem_function_get_status(seq, &status));
    assert(status.state == REFMEM_FUNCTION_RUNNING);
    refmem_function_service();
    assert(s_last_cmd == 21u && s_last_words == 3u);
    assert(s_last_params[0] == 3u && s_last_params[1] == 1u &&
           s_last_params[2] == 0u);

    s_poll_state = (uint32_t)REFMEM_COMMAND_STATE_ACKED;
    s_poll_ack = 0x01u;
    refmem_function_service();  /* ACK step 0, advance to READY */
    refmem_function_service();  /* gap: must not submit yet */
    assert(s_last_cmd == 21u);
    s_test_now += REFMEM_FUNCTION_STEP_GAP_MS;
    refmem_function_service();  /* submit step 1 after the gap */
    assert(s_last_cmd == 23u && s_last_words == 1u);
    assert(s_last_params[0] == 0x5u);

    refmem_function_service();  /* ACK step 1, finish */
    assert(refmem_function_get_status(seq, &status));
    assert(status.state == REFMEM_FUNCTION_COMPLETED);
    assert(status.step_index == 2u);
    assert(refmem_function_active_count() == 0u);

    /* ABORT_REMAINING: a NACK stops the sequence and keeps the reason. */
    reset_sim();
    assert(refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0x01u, 0x01u,
                               tdma_args, 4u, &seq));
    refmem_function_service();
    s_poll_state = (uint32_t)REFMEM_COMMAND_STATE_NACKED;
    s_poll_reason = 13u;
    s_poll_nack = 0x01u;
    refmem_function_service();
    assert(refmem_function_get_status(seq, &status));
    assert(status.state == REFMEM_FUNCTION_FAILED);
    assert(status.last_reason == 13u);
    assert(status.step_nack_mask == 0x01u);
    const uint32_t submits_after_fail = s_submit_count;
    refmem_function_service();
    assert(s_submit_count == submits_after_fail);

    /* Timeout path uses the wrap-safe signed diff. */
    reset_sim();
    const uint32_t clock_args[7] = {1u, 0u, 10, 20, 1000u, 100u, 500u};
    assert(refmem_function_run(REFMEM_FUNC_ID_FUNC_CLOCK_RECONFIG, 0x01u,
                               0x01u, clock_args, 7u, &seq));
    refmem_function_service();
    s_test_now = REFMEM_FUNCTION_STEP_TIMEOUT_MS + 1u;
    refmem_function_service();
    assert(refmem_function_get_status(seq, &status));
    assert(status.state == REFMEM_FUNCTION_FAILED);
    assert(status.last_reason == (uint32_t)REFMEM_COMMAND_REASON_TIMEOUT);

    /* Argument/identity validation. */
    reset_sim();
    assert(!refmem_function_run(999u, 0x01u, 0x01u, tdma_args, 4u, &seq));
    assert(!refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0u, 0u,
                                tdma_args, 4u, &seq));
    assert(!refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0x01u, 0x02u,
                                tdma_args, 4u, &seq));
    assert(!refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0x01u, 0x01u,
                                tdma_args, 3u, &seq));

    /* Single in-flight function: the command slot is shared, so a second
     * function is BUSY until the first reaches a terminal state. */
    reset_sim();
    assert(refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0x01u, 0x01u,
                               tdma_args, 4u, &seq));
    assert(!refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0x01u, 0x01u,
                                tdma_args, 4u, &seq));
    refmem_function_service();
    s_poll_state = (uint32_t)REFMEM_COMMAND_STATE_ACKED;
    refmem_function_service();  /* ACK step 0 */
    s_test_now += REFMEM_FUNCTION_STEP_GAP_MS;
    refmem_function_service();  /* submit step 1 */
    refmem_function_service();  /* ACK step 1, complete */
    assert(refmem_function_active_count() == 0u);
    assert(refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0x01u, 0x01u,
                               tdma_args, 4u, &seq));

    /* Multi-target mask is passed to the atomic submit unchanged. */
    reset_sim();
    assert(refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0x0Eu, 0x0Eu,
                               tdma_args, 4u, &seq));
    refmem_function_service();
    assert(s_last_target_mask == 0x0Eu);
    assert(s_last_required_mask == 0x0Eu);

    /* RUN_STATE_DENIED is retried a bounded number of times, then fails. */
    reset_sim();
    assert(refmem_function_run(REFMEM_FUNC_ID_FUNC_TDMA_ONLINE, 0x01u, 0x01u,
                               tdma_args, 4u, &seq));
    refmem_function_service();  /* submit step 0 */
    s_poll_state = (uint32_t)REFMEM_COMMAND_STATE_NACKED;
    s_poll_reason = (uint32_t)REFMEM_COMMAND_REASON_RUN_STATE_DENIED;
    refmem_function_service();  /* first 7: retry, not fail */
    assert(refmem_function_get_status(seq, &status));
    assert(status.state == REFMEM_FUNCTION_RUNNING);
    for (uint32_t i = 0u; i < (REFMEM_FUNCTION_STEP_RETRY_MAX + 2u) * 2u;
         ++i) {
        s_test_now += REFMEM_FUNCTION_STEP_RETRY_GAP_MS;
        refmem_function_service();
    }
    assert(refmem_function_get_status(seq, &status));
    assert(status.state == REFMEM_FUNCTION_FAILED);
    assert(status.last_reason ==
           (uint32_t)REFMEM_COMMAND_REASON_RUN_STATE_DENIED);

    printf("refmem_function: executor/state machine OK\n");
    return 0;
}


