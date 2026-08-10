#include "osal.h"

#include "hardware/sync.h"
#if PROJECT_USE_MULTICORE
#include "hardware/sync/spin_lock.h"
#include "pico/platform.h"
#endif
#include "pico/stdlib.h"

#if PROJECT_USE_MULTICORE
static spin_lock_t *s_osal_lock;
static uint32_t s_osal_irq_state[2];
static uint32_t s_osal_critical_depth[2];
#else
static uint32_t s_osal_irq_state;
static uint32_t s_osal_critical_depth;
#endif

bool osal_kernel_init(void)
{
#if PROJECT_USE_MULTICORE
    const int lock_num = spin_lock_claim_unused(false);
    if (lock_num < 0) {
        return false;
    }
    s_osal_lock = spin_lock_instance((uint)lock_num);
#endif
    return true;
}

bool osal_task_create(const osal_task_config_t *config, osal_task_handle_t *handle)
{
    (void)config;
    if (handle != NULL) {
        *handle = NULL;
    }
    return false;
}

void osal_kernel_start(void)
{
}

void osal_critical_enter(void)
{
#if PROJECT_USE_MULTICORE
    const uint core = get_core_num();
    if (s_osal_critical_depth[core] == 0u) {
        s_osal_irq_state[core] = spin_lock_blocking(s_osal_lock);
    }
    s_osal_critical_depth[core]++;
#else
    if (s_osal_critical_depth == 0u) {
        s_osal_irq_state = save_and_disable_interrupts();
    }
    s_osal_critical_depth++;
#endif
}

void osal_critical_exit(void)
{
#if PROJECT_USE_MULTICORE
    const uint core = get_core_num();
    if (s_osal_critical_depth[core] == 0u) {
        return;
    }

    s_osal_critical_depth[core]--;
    if (s_osal_critical_depth[core] == 0u) {
        spin_unlock(s_osal_lock, s_osal_irq_state[core]);
    }
#else
    if (s_osal_critical_depth == 0u) {
        return;
    }

    s_osal_critical_depth--;
    if (s_osal_critical_depth == 0u) {
        restore_interrupts(s_osal_irq_state);
    }
#endif
}

void osal_delay_ms(uint32_t delay_ms)
{
    sleep_ms(delay_ms);
}

void osal_task_delay_ms(uint32_t delay_ms)
{
    osal_delay_ms(delay_ms);
}

uint32_t osal_uptime_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

uint32_t osal_tick_ms(void)
{
    return osal_uptime_ms();
}

uint32_t osal_task_get_stats(osal_task_stats_t *stats, uint32_t max_count)
{
    (void)stats;
    (void)max_count;
    return 0u;
}

void osal_heap_get_status(uint32_t *free_bytes, uint32_t *minimum_ever_free_bytes)
{
    if (free_bytes != NULL) {
        *free_bytes = 0u;
    }
    if (minimum_ever_free_bytes != NULL) {
        *minimum_ever_free_bytes = 0u;
    }
}
