#ifndef DRV_FLASH_LOCKOUT_H
#define DRV_FLASH_LOCKOUT_H

#include <stdbool.h>
#include <stdint.h>

#define DRV_FLASH_LOCKOUT_DEFAULT_WAIT_LOOPS 1000000u

typedef enum {
    DRV_FLASH_LOCKOUT_PARK_OFFLINE = 0,
    DRV_FLASH_LOCKOUT_PARK_IDLE = 1,
    DRV_FLASH_LOCKOUT_PARK_REQUESTED = 2,
    DRV_FLASH_LOCKOUT_PARK_PARKED = 3,
    DRV_FLASH_LOCKOUT_PARK_RELEASING = 4,
    DRV_FLASH_LOCKOUT_PARK_TIMEOUT = 5,
    DRV_FLASH_LOCKOUT_PARK_FAULT = 6,
} drv_flash_lockout_park_state_t;

typedef enum {
    DRV_FLASH_LOCKOUT_RESULT_NONE = 0,
    DRV_FLASH_LOCKOUT_RESULT_ACKED = 1,
    DRV_FLASH_LOCKOUT_RESULT_TIMEOUT = 2,
    DRV_FLASH_LOCKOUT_RESULT_RELEASE_TIMEOUT = 3,
    DRV_FLASH_LOCKOUT_RESULT_FAULT_INJECTED = 4,
} drv_flash_lockout_result_t;

typedef enum {
    DRV_FLASH_LOCKOUT_FAULT_NONE = 0u,
    DRV_FLASH_LOCKOUT_FAULT_CORE1_NO_ACK = 1u << 0,
    DRV_FLASH_LOCKOUT_FAULT_CORE1_RELEASE_STUCK = 1u << 1,
} drv_flash_lockout_fault_t;

typedef struct {
    bool core1_lockout_supported;
    bool core1_lockout_online;
    bool core1_lockout_requested;
    bool core1_lockout_acknowledged;
    uint32_t wait_loop_budget;
    uint32_t park_state;
    uint32_t request_seq;
    uint32_t ack_seq;
    uint32_t release_seq;
    uint32_t timeout_count;
    uint32_t release_timeout_count;
    uint32_t fault_injection_flags;
    uint32_t last_result;
    uint32_t last_elapsed_us;
} drv_flash_lockout_status_t;

void drv_flash_lockout_init(bool supported);
void drv_flash_lockout_core1_poll(void);
bool drv_flash_lockout_begin(uint32_t wait_loop_budget);
void drv_flash_lockout_end(uint32_t wait_loop_budget);
void drv_flash_lockout_get_status(drv_flash_lockout_status_t *status);
void drv_flash_lockout_set_fault_injection(uint32_t flags);
void drv_flash_lockout_clear_fault_injection(void);

#endif
