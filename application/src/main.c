#include "app.h"
#include "board.h"
#include "diagnostics.h"
#include "osal.h"
#include "pico/stdlib.h"

int main(void)
{
    stdio_init_all();
    osal_delay_ms(1500u);

    if (!board_init()) {
        while (true) {
            board_status_led_toggle();
            osal_delay_ms(100u);
        }
    }

    if (!app_init()) {
        diagnostics_mark_fault("app", "application initialization failed");
    }

    while (true) {
        board_service();
        app_run_once();
    }
}
