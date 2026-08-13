#include "app_runtime.h"

int main(void)
{
    if (!app_runtime_init()) {
        app_runtime_fault_forever();
    }

    app_runtime_run();
    return 0;
}
