#ifndef OSAL_H
#define OSAL_H

#include <stdint.h>

void osal_delay_ms(uint32_t delay_ms);
uint32_t osal_uptime_ms(void);

#endif
