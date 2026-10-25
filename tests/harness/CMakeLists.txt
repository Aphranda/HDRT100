cmake_minimum_required(VERSION 3.13)

# Host harness: builds the real production units for the developer machine.
# The Pico SDK ships replacement implementations for its platform libraries
# when PICO_PLATFORM=host, so no hand-written shim layer is needed.
set(PICO_PLATFORM host CACHE STRING "" FORCE)
set(PICO_SDK_PATH "C:/Users/10447/.pico-sdk/sdk/2.2.0" CACHE PATH "" FORCE)

project(dhrt100_host_harness C)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
pico_sdk_init()

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

file(GLOB COMPONENT_INC_DIRS ${REPO_ROOT}/components/*/inc)
file(GLOB COMPONENT_SRC_DIRS ${REPO_ROOT}/components/*/src)
file(GLOB BOARD_INC_DIRS ${REPO_ROOT}/boards/*/inc)
file(GLOB MIDDLEWARE_INC_DIRS ${REPO_ROOT}/middleware/*/inc)
file(GLOB DRIVER_INC_DIRS ${REPO_ROOT}/drivers/*/inc)
file(GLOB THIRD_PARTY_INC_DIRS
    ${REPO_ROOT}/third_party/*/include
    ${REPO_ROOT}/third_party/*/inc)

# Every directory that holds a project header, so a unit's transitive includes
# resolve without maintaining a hand-written list.
file(GLOB_RECURSE PROJECT_HEADERS
    ${REPO_ROOT}/components/*/*.h
    ${REPO_ROOT}/middleware/*/*.h
    ${REPO_ROOT}/drivers/*/*.h
    ${REPO_ROOT}/boards/*/inc/*.h
    ${REPO_ROOT}/platform/*/*.h
    ${REPO_ROOT}/osal/*/*.h
    ${REPO_ROOT}/application/*/*.h
    ${REPO_ROOT}/bootloader/*/*.h
    ${REPO_ROOT}/third_party/*/*.h
    ${REPO_ROOT}/config/*.h
)
set(PROJECT_HEADER_DIRS "")
foreach(hdr ${PROJECT_HEADERS})
    get_filename_component(dir ${hdr} DIRECTORY)
    list(APPEND PROJECT_HEADER_DIRS ${dir})
endforeach()
list(REMOVE_DUPLICATES PROJECT_HEADER_DIRS)

set(HARNESS_INCLUDES
    ${COMPONENT_INC_DIRS}
    ${COMPONENT_SRC_DIRS}
    ${BOARD_INC_DIRS}
    ${MIDDLEWARE_INC_DIRS}
    ${DRIVER_INC_DIRS}
    ${THIRD_PARTY_INC_DIRS}
    ${PROJECT_HEADER_DIRS}
    ${REPO_ROOT}/platform/inc
    ${REPO_ROOT}/osal/inc
    ${REPO_ROOT}/application/inc
    ${REPO_ROOT}/config
)

add_executable(host_control_test
    scenarios/host_control_scenario.c
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_priority_host_control.c
)

target_include_directories(host_control_test BEFORE PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/host_include
    ${HARNESS_INCLUDES})

# Only the platform layer is needed: the unit under test calls no SDK service.
target_link_libraries(host_control_test PRIVATE pico_platform hardware_timer hardware_uart hardware_gpio)

enable_testing()
add_test(NAME host_control COMMAND host_control_test)

# Output-delay unit: platform services are replaced at link time.
add_executable(output_delay_test
    scenarios/output_delay_scenario.c
    fakes/output_delay_fakes.c
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_output_delay_config.c
)

target_include_directories(output_delay_test BEFORE PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/host_include
    ${HARNESS_INCLUDES})

target_link_libraries(output_delay_test PRIVATE
    pico_platform hardware_timer hardware_uart hardware_gpio)

target_link_options(output_delay_test PRIVATE
    -Wl,--wrap=get_core_num
    -Wl,--wrap=product_config_get_dpll_output_compensation_ns
    -Wl,--wrap=product_config_set_dpll_output_compensation_ns
    -Wl,--wrap=tdma_service_update_stopped_metadata
    -Wl,--wrap=tdma_service_run_stopped_maintenance
    -Wl,--wrap=vdc_dpll_manager_feedback_session
)

add_test(NAME output_delay COMMAND output_delay_test)

# Compile-only gate for the large merged units: proves they build for the host
# platform before their scenarios exist.
add_library(unit_compile_check OBJECT
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_output.c
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_dpll_feedback.c
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_boundary.c
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_priority_trace.c
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_priority_follow.c
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_priority_tx.c
)

target_include_directories(unit_compile_check BEFORE PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/host_include
    ${HARNESS_INCLUDES})

target_link_libraries(unit_compile_check PRIVATE
    pico_platform hardware_timer hardware_uart hardware_gpio)

# Some units rely on the firmware build's umbrella includes; force the SDK time
# header so the host build sees it without touching production sources.
target_compile_options(unit_compile_check PRIVATE -include host_time_compat.h)

# Reference-discipline scenario: real unit + the legacy harness stubs.
add_executable(reference_discipline_test
    scenarios/reference_discipline_scenario.c
    fakes/refdis_fakes.c
    fakes/refdis_unit_fakes.c
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_output.c
    ${REPO_ROOT}/components/vdc_dpll_manager/src/vdc_reference_config.c
)

target_include_directories(reference_discipline_test BEFORE PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/host_include
    ${HARNESS_INCLUDES})

target_link_libraries(reference_discipline_test PRIVATE
    pico_platform hardware_timer hardware_uart hardware_gpio)

target_compile_options(reference_discipline_test PRIVATE -include host_time_compat.h -DDHRT100_UNIT_TEST)

add_test(NAME reference_discipline COMMAND reference_discipline_test track)
