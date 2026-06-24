#include "osal.h"

#include "board.h"
#include "FreeRTOS.h"
#include "pico/stdlib.h"
#include "task.h"

struct osal_task_control_block {
    TaskHandle_t native_handle;
};

bool osal_kernel_init(void)
{
    return true;
}

bool osal_task_create(const osal_task_config_t *config, osal_task_handle_t *handle)
{
    if ((config == NULL) || (config->entry == NULL)) {
        return false;
    }

    TaskHandle_t native_handle = NULL;
    const BaseType_t result = xTaskCreate(config->entry,
                                          config->name != NULL ? config->name : "osal",
                                          config->stack_words,
                                          config->context,
                                          (UBaseType_t)config->priority,
                                          &native_handle);
    if (result != pdPASS) {
        return false;
    }

    if (handle != NULL) {
        *handle = (osal_task_handle_t)native_handle;
    }
    return true;
}

void osal_kernel_start(void)
{
    vTaskStartScheduler();
}

void osal_critical_enter(void)
{
    taskENTER_CRITICAL();
}

void osal_critical_exit(void)
{
    taskEXIT_CRITICAL();
}

void osal_delay_ms(uint32_t delay_ms)
{
    osal_task_delay_ms(delay_ms);
}

void osal_task_delay_ms(uint32_t delay_ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        sleep_ms(delay_ms);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

uint32_t osal_uptime_ms(void)
{
    return osal_tick_ms();
}

uint32_t osal_tick_ms(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return to_ms_since_boot(get_absolute_time());
    }

    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (true) {
        board_status_led_toggle();
        sleep_ms(100u);
    }
}

void vApplicationAssertHook(const char *file, int line)
{
    (void)file;
    (void)line;

    taskDISABLE_INTERRUPTS();
    while (true) {
        board_status_led_toggle();
        sleep_ms(100u);
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;

    taskDISABLE_INTERRUPTS();
    while (true) {
        board_status_led_toggle();
        sleep_ms(100u);
    }
}
