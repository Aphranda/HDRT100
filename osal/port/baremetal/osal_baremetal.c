#include "osal.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"

static uint32_t s_osal_irq_state;
static uint32_t s_osal_critical_depth;

bool osal_kernel_init(void)
{
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
    if (s_osal_critical_depth == 0u) {
        s_osal_irq_state = save_and_disable_interrupts();
    }
    s_osal_critical_depth++;
}

void osal_critical_exit(void)
{
    if (s_osal_critical_depth == 0u) {
        return;
    }

    s_osal_critical_depth--;
    if (s_osal_critical_depth == 0u) {
        restore_interrupts(s_osal_irq_state);
    }
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
