#include "refmem_trigger_session.h"

#include <stddef.h>

#define SLOT_BIT(slot) (1u << (slot))
#define SLOT_VALID(slot) ((slot) < 32u)

static bool required_all_armed(const refmem_trigger_session_t *s)
{
    return (s->armed_mask & s->required_mask) == s->required_mask;
}

static bool required_subset_target(uint32_t required, uint32_t target)
{
    return (required & ~target) == 0u;
}

static void reset_runtime(refmem_trigger_session_t *s)
{
    s->armed_mask = 0u;
    s->failed_mask = 0u;
    s->source_ready = false;
    s->last_reason = 0u;
}

static void fail_arming(refmem_trigger_session_t *s,
                        refmem_trigger_session_result_t reason,
                        uint32_t slot)
{
    s->failed_mask |= SLOT_BIT(slot);
    s->armed_mask = 0u;
    s->source_ready = false;
    s->last_reason = (uint32_t)reason;
    s->state = REFMEM_TRIGGER_SESSION_CONFIGURED;
}

void refmem_trigger_session_init(refmem_trigger_session_t *session)
{
    if (session == NULL) {
        return;
    }
    *session = (refmem_trigger_session_t){0};
    session->state = REFMEM_TRIGGER_SESSION_IDLE;
}

refmem_trigger_session_result_t refmem_trigger_session_configure(
    refmem_trigger_session_t *session,
    const refmem_trigger_session_config_t *config)
{
    if (session == NULL || config == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state == REFMEM_TRIGGER_SESSION_ARMING ||
        session->state == REFMEM_TRIGGER_SESSION_ARMED ||
        session->state == REFMEM_TRIGGER_SESSION_RUNNING ||
        session->state == REFMEM_TRIGGER_SESSION_STOPPING) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    if (config->session_id == 0u || config->run_id == 0u ||
        config->target_mask == 0u || config->required_mask == 0u ||
        !required_subset_target(config->required_mask, config->target_mask) ||
        !SLOT_VALID(config->source_slot)) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }

    session->state = REFMEM_TRIGGER_SESSION_CONFIGURED;
    session->session_id = config->session_id;
    session->run_id = config->run_id;
    session->source_slot = config->source_slot;
    session->target_mask = config->target_mask;
    session->required_mask = config->required_mask;
    reset_runtime(session);
    return REFMEM_TRIGGER_SESSION_OK;
}

refmem_trigger_session_result_t refmem_trigger_session_arm_begin(
    refmem_trigger_session_t *session)
{
    if (session == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state != REFMEM_TRIGGER_SESSION_CONFIGURED) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    reset_runtime(session);
    session->state = REFMEM_TRIGGER_SESSION_ARMING;
    return REFMEM_TRIGGER_SESSION_OK;
}

refmem_trigger_session_result_t refmem_trigger_session_arm_source(
    refmem_trigger_session_t *session, bool ready)
{
    if (session == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state != REFMEM_TRIGGER_SESSION_ARMING) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    session->source_ready = ready;
    if (!ready) {
        session->armed_mask = 0u;
        session->last_reason = (uint32_t)REFMEM_TRIGGER_SESSION_SOURCE_NOT_READY;
        session->state = REFMEM_TRIGGER_SESSION_CONFIGURED;
        return REFMEM_TRIGGER_SESSION_SOURCE_NOT_READY;
    }
    if (required_all_armed(session)) {
        session->state = REFMEM_TRIGGER_SESSION_ARMED;
    }
    return REFMEM_TRIGGER_SESSION_OK;
}

refmem_trigger_session_result_t refmem_trigger_session_arm_report(
    refmem_trigger_session_t *session, uint32_t slot, bool ok)
{
    if (session == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (!SLOT_VALID(slot) || (session->target_mask & SLOT_BIT(slot)) == 0u) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state != REFMEM_TRIGGER_SESSION_ARMING &&
        session->state != REFMEM_TRIGGER_SESSION_ARMED) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    if (!ok) {
        fail_arming(session, REFMEM_TRIGGER_SESSION_TARGET_FAILED, slot);
        return REFMEM_TRIGGER_SESSION_TARGET_FAILED;
    }
    session->armed_mask |= SLOT_BIT(slot);
    if (session->source_ready && required_all_armed(session)) {
        session->state = REFMEM_TRIGGER_SESSION_ARMED;
    }
    return REFMEM_TRIGGER_SESSION_OK;
}

refmem_trigger_session_result_t refmem_trigger_session_start(
    refmem_trigger_session_t *session)
{
    if (session == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state != REFMEM_TRIGGER_SESSION_ARMED) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    session->state = REFMEM_TRIGGER_SESSION_RUNNING;
    return REFMEM_TRIGGER_SESSION_OK;
}

refmem_trigger_session_result_t refmem_trigger_session_target_failed(
    refmem_trigger_session_t *session, uint32_t slot)
{
    if (session == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (!SLOT_VALID(slot) || (session->target_mask & SLOT_BIT(slot)) == 0u) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state != REFMEM_TRIGGER_SESSION_RUNNING) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    session->failed_mask |= SLOT_BIT(slot);
    session->last_reason = (uint32_t)REFMEM_TRIGGER_SESSION_TARGET_FAILED;
    session->state = REFMEM_TRIGGER_SESSION_DEGRADED;
    return REFMEM_TRIGGER_SESSION_TARGET_FAILED;
}

refmem_trigger_session_result_t refmem_trigger_session_stop_begin(
    refmem_trigger_session_t *session)
{
    if (session == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state != REFMEM_TRIGGER_SESSION_RUNNING &&
        session->state != REFMEM_TRIGGER_SESSION_ARMED &&
        session->state != REFMEM_TRIGGER_SESSION_DEGRADED) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    session->state = REFMEM_TRIGGER_SESSION_STOPPING;
    return REFMEM_TRIGGER_SESSION_OK;
}

refmem_trigger_session_result_t refmem_trigger_session_stop_complete(
    refmem_trigger_session_t *session)
{
    if (session == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state != REFMEM_TRIGGER_SESSION_STOPPING) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    session->state = REFMEM_TRIGGER_SESSION_IDLE;
    reset_runtime(session);
    return REFMEM_TRIGGER_SESSION_OK;
}

refmem_trigger_session_result_t refmem_trigger_session_stop_timeout(
    refmem_trigger_session_t *session)
{
    if (session == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state != REFMEM_TRIGGER_SESSION_STOPPING) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    session->last_reason = (uint32_t)REFMEM_TRIGGER_SESSION_STOP_TIMEOUT;
    session->state = REFMEM_TRIGGER_SESSION_DEGRADED;
    return REFMEM_TRIGGER_SESSION_STOP_TIMEOUT;
}

refmem_trigger_session_result_t refmem_trigger_session_abort(
    refmem_trigger_session_t *session)
{
    if (session == NULL) {
        return REFMEM_TRIGGER_SESSION_BAD_ARGUMENT;
    }
    if (session->state == REFMEM_TRIGGER_SESSION_ABORTED) {
        return REFMEM_TRIGGER_SESSION_OK;
    }
    if (session->state == REFMEM_TRIGGER_SESSION_IDLE) {
        return REFMEM_TRIGGER_SESSION_INVALID_STATE;
    }
    session->state = REFMEM_TRIGGER_SESSION_ABORTED;
    session->armed_mask = 0u;
    session->source_ready = false;
    return REFMEM_TRIGGER_SESSION_OK;
}

refmem_trigger_session_result_t refmem_trigger_session_mount(
    refmem_trigger_session_t *session,
    const refmem_trigger_session_config_t *config)
{
    refmem_trigger_session_result_t rc;
    uint32_t slot;

    rc = refmem_trigger_session_configure(session, config);
    if (rc != REFMEM_TRIGGER_SESSION_OK) {
        return rc;
    }
    rc = refmem_trigger_session_arm_begin(session);
    if (rc != REFMEM_TRIGGER_SESSION_OK) {
        return rc;
    }
    rc = refmem_trigger_session_arm_source(session, true);
    if (rc != REFMEM_TRIGGER_SESSION_OK) {
        return rc;
    }
    for (slot = 0u; slot < 32u; ++slot) {
        if ((config->target_mask & SLOT_BIT(slot)) == 0u) {
            continue;
        }
        rc = refmem_trigger_session_arm_report(session, slot, true);
        if (rc != REFMEM_TRIGGER_SESSION_OK) {
            return rc;
        }
    }
    return refmem_trigger_session_start(session);
}
