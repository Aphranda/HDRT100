add_library(vdc_domain STATIC
    src/vdc_domain.c
    src/vdc_domain_evidence.c
    src/vdc_domain_profile.c
    src/vdc_ring_observer.c
    src/vdc_sync_io_adapter.c
    src/vdc_tdma_payload.c
    src/vdc_timestamp.c
)

target_include_directories(vdc_domain PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/inc
    ${CMAKE_CURRENT_LIST_DIR}/../tdma/inc
)
