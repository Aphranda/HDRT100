#include "app.h"

#include "board.h"
#include "diagnostics.h"
#include "osal.h"
#include "project_config.h"
#include "sync_config_ui.h"

static uint32_t s_last_tick_ms;

bool app_init(void)
{
    s_last_tick_ms = board_uptime_ms();
    LOG_INFO("app", "application initialized");

    if (!sync_config_ui_init()) {
        diagnostics_mark_fault("ui", "sync config UI initialization failed");
        return false;
    }
    sync_config_ui_render();

    return true;
}

void app_run_once(void)
{
    const uint32_t now_ms = board_uptime_ms();

    if ((uint32_t)(now_ms - s_last_tick_ms) >= PROJECT_LOOP_PERIOD_MS) {
        s_last_tick_ms = now_ms;
        diagnostics_heartbeat(PROJECT_HEALTH_LOG_PERIOD_MS);
    }

    osal_delay_ms(1u);
}
