#include "refmem_application_model.h"

#include <stddef.h>
#include <string.h>

#include "ota_crc32.h"
#include "refmem_application_contract.h"
#include "refmem_realtime_contract.h"
#include "refmem_slot_claim.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"

#define REFMEM_APP_TABLE_WIRE_U32_SIZE 4u
#define REFMEM_APP_TABLE_WIRE_HEADER_WORDS 2u
#define REFMEM_APP_TABLE_WIRE_FB_INSTANCE_WORDS 20u
#define REFMEM_APP_TABLE_WIRE_EVENT_LINK_WORDS 12u
#define REFMEM_APP_TABLE_WIRE_DATA_LINK_WORDS 15u
#define REFMEM_APP_TABLE_WIRE_DEPLOYMENT_GATE_WORDS 9u
#define REFMEM_APP_TABLE_WIRE_CONNECTION_QUALITY_WORDS 16u
#define REFMEM_APP_TABLE_WIRE_SIZE(row_count, row_words) \
    ((REFMEM_APP_TABLE_WIRE_HEADER_WORDS + ((row_count) * (row_words))) * \
     REFMEM_APP_TABLE_WIRE_U32_SIZE)

#define REFMEM_APP_INLINE_PACKAGE_DIR_SIZE \
    (REFMEM_TABLE_REGISTRY_COUNT * REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE)
#define REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET \
    (REFMEM_TABLE_PACKAGE_HEADER_SIZE + REFMEM_APP_INLINE_PACKAGE_DIR_SIZE)

#define REFMEM_APP_INSTANCE_B0_REFMEM_SYNC      0u
#define REFMEM_APP_INSTANCE_B0_LOOP_ENGINE      1u
#define REFMEM_APP_INSTANCE_B0_PULSE_COUNTER    2u
#define REFMEM_APP_INSTANCE_B0_TRIGGER_MASTER   3u
#define REFMEM_APP_INSTANCE_B1_TRIGGER          4u
#define REFMEM_APP_INSTANCE_B2_LINK_SWITCH      5u
#define REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY   6u
#define REFMEM_APP_INSTANCE_B3_INSTRUMENT       7u
#define REFMEM_APP_INSTANCE_B3_CALIBRATION      8u
#define REFMEM_APP_INSTANCE_B4_MODEL_VNA        9u
#define REFMEM_APP_INSTANCE_B4_MODEL_TT         10u
#define REFMEM_APP_INSTANCE_TDMA_FOUNDATION     11u

static const refmem_application_map_t s_application_map = {
    .version = REFMEM_APP_MODEL_VERSION,
    .application_id = 1u,
    .application_version = 1u,
    .profile_id = 1u,
    .layout_version = DISTRIBUTED_REFMEM_LAYOUT_VERSION,
    .target_node_mask = 0xFFu,
};

static const refmem_board_capability_table_t s_board_capability_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .board_count = REFMEM_APP_MODEL_NODE_COUNT,
    .board = {
        {0u, 0xB0000000u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_PIO |
                            REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_RJ45 |
         REFMEM_APP_CAP_CORE1_RT | REFMEM_APP_CAP_SMA_IN |
                             REFMEM_APP_CAP_SMA_OUT,
         REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT |
             REFMEM_APP_IO_RJ45_SYNC | REFMEM_APP_IO_PIO_SPI_SYNC,
         REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE |
             REFMEM_APP_IP_RJ45_SYNC_DELTA | REFMEM_APP_IP_VDC_DPLL |
             REFMEM_APP_IP_PIO_SPI_SYNC_DELTA | REFMEM_APP_IP_TDMA_SCHEDULER,
         REFMEM_APP_PERSONA_TRIGGER_MASTER, 0u, 0u, 1u},
        {1u, 0xB0000001u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_PIO |
                            REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_RJ45 |
                            REFMEM_APP_CAP_CORE1_RT | REFMEM_APP_CAP_SMA_IN |
                            REFMEM_APP_CAP_SMA_OUT,
         REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT | REFMEM_APP_IO_RJ45_SYNC,
         REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE |
             REFMEM_APP_IP_RJ45_SYNC_DELTA,
         REFMEM_APP_PERSONA_DISTRIBUTED_TRIGGER, 0u, 1u, 1u},
        {2u, 0xB0000002u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_PIO |
                            REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_RJ45 |
                            REFMEM_APP_CAP_CORE1_RT | REFMEM_APP_CAP_LINK_CONTROL,
         REFMEM_APP_IO_LINK_CONTROL | REFMEM_APP_IO_RJ45_SYNC,
         REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_LINK_SEQUENCE |
             REFMEM_APP_IP_RJ45_SYNC_DELTA,
         REFMEM_APP_PERSONA_LINK_CONTROL, 0u, 2u, 1u},
        {3u, 0xB0000003u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_FLASH |
                            REFMEM_APP_CAP_SD | REFMEM_APP_CAP_USB |
                            REFMEM_APP_CAP_RJ45 | REFMEM_APP_CAP_UART_RS485,
         REFMEM_APP_IO_RJ45_SYNC | REFMEM_APP_IO_UART_RS485,
         REFMEM_APP_IP_RJ45_SYNC_DELTA,
         REFMEM_APP_PERSONA_GATEWAY, 0u, 3u, 1u},
        {4u, 0xB0000004u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_USB |
                            REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA |
                            REFMEM_APP_CAP_CORE1_RT | REFMEM_APP_CAP_BISS_C,
         REFMEM_APP_IO_BISS_C,
         REFMEM_APP_IP_BISS_C_CODEC,
         REFMEM_APP_PERSONA_MODEL_INSTRUMENTS, 0u, 4u, 0u},
        {5u, 0xB0000005u, REFMEM_APP_CAP_BASELINE,
         0u, 0u, REFMEM_APP_PERSONA_SPARE, 0u, 5u, 0u},
        {6u, 0xB0000006u, REFMEM_APP_CAP_BASELINE,
         0u, 0u, REFMEM_APP_PERSONA_SPARE, 0u, 6u, 0u},
        {7u, 0xB0000007u, REFMEM_APP_CAP_BASELINE,
         0u, 0u, REFMEM_APP_PERSONA_SPARE, 0u, 7u, 0u},
    },
};

static const refmem_generic_node_table_t s_generic_node_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .node_count = REFMEM_APP_MODEL_NODE_COUNT,
    .node = {
        {0u, 0xB0000000u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_PIO |
                            REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_RJ45 |
                            REFMEM_APP_CAP_CORE1_RT | REFMEM_APP_CAP_SMA_IN |
                            REFMEM_APP_CAP_SMA_OUT,
         REFMEM_APP_CLAIM_STRICT_UUID, 100u,
         REFMEM_APP_PERSONA_TRIGGER_MASTER, 0u, 1u, REFMEM_APP_FAIL_STOP},
        {1u, 0xB0000001u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_PIO |
                            REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_RJ45 |
                            REFMEM_APP_CAP_CORE1_RT | REFMEM_APP_CAP_SMA_IN |
                            REFMEM_APP_CAP_SMA_OUT,
         REFMEM_APP_CLAIM_STRICT_UUID, 90u,
         REFMEM_APP_PERSONA_DISTRIBUTED_TRIGGER, 0u, 1u, REFMEM_APP_FAIL_STOP},
        {2u, 0xB0000002u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_PIO |
                            REFMEM_APP_CAP_DMA | REFMEM_APP_CAP_RJ45 |
                            REFMEM_APP_CAP_CORE1_RT |
                            REFMEM_APP_CAP_LINK_CONTROL,
         REFMEM_APP_CLAIM_STRICT_UUID, 80u,
         REFMEM_APP_PERSONA_LINK_CONTROL, 0u, 1u, REFMEM_APP_FAIL_STOP},
        {3u, 0xB0000003u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_FLASH |
                            REFMEM_APP_CAP_SD | REFMEM_APP_CAP_USB |
                            REFMEM_APP_CAP_RJ45 | REFMEM_APP_CAP_UART_RS485,
         REFMEM_APP_CLAIM_STRICT_UUID, 70u,
         REFMEM_APP_PERSONA_GATEWAY, 0u, 1u, REFMEM_APP_FAIL_STOP},
        {4u, 0xB0000004u, REFMEM_APP_CAP_BASELINE | REFMEM_APP_CAP_USB |
                            REFMEM_APP_CAP_PIO | REFMEM_APP_CAP_DMA |
                            REFMEM_APP_CAP_CORE1_RT | REFMEM_APP_CAP_BISS_C,
         REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT, 40u,
         REFMEM_APP_PERSONA_MODEL_INSTRUMENTS, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY},
        {5u, 0xB0000005u, REFMEM_APP_CAP_BASELINE,
         REFMEM_APP_CLAIM_SPARE_DYNAMIC, 10u,
         REFMEM_APP_PERSONA_SPARE, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY},
        {6u, 0xB0000006u, REFMEM_APP_CAP_BASELINE,
         REFMEM_APP_CLAIM_SPARE_DYNAMIC, 9u,
         REFMEM_APP_PERSONA_SPARE, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY},
        {7u, 0xB0000007u, REFMEM_APP_CAP_BASELINE,
         REFMEM_APP_CLAIM_SPARE_DYNAMIC, 8u,
         REFMEM_APP_PERSONA_SPARE, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY},
    },
};

static const refmem_node_load_table_t s_node_load_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .load_count = REFMEM_APP_MODEL_NODE_LOAD_COUNT,
    .load = {
        /* Disabled functional rows are lint-safe templates; SCPI/SD staging selects the runtime slot. */
        {0u, 1u, 1u, 0u, REFMEM_APP_INSTANCE_B0_REFMEM_SYNC,
         REFMEM_APP_ROLE_BOARD, REFMEM_APP_PERSONA_TRIGGER_MASTER,
         1u, 1u, REFMEM_APP_FAIL_STOP, 0u},
        {1u, 1u, 1u, 0u, REFMEM_APP_INSTANCE_B0_LOOP_ENGINE,
         REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_TRIGGER_MASTER, 1u, 1u, REFMEM_APP_FAIL_STOP, 1u},
        {2u, 1u, 1u, 0u, REFMEM_APP_INSTANCE_B0_PULSE_COUNTER,
         REFMEM_APP_ROLE_PULSE_DISTRIBUTOR, REFMEM_APP_PERSONA_TRIGGER_MASTER,
         0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 2u},
        {3u, 1u, 1u, 0u, REFMEM_APP_INSTANCE_B0_TRIGGER_MASTER,
         REFMEM_APP_ROLE_PULSE_DISTRIBUTOR, REFMEM_APP_PERSONA_TRIGGER_MASTER,
         0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 3u},
        {4u, 1u, 1u, 1u, REFMEM_APP_INSTANCE_B1_TRIGGER,
         REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_DISTRIBUTED_TRIGGER, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 0u},
        {5u, 1u, 1u, 2u, REFMEM_APP_INSTANCE_B2_LINK_SWITCH,
         REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_LINK_SWITCHER,
         REFMEM_APP_PERSONA_LINK_CONTROL, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 0u},
        {6u, 1u, 1u, 3u, REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY,
         REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_GATEWAY,
         REFMEM_APP_PERSONA_GATEWAY, 1u, 1u, REFMEM_APP_FAIL_STOP, 0u},
        {7u, 1u, 1u, 3u, REFMEM_APP_INSTANCE_B3_INSTRUMENT,
         REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER | REFMEM_APP_ROLE_GATEWAY,
         REFMEM_APP_PERSONA_GATEWAY, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY, 1u},
        {8u, 1u, 1u, 3u, REFMEM_APP_INSTANCE_B3_CALIBRATION,
         REFMEM_APP_ROLE_GATEWAY, REFMEM_APP_PERSONA_GATEWAY,
         1u, 1u, REFMEM_APP_FAIL_STOP, 2u},
        {9u, 1u, 1u, 4u, REFMEM_APP_INSTANCE_B4_MODEL_VNA,
         REFMEM_APP_ROLE_MODEL_VNA | REFMEM_APP_ROLE_TEST_AGENT,
         REFMEM_APP_PERSONA_MODEL_INSTRUMENTS, 0u, 0u,
         REFMEM_APP_FAIL_REPORT_ONLY, 0u},
        {10u, 1u, 1u, 4u, REFMEM_APP_INSTANCE_B4_MODEL_TT,
         REFMEM_APP_ROLE_MODEL_TURNTABLE | REFMEM_APP_ROLE_TEST_AGENT,
         REFMEM_APP_PERSONA_MODEL_INSTRUMENTS, 0u, 0u,
         REFMEM_APP_FAIL_REPORT_ONLY, 1u},
        {11u, 1u, 1u, 0u, REFMEM_APP_INSTANCE_TDMA_FOUNDATION,
         REFMEM_APP_ROLE_BOARD, REFMEM_APP_PERSONA_TRIGGER_MASTER,
         1u, 1u, REFMEM_APP_FAIL_STOP, 4u},
    },
};

static const refmem_fb_instance_table_t s_fb_instance_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .instance_count = REFMEM_APP_MODEL_INSTANCE_COUNT,
    .instance = {
        {REFMEM_APP_INSTANCE_B0_REFMEM_SYNC, 0u, REFMEM_APP_DOMAIN_REFMEM, REFMEM_APP_FB_REFMEM_SYNC,
         REFMEM_APP_FB_REFMEM_SYNC, "B0.RefMemSyncFB", 1u, 1u,
         REFMEM_APP_RESOURCE_RJ45, REFMEM_APP_IO_RJ45_SYNC,
         REFMEM_APP_IP_RJ45_SYNC_DELTA, 1000u,
         REFMEM_VECTOR_REGION_GATEWAY, REFMEM_VECTOR_REGION_STATS, 2u, 1u, 3u, 2u, 1u, 1u},
        {REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, 0u, REFMEM_APP_DOMAIN_TRIG, REFMEM_APP_FB_LOOP_ENGINE,
         REFMEM_APP_FB_LOOP_ENGINE, "B0.LoopEngineAO", 1u, 1u,
         REFMEM_APP_RESOURCE_CORE1_RT, REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT,
         REFMEM_APP_IP_VDC_DPLL, 500u,
         REFMEM_VECTOR_REGION_LOOP, REFMEM_VECTOR_REGION_STATS, 3u, 2u, 5u, 3u, 2u, 2u},
        {REFMEM_APP_INSTANCE_B0_PULSE_COUNTER, 0u, REFMEM_APP_DOMAIN_TRIG,
         REFMEM_APP_FB_PULSE_COUNTER, REFMEM_APP_FB_PULSE_COUNTER,
         "Template.PulseCounterAO", 1u, 0u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA | REFMEM_APP_RESOURCE_CORE1_RT,
         REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT,
         REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE, 200u,
         REFMEM_VECTOR_REGION_TRIGGER, REFMEM_VECTOR_REGION_STATS, 5u, 1u, 8u, 1u, 0u, 2u},
        {REFMEM_APP_INSTANCE_B0_TRIGGER_MASTER, 0u, REFMEM_APP_DOMAIN_TRIG, REFMEM_APP_FB_TRIGGER_AO,
         REFMEM_APP_FB_TRIGGER_AO, "Template.TriggerMasterAO", 1u, 0u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA | REFMEM_APP_RESOURCE_CORE1_RT,
         REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT,
         REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE, 200u,
         REFMEM_VECTOR_REGION_TRIGGER, REFMEM_VECTOR_REGION_STATS, 5u, 1u, 8u, 1u, 3u, 2u},
        {REFMEM_APP_INSTANCE_B1_TRIGGER, 1u, REFMEM_APP_DOMAIN_TRIG, REFMEM_APP_FB_TRIGGER_AO,
         REFMEM_APP_FB_TRIGGER_AO, "Template.TriggerAO", 1u, 0u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA | REFMEM_APP_RESOURCE_CORE1_RT,
         REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT | REFMEM_APP_IO_RJ45_SYNC,
         REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE, 200u,
         REFMEM_VECTOR_REGION_TRIGGER, REFMEM_VECTOR_REGION_STATS, 6u, 1u, 9u, 1u, 3u, 2u},
        {REFMEM_APP_INSTANCE_B2_LINK_SWITCH, 2u, REFMEM_APP_DOMAIN_TRIG,
         REFMEM_APP_FB_LINK_SWITCHER, REFMEM_APP_FB_LINK_SWITCHER,
         "Template.LinkSwitcherAO", 1u, 0u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA | REFMEM_APP_RESOURCE_CORE1_RT,
         REFMEM_APP_IO_LINK_CONTROL | REFMEM_APP_IO_RJ45_SYNC,
         REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_LINK_SEQUENCE, 300u,
         REFMEM_VECTOR_REGION_IO, REFMEM_VECTOR_REGION_STATS, 8u, 3u, 10u, 3u, 4u, 2u},
        {REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY, 3u, REFMEM_APP_DOMAIN_SYSTEM,
         REFMEM_APP_FB_SYSTEM_AO, REFMEM_APP_FB_SYSTEM_AO,
         "B3.SystemGatewayAO", 1u, 1u,
         REFMEM_APP_RESOURCE_FLASH | REFMEM_APP_RESOURCE_SD | REFMEM_APP_RESOURCE_USB,
         0u, 0u, 1000u,
         REFMEM_VECTOR_REGION_SYSTEM, REFMEM_VECTOR_REGION_FAULT, 0u, 0u, 11u, 1u, 0u, 2u},
        {REFMEM_APP_INSTANCE_B3_INSTRUMENT, 3u, REFMEM_APP_DOMAIN_GATEWAY,
         REFMEM_APP_FB_GATEWAY_AO, REFMEM_APP_FB_INSTRUMENT_CONTROLLER,
         "Template.InstrumentControllerAO", 1u, 0u,
         REFMEM_APP_RESOURCE_RJ45, REFMEM_APP_IO_RJ45_SYNC | REFMEM_APP_IO_UART_RS485,
         REFMEM_APP_IP_RJ45_SYNC_DELTA, 1000u,
         REFMEM_VECTOR_REGION_GATEWAY, REFMEM_VECTOR_REGION_STATS, 0u, 0u, 13u, 1u, 5u, 1u},
        {REFMEM_APP_INSTANCE_B3_CALIBRATION, 3u, REFMEM_APP_DOMAIN_CAL, REFMEM_APP_FB_CALIBRATION_AO,
         REFMEM_APP_FB_CALIBRATION_AO, "B3.CalibrationAO", 1u, 1u,
         REFMEM_APP_RESOURCE_RJ45, REFMEM_APP_IO_RJ45_SYNC,
         REFMEM_APP_IP_RJ45_SYNC_DELTA, 1000u,
         REFMEM_VECTOR_REGION_CAL, REFMEM_VECTOR_REGION_STATS, 0u, 0u, 0u, 0u, 6u, 1u},
        {REFMEM_APP_INSTANCE_B4_MODEL_VNA, 4u, REFMEM_APP_DOMAIN_MEAS, REFMEM_APP_FB_MODEL_VNA,
         REFMEM_APP_FB_MODEL_VNA, "Template.ModelVnaAO", 1u, 0u,
         REFMEM_APP_RESOURCE_USB, 0u, 0u, 1000u,
         REFMEM_VECTOR_REGION_GATEWAY, REFMEM_VECTOR_REGION_STATS, 0u, 0u, 0u, 0u, 7u, 1u},
        {REFMEM_APP_INSTANCE_B4_MODEL_TT, 4u, REFMEM_APP_DOMAIN_MEAS, REFMEM_APP_FB_MODEL_TURNTABLE,
         REFMEM_APP_FB_MODEL_TURNTABLE, "Template.ModelTurntableAO", 1u, 0u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA | REFMEM_APP_RESOURCE_CORE1_RT,
         REFMEM_APP_IO_MODEL_TURNTABLE_PULSE, REFMEM_APP_IP_PULSE_FIRE, 500u,
         REFMEM_VECTOR_REGION_IO, REFMEM_VECTOR_REGION_STATS, 0u, 0u, 0u, 0u, 8u, 1u},
        {REFMEM_APP_INSTANCE_TDMA_FOUNDATION, 0u, REFMEM_APP_DOMAIN_TDMA,
         REFMEM_APP_FB_TDMA_SCHEDULER, REFMEM_APP_FB_TDMA_SCHEDULER,
         "Foundation.TdmaSchedulerAO", 1u, 1u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA |
             REFMEM_APP_RESOURCE_CORE1_RT | REFMEM_APP_RESOURCE_TDMA_SCHEDULER,
         REFMEM_APP_IO_PIO_SPI_SYNC,
         REFMEM_APP_IP_PIO_SPI_SYNC_DELTA | REFMEM_APP_IP_TDMA_SCHEDULER, 100u,
         REFMEM_VECTOR_REGION_SERVICE, REFMEM_VECTOR_REGION_STATS,
         0u, 0u, 0u, 0u, 9u, 1u},
    },
};

static const refmem_event_link_table_t s_event_link_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .event_link_count = REFMEM_APP_MODEL_EVENT_LINK_COUNT,
    .link = {
        {0u, REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY, REFMEM_APP_EVENT_CONFIG_STAGE, 0x0Fu,
         REFMEM_APP_INSTANCE_B0_REFMEM_SYNC, REFMEM_APP_EVENT_CONFIG_STAGE,
         REFMEM_APP_TRANSPORT_COMMAND_SLOT, 50000u, REFMEM_APP_ACK_ALL_REQUIRED, 0u, 1u, REFMEM_VECTOR_REGION_ACK_CMD},
        {1u, REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY, REFMEM_APP_EVENT_CONFIG_ACTIVATE, 0x0Fu,
         REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, REFMEM_APP_EVENT_CONFIG_ACTIVATE,
         REFMEM_APP_TRANSPORT_COMMAND_SLOT, 50000u, REFMEM_APP_ACK_ALL_REQUIRED, 0u, 1u, REFMEM_VECTOR_REGION_ACK_CMD},
        {2u, REFMEM_APP_INSTANCE_B0_REFMEM_SYNC, REFMEM_APP_EVENT_ACK, 0x08u,
         REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY, REFMEM_APP_EVENT_ACK,
         REFMEM_APP_TRANSPORT_RJ45_SYNC_RING, 20000u, REFMEM_APP_ACK_BITMAP, 0u, 1u, REFMEM_VECTOR_REGION_ACK_CMD},
        {3u, REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, REFMEM_APP_EVENT_START, 0x0Fu,
         REFMEM_APP_INSTANCE_B0_TRIGGER_MASTER, REFMEM_APP_EVENT_START,
         REFMEM_APP_TRANSPORT_COMMAND_SLOT, 20000u, REFMEM_APP_ACK_ALL_REQUIRED, 0u, 2u, REFMEM_VECTOR_REGION_ACK_CMD},
        {4u, REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, REFMEM_APP_EVENT_STOP, 0x0Fu,
         REFMEM_APP_INSTANCE_B0_TRIGGER_MASTER, REFMEM_APP_EVENT_STOP,
         REFMEM_APP_TRANSPORT_COMMAND_SLOT, 10000u, REFMEM_APP_ACK_ALL_REQUIRED, 0u, 2u, REFMEM_VECTOR_REGION_ACK_CMD},
        {5u, REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, REFMEM_APP_EVENT_FIRE_LOAD, 0x01u,
         REFMEM_APP_INSTANCE_B0_PULSE_COUNTER, REFMEM_APP_EVENT_FIRE_LOAD,
         REFMEM_APP_TRANSPORT_CORE_IPC, 1000u, REFMEM_APP_ACK_NONE, 0u, 3u, REFMEM_VECTOR_REGION_TRIGGER},
        {6u, REFMEM_APP_INSTANCE_B0_PULSE_COUNTER, REFMEM_APP_EVENT_DONE, 0x01u,
         REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, REFMEM_APP_EVENT_DONE,
         REFMEM_APP_TRANSPORT_RJ45_SYNC_RING, 10000u, REFMEM_APP_ACK_BITMAP, 0u, 2u, REFMEM_VECTOR_REGION_STATS},
        {7u, REFMEM_APP_INSTANCE_B0_PULSE_COUNTER, REFMEM_APP_EVENT_FAULT, 0x08u,
         REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY, REFMEM_APP_EVENT_FAULT,
         REFMEM_APP_TRANSPORT_RJ45_SYNC_RING, 10000u, REFMEM_APP_ACK_BITMAP, 0u, 3u, REFMEM_VECTOR_REGION_FAULT},
        {8u, REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, REFMEM_APP_EVENT_FIRE_LOAD, 0x04u,
         REFMEM_APP_INSTANCE_B2_LINK_SWITCH, REFMEM_APP_EVENT_FIRE_LOAD,
         REFMEM_APP_TRANSPORT_RJ45_SYNC_RING, 2000u, REFMEM_APP_ACK_BITMAP, 0u, 3u, REFMEM_VECTOR_REGION_IO},
        {9u, REFMEM_APP_INSTANCE_B2_LINK_SWITCH, REFMEM_APP_EVENT_DONE, 0x01u,
         REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, REFMEM_APP_EVENT_DONE,
         REFMEM_APP_TRANSPORT_RJ45_SYNC_RING, 10000u, REFMEM_APP_ACK_BITMAP, 0u, 2u, REFMEM_VECTOR_REGION_STATS},
        {10u, REFMEM_APP_INSTANCE_B2_LINK_SWITCH, REFMEM_APP_EVENT_FAULT, 0x08u,
         REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY, REFMEM_APP_EVENT_FAULT,
         REFMEM_APP_TRANSPORT_RJ45_SYNC_RING, 10000u, REFMEM_APP_ACK_BITMAP, 0u, 3u, REFMEM_VECTOR_REGION_FAULT},
    },
};

static const refmem_data_link_table_t s_data_link_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .data_link_count = REFMEM_APP_MODEL_DATA_LINK_COUNT,
    .link = {
        {0u, "SystemRegion.mode", REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY, 0xFFu, REFMEM_APP_DATA_ENUM,
         REFMEM_APP_UNIT_NONE, 1, 0, 8, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 10000u, 50000u, REFMEM_VECTOR_REGION_SYSTEM,
         REFMEM_APP_PERMISSION_COMMAND_WRITE},
        {1u, "RoleRegion.node_role", REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY, 0xFFu, REFMEM_APP_DATA_BITMASK,
         REFMEM_APP_UNIT_NONE, 1, 0, 255, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 100000u, 500000u, REFMEM_VECTOR_REGION_ROLE,
         REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE},
        {2u, "LoopRegion.active_sequence_crc", REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, 0xFFu,
         REFMEM_APP_DATA_CRC, REFMEM_APP_UNIT_NONE, 1, 0, 2147483647, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_SEQLOCK, 10000u, 50000u, REFMEM_VECTOR_REGION_LOOP,
         REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE},
        {3u, "LoopRegion.run_state", REFMEM_APP_INSTANCE_B0_LOOP_ENGINE, 0xFFu, REFMEM_APP_DATA_ENUM,
         REFMEM_APP_UNIT_NONE, 1, 0, 16, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 1000u, 10000u, REFMEM_VECTOR_REGION_LOOP,
         REFMEM_APP_PERMISSION_COMMAND_WRITE},
        {4u, "VdcRegion.dc_time64_ns", REFMEM_APP_INSTANCE_B0_REFMEM_SYNC, 0xFFu, REFMEM_APP_DATA_NS,
         REFMEM_APP_UNIT_NS, 1, 0, 2147483647, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_SEQLOCK, 1000u, 10000u, REFMEM_VECTOR_REGION_VDC,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {5u, "DpllRegion.lock_state", REFMEM_APP_INSTANCE_B0_REFMEM_SYNC, 0xFFu, REFMEM_APP_DATA_ENUM,
         REFMEM_APP_UNIT_NONE, 1, 0, 8, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 1000u, 10000u, REFMEM_VECTOR_REGION_DPLL,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {6u, "CalibrationRegion.delay_crc", REFMEM_APP_INSTANCE_B3_CALIBRATION, 0xFFu,
         REFMEM_APP_DATA_CRC, REFMEM_APP_UNIT_NONE, 1, 0, 2147483647, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_SEQLOCK, 100000u, 500000u, REFMEM_VECTOR_REGION_CAL,
         REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE},
        {7u, "AckCommandRegion.command_seq", REFMEM_APP_INSTANCE_B3_SYSTEM_GATEWAY, 0xFFu,
         REFMEM_APP_DATA_U32, REFMEM_APP_UNIT_COUNT, 1, 0, 2147483647, REFMEM_APP_LIFE_TRANSIENT,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 1000u, 50000u, REFMEM_VECTOR_REGION_ACK_CMD,
         REFMEM_APP_PERMISSION_COMMAND_WRITE},
        {8u, "TriggerRegion.fire_seq", REFMEM_APP_INSTANCE_B0_PULSE_COUNTER, 0x0Fu, REFMEM_APP_DATA_U32,
         REFMEM_APP_UNIT_COUNT, 1, 0, 2147483647, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_SEQLOCK, 1000u, 10000u, REFMEM_VECTOR_REGION_TRIGGER,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {9u, "TriggerRegion.node_heartbeat", REFMEM_APP_INSTANCE_B1_TRIGGER, 0x0Fu,
         REFMEM_APP_DATA_U32, REFMEM_APP_UNIT_COUNT, 1, 0, 2147483647, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 1000u, 10000u, REFMEM_VECTOR_REGION_NODE,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {10u, "IoRegion.link_state", REFMEM_APP_INSTANCE_B2_LINK_SWITCH, 0xFFu, REFMEM_APP_DATA_BITMASK,
         REFMEM_APP_UNIT_NONE, 1, 0, 65535, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 10000u, 50000u, REFMEM_VECTOR_REGION_IO,
         REFMEM_APP_PERMISSION_COMMAND_WRITE},
        {11u, "IoRegion.link_pulse_timestamp", REFMEM_APP_INSTANCE_B2_LINK_SWITCH, 0xFFu,
         REFMEM_APP_DATA_TICK, REFMEM_APP_UNIT_TICK, 1, 0, 2147483647, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_SEQLOCK, 1000u, 10000u, REFMEM_VECTOR_REGION_IO,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {12u, "IoRegion.link_sequence_state", REFMEM_APP_INSTANCE_B2_LINK_SWITCH, 0xFFu,
         REFMEM_APP_DATA_ENUM, REFMEM_APP_UNIT_NONE, 1, 0, 32, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 1000u, 10000u, REFMEM_VECTOR_REGION_IO,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {13u, "GatewayRegion.instrument_state", REFMEM_APP_INSTANCE_B3_INSTRUMENT, 0xFFu,
         REFMEM_APP_DATA_ENUM, REFMEM_APP_UNIT_NONE, 1, 0, 32, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 10000u, 50000u, REFMEM_VECTOR_REGION_GATEWAY,
         REFMEM_APP_PERMISSION_READ_ONLY},
    },
};

static const refmem_deployment_gate_table_t s_deployment_gate = {
    .version = REFMEM_APP_MODEL_VERSION,
    .check_count = REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT,
    .check = {
        {REFMEM_APP_GATE_LAYOUT, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_HEADER, 0u},
        {REFMEM_APP_GATE_NODE, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_NODE, 0u},
        {REFMEM_APP_GATE_INSTANCE, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_TLV, 0u},
        {REFMEM_APP_GATE_RESOURCE, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_SERVICE, 0u},
        {REFMEM_APP_GATE_IO, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_IO, 0u},
        {REFMEM_APP_GATE_WRITER, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_TLV, 0u},
        {REFMEM_APP_GATE_EVENT, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_ACK_CMD, 0u},
        {REFMEM_APP_GATE_DATA, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_TLV, 0u},
        {REFMEM_APP_GATE_CONFIG, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_SYSTEM, 0u},
        {REFMEM_APP_GATE_CAL_SYNC, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_CAL, 0u},
        {REFMEM_APP_GATE_QUALITY, 1u, REFMEM_APP_GATE_LATCH_FAULT, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_STATS, 0u},
        {REFMEM_APP_GATE_TDMA, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_REGION_SERVICE, 0u},
    },
};

static const refmem_connection_quality_table_t s_connection_quality = {
    .version = REFMEM_APP_MODEL_VERSION,
    .quality_count = REFMEM_APP_MODEL_QUALITY_COUNT,
    .quality = {
        {0u, REFMEM_APP_QUALITY_NODE, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        {1u, REFMEM_APP_QUALITY_NODE, 1u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u},
        {2u, REFMEM_APP_QUALITY_NODE, 2u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 2u},
        {3u, REFMEM_APP_QUALITY_NODE, 3u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 3u},
        {4u, REFMEM_APP_QUALITY_RJ45_LINK, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 4u},
        {5u, REFMEM_APP_QUALITY_RJ45_LINK, 1u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 5u},
        {6u, REFMEM_APP_QUALITY_EVENT_LINK, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 6u},
        {7u, REFMEM_APP_QUALITY_DATA_LINK, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 7u},
    },
};

static tdma_foundation_profile_t s_tdma_foundation_profile;

static refmem_application_model_snapshot_t s_snapshot;
static refmem_application_model_load_snapshot_t s_load_snapshot;
static refmem_board_capability_load_snapshot_t s_board_load_snapshot;
static refmem_node_load_table_t s_staging_node_load_table;
static refmem_board_capability_table_t s_staging_board_capability_table;
static uint8_t s_inline_package_image_buffer[REFMEM_TABLE_IMAGE_BUFFER_SIZE];
static refmem_application_map_t s_active_application_map;
static refmem_board_capability_table_t s_active_board_capability_table;
static refmem_generic_node_table_t s_active_generic_node_table;
static refmem_node_load_table_t s_active_node_load_table;
static refmem_fb_instance_table_t s_active_fb_instance_table;
static refmem_event_link_table_t s_active_event_link_table;
static refmem_data_link_table_t s_active_data_link_table;
static refmem_deployment_gate_table_t s_active_deployment_gate;
static refmem_connection_quality_table_t s_active_connection_quality;
static tdma_foundation_profile_t s_active_tdma_foundation_profile;
static bool s_staging_node_load_valid;
static bool s_staging_board_capability_valid;
static bool s_active_tables_from_image;
static bool s_initialized;

typedef struct {
    refmem_application_map_t application_map;
    refmem_board_capability_table_t board_capability;
    refmem_generic_node_table_t generic_node;
    refmem_node_load_table_t node_load;
    refmem_fb_instance_table_t fb_instance;
    refmem_event_link_table_t event_link;
    refmem_data_link_table_t data_link;
    refmem_deployment_gate_table_t deployment_gate;
    refmem_connection_quality_table_t connection_quality;
    tdma_foundation_profile_t tdma_foundation_profile;
    uint32_t table_crc32[REFMEM_TABLE_REGISTRY_COUNT];
    uint32_t package_crc32;
    uint32_t table_seq;
} refmem_model_parsed_tables_t;

static refmem_model_parsed_tables_t s_pending_tables;
static bool s_pending_tables_valid;

static bool refmem_model_instance_enabled(const refmem_fb_instance_entry_t *instance);
static void refmem_model_copy_text(char *dst, size_t dst_size, const char *src);
static void refmem_model_finish_load_idle(void);
static const refmem_application_map_t *refmem_model_current_application_map(void);
static const refmem_board_capability_table_t *refmem_model_current_board_capability_table(void);
static const refmem_generic_node_table_t *refmem_model_current_generic_node_table(void);
static const refmem_node_load_table_t *refmem_model_current_node_load_table(void);
static const refmem_fb_instance_table_t *refmem_model_current_fb_instance_table(void);
static const refmem_event_link_table_t *refmem_model_current_event_link_table(void);
static const refmem_data_link_table_t *refmem_model_current_data_link_table(void);
static const refmem_deployment_gate_table_t *refmem_model_current_deployment_gate_table(void);
static const refmem_connection_quality_table_t *refmem_model_current_connection_quality_table(void);
static const tdma_foundation_profile_t *refmem_model_current_tdma_foundation_profile(void);

static uint32_t refmem_model_crc32_update(uint32_t crc, const void *data, size_t size)
{
    return ota_crc32_update(crc, (const uint8_t *)data, size);
}

static uint32_t refmem_model_crc32_string(uint32_t crc, const char *text)
{
    if (text == NULL) {
        return crc;
    }
    while (*text != '\0') {
        crc = ota_crc32_update(crc, (const uint8_t *)text, 1u);
        text++;
    }
    return crc;
}

static uint32_t refmem_model_rmtp_crc32(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < size; i++) {
        crc ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static uint32_t refmem_model_rmtp_crc32_string(const char *text)
{
    if (text == NULL) {
        return 0u;
    }
    uint32_t crc = 0xFFFFFFFFu;
    while (*text != '\0') {
        crc ^= (uint8_t)*text;
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
        text++;
    }
    return ~crc;
}

static uint32_t refmem_model_rmtp_crc32_zero_field(const uint8_t *data,
                                                   size_t size,
                                                   size_t zero_offset)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0u; i < size; i++) {
        uint8_t byte = data[i];
        if (i >= zero_offset && i < zero_offset + sizeof(uint32_t)) {
            byte = 0u;
        }
        crc ^= byte;
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static bool refmem_model_write_u32_le(uint8_t *data,
                                      size_t size,
                                      size_t *cursor,
                                      uint32_t value)
{
    if (data == NULL || cursor == NULL || *cursor > size || size - *cursor < sizeof(uint32_t)) {
        return false;
    }
    data[*cursor + 0u] = (uint8_t)(value & 0xFFu);
    data[*cursor + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
    data[*cursor + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
    data[*cursor + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
    *cursor += sizeof(uint32_t);
    return true;
}

static bool refmem_model_write_i32_le(uint8_t *data,
                                      size_t size,
                                      size_t *cursor,
                                      int32_t value)
{
    return refmem_model_write_u32_le(data, size, cursor, (uint32_t)value);
}

static uint32_t refmem_model_crc32_fields(const uint32_t *fields, size_t count)
{
    return refmem_model_crc32_update(0xFFFFFFFFu, fields, count * sizeof(uint32_t));
}

static uint32_t refmem_model_read_u32_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int32_t refmem_model_read_i32_le(const uint8_t *data)
{
    return (int32_t)refmem_model_read_u32_le(data);
}

static bool refmem_model_validate_wire_header(const uint8_t *data,
                                              size_t size,
                                              uint32_t expected_count,
                                              uint32_t row_words,
                                              uint32_t *count)
{
    if (data == NULL ||
        count == NULL ||
        size != REFMEM_APP_TABLE_WIRE_SIZE(expected_count, row_words)) {
        return false;
    }

    const uint32_t version = refmem_model_read_u32_le(&data[0]);
    const uint32_t parsed_count = refmem_model_read_u32_le(&data[4]);
    if (version != REFMEM_APP_MODEL_VERSION || parsed_count != expected_count) {
        return false;
    }

    *count = parsed_count;
    return true;
}

static const char *refmem_model_default_instance_name(uint32_t instance_id)
{
    if (instance_id < s_fb_instance_table.instance_count) {
        return s_fb_instance_table.instance[instance_id].instance_name;
    }
    return "";
}

static const char *refmem_model_default_region_path(uint32_t data_link_id)
{
    if (data_link_id < s_data_link_table.data_link_count) {
        return s_data_link_table.link[data_link_id].region_path;
    }
    return "";
}

static bool refmem_model_parse_application_map_view(const uint8_t *data,
                                                    size_t size,
                                                    void *table)
{
    refmem_application_map_t *map = (refmem_application_map_t *)table;
    if (data == NULL || map == NULL || size != sizeof(*map)) {
        return false;
    }

    memset(map, 0, sizeof(*map));
    map->version = refmem_model_read_u32_le(&data[0]);
    map->application_id = refmem_model_read_u32_le(&data[4]);
    map->application_version = refmem_model_read_u32_le(&data[8]);
    map->profile_id = refmem_model_read_u32_le(&data[12]);
    map->layout_version = refmem_model_read_u32_le(&data[16]);
    map->target_node_mask = refmem_model_read_u32_le(&data[20]);
    return map->version == REFMEM_APP_MODEL_VERSION;
}

static bool refmem_model_parse_board_capability_view(const uint8_t *data,
                                                     size_t size,
                                                     void *table)
{
    refmem_board_capability_table_t *boards = (refmem_board_capability_table_t *)table;
    if (data == NULL || boards == NULL || size != sizeof(*boards)) {
        return false;
    }

    memset(boards, 0, sizeof(*boards));
    boards->version = refmem_model_read_u32_le(&data[0]);
    boards->board_count = refmem_model_read_u32_le(&data[4]);
    size_t cursor = 8u;
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT; i++) {
        refmem_board_capability_entry_t *board = &boards->board[i];
        board->board_id = refmem_model_read_u32_le(&data[cursor + 0u]);
        board->board_uuid_crc32 = refmem_model_read_u32_le(&data[cursor + 4u]);
        board->capability_mask = refmem_model_read_u32_le(&data[cursor + 8u]);
        board->io_constraint_mask = refmem_model_read_u32_le(&data[cursor + 12u]);
        board->ip_core_mask = refmem_model_read_u32_le(&data[cursor + 16u]);
        board->default_persona_mask = refmem_model_read_u32_le(&data[cursor + 20u]);
        board->hw_profile_crc32 = refmem_model_read_u32_le(&data[cursor + 24u]);
        board->active_default_slot = refmem_model_read_u32_le(&data[cursor + 28u]);
        board->online_required = refmem_model_read_u32_le(&data[cursor + 32u]);
        cursor += 9u * sizeof(uint32_t);
    }
    return boards->version == REFMEM_APP_MODEL_VERSION &&
           boards->board_count == REFMEM_APP_MODEL_NODE_COUNT;
}

static bool refmem_model_parse_generic_node_view(const uint8_t *data,
                                                 size_t size,
                                                 void *table)
{
    refmem_generic_node_table_t *nodes = (refmem_generic_node_table_t *)table;
    if (data == NULL || nodes == NULL || size != sizeof(*nodes)) {
        return false;
    }

    memset(nodes, 0, sizeof(*nodes));
    nodes->version = refmem_model_read_u32_le(&data[0]);
    nodes->node_count = refmem_model_read_u32_le(&data[4]);
    size_t cursor = 8u;
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        refmem_app_node_entry_t *node = &nodes->node[i];
        node->node_id = refmem_model_read_u32_le(&data[cursor + 0u]);
        node->node_uuid_crc32 = refmem_model_read_u32_le(&data[cursor + 4u]);
        node->capability_mask = refmem_model_read_u32_le(&data[cursor + 8u]);
        node->claim_policy = refmem_model_read_u32_le(&data[cursor + 12u]);
        node->claim_priority = refmem_model_read_u32_le(&data[cursor + 16u]);
        node->default_persona_mask = refmem_model_read_u32_le(&data[cursor + 20u]);
        node->hw_profile_crc32 = refmem_model_read_u32_le(&data[cursor + 24u]);
        node->online_required = refmem_model_read_u32_le(&data[cursor + 28u]);
        node->fail_policy = refmem_model_read_u32_le(&data[cursor + 32u]);
        cursor += 9u * sizeof(uint32_t);
    }
    return nodes->version == REFMEM_APP_MODEL_VERSION &&
           nodes->node_count == REFMEM_APP_MODEL_NODE_COUNT;
}

static bool refmem_model_parse_node_load_view(const uint8_t *data,
                                              size_t size,
                                              void *table)
{
    refmem_node_load_table_t *loads = (refmem_node_load_table_t *)table;
    if (data == NULL || loads == NULL || size != sizeof(*loads)) {
        return false;
    }

    memset(loads, 0, sizeof(*loads));
    loads->version = refmem_model_read_u32_le(&data[0]);
    loads->load_count = refmem_model_read_u32_le(&data[4]);
    size_t cursor = 8u;
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_NODE_LOAD_COUNT; i++) {
        refmem_node_load_entry_t *load = &loads->load[i];
        load->load_id = refmem_model_read_u32_le(&data[cursor + 0u]);
        load->application_id = refmem_model_read_u32_le(&data[cursor + 4u]);
        load->profile_id = refmem_model_read_u32_le(&data[cursor + 8u]);
        load->node_id = refmem_model_read_u32_le(&data[cursor + 12u]);
        load->instance_id = refmem_model_read_u32_le(&data[cursor + 16u]);
        load->role_mask = refmem_model_read_u32_le(&data[cursor + 20u]);
        load->persona_mask = refmem_model_read_u32_le(&data[cursor + 24u]);
        load->enabled = refmem_model_read_u32_le(&data[cursor + 28u]);
        load->required = refmem_model_read_u32_le(&data[cursor + 32u]);
        load->fail_policy = refmem_model_read_u32_le(&data[cursor + 36u]);
        load->load_order = refmem_model_read_u32_le(&data[cursor + 40u]);
        cursor += 11u * sizeof(uint32_t);
    }
    return loads->version == REFMEM_APP_MODEL_VERSION &&
           loads->load_count == REFMEM_APP_MODEL_NODE_LOAD_COUNT;
}

static bool refmem_model_parse_fb_instance_view(const uint8_t *data,
                                                size_t size,
                                                void *table)
{
    refmem_fb_instance_table_t *instances = (refmem_fb_instance_table_t *)table;
    uint32_t count = 0u;
    if (instances == NULL ||
        !refmem_model_validate_wire_header(data,
                                           size,
                                           REFMEM_APP_MODEL_INSTANCE_COUNT,
                                           REFMEM_APP_TABLE_WIRE_FB_INSTANCE_WORDS,
                                           &count)) {
        return false;
    }

    memset(instances, 0, sizeof(*instances));
    instances->version = REFMEM_APP_MODEL_VERSION;
    instances->instance_count = count;
    size_t cursor = REFMEM_APP_TABLE_WIRE_HEADER_WORDS * REFMEM_APP_TABLE_WIRE_U32_SIZE;
    for (uint32_t i = 0u; i < count; i++) {
        refmem_fb_instance_entry_t *instance = &instances->instance[i];
        instance->instance_id = refmem_model_read_u32_le(&data[cursor + 0u]);
        instance->default_node_id = refmem_model_read_u32_le(&data[cursor + 4u]);
        instance->domain = refmem_model_read_u32_le(&data[cursor + 8u]);
        instance->ao_type = refmem_model_read_u32_le(&data[cursor + 12u]);
        instance->fb_type = refmem_model_read_u32_le(&data[cursor + 16u]);
        instance->instance_name = refmem_model_default_instance_name(instance->instance_id);
        instance->version = refmem_model_read_u32_le(&data[cursor + 24u]);
        instance->enable_condition = refmem_model_read_u32_le(&data[cursor + 28u]);
        instance->resource_claim = refmem_model_read_u32_le(&data[cursor + 32u]);
        instance->io_claim = refmem_model_read_u32_le(&data[cursor + 36u]);
        instance->ip_core_claim = refmem_model_read_u32_le(&data[cursor + 40u]);
        instance->time_budget_us = refmem_model_read_u32_le(&data[cursor + 44u]);
        instance->state_region_ref = refmem_model_read_u32_le(&data[cursor + 48u]);
        instance->health_region_ref = refmem_model_read_u32_le(&data[cursor + 52u]);
        instance->event_first = refmem_model_read_u32_le(&data[cursor + 56u]);
        instance->event_count = refmem_model_read_u32_le(&data[cursor + 60u]);
        instance->data_first = refmem_model_read_u32_le(&data[cursor + 64u]);
        instance->data_count = refmem_model_read_u32_le(&data[cursor + 68u]);
        instance->conflict_class = refmem_model_read_u32_le(&data[cursor + 72u]);
        instance->restart_policy = refmem_model_read_u32_le(&data[cursor + 76u]);
        cursor += REFMEM_APP_TABLE_WIRE_FB_INSTANCE_WORDS * REFMEM_APP_TABLE_WIRE_U32_SIZE;
    }
    return true;
}

static bool refmem_model_parse_event_link_view(const uint8_t *data,
                                               size_t size,
                                               void *table)
{
    refmem_event_link_table_t *links = (refmem_event_link_table_t *)table;
    if (data == NULL || links == NULL || size != sizeof(*links)) {
        return false;
    }

    memset(links, 0, sizeof(*links));
    links->version = refmem_model_read_u32_le(&data[0]);
    links->event_link_count = refmem_model_read_u32_le(&data[4]);
    size_t cursor = 8u;
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_EVENT_LINK_COUNT; i++) {
        refmem_event_link_entry_t *link = &links->link[i];
        link->event_link_id = refmem_model_read_u32_le(&data[cursor + 0u]);
        link->source_instance = refmem_model_read_u32_le(&data[cursor + 4u]);
        link->source_event = refmem_model_read_u32_le(&data[cursor + 8u]);
        link->target_node_mask = refmem_model_read_u32_le(&data[cursor + 12u]);
        link->target_instance = refmem_model_read_u32_le(&data[cursor + 16u]);
        link->target_event = refmem_model_read_u32_le(&data[cursor + 20u]);
        link->transport = refmem_model_read_u32_le(&data[cursor + 24u]);
        link->timeout_us = refmem_model_read_u32_le(&data[cursor + 28u]);
        link->ack_policy = refmem_model_read_u32_le(&data[cursor + 32u]);
        link->retry_policy = refmem_model_read_u32_le(&data[cursor + 36u]);
        link->safety_class = refmem_model_read_u32_le(&data[cursor + 40u]);
        link->evidence_region_ref = refmem_model_read_u32_le(&data[cursor + 44u]);
        cursor += 12u * sizeof(uint32_t);
    }
    return links->version == REFMEM_APP_MODEL_VERSION &&
           links->event_link_count == REFMEM_APP_MODEL_EVENT_LINK_COUNT;
}

static bool refmem_model_parse_data_link_view(const uint8_t *data,
                                              size_t size,
                                              void *table)
{
    refmem_data_link_table_t *links = (refmem_data_link_table_t *)table;
    uint32_t count = 0u;
    if (links == NULL ||
        !refmem_model_validate_wire_header(data,
                                           size,
                                           REFMEM_APP_MODEL_DATA_LINK_COUNT,
                                           REFMEM_APP_TABLE_WIRE_DATA_LINK_WORDS,
                                           &count)) {
        return false;
    }

    memset(links, 0, sizeof(*links));
    links->version = REFMEM_APP_MODEL_VERSION;
    links->data_link_count = count;
    size_t cursor = REFMEM_APP_TABLE_WIRE_HEADER_WORDS * REFMEM_APP_TABLE_WIRE_U32_SIZE;
    for (uint32_t i = 0u; i < count; i++) {
        refmem_data_link_entry_t *link = &links->link[i];
        link->data_link_id = refmem_model_read_u32_le(&data[cursor + 0u]);
        link->region_path = refmem_model_default_region_path(link->data_link_id);
        link->writer_instance = refmem_model_read_u32_le(&data[cursor + 8u]);
        link->reader_mask = refmem_model_read_u32_le(&data[cursor + 12u]);
        link->type = refmem_model_read_u32_le(&data[cursor + 16u]);
        link->unit = refmem_model_read_u32_le(&data[cursor + 20u]);
        link->scale = refmem_model_read_i32_le(&data[cursor + 24u]);
        link->min_value = refmem_model_read_i32_le(&data[cursor + 28u]);
        link->max_value = refmem_model_read_i32_le(&data[cursor + 32u]);
        link->lifecycle = refmem_model_read_u32_le(&data[cursor + 36u]);
        link->snapshot_policy = refmem_model_read_u32_le(&data[cursor + 40u]);
        link->update_period_us = refmem_model_read_u32_le(&data[cursor + 44u]);
        link->stale_window_us = refmem_model_read_u32_le(&data[cursor + 48u]);
        link->crc_region_ref = refmem_model_read_u32_le(&data[cursor + 52u]);
        link->permission = refmem_model_read_u32_le(&data[cursor + 56u]);
        cursor += REFMEM_APP_TABLE_WIRE_DATA_LINK_WORDS * REFMEM_APP_TABLE_WIRE_U32_SIZE;
    }
    return true;
}

static bool refmem_model_parse_deployment_gate_view(const uint8_t *data,
                                                    size_t size,
                                                    void *table)
{
    refmem_deployment_gate_table_t *gate = (refmem_deployment_gate_table_t *)table;
    if (data == NULL || gate == NULL || size != sizeof(*gate)) {
        return false;
    }

    memset(gate, 0, sizeof(*gate));
    gate->version = refmem_model_read_u32_le(&data[0]);
    gate->check_count = refmem_model_read_u32_le(&data[4]);
    size_t cursor = 8u;
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT; i++) {
        refmem_deployment_gate_entry_t *check = &gate->check[i];
        check->check_id = refmem_model_read_u32_le(&data[cursor + 0u]);
        check->required = refmem_model_read_u32_le(&data[cursor + 4u]);
        check->fail_action = refmem_model_read_u32_le(&data[cursor + 8u]);
        check->last_state = refmem_model_read_u32_le(&data[cursor + 12u]);
        check->reject_code = refmem_model_read_u32_le(&data[cursor + 16u]);
        check->reject_instance = refmem_model_read_u32_le(&data[cursor + 20u]);
        check->reject_node = refmem_model_read_u32_le(&data[cursor + 24u]);
        check->reject_region_ref = refmem_model_read_u32_le(&data[cursor + 28u]);
        check->reject_evidence_index = refmem_model_read_u32_le(&data[cursor + 32u]);
        cursor += 9u * sizeof(uint32_t);
    }
    return gate->version == REFMEM_APP_MODEL_VERSION &&
           gate->check_count == REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT;
}

static bool refmem_model_parse_connection_quality_view(const uint8_t *data,
                                                       size_t size,
                                                       void *table)
{
    refmem_connection_quality_table_t *quality = (refmem_connection_quality_table_t *)table;
    if (data == NULL || quality == NULL || size != sizeof(*quality)) {
        return false;
    }

    memset(quality, 0, sizeof(*quality));
    quality->version = refmem_model_read_u32_le(&data[0]);
    quality->quality_count = refmem_model_read_u32_le(&data[4]);
    size_t cursor = 8u;
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_QUALITY_COUNT; i++) {
        refmem_connection_quality_entry_t *entry = &quality->quality[i];
        entry->quality_id = refmem_model_read_u32_le(&data[cursor + 0u]);
        entry->scope = refmem_model_read_u32_le(&data[cursor + 4u]);
        entry->source_node = refmem_model_read_u32_le(&data[cursor + 8u]);
        entry->target_node = refmem_model_read_u32_le(&data[cursor + 12u]);
        entry->seq_expected = refmem_model_read_u32_le(&data[cursor + 16u]);
        entry->seq_last = refmem_model_read_u32_le(&data[cursor + 20u]);
        entry->crc_error_count = refmem_model_read_u32_le(&data[cursor + 24u]);
        entry->stale_count = refmem_model_read_u32_le(&data[cursor + 28u]);
        entry->late_count = refmem_model_read_u32_le(&data[cursor + 32u]);
        entry->drop_count = refmem_model_read_u32_le(&data[cursor + 36u]);
        entry->timeout_count = refmem_model_read_u32_le(&data[cursor + 40u]);
        entry->last_error = refmem_model_read_u32_le(&data[cursor + 44u]);
        entry->last_error_tick = refmem_model_read_u32_le(&data[cursor + 48u]);
        entry->p99 = refmem_model_read_u32_le(&data[cursor + 52u]);
        entry->p999 = refmem_model_read_u32_le(&data[cursor + 56u]);
        entry->evidence_index = refmem_model_read_u32_le(&data[cursor + 60u]);
        cursor += 16u * sizeof(uint32_t);
    }
    return quality->version == REFMEM_APP_MODEL_VERSION &&
           quality->quality_count == REFMEM_APP_MODEL_QUALITY_COUNT;
}

static bool refmem_model_parse_tdma_foundation_profile_view(const uint8_t *data,
                                                            size_t size,
                                                            void *table)
{
    tdma_profile_result_t result = TDMA_PROFILE_BAD_ARGUMENT;
    return tdma_foundation_profile_decode_table(
        data,
        size,
        (tdma_foundation_profile_t *)table,
        &result);
}

static uint32_t refmem_model_application_map_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_application_map,
                                     sizeof(s_application_map));
}

static uint32_t refmem_model_board_capability_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_board_capability_table,
                                     sizeof(s_board_capability_table));
}

static uint32_t refmem_model_generic_node_crc32(void)
{
    uint32_t crc = refmem_model_crc32_update(0xFFFFFFFFu,
                                             &s_generic_node_table,
                                             offsetof(refmem_generic_node_table_t, node));
    for (uint32_t i = 0u; i < s_generic_node_table.node_count; i++) {
        crc = refmem_model_crc32_update(crc,
                                        &s_generic_node_table.node[i],
                                        sizeof(s_generic_node_table.node[i]));
    }
    return crc;
}

static uint32_t refmem_model_node_load_table_crc32(const refmem_node_load_table_t *table)
{
    if (table == NULL) {
        return 0u;
    }
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     table,
                                     sizeof(*table));
}

static uint32_t refmem_model_node_load_crc32(void)
{
    return refmem_model_node_load_table_crc32(&s_node_load_table);
}

static uint32_t refmem_model_fb_instance_crc32(void)
{
    uint32_t crc = refmem_model_crc32_update(0xFFFFFFFFu,
                                             &s_fb_instance_table.version,
                                             2u * sizeof(uint32_t));
    for (uint32_t i = 0u; i < s_fb_instance_table.instance_count; i++) {
        const refmem_fb_instance_entry_t *instance = &s_fb_instance_table.instance[i];
        const uint32_t fields[] = {
            instance->instance_id, instance->default_node_id, instance->domain, instance->ao_type,
            instance->fb_type, instance->version, instance->enable_condition,
            instance->resource_claim, instance->io_claim, instance->ip_core_claim,
            instance->time_budget_us,
            instance->state_region_ref, instance->health_region_ref, instance->event_first,
            instance->event_count, instance->data_first, instance->data_count,
            instance->conflict_class, instance->restart_policy,
        };
        crc = refmem_model_crc32_update(crc, fields, sizeof(fields));
        crc = refmem_model_crc32_string(crc, instance->instance_name);
    }
    return crc;
}

static uint32_t refmem_model_event_link_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_event_link_table,
                                     sizeof(s_event_link_table));
}

static uint32_t refmem_model_data_link_crc32(void)
{
    uint32_t crc = refmem_model_crc32_update(0xFFFFFFFFu,
                                             &s_data_link_table.version,
                                             2u * sizeof(uint32_t));
    for (uint32_t i = 0u; i < s_data_link_table.data_link_count; i++) {
        const refmem_data_link_entry_t *link = &s_data_link_table.link[i];
        const uint32_t fields[] = {
            link->data_link_id, link->writer_instance, link->reader_mask, link->type,
            link->unit, (uint32_t)link->scale, (uint32_t)link->min_value,
            (uint32_t)link->max_value, link->lifecycle, link->snapshot_policy,
            link->update_period_us, link->stale_window_us, link->crc_region_ref,
            link->permission,
        };
        crc = refmem_model_crc32_update(crc, fields, sizeof(fields));
        crc = refmem_model_crc32_string(crc, link->region_path);
    }
    return crc;
}

static uint32_t refmem_model_deployment_gate_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_deployment_gate,
                                     sizeof(s_deployment_gate));
}

static uint32_t refmem_model_connection_quality_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_connection_quality,
                                     sizeof(s_connection_quality));
}

static uint32_t refmem_model_tdma_foundation_profile_table_crc32(void)
{
    uint8_t wire[TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE];
    if (!tdma_foundation_profile_encode_table(&s_tdma_foundation_profile,
                                              wire,
                                              sizeof(wire))) {
        return 0u;
    }
    return refmem_model_rmtp_crc32(wire, sizeof(wire));
}

static const refmem_application_map_t *refmem_model_current_application_map(void)
{
    return s_active_tables_from_image ? &s_active_application_map : &s_application_map;
}

static const refmem_board_capability_table_t *refmem_model_current_board_capability_table(void)
{
    return s_active_tables_from_image ? &s_active_board_capability_table : &s_board_capability_table;
}

static const refmem_generic_node_table_t *refmem_model_current_generic_node_table(void)
{
    return s_active_tables_from_image ? &s_active_generic_node_table : &s_generic_node_table;
}

static const refmem_node_load_table_t *refmem_model_current_node_load_table(void)
{
    return s_active_tables_from_image ? &s_active_node_load_table : &s_node_load_table;
}

static const refmem_fb_instance_table_t *refmem_model_current_fb_instance_table(void)
{
    return s_active_tables_from_image ? &s_active_fb_instance_table : &s_fb_instance_table;
}

static const refmem_event_link_table_t *refmem_model_current_event_link_table(void)
{
    return s_active_tables_from_image ? &s_active_event_link_table : &s_event_link_table;
}

static const refmem_data_link_table_t *refmem_model_current_data_link_table(void)
{
    return s_active_tables_from_image ? &s_active_data_link_table : &s_data_link_table;
}

static const refmem_deployment_gate_table_t *refmem_model_current_deployment_gate_table(void)
{
    return s_active_tables_from_image ? &s_active_deployment_gate : &s_deployment_gate;
}

static const refmem_connection_quality_table_t *refmem_model_current_connection_quality_table(void)
{
    return s_active_tables_from_image ? &s_active_connection_quality : &s_connection_quality;
}

static const tdma_foundation_profile_t *refmem_model_current_tdma_foundation_profile(void)
{
    return s_active_tables_from_image ? &s_active_tdma_foundation_profile
                                      : &s_tdma_foundation_profile;
}

static bool refmem_model_instance_exists(uint32_t instance_id)
{
    const refmem_fb_instance_table_t *table = refmem_model_current_fb_instance_table();
    return instance_id < table->instance_count &&
           table->instance[instance_id].instance_id == instance_id;
}

static const refmem_fb_instance_entry_t *refmem_model_instance_by_id(uint32_t instance_id)
{
    if (!refmem_model_instance_exists(instance_id)) {
        return NULL;
    }
    return &refmem_model_current_fb_instance_table()->instance[instance_id];
}

static size_t refmem_model_table_payload_size(uint32_t table_id)
{
    switch (table_id) {
    case REFMEM_APP_TABLE_APPLICATION_MAP:
        return sizeof(refmem_application_map_t);
    case REFMEM_APP_TABLE_BOARD_CAPABILITY:
        return sizeof(refmem_board_capability_table_t);
    case REFMEM_APP_TABLE_GENERIC_NODE:
        return sizeof(refmem_generic_node_table_t);
    case REFMEM_APP_TABLE_NODE_LOAD:
        return sizeof(refmem_node_load_table_t);
    case REFMEM_APP_TABLE_FB_INSTANCE:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_INSTANCE_COUNT,
                                          REFMEM_APP_TABLE_WIRE_FB_INSTANCE_WORDS);
    case REFMEM_APP_TABLE_EVENT_LINK:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_EVENT_LINK_COUNT,
                                          REFMEM_APP_TABLE_WIRE_EVENT_LINK_WORDS);
    case REFMEM_APP_TABLE_DATA_LINK:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_DATA_LINK_COUNT,
                                          REFMEM_APP_TABLE_WIRE_DATA_LINK_WORDS);
    case REFMEM_APP_TABLE_DEPLOYMENT_GATE:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT,
                                          REFMEM_APP_TABLE_WIRE_DEPLOYMENT_GATE_WORDS);
    case REFMEM_APP_TABLE_CONNECTION_QUALITY:
        return REFMEM_APP_TABLE_WIRE_SIZE(REFMEM_APP_MODEL_QUALITY_COUNT,
                                          REFMEM_APP_TABLE_WIRE_CONNECTION_QUALITY_WORDS);
    case REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE:
        return TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE;
    default:
        return 0u;
    }
}

static bool refmem_model_serialize_wire_header(uint8_t *data,
                                               size_t size,
                                               size_t *cursor,
                                               uint32_t count)
{
    return refmem_model_write_u32_le(data, size, cursor, REFMEM_APP_MODEL_VERSION) &&
           refmem_model_write_u32_le(data, size, cursor, count);
}

static bool refmem_model_serialize_application_map(uint8_t *data,
                                                   size_t size,
                                                   size_t *cursor,
                                                   const refmem_application_map_t *table)
{
    return table != NULL &&
           refmem_model_write_u32_le(data, size, cursor, table->version) &&
           refmem_model_write_u32_le(data, size, cursor, table->application_id) &&
           refmem_model_write_u32_le(data, size, cursor, table->application_version) &&
           refmem_model_write_u32_le(data, size, cursor, table->profile_id) &&
           refmem_model_write_u32_le(data, size, cursor, table->layout_version) &&
           refmem_model_write_u32_le(data, size, cursor, table->target_node_mask);
}

static bool refmem_model_serialize_board_capability(
    uint8_t *data,
    size_t size,
    size_t *cursor,
    const refmem_board_capability_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->board_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT; i++) {
        const refmem_board_capability_entry_t *entry = &table->board[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->board_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->board_uuid_crc32) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->capability_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->io_constraint_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->ip_core_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->default_persona_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->hw_profile_crc32) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->active_default_slot) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->online_required)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_generic_node(uint8_t *data,
                                                size_t size,
                                                size_t *cursor,
                                                const refmem_generic_node_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->node_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_NODE_COUNT; i++) {
        const refmem_app_node_entry_t *entry = &table->node[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->node_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->node_uuid_crc32) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->capability_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->claim_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->claim_priority) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->default_persona_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->hw_profile_crc32) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->online_required) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->fail_policy)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_node_load(uint8_t *data,
                                             size_t size,
                                             size_t *cursor,
                                             const refmem_node_load_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->load_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_NODE_LOAD_COUNT; i++) {
        const refmem_node_load_entry_t *entry = &table->load[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->load_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->application_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->profile_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->node_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->instance_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->role_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->persona_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->enabled) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->required) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->fail_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->load_order)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_fb_instance(uint8_t *data,
                                               size_t size,
                                               size_t *cursor,
                                               const refmem_fb_instance_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->instance_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_INSTANCE_COUNT; i++) {
        const refmem_fb_instance_entry_t *entry = &table->instance[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->instance_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->default_node_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->domain) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->ao_type) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->fb_type) ||
            !refmem_model_write_u32_le(data, size, cursor,
                                       refmem_model_rmtp_crc32_string(entry->instance_name)) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->version) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->enable_condition) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->resource_claim) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->io_claim) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->ip_core_claim) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->time_budget_us) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->state_region_ref) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->health_region_ref) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->event_first) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->event_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->data_first) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->data_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->conflict_class) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->restart_policy)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_event_link(uint8_t *data,
                                              size_t size,
                                              size_t *cursor,
                                              const refmem_event_link_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->event_link_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_EVENT_LINK_COUNT; i++) {
        const refmem_event_link_entry_t *entry = &table->link[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->event_link_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->source_instance) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->source_event) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->target_node_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->target_instance) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->target_event) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->transport) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->timeout_us) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->ack_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->retry_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->safety_class) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->evidence_region_ref)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_data_link(uint8_t *data,
                                             size_t size,
                                             size_t *cursor,
                                             const refmem_data_link_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->data_link_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_DATA_LINK_COUNT; i++) {
        const refmem_data_link_entry_t *entry = &table->link[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->data_link_id) ||
            !refmem_model_write_u32_le(data, size, cursor,
                                       refmem_model_rmtp_crc32_string(entry->region_path)) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->writer_instance) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reader_mask) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->type) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->unit) ||
            !refmem_model_write_i32_le(data, size, cursor, entry->scale) ||
            !refmem_model_write_i32_le(data, size, cursor, entry->min_value) ||
            !refmem_model_write_i32_le(data, size, cursor, entry->max_value) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->lifecycle) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->snapshot_policy) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->update_period_us) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->stale_window_us) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->crc_region_ref) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->permission)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_deployment_gate(
    uint8_t *data,
    size_t size,
    size_t *cursor,
    const refmem_deployment_gate_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->check_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT; i++) {
        const refmem_deployment_gate_entry_t *entry = &table->check[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->check_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->required) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->fail_action) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->last_state) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_code) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_instance) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_node) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_region_ref) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->reject_evidence_index)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_connection_quality(
    uint8_t *data,
    size_t size,
    size_t *cursor,
    const refmem_connection_quality_table_t *table)
{
    if (table == NULL ||
        !refmem_model_serialize_wire_header(data, size, cursor, table->quality_count)) {
        return false;
    }
    for (uint32_t i = 0u; i < REFMEM_APP_MODEL_QUALITY_COUNT; i++) {
        const refmem_connection_quality_entry_t *entry = &table->quality[i];
        if (!refmem_model_write_u32_le(data, size, cursor, entry->quality_id) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->scope) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->source_node) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->target_node) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->seq_expected) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->seq_last) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->crc_error_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->stale_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->late_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->drop_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->timeout_count) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->last_error) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->last_error_tick) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->p99) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->p999) ||
            !refmem_model_write_u32_le(data, size, cursor, entry->evidence_index)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_serialize_tdma_foundation_profile(
    uint8_t *data,
    size_t size,
    size_t *cursor,
    const tdma_foundation_profile_t *profile)
{
    if (data == NULL || cursor == NULL || profile == NULL ||
        *cursor > size || size - *cursor < TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE ||
        !tdma_foundation_profile_encode_table(profile,
                                              &data[*cursor],
                                              TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE)) {
        return false;
    }
    *cursor += TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE;
    return true;
}

static bool refmem_model_serialize_candidate_table(uint32_t table_id,
                                                   uint8_t *data,
                                                   size_t size,
                                                   size_t *cursor)
{
    switch (table_id) {
    case REFMEM_APP_TABLE_APPLICATION_MAP:
        return refmem_model_serialize_application_map(data,
                                                      size,
                                                      cursor,
                                                      refmem_model_current_application_map());
    case REFMEM_APP_TABLE_BOARD_CAPABILITY:
        return refmem_model_serialize_board_capability(
            data,
            size,
            cursor,
            s_staging_board_capability_valid ? &s_staging_board_capability_table
                                             : refmem_model_current_board_capability_table());
    case REFMEM_APP_TABLE_GENERIC_NODE:
        return refmem_model_serialize_generic_node(data,
                                                   size,
                                                   cursor,
                                                   refmem_model_current_generic_node_table());
    case REFMEM_APP_TABLE_NODE_LOAD:
        return refmem_model_serialize_node_load(
            data,
            size,
            cursor,
            s_staging_node_load_valid ? &s_staging_node_load_table
                                      : refmem_model_current_node_load_table());
    case REFMEM_APP_TABLE_FB_INSTANCE:
        return refmem_model_serialize_fb_instance(data,
                                                  size,
                                                  cursor,
                                                  refmem_model_current_fb_instance_table());
    case REFMEM_APP_TABLE_EVENT_LINK:
        return refmem_model_serialize_event_link(data,
                                                 size,
                                                 cursor,
                                                 refmem_model_current_event_link_table());
    case REFMEM_APP_TABLE_DATA_LINK:
        return refmem_model_serialize_data_link(data,
                                                size,
                                                cursor,
                                                refmem_model_current_data_link_table());
    case REFMEM_APP_TABLE_DEPLOYMENT_GATE:
        return refmem_model_serialize_deployment_gate(
            data,
            size,
            cursor,
            refmem_model_current_deployment_gate_table());
    case REFMEM_APP_TABLE_CONNECTION_QUALITY:
        return refmem_model_serialize_connection_quality(
            data,
            size,
            cursor,
            refmem_model_current_connection_quality_table());
    case REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE:
        return refmem_model_serialize_tdma_foundation_profile(
            data,
            size,
            cursor,
            refmem_model_current_tdma_foundation_profile());
    default:
        return false;
    }
}

static bool refmem_model_build_inline_package_image(uint8_t *data,
                                                    size_t capacity,
                                                    size_t *package_size,
                                                    refmem_table_package_validation_t *validation)
{
    if (data == NULL || package_size == NULL || validation == NULL ||
        capacity < REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET) {
        return false;
    }

    const size_t table_dir_size = REFMEM_APP_INLINE_PACKAGE_DIR_SIZE;
    size_t total_size = REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET;
    for (uint32_t table_id = 0u; table_id < REFMEM_TABLE_REGISTRY_COUNT; table_id++) {
        const size_t table_size = refmem_model_table_payload_size(table_id);
        if (table_size == 0u ||
            total_size > capacity ||
            table_size > capacity - total_size) {
            return false;
        }
        total_size += table_size;
    }

    memset(data, 0, total_size);
    size_t cursor = 0u;
    if (!refmem_model_write_u32_le(data, total_size, &cursor, REFMEM_TABLE_PACKAGE_MAGIC) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, REFMEM_TABLE_PACKAGE_VERSION) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, REFMEM_TABLE_PACKAGE_HEADER_SIZE) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, (uint32_t)total_size) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, REFMEM_TABLE_REGISTRY_COUNT) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, (uint32_t)table_dir_size) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, 0u) ||
        !refmem_model_write_u32_le(data, total_size, &cursor, 0u)) {
        return false;
    }

    size_t payload_cursor = REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET;
    for (uint32_t table_id = 0u; table_id < REFMEM_TABLE_REGISTRY_COUNT; table_id++) {
        const size_t table_offset = payload_cursor;
        const size_t table_size = refmem_model_table_payload_size(table_id);
        if (!refmem_model_serialize_candidate_table(table_id,
                                                    data,
                                                    total_size,
                                                    &payload_cursor) ||
            payload_cursor - table_offset != table_size) {
            return false;
        }

        size_t dir_cursor =
            REFMEM_TABLE_PACKAGE_HEADER_SIZE +
            table_id * REFMEM_TABLE_PACKAGE_DIR_ENTRY_SIZE;
        const uint32_t table_crc = refmem_model_rmtp_crc32(&data[table_offset], table_size);
        if (!refmem_model_write_u32_le(data, total_size, &dir_cursor, table_id) ||
            !refmem_model_write_u32_le(data, total_size, &dir_cursor, (uint32_t)table_offset) ||
            !refmem_model_write_u32_le(data, total_size, &dir_cursor, (uint32_t)table_size) ||
            !refmem_model_write_u32_le(data, total_size, &dir_cursor, table_crc)) {
            return false;
        }
    }

    if (payload_cursor != total_size) {
        return false;
    }

    const uint32_t payload_crc =
        refmem_model_rmtp_crc32(&data[REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET],
                                total_size - REFMEM_APP_INLINE_PACKAGE_PAYLOAD_OFFSET);
    cursor = 24u;
    if (!refmem_model_write_u32_le(data, total_size, &cursor, payload_crc)) {
        return false;
    }
    const uint32_t package_crc = refmem_model_rmtp_crc32_zero_field(data, total_size, 28u);
    cursor = 28u;
    if (!refmem_model_write_u32_le(data, total_size, &cursor, package_crc)) {
        return false;
    }

    if (!refmem_table_registry_validate_package(data, total_size, validation)) {
        return false;
    }

    *package_size = total_size;
    return true;
}

static bool refmem_model_stage_inline_package_image(void)
{
    refmem_table_package_validation_t validation;
    memset(&validation, 0, sizeof(validation));
    size_t package_size = 0u;
    if (!refmem_model_build_inline_package_image(s_inline_package_image_buffer,
                                                 sizeof(s_inline_package_image_buffer),
                                                 &package_size,
                                                 &validation)) {
        s_load_snapshot.staging_package_crc32 = 0u;
        s_load_snapshot.staging_lint_error_count = 1u;
        s_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_BAD_TABLE_VERSION;
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_PACKAGE_INVALID;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        if (validation.error != REFMEM_TABLE_PACKAGE_OK &&
            validation.first_bad_table < REFMEM_TABLE_REGISTRY_COUNT) {
            (void)refmem_table_registry_stage_table(validation.first_bad_table,
                                                    0u,
                                                    REFMEM_TABLE_VALIDATION_FAILED,
                                                    validation.error);
        } else {
            refmem_table_registry_refresh_staging(&s_load_snapshot);
        }
        return false;
    }

    s_load_snapshot.staging_package_crc32 = validation.package_crc32;
    s_load_snapshot.staging_lint_error_count = 0u;
    s_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_OK;
    s_load_snapshot.staging_state = REFMEM_APP_STAGING_VALIDATED;
    s_load_snapshot.last_error = REFMEM_APP_LOAD_OK;
    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    return refmem_table_registry_stage_package_image(&s_load_snapshot,
                                                     s_inline_package_image_buffer,
                                                     package_size,
                                                     &validation);
}

static bool refmem_model_node_load_enabled(const refmem_node_load_entry_t *load)
{
    return load != NULL && load->enabled != 0u;
}

static const refmem_app_node_entry_t *refmem_model_generic_node_by_id(uint32_t node_id)
{
    if (node_id >= s_generic_node_table.node_count) {
        return NULL;
    }
    return &s_generic_node_table.node[node_id];
}

static bool refmem_model_instance_is_loaded(uint32_t instance_id)
{
    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *load = &s_node_load_table.load[i];
        if (refmem_model_node_load_enabled(load) && load->instance_id == instance_id) {
            return true;
        }
    }
    return false;
}

static bool refmem_model_validate_node_capabilities(void)
{
    refmem_slot_claim_map_t claim_map;
    if (!refmem_slot_claim_derive_map(&s_generic_node_table,
                                      &s_board_capability_table,
                                      &s_node_load_table,
                                      &s_fb_instance_table,
                                      &claim_map)) {
        return false;
    }

    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *load = &s_node_load_table.load[i];
        const refmem_fb_instance_entry_t *instance =
            refmem_model_instance_by_id(load->instance_id);
        const refmem_app_node_entry_t *node = refmem_model_generic_node_by_id(load->node_id);
        if (!refmem_model_node_load_enabled(load) ||
            !refmem_model_instance_enabled(instance) ||
            node == NULL) {
            continue;
        }

        refmem_realtime_contract_t contract;
        if (!refmem_realtime_contract_derive_from_claim_map(load,
                                                            instance,
                                                            node,
                                                            &claim_map,
                                                            &contract)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_validate_slot_claim_policy(void)
{
    for (uint32_t i = 0u; i < s_generic_node_table.node_count; i++) {
        const refmem_app_node_entry_t *node = &s_generic_node_table.node[i];
        if (node->claim_policy > REFMEM_APP_CLAIM_DISABLED) {
            return false;
        }

        if (node->online_required != 0u &&
            node->claim_policy != REFMEM_APP_CLAIM_STRICT_UUID &&
            node->claim_policy != REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT) {
            return false;
        }

        if (node->claim_policy == REFMEM_APP_CLAIM_SPARE_DYNAMIC &&
            (node->online_required != 0u || node->fail_policy != REFMEM_APP_FAIL_REPORT_ONLY)) {
            return false;
        }

        if (node->claim_policy == REFMEM_APP_CLAIM_DISABLED &&
            (node->online_required != 0u || node->claim_priority != 0u)) {
            return false;
        }
    }

    return true;
}

static bool refmem_model_validate_application_map(void)
{
    return refmem_application_contract_validate_application_map(&s_application_map);
}

static bool refmem_model_validate_board_capability_table(void)
{
    return refmem_application_contract_validate_board_capability_table(
        &s_board_capability_table,
        REFMEM_APP_MODEL_NODE_COUNT);
}

static bool refmem_model_validate_generic_node_table(void)
{
    return refmem_application_contract_validate_slot_substrate(
        &s_generic_node_table,
        &s_board_capability_table);
}

static bool refmem_model_validate_node_load_table(void)
{
    if (!refmem_application_contract_validate_node_load_table(&s_node_load_table,
                                                              &s_application_map)) {
        return false;
    }

    uint32_t loaded_instance_mask = 0u;
    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *load = &s_node_load_table.load[i];
        if (!refmem_model_instance_exists(load->instance_id)) {
            return false;
        }

        if (!refmem_model_node_load_enabled(load)) {
            continue;
        }

        const uint32_t instance_bit = 1u << load->instance_id;
        if ((loaded_instance_mask & instance_bit) != 0u) {
            return false;
        }
        loaded_instance_mask |= instance_bit;
    }

    for (uint32_t i = 0u; i < s_fb_instance_table.instance_count; i++) {
        const refmem_fb_instance_entry_t *instance = &s_fb_instance_table.instance[i];
        if (refmem_model_instance_enabled(instance) &&
            !refmem_model_instance_is_loaded(instance->instance_id)) {
            return false;
        }
    }

    return true;
}

static bool refmem_model_validate_instances(void)
{
    if (s_fb_instance_table.version != REFMEM_APP_MODEL_VERSION ||
        s_fb_instance_table.instance_count != REFMEM_APP_MODEL_INSTANCE_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_fb_instance_table.instance_count; i++) {
        const refmem_fb_instance_entry_t *instance = &s_fb_instance_table.instance[i];
        if (instance->instance_id != i ||
            instance->default_node_id >= REFMEM_APP_MODEL_NODE_COUNT ||
            instance->instance_name == NULL ||
            instance->domain > REFMEM_APP_DOMAIN_TDMA ||
            instance->state_region_ref >= REFMEM_VECTOR_REGION_COUNT ||
            instance->health_region_ref >= REFMEM_VECTOR_REGION_COUNT ||
            instance->event_first > s_event_link_table.event_link_count ||
            instance->event_count > (s_event_link_table.event_link_count - instance->event_first) ||
            instance->data_first > s_data_link_table.data_link_count ||
            instance->data_count > (s_data_link_table.data_link_count - instance->data_first)) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_validate_event_links(void)
{
    if (s_event_link_table.version != REFMEM_APP_MODEL_VERSION ||
        s_event_link_table.event_link_count != REFMEM_APP_MODEL_EVENT_LINK_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_event_link_table.event_link_count; i++) {
        const refmem_event_link_entry_t *link = &s_event_link_table.link[i];
        if (link->event_link_id != i ||
            !refmem_model_instance_exists(link->source_instance) ||
            !refmem_model_instance_exists(link->target_instance) ||
            (link->target_node_mask & ~s_application_map.target_node_mask) != 0u ||
            link->transport > REFMEM_APP_TRANSPORT_PIO_SPI ||
            link->ack_policy > REFMEM_APP_ACK_BITMAP ||
            link->evidence_region_ref >= REFMEM_VECTOR_REGION_COUNT) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_validate_data_links(void)
{
    if (s_data_link_table.version != REFMEM_APP_MODEL_VERSION ||
        s_data_link_table.data_link_count != REFMEM_APP_MODEL_DATA_LINK_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_data_link_table.data_link_count; i++) {
        const refmem_data_link_entry_t *link = &s_data_link_table.link[i];
        if (link->data_link_id != i ||
            link->region_path == NULL ||
            !refmem_model_instance_exists(link->writer_instance) ||
            (link->reader_mask & ~s_application_map.target_node_mask) != 0u ||
            link->type > REFMEM_APP_DATA_CRC ||
            link->unit > REFMEM_APP_UNIT_COUNT ||
            link->lifecycle > REFMEM_APP_LIFE_EVIDENCE ||
            link->snapshot_policy > REFMEM_APP_SNAPSHOT_EVIDENCE_REF ||
            link->crc_region_ref >= REFMEM_VECTOR_REGION_COUNT ||
            link->permission > REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE ||
            link->min_value > link->max_value) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_validate_gate_and_quality(void)
{
    if (s_deployment_gate.version != REFMEM_APP_MODEL_VERSION ||
        s_deployment_gate.check_count != REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT ||
        s_connection_quality.version != REFMEM_APP_MODEL_VERSION ||
        s_connection_quality.quality_count != REFMEM_APP_MODEL_QUALITY_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_deployment_gate.check_count; i++) {
        const refmem_deployment_gate_entry_t *check = &s_deployment_gate.check[i];
        if (check->check_id != i ||
            check->fail_action > REFMEM_APP_GATE_LATCH_FAULT ||
            check->last_state > REFMEM_APP_GATE_LATCH_FAULT ||
            check->reject_region_ref >= REFMEM_VECTOR_REGION_COUNT) {
            return false;
        }
    }

    for (uint32_t i = 0u; i < s_connection_quality.quality_count; i++) {
        const refmem_connection_quality_entry_t *quality = &s_connection_quality.quality[i];
        if (quality->quality_id != i ||
            quality->scope > REFMEM_APP_QUALITY_TRANSPORT_ADAPTER ||
            quality->source_node >= REFMEM_APP_MODEL_NODE_COUNT ||
            quality->target_node >= REFMEM_APP_MODEL_NODE_COUNT) {
            return false;
        }
    }

    refmem_slot_claim_map_t claim_map;
    refmem_slot_claim_gate_status_t claim_gate;
    if (!refmem_slot_claim_derive_map(&s_generic_node_table,
                                      &s_board_capability_table,
                                      &s_node_load_table,
                                      &s_fb_instance_table,
                                      &claim_map)) {
        return false;
    }
    if (!refmem_slot_claim_gate_evaluate(&claim_map, &claim_gate) ||
        claim_gate.ready == 0u) {
        return false;
    }
    return true;
}

static void refmem_model_lint_note(bool ok,
                                   uint32_t error,
                                   uint32_t *error_count,
                                   uint32_t *first_error)
{
    if (ok) {
        return;
    }

    if (*error_count == 0u) {
        *first_error = error;
    }
    (*error_count)++;
}

static bool refmem_model_instance_enabled(const refmem_fb_instance_entry_t *instance)
{
    return instance != NULL && instance->enable_condition != 0u;
}

static bool refmem_model_validate_resource_claims(void)
{
    const uint32_t exclusive_resource_mask = REFMEM_APP_RESOURCE_FLASH |
                                            REFMEM_APP_RESOURCE_SD |
                                            REFMEM_APP_RESOURCE_USB |
                                            REFMEM_APP_RESOURCE_LCD |
                                            REFMEM_APP_RESOURCE_TDMA_SCHEDULER;

    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *left_load = &s_node_load_table.load[i];
        const refmem_fb_instance_entry_t *left =
            refmem_model_instance_by_id(left_load->instance_id);
        if (!refmem_model_node_load_enabled(left_load) ||
            !refmem_model_instance_enabled(left)) {
            continue;
        }

        for (uint32_t j = i + 1u; j < s_node_load_table.load_count; j++) {
            const refmem_node_load_entry_t *right_load = &s_node_load_table.load[j];
            const refmem_fb_instance_entry_t *right =
                refmem_model_instance_by_id(right_load->instance_id);
            if (!refmem_model_node_load_enabled(right_load) ||
                !refmem_model_instance_enabled(right) ||
                left_load->node_id != right_load->node_id) {
                continue;
            }

            if (left->conflict_class != 0u &&
                left->conflict_class == right->conflict_class) {
                return false;
            }

            if (((left->resource_claim & right->resource_claim) & exclusive_resource_mask) != 0u) {
                return false;
            }
        }
    }

    return true;
}

static bool refmem_model_validate_tdma_profile_contract(
    const tdma_foundation_profile_t *profile,
    const refmem_generic_node_table_t *nodes,
    const refmem_board_capability_table_t *boards,
    const refmem_node_load_table_t *loads,
    const refmem_fb_instance_table_t *instances)
{
    tdma_profile_result_t profile_result = TDMA_PROFILE_BAD_ARGUMENT;
    if (!tdma_foundation_profile_validate(profile, &profile_result) ||
        nodes == NULL || boards == NULL || loads == NULL || instances == NULL ||
        profile->owner_instance_id >= instances->instance_count ||
        profile->ring.node_count > nodes->node_count ||
        profile->ring.local_index >= nodes->node_count) {
        return false;
    }

    const refmem_fb_instance_entry_t *owner =
        &instances->instance[profile->owner_instance_id];
    const uint32_t required_resource = REFMEM_APP_RESOURCE_DMA |
                                       REFMEM_APP_RESOURCE_CORE1_RT |
                                       REFMEM_APP_RESOURCE_TDMA_SCHEDULER |
        ((profile->resource.adapter_type == TDMA_ADAPTER_PIO_SPI ||
          profile->resource.adapter_type == TDMA_ADAPTER_BISS_C)
             ? REFMEM_APP_RESOURCE_PIO
             : 0u);
    if (owner->instance_id != profile->owner_instance_id ||
        owner->enable_condition == 0u ||
        owner->domain != REFMEM_APP_DOMAIN_TDMA ||
        owner->fb_type != REFMEM_APP_FB_TDMA_SCHEDULER ||
        (owner->resource_claim & required_resource) != required_resource ||
        profile->resource.io_claim_mask == 0u ||
        (profile->resource.ip_core_claim_mask & REFMEM_APP_IP_TDMA_SCHEDULER) == 0u ||
        (profile->resource.io_claim_mask & ~owner->io_claim) != 0u ||
        (profile->resource.ip_core_claim_mask & ~owner->ip_core_claim) != 0u) {
        return false;
    }

    uint32_t owner_count = 0u;
    const refmem_node_load_entry_t *owner_load = NULL;
    for (uint32_t i = 0u; i < loads->load_count; i++) {
        const refmem_node_load_entry_t *load = &loads->load[i];
        if (load->instance_id >= instances->instance_count || load->enabled == 0u) {
            continue;
        }
        const refmem_fb_instance_entry_t *instance = &instances->instance[load->instance_id];
        if (instance->domain == REFMEM_APP_DOMAIN_TDMA &&
            instance->fb_type == REFMEM_APP_FB_TDMA_SCHEDULER &&
            instance->enable_condition != 0u) {
            owner_count++;
            owner_load = load;
        }
        if (load->node_id == profile->ring.local_index &&
            load->instance_id != profile->owner_instance_id &&
            instance->enable_condition != 0u &&
            (instance->io_claim & profile->resource.io_claim_mask) != 0u) {
            return false;
        }
    }
    if (owner_count != 1u || owner_load == NULL ||
        owner_load->instance_id != profile->owner_instance_id ||
        owner_load->node_id != profile->ring.local_index) {
        return false;
    }

    refmem_slot_claim_map_t claim_map;
    refmem_realtime_contract_t realtime;
    return refmem_slot_claim_derive_map(nodes, boards, loads, instances, &claim_map) &&
           refmem_realtime_contract_derive_from_claim_map(
               owner_load,
               owner,
               &nodes->node[owner_load->node_id],
               &claim_map,
               &realtime) &&
           realtime.valid != 0u &&
           (profile->resource.io_claim_mask & ~realtime.target_io_constraint_mask) == 0u &&
           (profile->resource.ip_core_claim_mask & ~realtime.target_ip_core_mask) == 0u;
}

static bool refmem_model_validate_tdma_owner(void)
{
    return refmem_model_validate_tdma_profile_contract(
        &s_tdma_foundation_profile,
        &s_generic_node_table,
        &s_board_capability_table,
        &s_node_load_table,
        &s_fb_instance_table);
}

static bool refmem_model_validate_io_claims(void)
{
    const uint32_t exclusive_io_mask = REFMEM_APP_IO_LINK_CONTROL |
                                      REFMEM_APP_IO_BISS_C |
                                      REFMEM_APP_IO_UART_RS485 |
                                      REFMEM_APP_IO_PIO_SPI_SYNC |
                                      REFMEM_APP_IO_MODEL_SIGNAL_MASK;

    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *left_load = &s_node_load_table.load[i];
        const refmem_fb_instance_entry_t *left =
            refmem_model_instance_by_id(left_load->instance_id);
        if (!refmem_model_node_load_enabled(left_load) ||
            !refmem_model_instance_enabled(left)) {
            continue;
        }

        for (uint32_t j = i + 1u; j < s_node_load_table.load_count; j++) {
            const refmem_node_load_entry_t *right_load = &s_node_load_table.load[j];
            const refmem_fb_instance_entry_t *right =
                refmem_model_instance_by_id(right_load->instance_id);
            if (!refmem_model_node_load_enabled(right_load) ||
                !refmem_model_instance_enabled(right) ||
                left_load->node_id != right_load->node_id) {
                continue;
            }

            if (((left->io_claim & right->io_claim) & exclusive_io_mask) != 0u) {
                return false;
            }
        }
    }

    return true;
}

static bool refmem_model_validate_unique_writers(void)
{
    for (uint32_t i = 0u; i < s_data_link_table.data_link_count; i++) {
        const refmem_data_link_entry_t *left = &s_data_link_table.link[i];
        for (uint32_t j = i + 1u; j < s_data_link_table.data_link_count; j++) {
            const refmem_data_link_entry_t *right = &s_data_link_table.link[j];
            if (strcmp(left->region_path, right->region_path) == 0 &&
                left->writer_instance != right->writer_instance) {
                return false;
            }
        }
    }

    return true;
}

static bool refmem_model_validate_required_event_links(void)
{
    bool has_start = false;
    bool has_stop = false;
    bool has_fire_load = false;
    bool has_done = false;
    bool has_fault = false;

    for (uint32_t i = 0u; i < s_event_link_table.event_link_count; i++) {
        const refmem_event_link_entry_t *link = &s_event_link_table.link[i];
        has_start = has_start ||
                    (link->source_event == REFMEM_APP_EVENT_START &&
                     link->target_event == REFMEM_APP_EVENT_START);
        has_stop = has_stop ||
                   (link->source_event == REFMEM_APP_EVENT_STOP &&
                    link->target_event == REFMEM_APP_EVENT_STOP);
        has_fire_load = has_fire_load ||
                        (link->source_event == REFMEM_APP_EVENT_FIRE_LOAD &&
                         link->target_event == REFMEM_APP_EVENT_FIRE_LOAD);
        has_done = has_done ||
                   (link->source_event == REFMEM_APP_EVENT_DONE &&
                    link->target_event == REFMEM_APP_EVENT_DONE);
        has_fault = has_fault ||
                    (link->source_event == REFMEM_APP_EVENT_FAULT &&
                     link->target_event == REFMEM_APP_EVENT_FAULT);
    }

    return has_start && has_stop && has_fire_load && has_done && has_fault;
}

static bool refmem_model_validate_required_data_links(void)
{
    uint32_t region_mask = 0u;
    const uint32_t required_region_mask = (1u << REFMEM_VECTOR_REGION_SYSTEM) |
                                          (1u << REFMEM_VECTOR_REGION_ROLE) |
                                          (1u << REFMEM_VECTOR_REGION_VDC) |
                                          (1u << REFMEM_VECTOR_REGION_LOOP) |
                                          (1u << REFMEM_VECTOR_REGION_DPLL) |
                                          (1u << REFMEM_VECTOR_REGION_NODE) |
                                          (1u << REFMEM_VECTOR_REGION_TRIGGER) |
                                          (1u << REFMEM_VECTOR_REGION_IO) |
                                          (1u << REFMEM_VECTOR_REGION_CAL) |
                                          (1u << REFMEM_VECTOR_REGION_ACK_CMD) |
                                          (1u << REFMEM_VECTOR_REGION_GATEWAY);

    for (uint32_t i = 0u; i < s_data_link_table.data_link_count; i++) {
        const refmem_data_link_entry_t *link = &s_data_link_table.link[i];
        region_mask |= (1u << link->crc_region_ref);
    }

    return (region_mask & required_region_mask) == required_region_mask;
}

static void refmem_model_lint(uint32_t *error_count, uint32_t *first_error)
{
    *error_count = 0u;
    *first_error = REFMEM_APP_LINT_OK;

    refmem_model_lint_note(refmem_model_validate_application_map(),
                           REFMEM_APP_LINT_BAD_TABLE_VERSION,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_board_capability_table(),
                           REFMEM_APP_LINT_BAD_BOARD_CAPABILITY,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_generic_node_table(),
                           REFMEM_APP_LINT_BAD_NODE_RANGE,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_node_load_table(),
                           REFMEM_APP_LINT_BAD_NODE_RANGE,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_node_capabilities(),
                           REFMEM_APP_LINT_BAD_REALTIME_CONTRACT,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_slot_claim_policy(),
                           REFMEM_APP_LINT_BAD_SLOT_CLAIM,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_instances(),
                           REFMEM_APP_LINT_BAD_INSTANCE_RANGE,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_resource_claims(),
                           REFMEM_APP_LINT_RESOURCE_CONFLICT,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_tdma_owner(),
                           REFMEM_APP_LINT_BAD_TDMA_PROFILE,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_io_claims(),
                           REFMEM_APP_LINT_IO_CONFLICT,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_event_links(),
                           REFMEM_APP_LINT_BAD_EVENT_LINK,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_required_event_links(),
                           REFMEM_APP_LINT_BAD_EVENT_LINK,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_data_links(),
                           REFMEM_APP_LINT_BAD_DATA_LINK,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_unique_writers(),
                           REFMEM_APP_LINT_DUPLICATE_WRITER,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_required_data_links(),
                           REFMEM_APP_LINT_BAD_DATA_LINK,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_gate_and_quality(),
                           REFMEM_APP_LINT_BAD_GATE_OR_QUALITY,
                           error_count,
                           first_error);
}

static void refmem_model_copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0u) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    size_t i = 0u;
    while (i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void refmem_model_finish_load_idle(void)
{
    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    refmem_table_registry_refresh_staging(&s_load_snapshot);
}

typedef bool (*refmem_model_parse_table_view_fn)(const uint8_t *data, size_t size, void *table);

static bool refmem_model_parse_image_view(refmem_table_image_role_t role,
                                          uint32_t table_id,
                                          refmem_model_parse_table_view_fn parser,
                                          void *table,
                                          uint32_t *table_crc32,
                                          uint32_t *package_crc32,
                                          uint32_t *table_seq,
                                          bool *context_set)
{
    if (parser == NULL ||
        table == NULL ||
        table_crc32 == NULL ||
        package_crc32 == NULL ||
        table_seq == NULL ||
        context_set == NULL) {
        return false;
    }

    refmem_table_view_t view;
    if (!refmem_table_registry_access_table(role, table_id, &view)) {
        return false;
    }

    bool ok = true;
    if (*context_set) {
        ok = view.package_crc32 == *package_crc32 && view.table_seq == *table_seq;
    } else {
        *package_crc32 = view.package_crc32;
        *table_seq = view.table_seq;
        *context_set = true;
    }

    if (ok) {
        ok = parser(view.data, view.size, table);
    }
    if (ok) {
        *table_crc32 = view.table_crc32;
    }

    const bool released = refmem_table_registry_release_table(&view);
    return ok && released;
}

static bool refmem_model_parse_image_views(refmem_table_image_role_t role,
                                           refmem_model_parsed_tables_t *parsed)
{
    if (parsed == NULL) {
        return false;
    }

    memset(parsed, 0, sizeof(*parsed));
    bool context_set = false;

    if (!refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_APPLICATION_MAP,
                                       refmem_model_parse_application_map_view,
                                       &parsed->application_map,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_APPLICATION_MAP],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_BOARD_CAPABILITY,
                                       refmem_model_parse_board_capability_view,
                                       &parsed->board_capability,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_BOARD_CAPABILITY],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_GENERIC_NODE,
                                       refmem_model_parse_generic_node_view,
                                       &parsed->generic_node,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_GENERIC_NODE],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_NODE_LOAD,
                                       refmem_model_parse_node_load_view,
                                       &parsed->node_load,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_NODE_LOAD],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_FB_INSTANCE,
                                       refmem_model_parse_fb_instance_view,
                                       &parsed->fb_instance,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_FB_INSTANCE],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_EVENT_LINK,
                                       refmem_model_parse_event_link_view,
                                       &parsed->event_link,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_EVENT_LINK],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_DATA_LINK,
                                       refmem_model_parse_data_link_view,
                                       &parsed->data_link,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_DATA_LINK],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_DEPLOYMENT_GATE,
                                       refmem_model_parse_deployment_gate_view,
                                       &parsed->deployment_gate,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_DEPLOYMENT_GATE],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(role,
                                       REFMEM_APP_TABLE_CONNECTION_QUALITY,
                                       refmem_model_parse_connection_quality_view,
                                       &parsed->connection_quality,
                                       &parsed->table_crc32[REFMEM_APP_TABLE_CONNECTION_QUALITY],
                                       &parsed->package_crc32,
                                       &parsed->table_seq,
                                       &context_set) ||
        !refmem_model_parse_image_view(
            role,
            REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE,
            refmem_model_parse_tdma_foundation_profile_view,
            &parsed->tdma_foundation_profile,
            &parsed->table_crc32[REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE],
            &parsed->package_crc32,
            &parsed->table_seq,
                                       &context_set)) {
        return false;
    }

    return refmem_application_contract_validate_application_map(&parsed->application_map) &&
           refmem_application_contract_validate_slot_substrate(&parsed->generic_node,
                                                               &parsed->board_capability) &&
           refmem_application_contract_validate_node_load_table(&parsed->node_load,
                                                                 &parsed->application_map) &&
           refmem_model_validate_tdma_profile_contract(
               &parsed->tdma_foundation_profile,
               &parsed->generic_node,
               &parsed->board_capability,
               &parsed->node_load,
               &parsed->fb_instance);
}

static void refmem_model_apply_parsed_tables(const refmem_model_parsed_tables_t *parsed)
{
    if (parsed == NULL) {
        return;
    }

    s_active_application_map = parsed->application_map;
    s_active_board_capability_table = parsed->board_capability;
    s_active_generic_node_table = parsed->generic_node;
    s_active_node_load_table = parsed->node_load;
    s_active_fb_instance_table = parsed->fb_instance;
    s_active_event_link_table = parsed->event_link;
    s_active_data_link_table = parsed->data_link;
    s_active_deployment_gate = parsed->deployment_gate;
    s_active_connection_quality = parsed->connection_quality;
    s_active_tdma_foundation_profile = parsed->tdma_foundation_profile;
    s_active_tables_from_image = true;

    s_snapshot.version = REFMEM_APP_MODEL_VERSION;
    s_snapshot.valid = 1u;
    s_snapshot.target_node_mask = s_active_application_map.target_node_mask;
    s_snapshot.table_mask = REFMEM_APP_TABLE_MASK_ALL;
    s_snapshot.application_map_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_APPLICATION_MAP];
    s_snapshot.board_capability_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_BOARD_CAPABILITY];
    s_snapshot.generic_node_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_GENERIC_NODE];
    s_snapshot.node_load_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_NODE_LOAD];
    s_snapshot.fb_instance_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_FB_INSTANCE];
    s_snapshot.event_link_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_EVENT_LINK];
    s_snapshot.data_link_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_DATA_LINK];
    s_snapshot.deployment_gate_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_DEPLOYMENT_GATE];
    s_snapshot.connection_quality_crc32 = parsed->table_crc32[REFMEM_APP_TABLE_CONNECTION_QUALITY];
    s_snapshot.tdma_foundation_profile_table_crc32 =
        parsed->table_crc32[REFMEM_APP_TABLE_TDMA_FOUNDATION_PROFILE];
    s_snapshot.package_crc32 = parsed->package_crc32;
    s_snapshot.lint_error_count = 0u;
    s_snapshot.first_lint_error = REFMEM_APP_LINT_OK;
    s_load_snapshot.active_package_crc32 = parsed->package_crc32;
    s_board_load_snapshot.active_crc32 = s_snapshot.board_capability_crc32;
}

bool refmem_application_model_apply_active_table_views(void)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    refmem_model_parsed_tables_t parsed;
    if (!refmem_model_parse_image_views(REFMEM_TABLE_IMAGE_ACTIVE, &parsed)) {
        return false;
    }
    refmem_model_apply_parsed_tables(&parsed);
    return true;
}

bool refmem_application_model_prepare_staging_table_views(void)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    s_pending_tables_valid = false;
    if (!refmem_model_parse_image_views(REFMEM_TABLE_IMAGE_STAGING, &s_pending_tables)) {
        memset(&s_pending_tables, 0, sizeof(s_pending_tables));
        return false;
    }

    s_pending_tables_valid = true;
    return true;
}

bool refmem_application_model_commit_prepared_table_views(void)
{
    if (!s_pending_tables_valid) {
        return false;
    }

    refmem_model_apply_parsed_tables(&s_pending_tables);
    memset(&s_pending_tables, 0, sizeof(s_pending_tables));
    s_pending_tables_valid = false;
    return true;
}

void refmem_application_model_discard_prepared_table_views(void)
{
    memset(&s_pending_tables, 0, sizeof(s_pending_tables));
    s_pending_tables_valid = false;
}

bool refmem_application_model_get_prepared_tdma_foundation_profile(
    tdma_foundation_profile_t *profile)
{
    if (!s_pending_tables_valid || profile == NULL) {
        return false;
    }

    *profile = s_pending_tables.tdma_foundation_profile;
    return true;
}

static bool refmem_model_make_staging_node_load_table(
    uint32_t node_id,
    uint32_t instance_id,
    uint32_t role_mask,
    uint32_t persona_mask,
    uint32_t enabled,
    uint32_t required,
    uint32_t load_order,
    refmem_node_load_table_t *candidate,
    uint32_t *candidate_crc32)
{
    if (candidate == NULL || candidate_crc32 == NULL) {
        return false;
    }

    if (s_staging_node_load_valid) {
        *candidate = s_staging_node_load_table;
    } else {
        *candidate = *refmem_application_model_get_node_load_table();
    }

    refmem_node_load_entry_t *target = NULL;
    for (uint32_t i = 0u; i < candidate->load_count; i++) {
        refmem_node_load_entry_t *entry = &candidate->load[i];
        if (entry->instance_id == instance_id) {
            target = entry;
            break;
        }
    }

    if (target == NULL) {
        return false;
    }

    const refmem_application_map_t *application_map = refmem_model_current_application_map();
    target->application_id = application_map->application_id;
    target->profile_id = application_map->profile_id;
    target->node_id = node_id;
    target->role_mask = role_mask;
    target->persona_mask = persona_mask;
    target->enabled = enabled;
    target->required = required;
    target->load_order = load_order;

    if (!refmem_application_contract_validate_node_load_table(candidate, application_map)) {
        return false;
    }

    *candidate_crc32 = refmem_model_node_load_table_crc32(candidate);
    return *candidate_crc32 != 0u;
}

static bool refmem_model_make_staging_board_capability_table(
    const refmem_board_capability_entry_t *candidate_entry,
    refmem_board_capability_table_t *candidate,
    uint32_t *candidate_crc32)
{
    if (candidate_entry == NULL || candidate == NULL || candidate_crc32 == NULL ||
        candidate_entry->board_id >= REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT) {
        return false;
    }

    if (s_staging_board_capability_valid) {
        *candidate = s_staging_board_capability_table;
    } else {
        *candidate = *refmem_application_model_get_board_capability_table();
    }

    if (candidate_entry->board_id >= candidate->board_count) {
        return false;
    }

    candidate->board[candidate_entry->board_id] = *candidate_entry;
    if (!refmem_application_contract_validate_board_capability_table(
            candidate,
            refmem_application_model_get_generic_node_table()->node_count)) {
        return false;
    }

    *candidate_crc32 = refmem_model_rmtp_crc32(candidate, sizeof(*candidate));
    return *candidate_crc32 != 0u;
}

bool refmem_application_model_validate(void)
{
    uint32_t error_count;
    uint32_t first_error;
    refmem_model_lint(&error_count, &first_error);
    return error_count == 0u;
}

bool refmem_application_model_init(void)
{
    s_active_tables_from_image = false;
    refmem_application_model_discard_prepared_table_views();

    if (!tdma_foundation_profile_default(&s_tdma_foundation_profile,
                                         REFMEM_APP_INSTANCE_TDMA_FOUNDATION,
                                         0u,
                                         0u,
                                         TDMA_ADAPTER_PIO_SPI)) {
        return false;
    }
    s_tdma_foundation_profile.resource.io_claim_mask =
        REFMEM_APP_IO_PIO_SPI_SYNC;
    s_tdma_foundation_profile.resource.ip_core_claim_mask =
        REFMEM_APP_IP_PIO_SPI_SYNC_DELTA | REFMEM_APP_IP_TDMA_SCHEDULER;
    s_tdma_foundation_profile.profile_crc32 =
        tdma_foundation_profile_crc32(&s_tdma_foundation_profile);

    s_snapshot.version = REFMEM_APP_MODEL_VERSION;
    s_snapshot.target_node_mask = s_application_map.target_node_mask;
    s_snapshot.table_mask = REFMEM_APP_TABLE_MASK_ALL;
    s_snapshot.application_map_crc32 = refmem_model_application_map_crc32();
    s_snapshot.board_capability_crc32 = refmem_model_board_capability_crc32();
    s_snapshot.generic_node_crc32 = refmem_model_generic_node_crc32();
    s_snapshot.node_load_crc32 = refmem_model_node_load_crc32();
    s_snapshot.fb_instance_crc32 = refmem_model_fb_instance_crc32();
    s_snapshot.event_link_crc32 = refmem_model_event_link_crc32();
    s_snapshot.data_link_crc32 = refmem_model_data_link_crc32();
    s_snapshot.deployment_gate_crc32 = refmem_model_deployment_gate_crc32();
    s_snapshot.connection_quality_crc32 = refmem_model_connection_quality_crc32();
    s_snapshot.tdma_foundation_profile_table_crc32 =
        refmem_model_tdma_foundation_profile_table_crc32();

    const uint32_t package_fields[] = {
        s_snapshot.version,
        s_snapshot.target_node_mask,
        s_snapshot.table_mask,
        s_snapshot.application_map_crc32,
        s_snapshot.board_capability_crc32,
        s_snapshot.generic_node_crc32,
        s_snapshot.node_load_crc32,
        s_snapshot.fb_instance_crc32,
        s_snapshot.event_link_crc32,
        s_snapshot.data_link_crc32,
        s_snapshot.deployment_gate_crc32,
        s_snapshot.connection_quality_crc32,
        s_snapshot.tdma_foundation_profile_table_crc32,
    };
    s_snapshot.package_crc32 =
        refmem_model_crc32_fields(package_fields,
                                  sizeof(package_fields) / sizeof(package_fields[0]));
    refmem_model_lint(&s_snapshot.lint_error_count, &s_snapshot.first_lint_error);
    s_snapshot.valid = s_snapshot.lint_error_count == 0u ? 1u : 0u;

    memset(&s_load_snapshot, 0, sizeof(s_load_snapshot));
    s_load_snapshot.version = REFMEM_APP_MODEL_VERSION;
    s_load_snapshot.source = REFMEM_APP_LOAD_SOURCE_DEFAULT;
    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    s_load_snapshot.staging_state = REFMEM_APP_STAGING_EMPTY;
    s_load_snapshot.active_package_crc32 = s_snapshot.package_crc32;
    s_load_snapshot.last_error = REFMEM_APP_LOAD_OK;

    memset(&s_board_load_snapshot, 0, sizeof(s_board_load_snapshot));
    s_board_load_snapshot.version = REFMEM_APP_MODEL_VERSION;
    s_board_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    s_board_load_snapshot.staging_state = REFMEM_APP_STAGING_EMPTY;
    s_board_load_snapshot.active_crc32 = s_snapshot.board_capability_crc32;
    s_board_load_snapshot.last_error = REFMEM_APP_LOAD_OK;

    memset(&s_staging_node_load_table, 0, sizeof(s_staging_node_load_table));
    memset(&s_staging_board_capability_table, 0, sizeof(s_staging_board_capability_table));
    s_staging_node_load_valid = false;
    s_staging_board_capability_valid = false;
    s_initialized = true;
    refmem_table_registry_init(&s_snapshot);
    return s_snapshot.valid != 0u;
}

bool refmem_application_model_stage_sd_system_pack(const char *path,
                                                   uint32_t path_hash,
                                                   uint32_t manifest_status,
                                                   uint32_t manifest_schema,
                                                   uint32_t manifest_required_count,
                                                   uint32_t manifest_missing_count,
                                                   const char *manifest_build_id,
                                                   uint32_t package_crc32,
                                                   uint32_t package_valid,
                                                   uint32_t package_error)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    if (s_load_snapshot.mode != REFMEM_APP_MODEL_MODE_IDLE) {
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_BAD_ARGUMENT;
        return false;
    }

    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_LOAD_TO_STAGING;
    s_load_snapshot.load_seq++;
    s_load_snapshot.source = REFMEM_APP_LOAD_SOURCE_SD_SYSTEM_PACK;
    s_load_snapshot.manifest_status = manifest_status;
    s_load_snapshot.manifest_schema = manifest_schema;
    s_load_snapshot.manifest_required_count = manifest_required_count;
    s_load_snapshot.manifest_missing_count = manifest_missing_count;
    s_load_snapshot.path_hash = path_hash;
    s_load_snapshot.active_package_crc32 = s_snapshot.package_crc32;
    refmem_model_copy_text(s_load_snapshot.path, sizeof(s_load_snapshot.path), path);
    refmem_model_copy_text(s_load_snapshot.manifest_build_id,
                           sizeof(s_load_snapshot.manifest_build_id),
                           manifest_build_id);

    if (manifest_status != REFMEM_APP_MODEL_SD_MANIFEST_OK || manifest_missing_count != 0u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.staging_package_crc32 = 0u;
        s_load_snapshot.staging_lint_error_count = 1u;
        s_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_BAD_TABLE_VERSION;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_MANIFEST_NOT_OK;
        refmem_model_finish_load_idle();
        return false;
    }

    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_VALIDATING;
    s_load_snapshot.staging_package_crc32 = package_crc32;
    s_load_snapshot.staging_lint_error_count = s_snapshot.lint_error_count;
    s_load_snapshot.staging_first_lint_error = package_error;
    s_load_snapshot.staging_node_id = 0u;
    s_load_snapshot.staging_instance_id = 0u;
    s_load_snapshot.staging_role_mask = 0u;
    s_load_snapshot.staging_persona_mask = 0u;
    s_load_snapshot.staging_enabled = 0u;
    s_load_snapshot.staging_required = 0u;
    s_load_snapshot.staging_load_order = 0u;
    if (package_valid == 0u || package_crc32 == 0u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_PACKAGE_INVALID;
        refmem_model_finish_load_idle();
        return false;
    }

    if (s_snapshot.valid == 0u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.staging_first_lint_error = s_snapshot.first_lint_error;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_LINT_FAILED;
        refmem_model_finish_load_idle();
        return false;
    }

    s_load_snapshot.staging_state = REFMEM_APP_STAGING_VALIDATED;
    s_load_snapshot.last_error = REFMEM_APP_LOAD_OK;
    refmem_model_finish_load_idle();
    (void)refmem_table_registry_validate_staging(&s_load_snapshot);
    return true;
}

bool refmem_application_model_stage_scpi_node_config(uint32_t node_id,
                                                     uint32_t instance_id,
                                                     uint32_t role_mask,
                                                     uint32_t persona_mask,
                                                     uint32_t enabled,
                                                     uint32_t required,
                                                     uint32_t load_order)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    if (s_load_snapshot.mode != REFMEM_APP_MODEL_MODE_IDLE) {
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_BAD_ARGUMENT;
        return false;
    }

    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_LOAD_TO_STAGING;
    s_load_snapshot.load_seq++;
    s_load_snapshot.source = REFMEM_APP_LOAD_SOURCE_SCPI_INLINE;
    s_load_snapshot.manifest_status = 0u;
    s_load_snapshot.manifest_schema = 0u;
    s_load_snapshot.manifest_required_count = 0u;
    s_load_snapshot.manifest_missing_count = 0u;
    s_load_snapshot.path_hash = 0u;
    s_load_snapshot.active_package_crc32 = s_snapshot.package_crc32;
    s_load_snapshot.staging_node_id = node_id;
    s_load_snapshot.staging_instance_id = instance_id;
    s_load_snapshot.staging_role_mask = role_mask;
    s_load_snapshot.staging_persona_mask = persona_mask;
    s_load_snapshot.staging_enabled = enabled;
    s_load_snapshot.staging_required = required;
    s_load_snapshot.staging_load_order = load_order;
    s_load_snapshot.manifest_build_id[0] = '\0';
    s_load_snapshot.path[0] = '\0';

    if (node_id >= REFMEM_APP_MODEL_NODE_COUNT) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.staging_package_crc32 = 0u;
        s_load_snapshot.staging_lint_error_count = 1u;
        s_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_BAD_NODE_RANGE;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_NODE_RANGE;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_NODE_LOAD,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                s_load_snapshot.staging_first_lint_error);
        return false;
    }

    if (!refmem_model_instance_exists(instance_id) ||
        enabled > 1u ||
        required > 1u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.staging_package_crc32 = 0u;
        s_load_snapshot.staging_lint_error_count = 1u;
        s_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_BAD_INSTANCE_RANGE;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_INSTANCE_RANGE;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_NODE_LOAD,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                s_load_snapshot.staging_first_lint_error);
        return false;
    }

    s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_VALIDATING;
    refmem_node_load_table_t candidate;
    uint32_t candidate_crc32 = 0u;
    const bool candidate_valid =
        refmem_model_make_staging_node_load_table(node_id,
                                                  instance_id,
                                                  role_mask,
                                                  persona_mask,
                                                  enabled,
                                                  required,
                                                  load_order,
                                                  &candidate,
                                                  &candidate_crc32);
    s_load_snapshot.staging_lint_error_count =
        (s_snapshot.lint_error_count == 0u && candidate_valid) ? 0u : 1u;
    s_load_snapshot.staging_first_lint_error =
        candidate_valid ? s_snapshot.first_lint_error : REFMEM_APP_LINT_BAD_INSTANCE_RANGE;
    s_load_snapshot.staging_package_crc32 = 0u;
    if (s_snapshot.valid == 0u) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_LINT_FAILED;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_NODE_LOAD,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                s_load_snapshot.staging_first_lint_error);
        return false;
    }
    if (!candidate_valid) {
        s_load_snapshot.staging_state = REFMEM_APP_STAGING_FAILED;
        s_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_LINT_FAILED;
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_NODE_LOAD,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                s_load_snapshot.staging_first_lint_error);
        return false;
    }

    s_staging_node_load_table = candidate;
    s_staging_node_load_valid = true;
    return refmem_model_stage_inline_package_image();
}

bool refmem_application_model_stage_scpi_board_capability(uint32_t board_id,
                                                          uint32_t board_uuid_crc32,
                                                          uint32_t capability_mask,
                                                          uint32_t io_constraint_mask,
                                                          uint32_t ip_core_mask,
                                                          uint32_t default_persona_mask,
                                                          uint32_t hw_profile_crc32,
                                                          uint32_t active_default_slot,
                                                          uint32_t online_required)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }

    if (s_load_snapshot.mode != REFMEM_APP_MODEL_MODE_IDLE ||
        s_board_load_snapshot.mode != REFMEM_APP_MODEL_MODE_IDLE) {
        s_board_load_snapshot.last_error = REFMEM_APP_LOAD_ERR_BAD_ARGUMENT;
        return false;
    }

    refmem_board_capability_entry_t candidate = {
        board_id,
        board_uuid_crc32,
        capability_mask,
        io_constraint_mask,
        ip_core_mask,
        default_persona_mask,
        hw_profile_crc32,
        active_default_slot,
        online_required,
    };

    s_board_load_snapshot.mode = REFMEM_APP_MODEL_MODE_LOAD_TO_STAGING;
    s_board_load_snapshot.load_seq++;
    s_board_load_snapshot.active_crc32 = s_snapshot.board_capability_crc32;
    s_board_load_snapshot.staging_crc32 = 0u;
    s_board_load_snapshot.staging_board_id = board_id;
    s_board_load_snapshot.staging_board_uuid_crc32 = board_uuid_crc32;
    s_board_load_snapshot.staging_capability_mask = capability_mask;
    s_board_load_snapshot.staging_io_constraint_mask = io_constraint_mask;
    s_board_load_snapshot.staging_ip_core_mask = ip_core_mask;
    s_board_load_snapshot.staging_default_persona_mask = default_persona_mask;
    s_board_load_snapshot.staging_hw_profile_crc32 = hw_profile_crc32;
    s_board_load_snapshot.staging_active_default_slot = active_default_slot;
    s_board_load_snapshot.staging_online_required = online_required;

    bool valid = true;
    uint32_t first_error = REFMEM_APP_LINT_OK;
    if (board_id >= REFMEM_APP_MODEL_BOARD_CAPABILITY_COUNT ||
        board_uuid_crc32 == 0u ||
        active_default_slot >= REFMEM_APP_MODEL_NODE_COUNT ||
        online_required > 1u) {
        valid = false;
        first_error = REFMEM_APP_LINT_BAD_BOARD_CAPABILITY;
    }

    if (valid &&
        (capability_mask & REFMEM_APP_CAP_BASELINE) != REFMEM_APP_CAP_BASELINE) {
        valid = false;
        first_error = REFMEM_APP_LINT_BAD_BOARD_CAPABILITY;
    }

    const uint32_t io_capability =
        refmem_realtime_contract_io_capability_mask(io_constraint_mask);
    const uint32_t ip_capability =
        refmem_realtime_contract_ip_capability_mask(ip_core_mask);
    if (valid && (((io_capability | ip_capability) & ~capability_mask) != 0u)) {
        valid = false;
        first_error = REFMEM_APP_LINT_BAD_BOARD_CAPABILITY;
    }

    s_board_load_snapshot.mode = REFMEM_APP_MODEL_MODE_VALIDATING;
    refmem_board_capability_table_t candidate_table;
    uint32_t candidate_crc32 = 0u;
    if (valid &&
        !refmem_model_make_staging_board_capability_table(&candidate,
                                                          &candidate_table,
                                                          &candidate_crc32)) {
        valid = false;
        first_error = REFMEM_APP_LINT_BAD_BOARD_CAPABILITY;
    }

    s_board_load_snapshot.staging_lint_error_count = valid ? 0u : 1u;
    s_board_load_snapshot.staging_first_lint_error = first_error;
    s_board_load_snapshot.staging_crc32 = valid ? candidate_crc32 : 0u;
    s_board_load_snapshot.last_error = valid ? REFMEM_APP_LOAD_OK : REFMEM_APP_LOAD_ERR_LINT_FAILED;
    s_board_load_snapshot.staging_state =
        valid ? REFMEM_APP_STAGING_VALIDATED : REFMEM_APP_STAGING_FAILED;
    if (valid) {
        s_staging_board_capability_table = candidate_table;
        s_staging_board_capability_valid = true;
    }

    if (valid) {
        s_load_snapshot.mode = REFMEM_APP_MODEL_MODE_VALIDATING;
        s_load_snapshot.load_seq++;
        s_load_snapshot.source = REFMEM_APP_LOAD_SOURCE_SCPI_INLINE;
        s_load_snapshot.manifest_status = 0u;
        s_load_snapshot.manifest_schema = 0u;
        s_load_snapshot.manifest_required_count = 0u;
        s_load_snapshot.manifest_missing_count = 0u;
        s_load_snapshot.path_hash = 0u;
        s_load_snapshot.active_package_crc32 = s_snapshot.package_crc32;
        s_load_snapshot.staging_node_id = 0u;
        s_load_snapshot.staging_instance_id = 0u;
        s_load_snapshot.staging_role_mask = 0u;
        s_load_snapshot.staging_persona_mask = 0u;
        s_load_snapshot.staging_enabled = 0u;
        s_load_snapshot.staging_required = 0u;
        s_load_snapshot.staging_load_order = 0u;
        s_load_snapshot.manifest_build_id[0] = '\0';
        s_load_snapshot.path[0] = '\0';
        valid = refmem_model_stage_inline_package_image();
        s_board_load_snapshot.last_error =
            valid ? REFMEM_APP_LOAD_OK : REFMEM_APP_LOAD_ERR_PACKAGE_INVALID;
        s_board_load_snapshot.staging_state =
            valid ? REFMEM_APP_STAGING_VALIDATED : REFMEM_APP_STAGING_FAILED;
        if (!valid) {
            s_board_load_snapshot.staging_lint_error_count = 1u;
            s_board_load_snapshot.staging_first_lint_error = REFMEM_APP_LINT_BAD_TABLE_VERSION;
        }
    } else {
        (void)refmem_table_registry_stage_table(REFMEM_APP_TABLE_BOARD_CAPABILITY,
                                                0u,
                                                REFMEM_TABLE_VALIDATION_FAILED,
                                                first_error);
    }
    s_board_load_snapshot.mode = REFMEM_APP_MODEL_MODE_IDLE;
    return valid;
}

const refmem_application_map_t *refmem_application_model_get_application_map(void)
{
    return s_active_tables_from_image ? &s_active_application_map : &s_application_map;
}

const refmem_board_capability_table_t *refmem_application_model_get_board_capability_table(void)
{
    return s_active_tables_from_image ? &s_active_board_capability_table : &s_board_capability_table;
}

const refmem_generic_node_table_t *refmem_application_model_get_generic_node_table(void)
{
    return s_active_tables_from_image ? &s_active_generic_node_table : &s_generic_node_table;
}

const refmem_node_load_table_t *refmem_application_model_get_node_load_table(void)
{
    return s_active_tables_from_image ? &s_active_node_load_table : &s_node_load_table;
}

const refmem_fb_instance_table_t *refmem_application_model_get_fb_instance_table(void)
{
    return s_active_tables_from_image ? &s_active_fb_instance_table : &s_fb_instance_table;
}

const refmem_event_link_table_t *refmem_application_model_get_event_link_table(void)
{
    return s_active_tables_from_image ? &s_active_event_link_table : &s_event_link_table;
}

const refmem_data_link_table_t *refmem_application_model_get_data_link_table(void)
{
    return s_active_tables_from_image ? &s_active_data_link_table : &s_data_link_table;
}

const refmem_deployment_gate_table_t *refmem_application_model_get_deployment_gate(void)
{
    return s_active_tables_from_image ? &s_active_deployment_gate : &s_deployment_gate;
}

const refmem_connection_quality_table_t *refmem_application_model_get_connection_quality(void)
{
    return s_active_tables_from_image ? &s_active_connection_quality : &s_connection_quality;
}

const tdma_foundation_profile_t *refmem_application_model_get_tdma_foundation_profile(void)
{
    return refmem_model_current_tdma_foundation_profile();
}

bool refmem_application_model_set_tdma_ring_local_slot(uint32_t local_slot_id)
{
    tdma_foundation_profile_t *profile =
        s_active_tables_from_image ? &s_active_tdma_foundation_profile
                                   : &s_tdma_foundation_profile;
    if (local_slot_id >= TDMA_RING_NODE_MAX) {
        return false;
    }
    if (local_slot_id == profile->ring.local_index) {
        return true;
    }
    tdma_foundation_profile_t updated = *profile;
    if (!tdma_ring_profile_default(&updated.ring,
                                   local_slot_id,
                                   updated.ring.reference_index,
                                   updated.ring.node_count)) {
        return false;
    }
    updated.profile_crc32 = tdma_foundation_profile_crc32(&updated);
    *profile = updated;
    return true;
}

const refmem_application_model_snapshot_t *refmem_application_model_get_snapshot(void)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }
    return &s_snapshot;
}

bool refmem_application_model_get_staging_node_load_entry(
    uint32_t instance_id,
    refmem_node_load_entry_t *entry)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }
    if (entry == NULL || !s_staging_node_load_valid) {
        return false;
    }

    for (uint32_t i = 0u; i < s_staging_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *candidate = &s_staging_node_load_table.load[i];
        if (candidate->instance_id == instance_id) {
            *entry = *candidate;
            return true;
        }
    }
    return false;
}

void refmem_application_model_get_load_snapshot(refmem_application_model_load_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }
    *snapshot = s_load_snapshot;
}

void refmem_application_model_get_board_load_snapshot(refmem_board_capability_load_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }
    *snapshot = s_board_load_snapshot;
}
