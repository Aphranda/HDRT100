#include "drv_flash_lockout.h"

#include <stddef.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#if DRV_FLASH_LOCKOUT_PICO_RAM
#include "pico.h"
#define DRV_FLASH_LOCKOUT_CORE1_POLL_DEF \
    void __not_in_flash_func(drv_flash_lockout_core1_poll)(void)
#else
#define DRV_FLASH_LOCKOUT_CORE1_POLL_DEF void drv_flash_lockout_core1_poll(void)
#endif

#ifndef DRV_FLASH_LOCKOUT_WAIT_INSTRUCTION
#if DRV_FLASH_LOCKOUT_USE_ARM_EVENTS
#define DRV_FLASH_LOCKOUT_WAIT_INSTRUCTION() __asm volatile("wfe")
#elif defined(_WIN32)
#define DRV_FLASH_LOCKOUT_WAIT_INSTRUCTION() Sleep(0)
#else
#define DRV_FLASH_LOCKOUT_WAIT_INSTRUCTION() ((void)0)
#endif
#endif

#ifndef DRV_FLASH_LOCKOUT_WAKE_INSTRUCTION
#if DRV_FLASH_LOCKOUT_USE_ARM_EVENTS
#define DRV_FLASH_LOCKOUT_WAKE_INSTRUCTION() __asm volatile("sev")
#else
#define DRV_FLASH_LOCKOUT_WAKE_INSTRUCTION() ((void)0)
#endif
#endif

#ifndef DRV_FLASH_LOCKOUT_SPIN_INSTRUCTION
#if DRV_FLASH_LOCKOUT_USE_ARM_EVENTS
#define DRV_FLASH_LOCKOUT_SPIN_INSTRUCTION() __asm volatile("nop")
#elif defined(_WIN32)
#define DRV_FLASH_LOCKOUT_SPIN_INSTRUCTION() Sleep(0)
#else
#define DRV_FLASH_LOCKOUT_SPIN_INSTRUCTION() ((void)0)
#endif
#endif

typedef struct {
    volatile bool supported;
    volatile bool online;
    volatile bool requested;
    volatile bool acknowledged;
    volatile uint32_t park_state;
    volatile uint32_t request_seq;
    volatile uint32_t ack_seq;
    volatile uint32_t release_seq;
    volatile uint32_t timeout_count;
    volatile uint32_t release_timeout_count;
    volatile uint32_t fault_injection_flags;
    volatile uint32_t last_result;
} drv_flash_lockout_context_t;

static drv_flash_lockout_context_t s_lockout = {
    .supported = false,
    .online = false,
    .requested = false,
    .acknowledged = false,
    .park_state = DRV_FLASH_LOCKOUT_PARK_OFFLINE,
};

void drv_flash_lockout_init(bool supported)
{
    s_lockout.supported = supported;
    s_lockout.online = false;
    s_lockout.requested = false;
    s_lockout.acknowledged = false;
    s_lockout.park_state = supported ?
                               (uint32_t)DRV_FLASH_LOCKOUT_PARK_OFFLINE :
                               (uint32_t)DRV_FLASH_LOCKOUT_PARK_IDLE;
    s_lockout.request_seq = 0u;
    s_lockout.ack_seq = 0u;
    s_lockout.release_seq = 0u;
    s_lockout.timeout_count = 0u;
    s_lockout.release_timeout_count = 0u;
    s_lockout.fault_injection_flags = 0u;
    s_lockout.last_result = DRV_FLASH_LOCKOUT_RESULT_NONE;
}

DRV_FLASH_LOCKOUT_CORE1_POLL_DEF
{
    if (!s_lockout.supported) {
        return;
    }

    s_lockout.online = true;
    if (!s_lockout.requested) {
        if (!s_lockout.acknowledged) {
            s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_IDLE;
        }
        return;
    }

    if ((s_lockout.fault_injection_flags &
         DRV_FLASH_LOCKOUT_FAULT_CORE1_NO_ACK) != 0u) {
        return;
    }

    s_lockout.acknowledged = true;
    s_lockout.ack_seq = s_lockout.request_seq;
    s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_PARKED;
    s_lockout.last_result = DRV_FLASH_LOCKOUT_RESULT_ACKED;
    DRV_FLASH_LOCKOUT_WAKE_INSTRUCTION();

    while (s_lockout.requested) {
        if ((s_lockout.fault_injection_flags &
             DRV_FLASH_LOCKOUT_FAULT_CORE1_RELEASE_STUCK) != 0u) {
            continue;
        }
        DRV_FLASH_LOCKOUT_WAIT_INSTRUCTION();
    }

    s_lockout.acknowledged = false;
    s_lockout.release_seq++;
    s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_IDLE;
    DRV_FLASH_LOCKOUT_WAKE_INSTRUCTION();
}

bool drv_flash_lockout_begin(uint32_t wait_loop_budget)
{
    if (!s_lockout.supported) {
        s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_IDLE;
        s_lockout.last_result = DRV_FLASH_LOCKOUT_RESULT_NONE;
        return true;
    }

    if (!s_lockout.online) {
        s_lockout.timeout_count++;
        s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_OFFLINE;
        s_lockout.last_result = DRV_FLASH_LOCKOUT_RESULT_TIMEOUT;
        return false;
    }

    if ((s_lockout.fault_injection_flags &
         DRV_FLASH_LOCKOUT_FAULT_CORE1_NO_ACK) != 0u) {
        s_lockout.request_seq++;
        s_lockout.requested = true;
        s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_REQUESTED;
        s_lockout.last_result = DRV_FLASH_LOCKOUT_RESULT_FAULT_INJECTED;
        s_lockout.timeout_count++;
        s_lockout.requested = false;
        s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_FAULT;
        DRV_FLASH_LOCKOUT_WAKE_INSTRUCTION();
        return false;
    }

    s_lockout.request_seq++;
    s_lockout.requested = true;
    s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_REQUESTED;
    DRV_FLASH_LOCKOUT_WAKE_INSTRUCTION();

    for (uint32_t i = 0u; i < wait_loop_budget; i++) {
        if (s_lockout.acknowledged &&
            s_lockout.ack_seq == s_lockout.request_seq) {
            s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_PARKED;
            s_lockout.last_result = DRV_FLASH_LOCKOUT_RESULT_ACKED;
            return true;
        }
        DRV_FLASH_LOCKOUT_SPIN_INSTRUCTION();
    }

    s_lockout.timeout_count++;
    s_lockout.requested = false;
    s_lockout.last_result = DRV_FLASH_LOCKOUT_RESULT_TIMEOUT;
    s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_TIMEOUT;
    DRV_FLASH_LOCKOUT_WAKE_INSTRUCTION();
    return false;
}

void drv_flash_lockout_end(uint32_t wait_loop_budget)
{
    if (!s_lockout.supported || !s_lockout.online) {
        s_lockout.requested = false;
        s_lockout.acknowledged = false;
        s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_IDLE;
        return;
    }

    s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_RELEASING;
    s_lockout.requested = false;
    DRV_FLASH_LOCKOUT_WAKE_INSTRUCTION();

    for (uint32_t i = 0u; i < wait_loop_budget; i++) {
        if (!s_lockout.acknowledged) {
            s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_IDLE;
            return;
        }
        DRV_FLASH_LOCKOUT_SPIN_INSTRUCTION();
    }

    s_lockout.release_timeout_count++;
    s_lockout.last_result = DRV_FLASH_LOCKOUT_RESULT_RELEASE_TIMEOUT;
    s_lockout.park_state = DRV_FLASH_LOCKOUT_PARK_FAULT;
}

void drv_flash_lockout_get_status(drv_flash_lockout_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->core1_lockout_supported = s_lockout.supported;
    status->core1_lockout_online = s_lockout.online;
    status->core1_lockout_requested = s_lockout.requested;
    status->core1_lockout_acknowledged = s_lockout.acknowledged;
    status->wait_loop_budget = DRV_FLASH_LOCKOUT_DEFAULT_WAIT_LOOPS;
    status->park_state = s_lockout.park_state;
    status->request_seq = s_lockout.request_seq;
    status->ack_seq = s_lockout.ack_seq;
    status->release_seq = s_lockout.release_seq;
    status->timeout_count = s_lockout.timeout_count;
    status->release_timeout_count = s_lockout.release_timeout_count;
    status->fault_injection_flags = s_lockout.fault_injection_flags;
    status->last_result = s_lockout.last_result;
}

void drv_flash_lockout_set_fault_injection(uint32_t flags)
{
    s_lockout.fault_injection_flags = flags;
}

void drv_flash_lockout_clear_fault_injection(void)
{
    s_lockout.fault_injection_flags = 0u;
}
