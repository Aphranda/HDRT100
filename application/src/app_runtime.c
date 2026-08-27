#include "app.h"
#include "app_runtime.h"
#include "app_tasks.h"
#include "board.h"
#include "board_config.h"
#include "diagnostics.h"
#include "drv_flash.h"
#include "led_manager.h"
#include "hardware/regs/m33.h"
#include "osal.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "project_config.h"
#include "tdma_pio_spi_phys.h"
#include "tdma_flight_engine.h"
#include "tdma_process_image_layout.h"

#if !PROJECT_USE_FREERTOS || !PROJECT_USE_MULTICORE
#error "feature/rtos-multicore-haofv requires PROJECT_USE_FREERTOS=ON and PROJECT_USE_MULTICORE=ON"
#endif

void app_runtime_fault_forever(void)
{
    while (true) {
        board_status_led_toggle();
        osal_delay_ms(100u);
    }
}

bool app_runtime_bringup(void)
{
    stdio_init_all();
    osal_delay_ms(1500u);

    if (!board_init()) {
        app_runtime_fault_forever();
    }
    led_manager_init();

    if (!app_init()) {
        diagnostics_mark_fault("app", "application initialization failed");
    }

    return true;
}

static bool s_core1_started;

#define APP_REALTIME_WIRE_MAX_BYTES \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_SHORT_PACKET_MAX + \
     TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES)
#define APP_REALTIME_SPI_CYCLES_PER_BIT \
    (BOARD_SYS_CLOCK_HZ / BOARD_TDMA_SPI_BAUD_HZ)
#define APP_REALTIME_WIRE_MAX_CYCLES \
    (APP_REALTIME_WIRE_MAX_BYTES * 8u * APP_REALTIME_SPI_CYCLES_PER_BIT)

_Static_assert(BOARD_SYS_CLOCK_HZ % PROJECT_CORE1_CYCLE_RATE_HZ == 0u,
               "core1 cycle rate must divide clk_sys exactly");
_Static_assert(PROJECT_CORE1_CYCLE_CYCLES ==
                   BOARD_SYS_CLOCK_HZ / PROJECT_CORE1_CYCLE_RATE_HZ,
               "core1 cycle cycles must track the board clock");
_Static_assert(BOARD_SYS_CLOCK_HZ % BOARD_TDMA_SPI_BAUD_HZ == 0u,
               "TDMA SPI bit time must be an integer clk_sys cycle count");
_Static_assert(PROJECT_CORE1_CYCLE_CYCLES <= M33_SYST_RVR_RELOAD_BITS,
               "one core1 cycle must fit in the core-local SysTick counter");

#define APP_REALTIME_ASSERT_PHASE(name, start, end, wcet) \
    _Static_assert((start) < (end), #name " phase must be non-empty"); \
    _Static_assert((wcet) <= (end) - (start), \
                   #name " WCET must fit its own phase"); \
    _Static_assert((end) <= PROJECT_CORE1_CYCLE_CYCLES, \
                   #name " phase must fit the core1 cycle");
APP_REALTIME_PHASE_TABLE(APP_REALTIME_ASSERT_PHASE)
#undef APP_REALTIME_ASSERT_PHASE

_Static_assert(PROJECT_CORE1_PHASE_TDMA_START_CYCLE == 0u,
               "TDMA must own the first cycle");
_Static_assert(PROJECT_CORE1_PHASE_TDMA_END_CYCLE <=
                   PROJECT_CORE1_PHASE_VDC_START_CYCLE &&
               PROJECT_CORE1_PHASE_VDC_END_CYCLE <=
                   PROJECT_CORE1_PHASE_DPLL_START_CYCLE &&
               PROJECT_CORE1_PHASE_DPLL_END_CYCLE <=
                   PROJECT_CORE1_PHASE_CALIBRATION_START_CYCLE &&
               PROJECT_CORE1_PHASE_CALIBRATION_END_CYCLE <=
                   PROJECT_CORE1_PHASE_SYNC_CAPTURE_START_CYCLE &&
               PROJECT_CORE1_PHASE_SYNC_CAPTURE_END_CYCLE <=
                   PROJECT_CORE1_PHASE_REFMEM_START_CYCLE &&
               PROJECT_CORE1_PHASE_REFMEM_END_CYCLE <=
                   PROJECT_CORE1_PHASE_MODEL_START_CYCLE &&
               PROJECT_CORE1_PHASE_MODEL_END_CYCLE <=
                   PROJECT_CORE1_PHASE_SYNC_TRIGGER_START_CYCLE &&
               PROJECT_CORE1_PHASE_SYNC_TRIGGER_END_CYCLE <=
                   PROJECT_CORE1_PHASE_TRIGGER_MEASURE_START_CYCLE &&
               PROJECT_CORE1_PHASE_TRIGGER_MEASURE_END_CYCLE <=
                   PROJECT_CORE1_PHASE_GUARD_START_CYCLE &&
               PROJECT_CORE1_PHASE_GUARD_END_CYCLE ==
                   PROJECT_CORE1_CYCLE_CYCLES,
               "core1 phases must be ordered, disjoint, and fill the cycle");
_Static_assert(APP_REALTIME_WIRE_MAX_CYCLES <=
                   PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES,
               "maximum flight wire time must fit the TDMA phase WCET");
_Static_assert(TDMA_FLIGHT_SHORT_PAYLOAD_SIZE <=
                   TDMA_TRANSPORT_SHORT_PAYLOAD_MAX,
               "fixed process image must fit one short transport payload");
_Static_assert(TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE +
                       TDMA_FLIGHT_MAILBOX_BODY_SIZE ==
                   TDMA_FLIGHT_SHORT_SLOT_SIZE,
               "Node mailbox header and shared body must exactly fill a slot");
_Static_assert(TDMA_PROCESS_IMAGE_MANDATORY_BODY_SIZE <=
                   TDMA_FLIGHT_MAILBOX_BODY_SIZE,
               "mandatory process-image load must fit the Node body");
_Static_assert(TDMA_PROCESS_IMAGE_VDC_OFFSET ==
                   TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE &&
               TDMA_PROCESS_IMAGE_REFMEM_OFFSET ==
                   TDMA_PROCESS_IMAGE_VDC_OFFSET +
                       TDMA_PROCESS_IMAGE_VDC_SIZE &&
               TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET ==
                   TDMA_PROCESS_IMAGE_REFMEM_OFFSET +
                       TDMA_PROCESS_IMAGE_REFMEM_SIZE &&
               TDMA_PROCESS_IMAGE_CONTROL_OFFSET ==
                   TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET +
                       TDMA_PROCESS_IMAGE_ACK_QUALITY_SIZE &&
               TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET ==
                   TDMA_PROCESS_IMAGE_CONTROL_OFFSET +
                       TDMA_PROCESS_IMAGE_CONTROL_SIZE &&
               TDMA_PROCESS_IMAGE_CRC_OFFSET ==
                   TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET +
                       TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_SIZE,
               "process-image regions must be contiguous and non-overlapping");
_Static_assert(TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_SIZE <=
                   TDMA_PROCESS_IMAGE_OPTIONAL_BODY_CAPACITY,
               "optional process-image load may use only residual capacity");
_Static_assert(TDMA_PROCESS_IMAGE_CONFIGURED_BODY_SIZE ==
                   TDMA_FLIGHT_MAILBOX_BODY_SIZE,
               "configured process-image layout must exactly fill the Node body");
_Static_assert(TDMA_PROCESS_IMAGE_CRC_OFFSET +
                       TDMA_PROCESS_IMAGE_CRC_SIZE ==
                   TDMA_FLIGHT_SHORT_SLOT_SIZE,
               "Node mailbox CRC must terminate the fixed layout");

static void core1_realtime_entry(void)
{
    while (!app_is_ready()) {
        tight_loop_contents();
    }

    /* HAOFV realtime core discipline: core1 uses a cycle count as its timing
     * source of truth. Every round executes at a deterministic phase so the
     * TDMA/VDC window schedule (vdc_domain_plan_tdma_window) stays
     * predictable; microseconds below are only the pico-sdk sleep API's
     * derived representation. */
    _Static_assert(BOARD_SYS_CLOCK_HZ % 1000000u == 0u,
                   "clk_sys must convert to whole cycles per microsecond");
    const uint64_t tick_us = PROJECT_CORE1_CYCLE_CYCLES /
        (BOARD_SYS_CLOCK_HZ / 1000000u);
    app_realtime_cycle_counter_init();
    uint64_t next_tick_us = to_us_since_boot(get_absolute_time()) + tick_us;
    while (true) {
        sleep_until(from_us_since_boot(next_tick_us));
        next_tick_us += tick_us;
        drv_flash_core1_lockout_poll();
        app_realtime_run_once();
        tight_loop_contents();
    }
}

void app_runtime_start_realtime_core(void)
{
    if (s_core1_started) {
        return;
    }

    drv_flash_lockout_status_t lockout_status;
    drv_flash_get_lockout_status(&lockout_status);
    multicore_launch_core1(core1_realtime_entry);
    s_core1_started = true;
}

bool app_runtime_init(void)
{
    if (!osal_kernel_init()) {
        return false;
    }

    return app_tasks_create_all();
}

void app_runtime_run(void)
{
    osal_kernel_start();
    app_runtime_fault_forever();
}
