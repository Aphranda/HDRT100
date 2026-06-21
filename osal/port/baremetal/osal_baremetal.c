#include "osal.h"

#include "pico/stdlib.h"

void osal_delay_ms(uint32_t delay_ms)
{
    sleep_ms(delay_ms);
}

uint32_t osal_uptime_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}
