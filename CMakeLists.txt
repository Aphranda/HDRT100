cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_FLASH_SIZE_BYTES 16777216 CACHE STRING "Product W25Q128JV flash size (16 MiB)")
set(PROJECT_ENABLE_USB_STDIO ON CACHE BOOL "Enable stdio over USB CDC")
set(PROJECT_ENABLE_USBTMC OFF CACHE BOOL "Enable USBTMC/USB488 SCPI device interface")
set(PROJECT_ENABLE_USB_RUNTIME_SWITCH OFF CACHE BOOL "Enable flash-configured USB CDC/USBTMC mode selection at boot")
set(PROJECT_USB_DEFAULT_MODE CDC CACHE STRING "Default USB mode when product config is blank: CDC or USBTMC")
set(PROJECT_ENABLE_UART_STDIO OFF CACHE BOOL "Enable product debug stdio over UART0 GPIO0/1")
set(PROJECT_ENABLE_LOG_UART_OUTPUT ON CACHE BOOL "Route live diagnostic LOG output to the product debug UART")
set(PROJECT_WARNINGS_AS_ERRORS OFF CACHE BOOL "Treat compiler warnings as errors")
set(PROJECT_ENABLE_OTA_FAULT_INJECTION OFF CACHE BOOL "Enable destructive OTA validation SCPI commands")
set(PROJECT_ENABLE_WATCHDOG_TEST OFF CACHE BOOL "Enable destructive watchdog validation SCPI command")
set(PROJECT_OTA_DEFAULT_BOOT_MODE DIRECT_AB CACHE STRING "Default OTA boot mode for blank factory metadata: COPY_TO_ACTIVE or DIRECT_AB")
set(PROJECT_USE_FREERTOS ON CACHE BOOL "Build the App firmware with the FreeRTOS OSAL port")
set(PROJECT_USE_MULTICORE ON CACHE BOOL "Run realtime trigger service on RP2350 core1")
set(PROJECT_SYNC_IO_INPUT_BASE_PIN 20 CACHE STRING "Product SMA input GPIO base (physical order IN4..IN1)")
set(PROJECT_SYNC_IO_OUTPUT_BASE_PIN 16 CACHE STRING "Product SMA output GPIO base (OUT1..OUT4)")
set(PROJECT_TDMA_SPI_BAUD_HZ 10000000 CACHE STRING "Product TDMA PIO-SPI bit rate")
set_property(CACHE PROJECT_OTA_DEFAULT_BOOT_MODE PROPERTY STRINGS COPY_TO_ACTIVE DIRECT_AB)
set_property(CACHE PROJECT_USB_DEFAULT_MODE PROPERTY STRINGS CDC USBTMC)

foreach(PROJECT_SYNC_IO_PIN_VAR IN ITEMS PROJECT_SYNC_IO_INPUT_BASE_PIN PROJECT_SYNC_IO_OUTPUT_BASE_PIN)
    if(NOT "${${PROJECT_SYNC_IO_PIN_VAR}}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${PROJECT_SYNC_IO_PIN_VAR} must be an integer GPIO base pin")
    endif()
endforeach()

if(NOT "${PROJECT_TDMA_SPI_BAUD_HZ}" MATCHES "^[0-9]+$")
    message(FATAL_ERROR "PROJECT_TDMA_SPI_BAUD_HZ must be an integer frequency")
endif()
set(PROJECT_TDMA_SPI_SUPPORTED_BAUDS
    10000000 25000000 30000000 35000000 40000000 45000000 50000000)
list(FIND PROJECT_TDMA_SPI_SUPPORTED_BAUDS "${PROJECT_TDMA_SPI_BAUD_HZ}"
     PROJECT_TDMA_SPI_BAUD_INDEX)
if(PROJECT_TDMA_SPI_BAUD_INDEX EQUAL -1)
    message(FATAL_ERROR
        "PROJECT_TDMA_SPI_BAUD_HZ must select a frozen TDMA operating-profile rate: ${PROJECT_TDMA_SPI_SUPPORTED_BAUDS}")
endif()

math(EXPR PROJECT_SYNC_IO_INPUT_LAST_PIN "${PROJECT_SYNC_IO_INPUT_BASE_PIN} + 3")
math(EXPR PROJECT_SYNC_IO_OUTPUT_LAST_PIN "${PROJECT_SYNC_IO_OUTPUT_BASE_PIN} + 3")
if(PROJECT_SYNC_IO_INPUT_LAST_PIN GREATER 29 OR PROJECT_SYNC_IO_OUTPUT_LAST_PIN GREATER 29)
    message(FATAL_ERROR "SYNC_IO input/output 4-pin groups must stay within GPIO0..29")
endif()
if(PROJECT_SYNC_IO_OUTPUT_BASE_PIN LESS_EQUAL PROJECT_SYNC_IO_INPUT_LAST_PIN AND
   PROJECT_SYNC_IO_OUTPUT_LAST_PIN GREATER_EQUAL PROJECT_SYNC_IO_INPUT_BASE_PIN)
    message(FATAL_ERROR "SYNC_IO input and output 4-pin groups must not overlap")
endif()

if(PROJECT_OTA_DEFAULT_BOOT_MODE STREQUAL "DIRECT_AB")
    set(PROJECT_OTA_DEFAULT_BOOT_MODE_DIRECT_AB ON)
elseif(PROJECT_OTA_DEFAULT_BOOT_MODE STREQUAL "COPY_TO_ACTIVE")
    set(PROJECT_OTA_DEFAULT_BOOT_MODE_DIRECT_AB OFF)
else()
    message(FATAL_ERROR "PROJECT_OTA_DEFAULT_BOOT_MODE must be COPY_TO_ACTIVE or DIRECT_AB")
endif()

if(PROJECT_USB_DEFAULT_MODE STREQUAL "USBTMC")
    set(PROJECT_USB_DEFAULT_MODE_USBTMC ON)
elseif(PROJECT_USB_DEFAULT_MODE STREQUAL "CDC")
    set(PROJECT_USB_DEFAULT_MODE_USBTMC OFF)
else()
    message(FATAL_ERROR "PROJECT_USB_DEFAULT_MODE must be CDC or USBTMC")
endif()

if(PROJECT_USE_MULTICORE AND PROJECT_USE_FREERTOS)
    message(STATUS "PROJECT_USE_FREERTOS=ON + PROJECT_USE_MULTICORE=ON uses AMP smoke mode: FreeRTOS on core0, realtime loop on core1")
endif()

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(RP2350_TRIG C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PROJECT_BUILD_INFO_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/project_build_info.h)
set(PROJECT_BUILD_INFO_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/generated/project_build_info.c)
set(PROJECT_OTA_METADATA_SIZE 0x010000)
set(PROJECT_FREERTOS_KERNEL_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/freertos/FreeRTOS-Kernel CACHE PATH "FreeRTOS-Kernel source directory")
set(PROJECT_FATFS_SOURCE_DIR ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source CACHE PATH "FatFs source directory")
set(PROJECT_FATFS_CONFIGURED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/fatfs)
set(PROJECT_OSAL_SOURCES osal/port/baremetal/osal_baremetal.c)
set(PROJECT_APP_EXTRA_LIBS)
set(PROJECT_EFFECTIVE_USB_STDIO ${PROJECT_ENABLE_USB_STDIO})

if(PROJECT_ENABLE_USBTMC AND PROJECT_ENABLE_USB_STDIO)
    message(WARNING "PROJECT_ENABLE_USBTMC=ON disables Pico USB stdio CDC for the app targets")
    set(PROJECT_EFFECTIVE_USB_STDIO OFF)
endif()

if(PROJECT_ENABLE_USB_RUNTIME_SWITCH)
    message(STATUS "PROJECT_ENABLE_USB_RUNTIME_SWITCH=ON disables Pico USB stdio CDC and uses TinyUSB CDC/USBTMC")
    set(PROJECT_EFFECTIVE_USB_STDIO OFF)
endif()

if(PROJECT_USE_MULTICORE)
    list(APPEND PROJECT_APP_EXTRA_LIBS pico_multicore)
endif()

if(PROJECT_ENABLE_USBTMC OR PROJECT_ENABLE_USB_RUNTIME_SWITCH)
    list(APPEND PROJECT_APP_EXTRA_LIBS tinyusb_device pico_unique_id)
endif()

if(PROJECT_USE_FREERTOS)
    if(NOT EXISTS ${PROJECT_FREERTOS_KERNEL_DIR}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
        message(FATAL_ERROR
            "PROJECT_USE_FREERTOS=ON requires FreeRTOS-Kernel at "
            "${PROJECT_FREERTOS_KERNEL_DIR}. Keep the option OFF for the "
            "current baremetal release path, or add the kernel source under "
            "third_party/freertos/FreeRTOS-Kernel.")
    endif()

    set(PROJECT_OSAL_SOURCES osal/port/freertos/osal_freertos.c)
    set(FREERTOS_KERNEL_PATH ${PROJECT_FREERTOS_KERNEL_DIR})
    set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/config/freertos)
    include(${PROJECT_FREERTOS_KERNEL_DIR}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
    list(APPEND PROJECT_APP_EXTRA_LIBS FreeRTOS-Kernel FreeRTOS-Kernel-Heap4)
endif()

set(PROJECT_PORTABLE_OTA_ADAPTER_SOURCES
        middleware/portable_ota_port/src/portable_ota_core_port.c
        middleware/portable_ota_port/src/portable_ota_image_port.c
        middleware/portable_ota_port/src/portable_ota_metadata_port.c
        third_party/portable_ota/src/pota_crc32.c
        third_party/portable_ota/src/pota_image.c
        third_party/portable_ota/src/pota_metadata.c
        third_party/portable_ota/src/pota_strings.c
        third_party/portable_ota/src/pota_compat.c
)

add_custom_target(project_build_info
        ALL
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/build_info/gen_build_info.py
                ${PROJECT_BUILD_INFO_HEADER}
                ${PROJECT_BUILD_INFO_SOURCE}
        BYPRODUCTS ${PROJECT_BUILD_INFO_HEADER} ${PROJECT_BUILD_INFO_SOURCE}
        VERBATIM
)

add_custom_target(project_scpi_usb_namespace_check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/checks/check_scpi_usb_namespace.py
                --root ${CMAKE_CURRENT_LIST_DIR}
        VERBATIM
)

file(MAKE_DIRECTORY ${PROJECT_FATFS_CONFIGURED_DIR})
foreach(PROJECT_FATFS_SOURCE_FILE ff.c ffsystem.c ffunicode.c ff.h diskio.h)
    configure_file(${PROJECT_FATFS_SOURCE_DIR}/${PROJECT_FATFS_SOURCE_FILE}
                   ${PROJECT_FATFS_CONFIGURED_DIR}/${PROJECT_FATFS_SOURCE_FILE}
                   COPYONLY)
endforeach()
file(READ ${PROJECT_FATFS_SOURCE_DIR}/ffconf.h PROJECT_FATFS_CONFIG_TEXT)
string(REPLACE "#define FF_USE_MKFS\t\t0"
               "#define FF_USE_MKFS\t\t1"
               PROJECT_FATFS_CONFIG_TEXT
               "${PROJECT_FATFS_CONFIG_TEXT}")
file(WRITE ${PROJECT_FATFS_CONFIGURED_DIR}/ffconf.h "${PROJECT_FATFS_CONFIG_TEXT}")

set(PROJECT_APP_SOURCES
        application/src/main.c
        application/src/app_runtime.c
        application/src/app.c
        application/src/app_tasks.c
        ${PROJECT_BUILD_INFO_SOURCE}
        boards/rp2350_trig/src/board.c
        third_party/portable_log/src/portable_log.c
        middleware/portable_log_port/src/portable_log_port.c
        components/calibration_manager/src/calibration_manager.c
        components/diagnostics/src/diagnostics.c
        components/distributed_config/src/distributed_config.c
        components/distributed_refmem/src/distributed_refmem.c
        components/distributed_refmem/src/refmem_application_contract.c
        components/distributed_refmem/src/refmem_application_model.c
        components/distributed_refmem/src/refmem_claim_protocol.c
        components/distributed_refmem/src/refmem_command.c
        components/distributed_refmem/src/refmem_node_load_sync.c
        components/distributed_refmem/src/refmem_pio_spi_adapter.c
        components/distributed_refmem/src/refmem_quality.c
        components/distributed_refmem/src/refmem_realtime_tdma.c
        components/distributed_refmem/src/refmem_realtime_contract.c
        components/distributed_refmem/src/refmem_spi_physical_adapter.c
        components/distributed_refmem/src/refmem_slot_claim.c
        components/distributed_refmem/src/refmem_sync.c
        components/distributed_refmem/src/refmem_sync_frame.c
        components/distributed_refmem/src/refmem_sync_hello.c
        components/distributed_refmem/src/refmem_table_registry.c
        components/distributed_refmem/src/refmem_tdma_payload.c
        components/distributed_refmem/src/refmem_vector_table.c
        components/distributed_refmem/src/refmem_vdc_bridge.c
        components/event_bus/src/event_bus.c
        components/loop_engine/src/loop_engine.c
        components/led_manager/src/led_manager.c
        components/model_turntable/src/model_turntable.c
        components/ota_manager/src/ota_ao.c
        components/ota_manager/src/ota_fb.c
        components/ota_manager/src/ota_crc32.c
        components/ota_manager/src/ota_image.c
        components/ota_manager/src/ota_metadata.c
        components/ota_manager/src/ota_package.c
        components/product_config/src/product_config.c
        ${PROJECT_PORTABLE_OTA_ADAPTER_SOURCES}
        middleware/portable_ota_port/src/portable_ota_port.c
        third_party/portable_ota/src/pota_core.c
        third_party/portable_ota/src/pota_operation.c
        third_party/portable_ota/src/pota_session.c
        third_party/portable_ota/src/pota_package.c
        components/resource_arbiter/src/resource_arbiter.c
        components/storage_manager/src/storage_manager.c
        components/system_manager/src/system_manager.c
        components/sync_trigger/src/biss_node_io.c
        components/sync_trigger/src/biss_protocol.c
        components/sync_trigger/src/sync_trigger.c
        components/sync_trigger/src/trigger_fb.c
        components/sync_trigger/src/trigger_measure.c
        components/sync_trigger/src/trigger_resource_map.c
        components/sync_io/src/sync_io.c
        components/sync_io/src/sync_io_model_sched.c
        components/sync_io/src/sync_io_mode.c
        components/sync_io/src/sync_io_mode_biss_tap.c
        components/sync_io/src/sync_io_mode_enc_count.c
        components/sync_io/src/sync_io_mode_seq_step.c
        components/tdma/src/tdma_service.c
        components/tdma/src/tdma_operating_profile.c
        components/tdma/src/tdma_profile.c
        components/tdma/src/tdma_payload_registry.c
        components/tdma/src/tdma_flight_fifo.c
        components/tdma/src/tdma_flight_engine.c
        components/tdma/src/tdma_pio_spi_phys.c
        components/tdma/src/tdma_pio_spi_ring_adapter.c
        components/tdma/src/tdma_process_image_map.c
        components/tdma/src/tdma_ring_runtime.c
        components/tdma/src/tdma_traffic_scheduler.c
        components/tdma/src/tdma_runtime_owner.c
        components/tdma/src/tdma_transport_frame.c
        components/ui_manager/src/status_ui.c
        components/ui_manager/src/ui_key_input.c
        components/ui_manager/src/ui_manager.c
        components/vdc_domain/src/vdc_domain.c
        components/vdc_domain/src/vdc_sync_io_adapter.c
        components/vdc_domain/src/vdc_tdma_payload.c
        components/vdc_domain/src/vdc_timestamp_clock.c
        components/vdc_domain/src/vdc_timestamp.c
        components/vdc_dpll_manager/src/vdc_dpll_manager.c
        drivers/mcu/spi/src/drv_spi.c
        drivers/mcu/i2c/src/drv_i2c.c
        drivers/mcu/uart/src/drv_uart.c
        drivers/mcu/flash/src/drv_flash.c
        drivers/mcu/flash/src/drv_flash_lockout.c
        drivers/mcu/watchdog/src/drv_watchdog.c
        drivers/external/lcd/src/lcd_st7789.c
        drivers/external/sd_card/src/sd_card.c
        middleware/fatfs_port/src/fatfs_port.c
        middleware/fatfs_port/src/fatfs_diskio.c
        ${PROJECT_FATFS_CONFIGURED_DIR}/ff.c
        ${PROJECT_FATFS_CONFIGURED_DIR}/ffsystem.c
        ${PROJECT_FATFS_CONFIGURED_DIR}/ffunicode.c
        middleware/scpi_port/src/scpi_calibration_commands.c
        middleware/scpi_port/src/scpi_communication_biss_commands.c
        middleware/scpi_port/src/scpi_communication_uart_commands.c
        middleware/scpi_port/src/scpi_config_commands.c
        middleware/scpi_port/src/scpi_port.c
        middleware/scpi_port/src/scpi_loop_engine_commands.c
        middleware/scpi_port/src/scpi_measure_commands.c
        middleware/scpi_port/src/scpi_model_commands.c
        middleware/scpi_port/src/scpi_ota_commands.c
        middleware/scpi_port/src/scpi_storage_commands.c
        middleware/scpi_port/src/scpi_realtime_component_commands.c
        middleware/scpi_port/src/scpi_realtime_encoder_commands.c
        middleware/scpi_port/src/scpi_realtime_io_commands.c
        middleware/scpi_port/src/scpi_realtime_pcnt_commands.c
        middleware/scpi_port/src/scpi_realtime_sequence_commands.c
        middleware/scpi_port/src/scpi_realtime_status_commands.c
        middleware/scpi_port/src/scpi_sync_commands.c
        middleware/scpi_port/src/scpi_system_access_commands.c
        middleware/scpi_port/src/scpi_system_diagnostics_commands.c
        middleware/scpi_port/src/scpi_system_runtime_commands.c
        middleware/scpi_port/src/scpi_system_snapshot_commands.c
        middleware/scpi_port/src/scpi_tdma_commands.c
        middleware/scpi_port/src/scpi_trigger_commands.c
        middleware/u8g2_port/src/u8g2_port.c
        third_party/scpi-parser/libscpi/src/error.c
        third_party/scpi-parser/libscpi/src/expression.c
        third_party/scpi-parser/libscpi/src/fifo.c
        third_party/scpi-parser/libscpi/src/ieee488.c
        third_party/scpi-parser/libscpi/src/lexer.c
        third_party/scpi-parser/libscpi/src/minimal.c
        third_party/scpi-parser/libscpi/src/parser.c
        third_party/scpi-parser/libscpi/src/units.c
        third_party/scpi-parser/libscpi/src/utils.c
        third_party/u8g2/csrc/u8g2_arc.c
        third_party/u8g2/csrc/u8g2_bitmap.c
        third_party/u8g2/csrc/u8g2_box.c
        third_party/u8g2/csrc/u8g2_buffer.c
        third_party/u8g2/csrc/u8g2_button.c
        third_party/u8g2/csrc/u8g2_circle.c
        third_party/u8g2/csrc/u8g2_cleardisplay.c
        third_party/u8g2/csrc/u8g2_font.c
        third_party/u8g2/csrc/u8g2_fonts.c
        third_party/u8g2/csrc/u8g2_hvline.c
        third_party/u8g2/csrc/u8g2_intersection.c
        third_party/u8g2/csrc/u8g2_kerning.c
        third_party/u8g2/csrc/u8g2_line.c
        third_party/u8g2/csrc/u8g2_ll_hvline.c
        third_party/u8g2/csrc/u8g2_polygon.c
        third_party/u8g2/csrc/u8g2_setup.c
        third_party/u8g2/csrc/u8x8_8x8.c
        third_party/u8g2/csrc/u8x8_byte.c
        third_party/u8g2/csrc/u8x8_cad.c
        third_party/u8g2/csrc/u8x8_display.c
        third_party/u8g2/csrc/u8x8_fonts.c
        third_party/u8g2/csrc/u8x8_gpio.c
        third_party/u8g2/csrc/u8x8_input_value.c
        third_party/u8g2/csrc/u8x8_message.c
        third_party/u8g2/csrc/u8x8_selection_list.c
        third_party/u8g2/csrc/u8x8_setup.c
        third_party/u8g2/csrc/u8x8_string.c
        third_party/u8g2/csrc/u8x8_u8toa.c
        third_party/u8g2/csrc/u8x8_u16toa.c
        ${PROJECT_OSAL_SOURCES}
)

if(PROJECT_ENABLE_USBTMC OR PROJECT_ENABLE_USB_RUNTIME_SWITCH)
    list(APPEND PROJECT_APP_SOURCES
        middleware/scpi_port/src/scpi_usb_control.c
        middleware/usbtmc_scpi_port/src/usbtmc_scpi_port.c
    )
endif()

add_executable(${PROJECT_NAME}
        ${PROJECT_APP_SOURCES}
)

function(project_configure_app_target target_name app_name linker_script)
    pico_set_program_name(${target_name} ${app_name})
    pico_set_program_version(${target_name} "0.1.0")

    pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_LIST_DIR}/components/sync_io/src/sync_io.pio)
    pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_LIST_DIR}/components/sync_io/src/seq_step.pio)
    pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_LIST_DIR}/components/sync_io/src/enc_count.pio)
    pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_LIST_DIR}/components/sync_io/src/biss_tap_rx.pio)
    pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_LIST_DIR}/components/distributed_refmem/src/refmem_spi_physical.pio)
    pico_generate_pio_header(${target_name} ${CMAKE_CURRENT_LIST_DIR}/components/tdma/src/tdma_pio_spi.pio)

    pico_enable_stdio_uart(${target_name} ${PROJECT_ENABLE_UART_STDIO})
    pico_enable_stdio_usb(${target_name} ${PROJECT_EFFECTIVE_USB_STDIO})

    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/application/inc
        ${CMAKE_CURRENT_LIST_DIR}/boards/rp2350_trig/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/calibration_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/diagnostics/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/distributed_config/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/distributed_refmem/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/event_bus/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/loop_engine/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/led_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/model_turntable/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/ota_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/product_config/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/resource_arbiter/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/storage_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/system_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/sync_trigger/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/sync_io/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/tdma/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/ui_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/vdc_domain/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/vdc_dpll_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/config
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/spi/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/i2c/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/uart/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/flash/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/watchdog/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/external/lcd/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/external/sd_card/inc
        ${CMAKE_CURRENT_LIST_DIR}/middleware/fatfs_port/inc
        ${PROJECT_FATFS_CONFIGURED_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/middleware/portable_log_port/inc
        ${CMAKE_CURRENT_LIST_DIR}/middleware/portable_ota_port/inc
        ${CMAKE_CURRENT_LIST_DIR}/middleware/scpi_port/inc
        $<$<OR:$<BOOL:${PROJECT_ENABLE_USBTMC}>,$<BOOL:${PROJECT_ENABLE_USB_RUNTIME_SWITCH}>>:${CMAKE_CURRENT_LIST_DIR}/middleware/usbtmc_scpi_port/inc>
        ${CMAKE_CURRENT_LIST_DIR}/middleware/u8g2_port/inc
        ${CMAKE_CURRENT_LIST_DIR}/osal/inc
        ${CMAKE_CURRENT_LIST_DIR}/config/freertos
        ${CMAKE_CURRENT_LIST_DIR}/third_party/scpi-parser/libscpi/inc
        ${CMAKE_CURRENT_LIST_DIR}/third_party/portable_log/include
        ${CMAKE_CURRENT_LIST_DIR}/third_party/u8g2/csrc
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    )

    target_link_libraries(${target_name}
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_uart
        hardware_watchdog
        hardware_pio
        hardware_dma
        hardware_pwm
        hardware_clocks
        hardware_flash
        ${PROJECT_APP_EXTRA_LIBS}
    )

    target_compile_definitions(${target_name} PRIVATE
        PROJECT_ENABLE_USB_STDIO=$<BOOL:${PROJECT_EFFECTIVE_USB_STDIO}>
        PROJECT_ENABLE_USBTMC=$<BOOL:${PROJECT_ENABLE_USBTMC}>
        PROJECT_ENABLE_USB_RUNTIME_SWITCH=$<BOOL:${PROJECT_ENABLE_USB_RUNTIME_SWITCH}>
        PROJECT_USB_DEFAULT_MODE_USBTMC=$<BOOL:${PROJECT_USB_DEFAULT_MODE_USBTMC}>
        PROJECT_ENABLE_UART_STDIO=$<BOOL:${PROJECT_ENABLE_UART_STDIO}>
        PROJECT_ENABLE_LOG_UART_OUTPUT=$<BOOL:${PROJECT_ENABLE_LOG_UART_OUTPUT}>
        PROJECT_ENABLE_OTA_FAULT_INJECTION=$<BOOL:${PROJECT_ENABLE_OTA_FAULT_INJECTION}>
        PROJECT_ENABLE_WATCHDOG_TEST=$<BOOL:${PROJECT_ENABLE_WATCHDOG_TEST}>
        PROJECT_OTA_DEFAULT_BOOT_MODE_DIRECT_AB=$<BOOL:${PROJECT_OTA_DEFAULT_BOOT_MODE_DIRECT_AB}>
        PROJECT_USE_FREERTOS=$<BOOL:${PROJECT_USE_FREERTOS}>
        PROJECT_USE_MULTICORE=$<BOOL:${PROJECT_USE_MULTICORE}>
        PROJECT_SYNC_IO_INPUT_BASE_PIN=${PROJECT_SYNC_IO_INPUT_BASE_PIN}
        PROJECT_SYNC_IO_OUTPUT_BASE_PIN=${PROJECT_SYNC_IO_OUTPUT_BASE_PIN}
        PROJECT_TDMA_SPI_BAUD_HZ=${PROJECT_TDMA_SPI_BAUD_HZ}
        PORTABLE_OTA_PORT_ENABLE_SESSION=1
        DRV_FLASH_LOCKOUT_PICO_RAM=1
        DRV_FLASH_LOCKOUT_USE_ARM_EVENTS=1
        DRV_FLASH_LOCKOUT_PICO_TIME=1
        SCPI_USER_CONFIG=1
        PICO_TARGET_NAME="${target_name}"
    )

    add_dependencies(${target_name} project_build_info)
    add_dependencies(${target_name} project_scpi_usb_namespace_check)

    target_compile_options(${target_name} PRIVATE ${PROJECT_WARNING_FLAGS})

    pico_set_linker_script(${target_name} ${linker_script})

    pico_add_dis_output(${target_name})
    if(PICO_32BIT)
        pico_add_hex_output(${target_name})
    endif()
    pico_add_bin_output(${target_name})
    pico_add_map_output(${target_name})
endfunction()

set_source_files_properties(
        middleware/portable_ota_port/src/portable_ota_port.c
        middleware/portable_ota_port/src/portable_ota_core_port.c
        middleware/portable_ota_port/src/portable_ota_image_port.c
        middleware/portable_ota_port/src/portable_ota_metadata_port.c
        third_party/portable_ota/src/pota_core.c
        third_party/portable_ota/src/pota_operation.c
        third_party/portable_ota/src/pota_session.c
        third_party/portable_ota/src/pota_package.c
        third_party/portable_ota/src/pota_crc32.c
        third_party/portable_ota/src/pota_image.c
        third_party/portable_ota/src/pota_metadata.c
        third_party/portable_ota/src/pota_strings.c
        third_party/portable_ota/src/pota_compat.c
        PROPERTIES
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/third_party/portable_ota/include"
)

set(PROJECT_WARNING_FLAGS
        -Wall
        -Wextra
        -Wshadow
)

if(PROJECT_WARNINGS_AS_ERRORS)
    list(APPEND PROJECT_WARNING_FLAGS -Werror)
endif()

project_configure_app_target(
        ${PROJECT_NAME}
        "RP2350_TRIG"
        ${CMAKE_CURRENT_LIST_DIR}/linker/rp2350_app_slot_a.ld
)

add_executable(RP2350_TRIG_B
        ${PROJECT_APP_SOURCES}
)

project_configure_app_target(
        RP2350_TRIG_B
        "RP2350_TRIG_B"
        ${CMAKE_CURRENT_LIST_DIR}/linker/rp2350_app_slot_b.ld
)

add_executable(RP2350_TRIG_BOOT
        bootloader/src/bootloader_main.c
        components/ota_manager/src/ota_crc32.c
        components/ota_manager/src/ota_image.c
        components/ota_manager/src/ota_metadata.c
        ${PROJECT_PORTABLE_OTA_ADAPTER_SOURCES}
        drivers/mcu/flash/src/drv_flash.c
        drivers/mcu/flash/src/drv_flash_lockout.c
)

pico_set_program_name(RP2350_TRIG_BOOT "RP2350_TRIG_BOOT")
pico_set_program_version(RP2350_TRIG_BOOT "0.1.0")

pico_enable_stdio_uart(RP2350_TRIG_BOOT OFF)
pico_enable_stdio_usb(RP2350_TRIG_BOOT OFF)

target_include_directories(RP2350_TRIG_BOOT PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/bootloader/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/ota_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/middleware/portable_ota_port/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/flash/inc
        ${CMAKE_CURRENT_LIST_DIR}/config
)

target_link_libraries(RP2350_TRIG_BOOT
        pico_stdlib
        hardware_flash
)

target_compile_definitions(RP2350_TRIG_BOOT PRIVATE
        PROJECT_ENABLE_OTA_FAULT_INJECTION=$<BOOL:${PROJECT_ENABLE_OTA_FAULT_INJECTION}>
        PROJECT_OTA_DEFAULT_BOOT_MODE_DIRECT_AB=$<BOOL:${PROJECT_OTA_DEFAULT_BOOT_MODE_DIRECT_AB}>
)

target_compile_options(RP2350_TRIG_BOOT PRIVATE ${PROJECT_WARNING_FLAGS})

pico_set_linker_script(RP2350_TRIG_BOOT ${CMAKE_CURRENT_LIST_DIR}/linker/rp2350_bootloader.ld)

pico_add_extra_outputs(RP2350_TRIG_BOOT)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ota_metadata_clear.bin
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/uf2_join/make_fill_bin.py
                ${CMAKE_CURRENT_BINARY_DIR}/ota_metadata_clear.bin
                ${PROJECT_OTA_METADATA_SIZE}
                --byte 0xFF
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/tools/uf2_join/make_fill_bin.py
        VERBATIM
)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_FACTORY.uf2
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/uf2_join/uf2_join.py
                ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_FACTORY.uf2
                ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_BOOT.bin@0x10000000
                ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG.bin@0x10040000
                ${CMAKE_CURRENT_BINARY_DIR}/ota_metadata_clear.bin@0x10340000
        DEPENDS RP2350_TRIG_BOOT ${PROJECT_NAME} ${CMAKE_CURRENT_BINARY_DIR}/ota_metadata_clear.bin ${CMAKE_CURRENT_LIST_DIR}/tools/uf2_join/uf2_join.py
        VERBATIM
)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_UPDATE.pkg
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/ota_packager/ota_packager.py
                --image-a ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG.bin
                --image-b ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_B.bin
                --app-version 0.1.0
                --build-id-file ${PROJECT_BUILD_INFO_SOURCE}
                --min-bootloader-version 0.1.0
                -o ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_UPDATE.pkg
        DEPENDS ${PROJECT_NAME} RP2350_TRIG_B project_build_info ${CMAKE_CURRENT_LIST_DIR}/tools/ota_packager/ota_packager.py
        VERBATIM
)

add_custom_target(RP2350_TRIG_FACTORY ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_FACTORY.uf2
)

add_custom_target(RP2350_TRIG_UPDATE ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_UPDATE.pkg
)
