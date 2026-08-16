add_library(vdc_domain STATIC
    src/vdc_domain.c
    src/vdc_timestamp.c
)

target_include_directories(vdc_domain PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/inc
)
