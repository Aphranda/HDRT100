#include "osal.h"

#include "board.h"
#include "FreeRTOS.h"
#if PROJECT_USE_MULTICORE
#include "hardware/sync.h"
#include "hardware/sync/spin_lock.h"
#include "pico/platform.h"
#endif
#include "pico/stdlib.h"
#include "task.h"

struct osal_task_control_block {
    TaskHandle_t native_handle;
};

#define OSAL_TASK_REGISTRY_MAX 12u

typedef struct {
    const char *name;
    TaskHandle_t handle;
    uint32_t stack_words;
    uint32_t priority;
} osal_task_registry_entry_t;

static osal_task_registry_entry_t s_osal_task_registry[OSAL_TASK_REGISTRY_MAX];
static uint32_t s_osal_task_registry_count;

#if PROJECT_USE_MULTICORE
static spin_lock_t *s_osal_lock;
static uint32_t s_osal_irq_state[2];
static uint32_t s_osal_critical_depth[2];
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
    if (s_osal_task_registry_count < OSAL_TASK_REGISTRY_MAX) {
        osal_task_registry_entry_t *entry =
            &s_osal_task_registry[s_osal_task_registry_count++];
        entry->name = config->name != NULL ? config->name : "osal";
        entry->handle = native_handle;
        entry->stack_words = config->stack_words;
        entry->priority = config->priority;
    }
    return true;
}

void osal_kernel_start(void)
{
    vTaskStartScheduler();
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
    taskENTER_CRITICAL();
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
    taskEXIT_CRITICAL();
#endif
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

uint32_t osal_task_get_stats(osal_task_stats_t *stats, uint32_t max_count)
{
    const uint32_t count = s_osal_task_registry_count < max_count ?
                           s_osal_task_registry_count :
                           max_count;

    if (stats == NULL) {
        return s_osal_task_registry_count;
    }

    for (uint32_t i = 0u; i < count; i++) {
        const osal_task_registry_entry_t *entry = &s_osal_task_registry[i];
        stats[i].name = entry->name;
        stats[i].stack_words = entry->stack_words;
        stats[i].priority = entry->priority;
#if INCLUDE_uxTaskGetStackHighWaterMark
        stats[i].stack_free_words =
            (uint32_t)uxTaskGetStackHighWaterMark(entry->handle);
#else
        stats[i].stack_free_words = 0u;
#endif
    }

    return count;
}

void osal_heap_get_status(uint32_t *free_bytes, uint32_t *minimum_ever_free_bytes)
{
    if (free_bytes != NULL) {
        *free_bytes = (uint32_t)xPortGetFreeHeapSize();
    }
    if (minimum_ever_free_bytes != NULL) {
        *minimum_ever_free_bytes = (uint32_t)xPortGetMinimumEverFreeHeapSize();
    }
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
