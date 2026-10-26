#include "refmem_application_model_internal.h"

const refmem_application_map_t s_application_map = {
    .version = REFMEM_APP_MODEL_VERSION,
    .application_id = 1u,
    .application_version = 1u,
    .profile_id = 1u,
    .layout_version = DISTRIBUTED_REFMEM_LAYOUT_VERSION,
    .target_node_mask = 0xFFu,
};

const refmem_board_capability_table_t s_board_capability_table = {
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

const refmem_generic_node_table_t s_generic_node_table = {
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

const refmem_node_load_table_t s_node_load_table = {
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

const refmem_fb_instance_table_t s_fb_instance_table = {
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

const refmem_event_link_table_t s_event_link_table = {
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

const refmem_data_link_table_t s_data_link_table = {
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

const refmem_deployment_gate_table_t s_deployment_gate = {
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

const refmem_connection_quality_table_t s_connection_quality = {
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

tdma_foundation_profile_t s_tdma_foundation_profile;
/* Design-time baseline of the foundation profile.  `s_tdma_foundation_profile`
 * (or the active copy) carries the runtime ring seat written by
 * `SYSTem:TDMA:RING:TOPology`; packaging and owner lint must use the design
 * baseline, otherwise a board seated on slot != 0 serializes its own seat into
 * the package and fails node_load owner validation (N5.2a.3). */
tdma_foundation_profile_t s_design_tdma_foundation_profile;

refmem_application_model_snapshot_t s_snapshot;
refmem_application_model_load_snapshot_t s_load_snapshot;
refmem_board_capability_load_snapshot_t s_board_load_snapshot;
refmem_node_load_table_t s_staging_node_load_table;
refmem_board_capability_table_t s_staging_board_capability_table;
refmem_application_map_t s_active_application_map;
refmem_board_capability_table_t s_active_board_capability_table;
refmem_generic_node_table_t s_active_generic_node_table;
refmem_node_load_table_t s_active_node_load_table;
refmem_fb_instance_table_t s_active_fb_instance_table;
refmem_event_link_table_t s_active_event_link_table;
refmem_data_link_table_t s_active_data_link_table;
refmem_deployment_gate_table_t s_active_deployment_gate;
uint32_t s_origin_model_epoch = 2u;
refmem_connection_quality_table_t s_active_connection_quality;
tdma_foundation_profile_t s_active_tdma_foundation_profile;
bool s_staging_node_load_valid;
bool s_staging_board_capability_valid;
bool s_active_tables_from_image;
bool s_initialized;


refmem_model_parsed_tables_t s_pending_tables;
bool s_pending_tables_valid;

bool refmem_model_instance_enabled(const refmem_fb_instance_entry_t *instance);
void refmem_model_copy_text(char *dst, size_t dst_size, const char *src);
void refmem_model_finish_load_idle(void);
const refmem_application_map_t *refmem_model_current_application_map(void);
const refmem_board_capability_table_t *refmem_model_current_board_capability_table(void);
const refmem_generic_node_table_t *refmem_model_current_generic_node_table(void);
const refmem_node_load_table_t *refmem_model_current_node_load_table(void);
const refmem_fb_instance_table_t *refmem_model_current_fb_instance_table(void);
const refmem_event_link_table_t *refmem_model_current_event_link_table(void);
const refmem_data_link_table_t *refmem_model_current_data_link_table(void);
const refmem_deployment_gate_table_t *refmem_model_current_deployment_gate_table(void);
const refmem_connection_quality_table_t *refmem_model_current_connection_quality_table(void);
const tdma_foundation_profile_t *refmem_model_current_tdma_foundation_profile(void);
const tdma_foundation_profile_t *refmem_model_design_tdma_foundation_profile(void);

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

uint32_t refmem_model_rmtp_crc32(const void *data, size_t size)
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

uint32_t refmem_model_rmtp_crc32_string(const char *text)
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

uint32_t refmem_model_rmtp_crc32_zero_field(const uint8_t *data,
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

bool refmem_model_write_u32_le(uint8_t *data,
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

bool refmem_model_write_i32_le(uint8_t *data,
                                      size_t size,
                                      size_t *cursor,
                                      int32_t value)
{
    return refmem_model_write_u32_le(data, size, cursor, (uint32_t)value);
}

uint32_t refmem_model_crc32_fields(const uint32_t *fields, size_t count)
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

bool refmem_model_parse_application_map_view(const uint8_t *data,
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

bool refmem_model_parse_board_capability_view(const uint8_t *data,
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

bool refmem_model_parse_generic_node_view(const uint8_t *data,
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

bool refmem_model_parse_node_load_view(const uint8_t *data,
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

bool refmem_model_parse_fb_instance_view(const uint8_t *data,
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

bool refmem_model_parse_event_link_view(const uint8_t *data,
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

bool refmem_model_parse_data_link_view(const uint8_t *data,
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

bool refmem_model_parse_deployment_gate_view(const uint8_t *data,
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

bool refmem_model_parse_connection_quality_view(const uint8_t *data,
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

bool refmem_model_parse_tdma_foundation_profile_view(const uint8_t *data,
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

uint32_t refmem_model_application_map_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_application_map,
                                     sizeof(s_application_map));
}

uint32_t refmem_model_board_capability_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_board_capability_table,
                                     sizeof(s_board_capability_table));
}

uint32_t refmem_model_generic_node_crc32(void)
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

uint32_t refmem_model_node_load_table_crc32(const refmem_node_load_table_t *table)
{
    if (table == NULL) {
        return 0u;
    }
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     table,
                                     sizeof(*table));
}

uint32_t refmem_model_node_load_crc32(void)
{
    return refmem_model_node_load_table_crc32(&s_node_load_table);
}

uint32_t refmem_model_fb_instance_crc32(void)
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

uint32_t refmem_model_event_link_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_event_link_table,
                                     sizeof(s_event_link_table));
}

uint32_t refmem_model_data_link_crc32(void)
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

uint32_t refmem_model_deployment_gate_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_deployment_gate,
                                     sizeof(s_deployment_gate));
}

uint32_t refmem_model_connection_quality_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_connection_quality,
                                     sizeof(s_connection_quality));
}

uint32_t refmem_model_tdma_foundation_profile_table_crc32(void)
{
    uint8_t wire[TDMA_FOUNDATION_PROFILE_TABLE_WIRE_SIZE];
    if (!tdma_foundation_profile_encode_table(&s_design_tdma_foundation_profile,
                                              wire,
                                              sizeof(wire))) {
        return 0u;
    }
    return refmem_model_rmtp_crc32(wire, sizeof(wire));
}

const refmem_application_map_t *refmem_model_current_application_map(void)
{
    return s_active_tables_from_image ? &s_active_application_map : &s_application_map;
}

const refmem_board_capability_table_t *refmem_model_current_board_capability_table(void)
{
    return s_active_tables_from_image ? &s_active_board_capability_table : &s_board_capability_table;
}

const refmem_generic_node_table_t *refmem_model_current_generic_node_table(void)
{
    return s_active_tables_from_image ? &s_active_generic_node_table : &s_generic_node_table;
}

const refmem_node_load_table_t *refmem_model_current_node_load_table(void)
{
    return s_active_tables_from_image ? &s_active_node_load_table : &s_node_load_table;
}

const refmem_fb_instance_table_t *refmem_model_current_fb_instance_table(void)
{
    return s_active_tables_from_image ? &s_active_fb_instance_table : &s_fb_instance_table;
}
