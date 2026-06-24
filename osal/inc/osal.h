#ifndef OSAL_H
#define OSAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*osal_task_fn_t)(void *context);

typedef struct osal_task_control_block osal_task_control_block_t;
typedef osal_task_control_block_t *osal_task_handle_t;

typedef struct {
    const char *name;
    osal_task_fn_t entry;
    void *context;
    uint32_t stack_words;
    uint32_t priority;
} osal_task_config_t;

bool osal_kernel_init(void);
bool osal_task_create(const osal_task_config_t *config, osal_task_handle_t *handle);
void osal_kernel_start(void);
void osal_critical_enter(void);
void osal_critical_exit(void);
void osal_delay_ms(uint32_t delay_ms);
void osal_task_delay_ms(uint32_t delay_ms);
uint32_t osal_uptime_ms(void);
uint32_t osal_tick_ms(void);

#endif
