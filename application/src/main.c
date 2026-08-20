#include "app_runtime.h"
#include "drv_watchdog.h"

int main(void)
{
    /* A hardware-watchdog reset leaves the peripheral enabled.  Stop the
     * inherited countdown before the staged board/application bring-up; the
     * system supervisor enables it again after all required tasks are live. */
    drv_watchdog_disable();

    if (!app_runtime_init()) {
        app_runtime_fault_forever();
    }

    app_runtime_run();
    return 0;
}
