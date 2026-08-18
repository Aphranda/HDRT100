#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LED_MANAGER_PATTERN_OFF = 0,
    LED_MANAGER_PATTERN_ON,
    LED_MANAGER_PATTERN_HEARTBEAT,
    LED_MANAGER_PATTERN_SLOW_BLINK,
    LED_MANAGER_PATTERN_FAST_BLINK,
    LED_MANAGER_PATTERN_DOUBLE_BLINK,
    LED_MANAGER_PATTERN_EVENT_PULSE,
    LED_MANAGER_PATTERN_COUNT,
} led_manager_pattern_t;

typedef enum {
    LED_MANAGER_POLICY_BOOT = 0,
    LED_MANAGER_POLICY_NORMAL,
    LED_MANAGER_POLICY_DEGRADED,
    LED_MANAGER_POLICY_ARMED,
    LED_MANAGER_POLICY_OTA,
    LED_MANAGER_POLICY_FAULT,
    LED_MANAGER_POLICY_FATAL,
    LED_MANAGER_POLICY_COUNT,
} led_manager_policy_t;

enum {
    LED_MANAGER_HEALTH_CORE1_STALE = 1u << 0,
};

typedef struct {
    led_manager_policy_t policy;
    led_manager_pattern_t system_pattern;
    led_manager_pattern_t arm_pattern;
    led_manager_pattern_t fault_pattern;
    bool system_level;
    bool arm_level;
    bool fault_level;
    bool fault_latched;
    bool config_ready;
    bool sd_ready;
    bool core1_stale;
    uint32_t health_flags;
    uint32_t trigger_state;
    uint32_t ota_state;
    uint32_t event_sequence;
    uint32_t trigger_pulse_count;
    uint32_t fault_transition_count;
    uint32_t pattern_transition_count;
} led_manager_status_t;

void led_manager_init(void);
void led_manager_service(bool app_ready);
void led_manager_get_status(led_manager_status_t *status);
const char *led_manager_policy_string(led_manager_policy_t policy);
const char *led_manager_pattern_string(led_manager_pattern_t pattern);

#endif
