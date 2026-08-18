#include "led_manager.h"

#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "osal.h"
#include "ota_ao.h"
#include "ota_vector.h"
#include "storage_manager.h"
#include "sync_trigger.h"
#include "system_manager.h"

#define LED_MANAGER_SERVICE_PERIOD_MS       20u
#define LED_MANAGER_TRIGGER_PULSE_MS        120u
#define LED_MANAGER_TRIGGER_PULSE_GAP_MS    500u
#define LED_MANAGER_CORE1_START_GRACE_MS    5000u
#define LED_MANAGER_CORE1_STALE_MS          3000u

typedef struct {
    led_manager_status_t status;
    bool initialized;
    bool ready_seen;
    bool core1_fault_logged;
    bool trigger_pulse_started;
    uint32_t last_service_ms;
    uint32_t ready_since_ms;
    uint32_t last_core1_progress_ms;
    uint32_t last_core1_loop_count;
    uint32_t last_trigger_count;
    uint32_t trigger_pulse_until_ms;
    uint32_t next_trigger_pulse_ms;
} led_manager_context_t;

static led_manager_context_t s_led_manager;

static bool led_manager_time_before(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) < 0;
}

static bool led_manager_pattern_level(led_manager_pattern_t pattern,
                                      uint32_t now_ms,
                                      bool event_level)
{
    const uint32_t phase_1000 = now_ms % 1000u;
    switch (pattern) {
    case LED_MANAGER_PATTERN_OFF:
        return false;
    case LED_MANAGER_PATTERN_ON:
        return true;
    case LED_MANAGER_PATTERN_HEARTBEAT:
        return phase_1000 < 100u;
    case LED_MANAGER_PATTERN_SLOW_BLINK:
        return phase_1000 < 500u;
    case LED_MANAGER_PATTERN_FAST_BLINK:
        return (now_ms % 200u) < 100u;
    case LED_MANAGER_PATTERN_DOUBLE_BLINK:
        return phase_1000 < 100u || (phase_1000 >= 200u && phase_1000 < 300u);
    case LED_MANAGER_PATTERN_EVENT_PULSE:
        return event_level;
    default:
        return false;
    }
}

static bool led_manager_trigger_is_configured(trig_state_t state)
{
    return state == TRIG_STATE_SEQ_CONFIGURED ||
           state == TRIG_STATE_ENC_CONFIGURED ||
           state == TRIG_STATE_BISS_CONFIGURED;
}

static bool led_manager_trigger_is_armed(trig_state_t state)
{
    return state == TRIG_STATE_SEQ_ARMED ||
           state == TRIG_STATE_ENC_ARMED ||
           state == TRIG_STATE_BISS_ARMED;
}

static bool led_manager_ota_is_active(uint32_t state)
{
    return state >= (uint32_t)OTA_STATE_CHECK_PERMISSION &&
           state <= (uint32_t)OTA_STATE_READY_TO_REBOOT;
}

static void led_manager_monitor_core1(bool app_ready,
                                      uint32_t now_ms,
                                      const diagnostics_core_status_t *core)
{
    if (!app_ready || !core->core1_enabled) {
        s_led_manager.ready_seen = false;
        s_led_manager.status.core1_stale = false;
        return;
    }

    if (!s_led_manager.ready_seen) {
        s_led_manager.ready_seen = true;
        s_led_manager.ready_since_ms = now_ms;
        s_led_manager.last_core1_progress_ms = now_ms;
        s_led_manager.last_core1_loop_count = core->core1_loop_count;
        return;
    }

    if (core->core1_loop_count != s_led_manager.last_core1_loop_count) {
        s_led_manager.last_core1_loop_count = core->core1_loop_count;
        s_led_manager.last_core1_progress_ms = now_ms;
        return;
    }

    if ((uint32_t)(now_ms - s_led_manager.ready_since_ms) >=
            LED_MANAGER_CORE1_START_GRACE_MS &&
        (uint32_t)(now_ms - s_led_manager.last_core1_progress_ms) >=
            LED_MANAGER_CORE1_STALE_MS) {
        s_led_manager.status.core1_stale = true;
        s_led_manager.status.health_flags |= LED_MANAGER_HEALTH_CORE1_STALE;
        if (!s_led_manager.core1_fault_logged) {
            s_led_manager.core1_fault_logged = true;
            diagnostics_mark_fault("led_health", "core1 heartbeat stale");
        }
    }
}

static void led_manager_set_patterns(led_manager_policy_t policy,
                                     led_manager_pattern_t system_pattern,
                                     led_manager_pattern_t arm_pattern,
                                     led_manager_pattern_t fault_pattern)
{
    led_manager_status_t *status = &s_led_manager.status;
    const bool changed = status->policy != policy ||
                         status->system_pattern != system_pattern ||
                         status->arm_pattern != arm_pattern ||
                         status->fault_pattern != fault_pattern;
    if (changed) {
        status->pattern_transition_count++;
        status->event_sequence++;
    }
    status->policy = policy;
    status->system_pattern = system_pattern;
    status->arm_pattern = arm_pattern;
    status->fault_pattern = fault_pattern;
}

void led_manager_init(void)
{
    memset(&s_led_manager, 0, sizeof(s_led_manager));
    s_led_manager.initialized = true;
    s_led_manager.last_service_ms = board_uptime_ms();
    s_led_manager.status.policy = LED_MANAGER_POLICY_BOOT;
    s_led_manager.status.system_pattern = LED_MANAGER_PATTERN_FAST_BLINK;
    s_led_manager.status.arm_pattern = LED_MANAGER_PATTERN_OFF;
    s_led_manager.status.fault_pattern = LED_MANAGER_PATTERN_OFF;
    board_led_set(BOARD_LED_SYSTEM, true);
    board_led_set(BOARD_LED_ARM_TRIGGER, false);
    board_led_set(BOARD_LED_FAULT, false);
}

void led_manager_service(bool app_ready)
{
    const uint32_t now_ms = board_uptime_ms();
    if (!s_led_manager.initialized ||
        (uint32_t)(now_ms - s_led_manager.last_service_ms) <
            LED_MANAGER_SERVICE_PERIOD_MS) {
        return;
    }
    s_led_manager.last_service_ms = now_ms;

    diagnostics_core_status_t core;
    ota_vector_t ota;
    storage_manager_vector_t storage;
    system_manager_config_gate_status_t gate;
    trigger_vector_t trigger;
    diagnostics_get_core_status(&core);
    ota_ao_get_vector(&ota);
    storage_manager_get_vector(&storage);
    system_manager_get_config_gate_status(&gate);
    sync_trigger_get_vector(&trigger);

    led_manager_monitor_core1(app_ready, now_ms, &core);

    const uint32_t trigger_delta =
        trigger.trigger_count >= s_led_manager.last_trigger_count ?
        trigger.trigger_count - s_led_manager.last_trigger_count :
        trigger.trigger_count;
    s_led_manager.last_trigger_count = trigger.trigger_count;
    if (trigger_delta != 0u) {
        s_led_manager.status.trigger_pulse_count += trigger_delta;
        s_led_manager.status.event_sequence += trigger_delta;
        if (!s_led_manager.trigger_pulse_started ||
            !led_manager_time_before(now_ms,
                                     s_led_manager.next_trigger_pulse_ms)) {
            s_led_manager.trigger_pulse_started = true;
            s_led_manager.trigger_pulse_until_ms =
                now_ms + LED_MANAGER_TRIGGER_PULSE_MS;
            s_led_manager.next_trigger_pulse_ms =
                now_ms + LED_MANAGER_TRIGGER_PULSE_GAP_MS;
        }
    }

    const bool trigger_pulse = led_manager_time_before(
        now_ms,
        s_led_manager.trigger_pulse_until_ms);
    const bool trigger_fault = trigger.state == TRIG_STATE_FAULT;
    const bool ota_failed = ota.state == (uint32_t)OTA_STATE_FAILED;
    const bool storage_failed = storage.state == STORAGE_MANAGER_STATE_FAILED;
    const bool fault_active = diagnostics_has_fault() || trigger_fault ||
                              ota_failed || storage_failed;
    if (fault_active != s_led_manager.status.fault_latched) {
        s_led_manager.status.fault_transition_count++;
        s_led_manager.status.event_sequence++;
    }

    s_led_manager.status.fault_latched = fault_active;
    s_led_manager.status.config_ready = app_ready && gate.ready;
    s_led_manager.status.sd_ready =
        storage.state == STORAGE_MANAGER_STATE_CARD_READY &&
        storage.card_present && storage.fs_mounted;
    s_led_manager.status.trigger_state = (uint32_t)trigger.state;
    s_led_manager.status.ota_state = ota.state;

    led_manager_policy_t policy;
    led_manager_pattern_t system_pattern;
    led_manager_pattern_t arm_pattern;
    led_manager_pattern_t fault_pattern;

    if (s_led_manager.status.health_flags != 0u) {
        policy = LED_MANAGER_POLICY_FATAL;
        system_pattern = LED_MANAGER_PATTERN_OFF;
        arm_pattern = LED_MANAGER_PATTERN_OFF;
        fault_pattern = LED_MANAGER_PATTERN_FAST_BLINK;
    } else if (fault_active) {
        policy = LED_MANAGER_POLICY_FAULT;
        system_pattern = LED_MANAGER_PATTERN_OFF;
        arm_pattern = LED_MANAGER_PATTERN_OFF;
        fault_pattern = LED_MANAGER_PATTERN_ON;
    } else if (led_manager_ota_is_active(ota.state)) {
        policy = LED_MANAGER_POLICY_OTA;
        system_pattern = LED_MANAGER_PATTERN_FAST_BLINK;
        arm_pattern = LED_MANAGER_PATTERN_OFF;
        fault_pattern = LED_MANAGER_PATTERN_OFF;
    } else if (!app_ready) {
        policy = LED_MANAGER_POLICY_BOOT;
        system_pattern = LED_MANAGER_PATTERN_FAST_BLINK;
        arm_pattern = LED_MANAGER_PATTERN_OFF;
        fault_pattern = LED_MANAGER_PATTERN_OFF;
    } else if (led_manager_trigger_is_armed(trigger.state)) {
        policy = LED_MANAGER_POLICY_ARMED;
        system_pattern = LED_MANAGER_PATTERN_HEARTBEAT;
        arm_pattern = trigger_pulse ? LED_MANAGER_PATTERN_EVENT_PULSE :
                                      LED_MANAGER_PATTERN_ON;
        fault_pattern = LED_MANAGER_PATTERN_OFF;
    } else if (!gate.ready) {
        policy = LED_MANAGER_POLICY_DEGRADED;
        system_pattern = LED_MANAGER_PATTERN_DOUBLE_BLINK;
        arm_pattern = led_manager_trigger_is_configured(trigger.state) ?
                      LED_MANAGER_PATTERN_SLOW_BLINK :
                      LED_MANAGER_PATTERN_OFF;
        fault_pattern = LED_MANAGER_PATTERN_OFF;
    } else {
        policy = LED_MANAGER_POLICY_NORMAL;
        system_pattern = LED_MANAGER_PATTERN_HEARTBEAT;
        if (trigger_pulse) {
            arm_pattern = LED_MANAGER_PATTERN_EVENT_PULSE;
        } else if (led_manager_trigger_is_configured(trigger.state)) {
            arm_pattern = LED_MANAGER_PATTERN_SLOW_BLINK;
        } else {
            arm_pattern = LED_MANAGER_PATTERN_OFF;
        }
        fault_pattern = LED_MANAGER_PATTERN_OFF;
    }

    led_manager_set_patterns(policy,
                             system_pattern,
                             arm_pattern,
                             fault_pattern);

    const bool system_level = led_manager_pattern_level(
        system_pattern,
        now_ms,
        trigger_pulse);
    const bool arm_level = led_manager_pattern_level(
        arm_pattern,
        now_ms,
        trigger_pulse && !led_manager_trigger_is_armed(trigger.state));
    const bool fault_level = led_manager_pattern_level(
        fault_pattern,
        now_ms,
        false);

    osal_critical_enter();
    s_led_manager.status.system_level = system_level;
    s_led_manager.status.arm_level = arm_level;
    s_led_manager.status.fault_level = fault_level;
    osal_critical_exit();

    board_led_set(BOARD_LED_SYSTEM, system_level);
    board_led_set(BOARD_LED_ARM_TRIGGER, arm_level);
    board_led_set(BOARD_LED_FAULT, fault_level);
}

void led_manager_get_status(led_manager_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_led_manager.status;
    osal_critical_exit();
}

const char *led_manager_policy_string(led_manager_policy_t policy)
{
    switch (policy) {
    case LED_MANAGER_POLICY_BOOT:     return "BOOT";
    case LED_MANAGER_POLICY_NORMAL:   return "NORMAL";
    case LED_MANAGER_POLICY_DEGRADED: return "DEGRADED";
    case LED_MANAGER_POLICY_ARMED:    return "ARMED";
    case LED_MANAGER_POLICY_OTA:      return "OTA";
    case LED_MANAGER_POLICY_FAULT:    return "FAULT";
    case LED_MANAGER_POLICY_FATAL:    return "FATAL";
    default:                          return "UNKNOWN";
    }
}

const char *led_manager_pattern_string(led_manager_pattern_t pattern)
{
    switch (pattern) {
    case LED_MANAGER_PATTERN_OFF:          return "OFF";
    case LED_MANAGER_PATTERN_ON:           return "ON";
    case LED_MANAGER_PATTERN_HEARTBEAT:    return "HEARTBEAT";
    case LED_MANAGER_PATTERN_SLOW_BLINK:   return "SLOW_BLINK";
    case LED_MANAGER_PATTERN_FAST_BLINK:   return "FAST_BLINK";
    case LED_MANAGER_PATTERN_DOUBLE_BLINK: return "DOUBLE_BLINK";
    case LED_MANAGER_PATTERN_EVENT_PULSE:  return "EVENT_PULSE";
    default:                               return "UNKNOWN";
    }
}
