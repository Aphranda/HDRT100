#ifndef REFMEM_TRIGGER_SESSION_H
#define REFMEM_TRIGGER_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REFMEM_TRIGGER_SESSION_IDLE = 0u,
    REFMEM_TRIGGER_SESSION_CONFIGURED = 1u,
    REFMEM_TRIGGER_SESSION_ARMING = 2u,
    REFMEM_TRIGGER_SESSION_ARMED = 3u,
    REFMEM_TRIGGER_SESSION_RUNNING = 4u,
    REFMEM_TRIGGER_SESSION_DEGRADED = 5u,
    REFMEM_TRIGGER_SESSION_STOPPING = 6u,
    REFMEM_TRIGGER_SESSION_ABORTED = 7u,
    REFMEM_TRIGGER_SESSION_FAULT = 8u,
} refmem_trigger_session_state_t;

typedef enum {
    REFMEM_TRIGGER_SESSION_OK = 0u,
    REFMEM_TRIGGER_SESSION_BAD_ARGUMENT = 1u,
    REFMEM_TRIGGER_SESSION_INVALID_STATE = 2u,
    REFMEM_TRIGGER_SESSION_TARGET_FAILED = 3u,
    REFMEM_TRIGGER_SESSION_SOURCE_NOT_READY = 4u,
    REFMEM_TRIGGER_SESSION_STOP_TIMEOUT = 5u,
} refmem_trigger_session_result_t;

typedef struct {
    uint32_t session_id;
    uint32_t run_id;
    uint32_t source_slot;
    uint32_t target_mask;
    uint32_t required_mask;
} refmem_trigger_session_config_t;

typedef struct {
    refmem_trigger_session_state_t state;
    uint32_t session_id;
    uint32_t run_id;
    uint32_t source_slot;
    uint32_t target_mask;
    uint32_t required_mask;
    uint32_t armed_mask;
    uint32_t failed_mask;
    uint32_t last_reason;
    bool source_ready;
} refmem_trigger_session_t;

void refmem_trigger_session_init(refmem_trigger_session_t *session);
refmem_trigger_session_result_t refmem_trigger_session_configure(
    refmem_trigger_session_t *session,
    const refmem_trigger_session_config_t *config);
refmem_trigger_session_result_t refmem_trigger_session_arm_begin(
    refmem_trigger_session_t *session);
refmem_trigger_session_result_t refmem_trigger_session_arm_source(
    refmem_trigger_session_t *session, bool ready);
refmem_trigger_session_result_t refmem_trigger_session_arm_report(
    refmem_trigger_session_t *session, uint32_t slot, bool ok);
refmem_trigger_session_result_t refmem_trigger_session_start(
    refmem_trigger_session_t *session);
refmem_trigger_session_result_t refmem_trigger_session_target_failed(
    refmem_trigger_session_t *session, uint32_t slot);
refmem_trigger_session_result_t refmem_trigger_session_stop_begin(
    refmem_trigger_session_t *session);
refmem_trigger_session_result_t refmem_trigger_session_stop_complete(
    refmem_trigger_session_t *session);
refmem_trigger_session_result_t refmem_trigger_session_stop_timeout(
    refmem_trigger_session_t *session);
refmem_trigger_session_result_t refmem_trigger_session_abort(
    refmem_trigger_session_t *session);
refmem_trigger_session_result_t refmem_trigger_session_mount(
    refmem_trigger_session_t *session,
    const refmem_trigger_session_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_TRIGGER_SESSION_H */
