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
set(PROJECT_ENABLE_USB_STDIO ON CACHE BOOL "Enable stdio over USB CDC")
set(PROJECT_ENABLE_UART_STDIO OFF CACHE BOOL "Enable stdio over default UART")
set(PROJECT_WARNINGS_AS_ERRORS OFF CACHE BOOL "Treat compiler warnings as errors")
set(PROJECT_ENABLE_OTA_FAULT_INJECTION OFF CACHE BOOL "Enable destructive OTA validation SCPI commands")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(RP2350_TRIG C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(PROJECT_BUILD_INFO_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/project_build_info.h)
set(PROJECT_BUILD_INFO_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/generated/project_build_info.c)

add_custom_target(project_build_info
        ALL
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/build_info/gen_build_info.py
                ${PROJECT_BUILD_INFO_HEADER}
                ${PROJECT_BUILD_INFO_SOURCE}
        BYPRODUCTS ${PROJECT_BUILD_INFO_HEADER} ${PROJECT_BUILD_INFO_SOURCE}
        VERBATIM
)

add_executable(${PROJECT_NAME}
        application/src/main.c
        application/src/app.c
        ${PROJECT_BUILD_INFO_SOURCE}
        boards/rp2350_trig/src/board.c
        components/diagnostics/src/diagnostics.c
        components/ota_manager/src/ota_ao.c
        components/ota_manager/src/ota_fb.c
        components/ota_manager/src/ota_crc32.c
        components/ota_manager/src/ota_image.c
        components/ota_manager/src/ota_metadata.c
        components/sync_io/src/sync_io.c
        components/sync_config_ui/src/sync_config_ui.c
        drivers/mcu/spi/src/drv_spi.c
        drivers/mcu/i2c/src/drv_i2c.c
        drivers/mcu/uart/src/drv_uart.c
        drivers/mcu/flash/src/drv_flash.c
        drivers/mcu/watchdog/src/drv_watchdog.c
        drivers/external/lcd/src/lcd_st7789.c
        middleware/scpi_port/src/scpi_port.c
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
        osal/port/baremetal/osal_baremetal.c
)

pico_set_program_name(${PROJECT_NAME} "RP2350_TRIG")
pico_set_program_version(${PROJECT_NAME} "0.1.0")

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/components/sync_io/src/sync_io.pio)

pico_enable_stdio_uart(${PROJECT_NAME} ${PROJECT_ENABLE_UART_STDIO})
pico_enable_stdio_usb(${PROJECT_NAME} ${PROJECT_ENABLE_USB_STDIO})

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/application/inc
        ${CMAKE_CURRENT_LIST_DIR}/boards/rp2350_trig/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/diagnostics/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/ota_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/sync_io/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/sync_config_ui/inc
        ${CMAKE_CURRENT_LIST_DIR}/config
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/spi/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/i2c/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/uart/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/flash/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/watchdog/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/external/lcd/inc
        ${CMAKE_CURRENT_LIST_DIR}/middleware/scpi_port/inc
        ${CMAKE_CURRENT_LIST_DIR}/middleware/u8g2_port/inc
        ${CMAKE_CURRENT_LIST_DIR}/osal/inc
        ${CMAKE_CURRENT_LIST_DIR}/third_party/scpi-parser/libscpi/inc
        ${CMAKE_CURRENT_LIST_DIR}/third_party/u8g2/csrc
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(${PROJECT_NAME}
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_uart
        hardware_watchdog
        hardware_pio
        hardware_clocks
        hardware_flash
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        PROJECT_ENABLE_USB_STDIO=$<BOOL:${PROJECT_ENABLE_USB_STDIO}>
        PROJECT_ENABLE_UART_STDIO=$<BOOL:${PROJECT_ENABLE_UART_STDIO}>
        PROJECT_ENABLE_OTA_FAULT_INJECTION=$<BOOL:${PROJECT_ENABLE_OTA_FAULT_INJECTION}>
        SCPI_USER_CONFIG=1
)

add_dependencies(${PROJECT_NAME} project_build_info)

set(PROJECT_WARNING_FLAGS
        -Wall
        -Wextra
        -Wshadow
)

if(PROJECT_WARNINGS_AS_ERRORS)
    list(APPEND PROJECT_WARNING_FLAGS -Werror)
endif()

target_compile_options(${PROJECT_NAME} PRIVATE ${PROJECT_WARNING_FLAGS})

pico_set_linker_script(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/linker/rp2350_app_slot_a.ld)

pico_add_dis_output(${PROJECT_NAME})
if(PICO_32BIT)
    pico_add_hex_output(${PROJECT_NAME})
endif()
pico_add_bin_output(${PROJECT_NAME})
pico_add_map_output(${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME} PRIVATE
        PICO_TARGET_NAME="${PROJECT_NAME}"
)

add_executable(RP2350_TRIG_BOOT
        bootloader/src/bootloader_main.c
        components/ota_manager/src/ota_crc32.c
        components/ota_manager/src/ota_image.c
        components/ota_manager/src/ota_metadata.c
        drivers/mcu/flash/src/drv_flash.c
)

pico_set_program_name(RP2350_TRIG_BOOT "RP2350_TRIG_BOOT")
pico_set_program_version(RP2350_TRIG_BOOT "0.1.0")

pico_enable_stdio_uart(RP2350_TRIG_BOOT OFF)
pico_enable_stdio_usb(RP2350_TRIG_BOOT OFF)

target_include_directories(RP2350_TRIG_BOOT PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/bootloader/inc
        ${CMAKE_CURRENT_LIST_DIR}/components/ota_manager/inc
        ${CMAKE_CURRENT_LIST_DIR}/drivers/mcu/flash/inc
)

target_link_libraries(RP2350_TRIG_BOOT
        pico_stdlib
        hardware_flash
)

target_compile_definitions(RP2350_TRIG_BOOT PRIVATE
        PROJECT_ENABLE_OTA_FAULT_INJECTION=$<BOOL:${PROJECT_ENABLE_OTA_FAULT_INJECTION}>
)

target_compile_options(RP2350_TRIG_BOOT PRIVATE ${PROJECT_WARNING_FLAGS})

pico_set_linker_script(RP2350_TRIG_BOOT ${CMAKE_CURRENT_LIST_DIR}/linker/rp2350_bootloader.ld)

pico_add_extra_outputs(RP2350_TRIG_BOOT)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_FACTORY.uf2
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/uf2_join/uf2_join.py
                ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_FACTORY.uf2
                ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_BOOT.bin@0x10000000
                ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG.bin@0x10040000
        DEPENDS RP2350_TRIG_BOOT ${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/tools/uf2_join/uf2_join.py
        VERBATIM
)

add_custom_target(RP2350_TRIG_FACTORY ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/RP2350_TRIG_FACTORY.uf2
)
