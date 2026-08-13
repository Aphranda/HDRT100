#include "refmem_application_model.h"

#include <stddef.h>
#include <string.h>

#include "ota_crc32.h"
#include "refmem_vector_table.h"

#define REFMEM_APP_INSTANCE_A0_REFMEM_SYNC      0u
#define REFMEM_APP_INSTANCE_A0_LOOP_ENGINE      1u
#define REFMEM_APP_INSTANCE_A0_PULSE_COUNTER    2u
#define REFMEM_APP_INSTANCE_A0_TRIGGER_MASTER   3u
#define REFMEM_APP_INSTANCE_A1_TRIGGER          4u
#define REFMEM_APP_INSTANCE_A2_LINK_SWITCH      5u
#define REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY   6u
#define REFMEM_APP_INSTANCE_A3_INSTRUMENT       7u
#define REFMEM_APP_INSTANCE_A3_CALIBRATION      8u
#define REFMEM_APP_INSTANCE_A4_MODEL_VNA        9u
#define REFMEM_APP_INSTANCE_A4_MODEL_TT         10u

static const refmem_application_map_t s_application_map = {
    .version = REFMEM_APP_MODEL_VERSION,
    .application_id = 1u,
    .application_version = 1u,
    .profile_id = 1u,
    .layout_version = DISTRIBUTED_REFMEM_LAYOUT_VERSION,
    .node_count = REFMEM_APP_MODEL_NODE_COUNT,
    .target_node_mask = 0xFFu,
    .node = {
        {0u, 0xA0000000u, REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_A0_TRIGGER_MASTER, 0u, 1u, REFMEM_APP_FAIL_STOP},
        {1u, 0xA0000001u, REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_A1_DISTRIBUTED_TRIGGER, 0u, 1u, REFMEM_APP_FAIL_STOP},
        {2u, 0xA0000002u, REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_LINK_SWITCHER,
         REFMEM_APP_PERSONA_A2_LINK_SWITCH, 0u, 1u, REFMEM_APP_FAIL_STOP},
        {3u, 0xA0000003u, REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER |
                            REFMEM_APP_ROLE_GATEWAY,
         REFMEM_APP_PERSONA_A3_GATEWAY, 0u, 1u, REFMEM_APP_FAIL_STOP},
        {4u, 0xA0000004u, REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_MODEL_VNA |
                            REFMEM_APP_ROLE_MODEL_TURNTABLE | REFMEM_APP_ROLE_TEST_AGENT,
         REFMEM_APP_PERSONA_A4_MODEL_INSTRUMENTS, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY},
        {5u, 0xA0000005u, REFMEM_APP_ROLE_BOARD,
         REFMEM_APP_PERSONA_SPARE, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY},
        {6u, 0xA0000006u, REFMEM_APP_ROLE_BOARD,
         REFMEM_APP_PERSONA_SPARE, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY},
        {7u, 0xA0000007u, REFMEM_APP_ROLE_BOARD,
         REFMEM_APP_PERSONA_SPARE, 0u, 0u, REFMEM_APP_FAIL_REPORT_ONLY},
    },
};

static const refmem_node_load_table_t s_node_load_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .load_count = REFMEM_APP_MODEL_NODE_LOAD_COUNT,
    .load = {
        {0u, 1u, 1u, 0u, REFMEM_APP_INSTANCE_A0_REFMEM_SYNC,
         REFMEM_APP_ROLE_BOARD, REFMEM_APP_PERSONA_A0_TRIGGER_MASTER,
         1u, 1u, REFMEM_APP_FAIL_STOP, 0u},
        {1u, 1u, 1u, 0u, REFMEM_APP_INSTANCE_A0_LOOP_ENGINE,
         REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_A0_TRIGGER_MASTER, 1u, 1u, REFMEM_APP_FAIL_STOP, 1u},
        {2u, 1u, 1u, 0u, REFMEM_APP_INSTANCE_A0_PULSE_COUNTER,
         REFMEM_APP_ROLE_PULSE_DISTRIBUTOR, REFMEM_APP_PERSONA_A0_TRIGGER_MASTER,
         1u, 1u, REFMEM_APP_FAIL_STOP, 2u},
        {3u, 1u, 1u, 0u, REFMEM_APP_INSTANCE_A0_TRIGGER_MASTER,
         REFMEM_APP_ROLE_PULSE_DISTRIBUTOR, REFMEM_APP_PERSONA_A0_TRIGGER_MASTER,
         1u, 1u, REFMEM_APP_FAIL_STOP, 3u},
        {4u, 1u, 1u, 1u, REFMEM_APP_INSTANCE_A1_TRIGGER,
         REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_PULSE_DISTRIBUTOR,
         REFMEM_APP_PERSONA_A1_DISTRIBUTED_TRIGGER, 1u, 1u, REFMEM_APP_FAIL_STOP, 0u},
        {5u, 1u, 1u, 2u, REFMEM_APP_INSTANCE_A2_LINK_SWITCH,
         REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_LINK_SWITCHER,
         REFMEM_APP_PERSONA_A2_LINK_SWITCH, 1u, 1u, REFMEM_APP_FAIL_STOP, 0u},
        {6u, 1u, 1u, 3u, REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY,
         REFMEM_APP_ROLE_BOARD | REFMEM_APP_ROLE_GATEWAY,
         REFMEM_APP_PERSONA_A3_GATEWAY, 1u, 1u, REFMEM_APP_FAIL_STOP, 0u},
        {7u, 1u, 1u, 3u, REFMEM_APP_INSTANCE_A3_INSTRUMENT,
         REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER | REFMEM_APP_ROLE_GATEWAY,
         REFMEM_APP_PERSONA_A3_GATEWAY, 1u, 1u, REFMEM_APP_FAIL_STOP, 1u},
        {8u, 1u, 1u, 3u, REFMEM_APP_INSTANCE_A3_CALIBRATION,
         REFMEM_APP_ROLE_GATEWAY, REFMEM_APP_PERSONA_A3_GATEWAY,
         1u, 1u, REFMEM_APP_FAIL_STOP, 2u},
        {9u, 1u, 1u, 4u, REFMEM_APP_INSTANCE_A4_MODEL_VNA,
         REFMEM_APP_ROLE_MODEL_VNA | REFMEM_APP_ROLE_TEST_AGENT,
         REFMEM_APP_PERSONA_A4_MODEL_INSTRUMENTS, 1u, 0u,
         REFMEM_APP_FAIL_REPORT_ONLY, 0u},
        {10u, 1u, 1u, 4u, REFMEM_APP_INSTANCE_A4_MODEL_TT,
         REFMEM_APP_ROLE_MODEL_TURNTABLE | REFMEM_APP_ROLE_TEST_AGENT,
         REFMEM_APP_PERSONA_A4_MODEL_INSTRUMENTS, 1u, 0u,
         REFMEM_APP_FAIL_REPORT_ONLY, 1u},
    },
};

static const refmem_fb_instance_table_t s_fb_instance_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .instance_count = REFMEM_APP_MODEL_INSTANCE_COUNT,
    .instance = {
        {REFMEM_APP_INSTANCE_A0_REFMEM_SYNC, 0u, REFMEM_APP_DOMAIN_REFMEM, REFMEM_APP_FB_REFMEM_SYNC,
         REFMEM_APP_FB_REFMEM_SYNC, "A0.RefMemSyncFB", 1u, 1u,
         REFMEM_APP_RESOURCE_RJ45, REFMEM_APP_IO_RJ45_SYNC, 1000u,
         REFMEM_VECTOR_SLOT_GATEWAY, REFMEM_VECTOR_SLOT_STATS, 2u, 1u, 3u, 2u, 1u, 1u},
        {REFMEM_APP_INSTANCE_A0_LOOP_ENGINE, 0u, REFMEM_APP_DOMAIN_TRIG, REFMEM_APP_FB_LOOP_ENGINE,
         REFMEM_APP_FB_LOOP_ENGINE, "A0.LoopEngineAO", 1u, 1u,
         REFMEM_APP_RESOURCE_CORE1_RT, REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT,
         500u, REFMEM_VECTOR_SLOT_LOOP, REFMEM_VECTOR_SLOT_STATS, 3u, 2u, 5u, 3u, 2u, 2u},
        {REFMEM_APP_INSTANCE_A0_PULSE_COUNTER, 0u, REFMEM_APP_DOMAIN_TRIG,
         REFMEM_APP_FB_PULSE_COUNTER, REFMEM_APP_FB_PULSE_COUNTER,
         "A0.PulseCounterAO", 1u, 1u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA | REFMEM_APP_RESOURCE_CORE1_RT,
         REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT, 200u,
         REFMEM_VECTOR_SLOT_TRIGGER, REFMEM_VECTOR_SLOT_STATS, 5u, 1u, 8u, 1u, 0u, 2u},
        {REFMEM_APP_INSTANCE_A0_TRIGGER_MASTER, 0u, REFMEM_APP_DOMAIN_TRIG, REFMEM_APP_FB_TRIGGER_AO,
         REFMEM_APP_FB_TRIGGER_AO, "A0.TriggerMasterAO", 1u, 1u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA | REFMEM_APP_RESOURCE_CORE1_RT,
         REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT, 200u,
         REFMEM_VECTOR_SLOT_TRIGGER, REFMEM_VECTOR_SLOT_STATS, 5u, 1u, 8u, 1u, 3u, 2u},
        {REFMEM_APP_INSTANCE_A1_TRIGGER, 1u, REFMEM_APP_DOMAIN_TRIG, REFMEM_APP_FB_TRIGGER_AO,
         REFMEM_APP_FB_TRIGGER_AO, "A1.TriggerAO", 1u, 1u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA | REFMEM_APP_RESOURCE_CORE1_RT,
         REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT | REFMEM_APP_IO_RJ45_SYNC,
         200u, REFMEM_VECTOR_SLOT_TRIGGER, REFMEM_VECTOR_SLOT_STATS, 6u, 1u, 9u, 1u, 3u, 2u},
        {REFMEM_APP_INSTANCE_A2_LINK_SWITCH, 2u, REFMEM_APP_DOMAIN_TRIG, REFMEM_APP_FB_TRIGGER_AO,
         REFMEM_APP_FB_TRIGGER_AO, "A2.LinkSwitcherAO", 1u, 1u,
         REFMEM_APP_RESOURCE_PIO | REFMEM_APP_RESOURCE_DMA,
         REFMEM_APP_IO_LINK_CONTROL | REFMEM_APP_IO_RJ45_SYNC,
         300u, REFMEM_VECTOR_SLOT_IO, REFMEM_VECTOR_SLOT_STATS, 7u, 1u, 10u, 1u, 4u, 2u},
        {REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY, 3u, REFMEM_APP_DOMAIN_SYSTEM,
         REFMEM_APP_FB_SYSTEM_AO, REFMEM_APP_FB_SYSTEM_AO,
         "A3.SystemGatewayAO", 1u, 1u,
         REFMEM_APP_RESOURCE_FLASH | REFMEM_APP_RESOURCE_SD | REFMEM_APP_RESOURCE_USB,
         0u, 1000u, REFMEM_VECTOR_SLOT_SYSTEM, REFMEM_VECTOR_SLOT_FAULT, 0u, 0u, 11u, 1u, 0u, 2u},
        {REFMEM_APP_INSTANCE_A3_INSTRUMENT, 3u, REFMEM_APP_DOMAIN_GATEWAY,
         REFMEM_APP_FB_GATEWAY_AO, REFMEM_APP_FB_INSTRUMENT_CONTROLLER,
         "A3.InstrumentControllerAO", 1u, 1u,
         REFMEM_APP_RESOURCE_RJ45, REFMEM_APP_IO_RJ45_SYNC | REFMEM_APP_IO_UART_RS485,
         1000u, REFMEM_VECTOR_SLOT_GATEWAY, REFMEM_VECTOR_SLOT_STATS, 0u, 0u, 11u, 1u, 5u, 1u},
        {REFMEM_APP_INSTANCE_A3_CALIBRATION, 3u, REFMEM_APP_DOMAIN_CAL, REFMEM_APP_FB_CALIBRATION_AO,
         REFMEM_APP_FB_CALIBRATION_AO, "A3.CalibrationAO", 1u, 1u,
         REFMEM_APP_RESOURCE_RJ45, REFMEM_APP_IO_RJ45_SYNC,
         1000u, REFMEM_VECTOR_SLOT_CAL, REFMEM_VECTOR_SLOT_STATS, 0u, 0u, 0u, 0u, 6u, 1u},
        {REFMEM_APP_INSTANCE_A4_MODEL_VNA, 4u, REFMEM_APP_DOMAIN_MEAS, REFMEM_APP_FB_MODEL_VNA,
         REFMEM_APP_FB_MODEL_VNA, "A4.ModelVnaAO", 1u, 1u,
         REFMEM_APP_RESOURCE_USB, 0u, 1000u,
         REFMEM_VECTOR_SLOT_GATEWAY, REFMEM_VECTOR_SLOT_STATS, 0u, 0u, 0u, 0u, 7u, 1u},
        {REFMEM_APP_INSTANCE_A4_MODEL_TT, 4u, REFMEM_APP_DOMAIN_MEAS, REFMEM_APP_FB_MODEL_TURNTABLE,
         REFMEM_APP_FB_MODEL_TURNTABLE, "A4.ModelTurntableAO", 1u, 1u,
         REFMEM_APP_RESOURCE_PIO, REFMEM_APP_IO_BISS_C,
         1000u, REFMEM_VECTOR_SLOT_IO, REFMEM_VECTOR_SLOT_STATS, 0u, 0u, 0u, 0u, 8u, 1u},
    },
};

static const refmem_event_link_table_t s_event_link_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .event_link_count = REFMEM_APP_MODEL_EVENT_LINK_COUNT,
    .link = {
        {0u, REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY, REFMEM_APP_EVENT_CONFIG_STAGE, 0x0Fu,
         REFMEM_APP_INSTANCE_A0_REFMEM_SYNC, REFMEM_APP_EVENT_CONFIG_STAGE,
         REFMEM_APP_TRANSPORT_COMMAND_SLOT, 50000u, REFMEM_APP_ACK_ALL_REQUIRED, 0u, 1u, REFMEM_VECTOR_SLOT_ACK_CMD},
        {1u, REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY, REFMEM_APP_EVENT_CONFIG_ACTIVATE, 0x0Fu,
         REFMEM_APP_INSTANCE_A0_LOOP_ENGINE, REFMEM_APP_EVENT_CONFIG_ACTIVATE,
         REFMEM_APP_TRANSPORT_COMMAND_SLOT, 50000u, REFMEM_APP_ACK_ALL_REQUIRED, 0u, 1u, REFMEM_VECTOR_SLOT_ACK_CMD},
        {2u, REFMEM_APP_INSTANCE_A0_REFMEM_SYNC, REFMEM_APP_EVENT_ACK, 0x08u,
         REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY, REFMEM_APP_EVENT_ACK,
         REFMEM_APP_TRANSPORT_RJ45_SYNC_RING, 20000u, REFMEM_APP_ACK_BITMAP, 0u, 1u, REFMEM_VECTOR_SLOT_ACK_CMD},
        {3u, REFMEM_APP_INSTANCE_A0_LOOP_ENGINE, REFMEM_APP_EVENT_START, 0x0Fu,
         REFMEM_APP_INSTANCE_A0_TRIGGER_MASTER, REFMEM_APP_EVENT_START,
         REFMEM_APP_TRANSPORT_COMMAND_SLOT, 20000u, REFMEM_APP_ACK_ALL_REQUIRED, 0u, 2u, REFMEM_VECTOR_SLOT_ACK_CMD},
        {4u, REFMEM_APP_INSTANCE_A0_LOOP_ENGINE, REFMEM_APP_EVENT_STOP, 0x0Fu,
         REFMEM_APP_INSTANCE_A0_TRIGGER_MASTER, REFMEM_APP_EVENT_STOP,
         REFMEM_APP_TRANSPORT_COMMAND_SLOT, 10000u, REFMEM_APP_ACK_ALL_REQUIRED, 0u, 2u, REFMEM_VECTOR_SLOT_ACK_CMD},
        {5u, REFMEM_APP_INSTANCE_A0_LOOP_ENGINE, REFMEM_APP_EVENT_FIRE_LOAD, 0x01u,
         REFMEM_APP_INSTANCE_A0_PULSE_COUNTER, REFMEM_APP_EVENT_FIRE_LOAD,
         REFMEM_APP_TRANSPORT_CORE_IPC, 1000u, REFMEM_APP_ACK_NONE, 0u, 3u, REFMEM_VECTOR_SLOT_TRIGGER},
        {6u, REFMEM_APP_INSTANCE_A0_PULSE_COUNTER, REFMEM_APP_EVENT_DONE, 0x01u,
         REFMEM_APP_INSTANCE_A0_LOOP_ENGINE, REFMEM_APP_EVENT_DONE,
         REFMEM_APP_TRANSPORT_RJ45_SYNC_RING, 10000u, REFMEM_APP_ACK_BITMAP, 0u, 2u, REFMEM_VECTOR_SLOT_STATS},
        {7u, REFMEM_APP_INSTANCE_A0_PULSE_COUNTER, REFMEM_APP_EVENT_FAULT, 0x08u,
         REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY, REFMEM_APP_EVENT_FAULT,
         REFMEM_APP_TRANSPORT_RJ45_SYNC_RING, 10000u, REFMEM_APP_ACK_BITMAP, 0u, 3u, REFMEM_VECTOR_SLOT_FAULT},
    },
};

static const refmem_data_link_table_t s_data_link_table = {
    .version = REFMEM_APP_MODEL_VERSION,
    .data_link_count = REFMEM_APP_MODEL_DATA_LINK_COUNT,
    .link = {
        {0u, "SystemSlot.mode", REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY, 0xFFu, REFMEM_APP_DATA_ENUM,
         REFMEM_APP_UNIT_NONE, 1, 0, 8, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 10000u, 50000u, REFMEM_VECTOR_SLOT_SYSTEM,
         REFMEM_APP_PERMISSION_COMMAND_WRITE},
        {1u, "RoleSlot.node_role", REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY, 0xFFu, REFMEM_APP_DATA_BITMASK,
         REFMEM_APP_UNIT_NONE, 1, 0, 255, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 100000u, 500000u, REFMEM_VECTOR_SLOT_ROLE,
         REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE},
        {2u, "LoopSlot.active_sequence_crc", REFMEM_APP_INSTANCE_A0_LOOP_ENGINE, 0xFFu,
         REFMEM_APP_DATA_CRC, REFMEM_APP_UNIT_NONE, 1, 0, 2147483647, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_SEQLOCK, 10000u, 50000u, REFMEM_VECTOR_SLOT_LOOP,
         REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE},
        {3u, "LoopSlot.run_state", REFMEM_APP_INSTANCE_A0_LOOP_ENGINE, 0xFFu, REFMEM_APP_DATA_ENUM,
         REFMEM_APP_UNIT_NONE, 1, 0, 16, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 1000u, 10000u, REFMEM_VECTOR_SLOT_LOOP,
         REFMEM_APP_PERMISSION_COMMAND_WRITE},
        {4u, "VdcSlot.dc_time64_ns", REFMEM_APP_INSTANCE_A0_REFMEM_SYNC, 0xFFu, REFMEM_APP_DATA_NS,
         REFMEM_APP_UNIT_NS, 1, 0, 2147483647, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_SEQLOCK, 1000u, 10000u, REFMEM_VECTOR_SLOT_VDC,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {5u, "DpllSlot.lock_state", REFMEM_APP_INSTANCE_A0_REFMEM_SYNC, 0xFFu, REFMEM_APP_DATA_ENUM,
         REFMEM_APP_UNIT_NONE, 1, 0, 8, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 1000u, 10000u, REFMEM_VECTOR_SLOT_DPLL,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {6u, "CalibrationSlot.delay_crc", REFMEM_APP_INSTANCE_A3_CALIBRATION, 0xFFu,
         REFMEM_APP_DATA_CRC, REFMEM_APP_UNIT_NONE, 1, 0, 2147483647, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_SEQLOCK, 100000u, 500000u, REFMEM_VECTOR_SLOT_CAL,
         REFMEM_APP_PERMISSION_CONFIG_STAGE_WRITE},
        {7u, "AckCommandSlot.command_seq", REFMEM_APP_INSTANCE_A3_SYSTEM_GATEWAY, 0xFFu,
         REFMEM_APP_DATA_U32, REFMEM_APP_UNIT_COUNT, 1, 0, 2147483647, REFMEM_APP_LIFE_TRANSIENT,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 1000u, 50000u, REFMEM_VECTOR_SLOT_ACK_CMD,
         REFMEM_APP_PERMISSION_COMMAND_WRITE},
        {8u, "TriggerSlot.fire_seq", REFMEM_APP_INSTANCE_A0_PULSE_COUNTER, 0x0Fu, REFMEM_APP_DATA_U32,
         REFMEM_APP_UNIT_COUNT, 1, 0, 2147483647, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_SEQLOCK, 1000u, 10000u, REFMEM_VECTOR_SLOT_TRIGGER,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {9u, "TriggerSlot.node_heartbeat", REFMEM_APP_INSTANCE_A1_TRIGGER, 0x0Fu,
         REFMEM_APP_DATA_U32, REFMEM_APP_UNIT_COUNT, 1, 0, 2147483647, REFMEM_APP_LIFE_RUN,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 1000u, 10000u, REFMEM_VECTOR_SLOT_NODE,
         REFMEM_APP_PERMISSION_READ_ONLY},
        {10u, "IoSlot.link_state", REFMEM_APP_INSTANCE_A2_LINK_SWITCH, 0xFFu, REFMEM_APP_DATA_BITMASK,
         REFMEM_APP_UNIT_NONE, 1, 0, 65535, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 10000u, 50000u, REFMEM_VECTOR_SLOT_IO,
         REFMEM_APP_PERMISSION_COMMAND_WRITE},
        {11u, "GatewaySlot.instrument_state", REFMEM_APP_INSTANCE_A3_INSTRUMENT, 0xFFu,
         REFMEM_APP_DATA_ENUM, REFMEM_APP_UNIT_NONE, 1, 0, 32, REFMEM_APP_LIFE_ACTIVE,
         REFMEM_APP_SNAPSHOT_DIRECT_ATOMIC, 10000u, 50000u, REFMEM_VECTOR_SLOT_GATEWAY,
         REFMEM_APP_PERMISSION_READ_ONLY},
    },
};

static const refmem_deployment_gate_table_t s_deployment_gate = {
    .version = REFMEM_APP_MODEL_VERSION,
    .check_count = REFMEM_APP_MODEL_DEPLOYMENT_CHECK_COUNT,
    .check = {
        {REFMEM_APP_GATE_LAYOUT, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_HEADER, 0u},
        {REFMEM_APP_GATE_NODE, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_NODE, 0u},
        {REFMEM_APP_GATE_INSTANCE, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_TLV, 0u},
        {REFMEM_APP_GATE_RESOURCE, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_SERVICE, 0u},
        {REFMEM_APP_GATE_IO, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_IO, 0u},
        {REFMEM_APP_GATE_WRITER, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_TLV, 0u},
        {REFMEM_APP_GATE_EVENT, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_ACK_CMD, 0u},
        {REFMEM_APP_GATE_DATA, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_TLV, 0u},
        {REFMEM_APP_GATE_CONFIG, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_SYSTEM, 0u},
        {REFMEM_APP_GATE_CAL_SYNC, 1u, REFMEM_APP_GATE_REJECT_RUN, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_CAL, 0u},
        {REFMEM_APP_GATE_QUALITY, 1u, REFMEM_APP_GATE_LATCH_FAULT, REFMEM_APP_GATE_PASS, 0u, 0u, 0u, REFMEM_VECTOR_SLOT_STATS, 0u},
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

static refmem_application_model_snapshot_t s_snapshot;
static bool s_initialized;

static bool refmem_model_instance_enabled(const refmem_fb_instance_entry_t *instance);

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

static uint32_t refmem_model_crc32_fields(const uint32_t *fields, size_t count)
{
    return refmem_model_crc32_update(0xFFFFFFFFu, fields, count * sizeof(uint32_t));
}

static uint32_t refmem_model_application_map_crc32(void)
{
    uint32_t crc = refmem_model_crc32_update(0xFFFFFFFFu,
                                             &s_application_map,
                                             offsetof(refmem_application_map_t, node));
    for (uint32_t i = 0u; i < s_application_map.node_count; i++) {
        crc = refmem_model_crc32_update(crc,
                                        &s_application_map.node[i],
                                        sizeof(s_application_map.node[i]));
    }
    return crc;
}

static uint32_t refmem_model_node_load_crc32(void)
{
    return refmem_model_crc32_update(0xFFFFFFFFu,
                                     &s_node_load_table,
                                     sizeof(s_node_load_table));
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
            instance->resource_claim, instance->io_claim, instance->time_budget_us,
            instance->state_slot_ref, instance->health_slot_ref, instance->event_first,
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
            link->update_period_us, link->stale_window_us, link->crc_scope,
            link->permission,
        };
        crc = refmem_model_crc32_update(crc, fields, sizeof(fields));
        crc = refmem_model_crc32_string(crc, link->slot_path);
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

static bool refmem_model_instance_exists(uint32_t instance_id)
{
    return instance_id < s_fb_instance_table.instance_count &&
           s_fb_instance_table.instance[instance_id].instance_id == instance_id;
}

static const refmem_fb_instance_entry_t *refmem_model_instance_by_id(uint32_t instance_id)
{
    if (!refmem_model_instance_exists(instance_id)) {
        return NULL;
    }
    return &s_fb_instance_table.instance[instance_id];
}

static bool refmem_model_node_load_enabled(const refmem_node_load_entry_t *load)
{
    return load != NULL && load->enabled != 0u;
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

static bool refmem_model_validate_application_map(void)
{
    if (s_application_map.version != REFMEM_APP_MODEL_VERSION ||
        s_application_map.layout_version != DISTRIBUTED_REFMEM_LAYOUT_VERSION ||
        s_application_map.node_count != REFMEM_APP_MODEL_NODE_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_application_map.node_count; i++) {
        const refmem_app_node_entry_t *node = &s_application_map.node[i];
        if (node->node_id != i ||
            node->fail_policy > REFMEM_APP_FAIL_REPORT_ONLY) {
            return false;
        }
    }
    return true;
}

static bool refmem_model_validate_node_load_table(void)
{
    if (s_node_load_table.version != REFMEM_APP_MODEL_VERSION ||
        s_node_load_table.load_count != REFMEM_APP_MODEL_NODE_LOAD_COUNT) {
        return false;
    }

    uint32_t loaded_instance_mask = 0u;
    for (uint32_t i = 0u; i < s_node_load_table.load_count; i++) {
        const refmem_node_load_entry_t *load = &s_node_load_table.load[i];
        if (load->load_id != i ||
            load->application_id != s_application_map.application_id ||
            load->profile_id != s_application_map.profile_id ||
            load->node_id >= REFMEM_APP_MODEL_NODE_COUNT ||
            !refmem_model_instance_exists(load->instance_id) ||
            load->fail_policy > REFMEM_APP_FAIL_REPORT_ONLY ||
            (load->enabled > 1u) ||
            (load->required > 1u)) {
            return false;
        }

        if (!refmem_model_node_load_enabled(load)) {
            continue;
        }

        if ((s_application_map.target_node_mask & (1u << load->node_id)) == 0u) {
            return false;
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
            instance->domain > REFMEM_APP_DOMAIN_GATEWAY ||
            instance->state_slot_ref >= REFMEM_VECTOR_SLOT_COUNT ||
            instance->health_slot_ref >= REFMEM_VECTOR_SLOT_COUNT ||
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
            link->transport > REFMEM_APP_TRANSPORT_RJ45_SYNC_RING ||
            link->ack_policy > REFMEM_APP_ACK_BITMAP ||
            link->evidence_ref >= REFMEM_VECTOR_SLOT_COUNT) {
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
            link->slot_path == NULL ||
            !refmem_model_instance_exists(link->writer_instance) ||
            (link->reader_mask & ~s_application_map.target_node_mask) != 0u ||
            link->type > REFMEM_APP_DATA_CRC ||
            link->unit > REFMEM_APP_UNIT_COUNT ||
            link->lifecycle > REFMEM_APP_LIFE_EVIDENCE ||
            link->snapshot_policy > REFMEM_APP_SNAPSHOT_EVIDENCE_REF ||
            link->crc_scope >= REFMEM_VECTOR_SLOT_COUNT ||
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
            check->reject_slot >= REFMEM_VECTOR_SLOT_COUNT) {
            return false;
        }
    }

    for (uint32_t i = 0u; i < s_connection_quality.quality_count; i++) {
        const refmem_connection_quality_entry_t *quality = &s_connection_quality.quality[i];
        if (quality->quality_id != i ||
            quality->scope > REFMEM_APP_QUALITY_DATA_LINK ||
            quality->source_node >= REFMEM_APP_MODEL_NODE_COUNT ||
            quality->target_node >= REFMEM_APP_MODEL_NODE_COUNT) {
            return false;
        }
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
                                            REFMEM_APP_RESOURCE_LCD;

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

static bool refmem_model_validate_io_claims(void)
{
    const uint32_t exclusive_io_mask = REFMEM_APP_IO_LINK_CONTROL |
                                      REFMEM_APP_IO_BISS_C |
                                      REFMEM_APP_IO_UART_RS485;

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
            if (strcmp(left->slot_path, right->slot_path) == 0 &&
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
    uint32_t slot_mask = 0u;
    const uint32_t required_slot_mask = (1u << REFMEM_VECTOR_SLOT_SYSTEM) |
                                        (1u << REFMEM_VECTOR_SLOT_ROLE) |
                                        (1u << REFMEM_VECTOR_SLOT_VDC) |
                                        (1u << REFMEM_VECTOR_SLOT_LOOP) |
                                        (1u << REFMEM_VECTOR_SLOT_DPLL) |
                                        (1u << REFMEM_VECTOR_SLOT_NODE) |
                                        (1u << REFMEM_VECTOR_SLOT_TRIGGER) |
                                        (1u << REFMEM_VECTOR_SLOT_IO) |
                                        (1u << REFMEM_VECTOR_SLOT_CAL) |
                                        (1u << REFMEM_VECTOR_SLOT_ACK_CMD) |
                                        (1u << REFMEM_VECTOR_SLOT_GATEWAY);

    for (uint32_t i = 0u; i < s_data_link_table.data_link_count; i++) {
        const refmem_data_link_entry_t *link = &s_data_link_table.link[i];
        slot_mask |= (1u << link->crc_scope);
    }

    return (slot_mask & required_slot_mask) == required_slot_mask;
}

static void refmem_model_lint(uint32_t *error_count, uint32_t *first_error)
{
    *error_count = 0u;
    *first_error = REFMEM_APP_LINT_OK;

    refmem_model_lint_note(refmem_model_validate_application_map(),
                           REFMEM_APP_LINT_BAD_TABLE_VERSION,
                           error_count,
                           first_error);
    refmem_model_lint_note(refmem_model_validate_node_load_table(),
                           REFMEM_APP_LINT_BAD_NODE_RANGE,
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

bool refmem_application_model_validate(void)
{
    uint32_t error_count;
    uint32_t first_error;
    refmem_model_lint(&error_count, &first_error);
    return error_count == 0u;
}

bool refmem_application_model_init(void)
{
    s_snapshot.version = REFMEM_APP_MODEL_VERSION;
    s_snapshot.target_node_mask = s_application_map.target_node_mask;
    s_snapshot.table_mask = REFMEM_APP_TABLE_MASK_ALL;
    s_snapshot.application_map_crc32 = refmem_model_application_map_crc32();
    s_snapshot.node_load_crc32 = refmem_model_node_load_crc32();
    s_snapshot.fb_instance_crc32 = refmem_model_fb_instance_crc32();
    s_snapshot.event_link_crc32 = refmem_model_event_link_crc32();
    s_snapshot.data_link_crc32 = refmem_model_data_link_crc32();
    s_snapshot.deployment_gate_crc32 = refmem_model_deployment_gate_crc32();
    s_snapshot.connection_quality_crc32 = refmem_model_connection_quality_crc32();

    const uint32_t package_fields[] = {
        s_snapshot.version,
        s_snapshot.target_node_mask,
        s_snapshot.table_mask,
        s_snapshot.application_map_crc32,
        s_snapshot.node_load_crc32,
        s_snapshot.fb_instance_crc32,
        s_snapshot.event_link_crc32,
        s_snapshot.data_link_crc32,
        s_snapshot.deployment_gate_crc32,
        s_snapshot.connection_quality_crc32,
    };
    s_snapshot.package_crc32 =
        refmem_model_crc32_fields(package_fields,
                                  sizeof(package_fields) / sizeof(package_fields[0]));
    refmem_model_lint(&s_snapshot.lint_error_count, &s_snapshot.first_lint_error);
    s_snapshot.valid = s_snapshot.lint_error_count == 0u ? 1u : 0u;
    s_initialized = true;
    return s_snapshot.valid != 0u;
}

const refmem_application_map_t *refmem_application_model_get_application_map(void)
{
    return &s_application_map;
}

const refmem_node_load_table_t *refmem_application_model_get_node_load_table(void)
{
    return &s_node_load_table;
}

const refmem_fb_instance_table_t *refmem_application_model_get_fb_instance_table(void)
{
    return &s_fb_instance_table;
}

const refmem_event_link_table_t *refmem_application_model_get_event_link_table(void)
{
    return &s_event_link_table;
}

const refmem_data_link_table_t *refmem_application_model_get_data_link_table(void)
{
    return &s_data_link_table;
}

const refmem_deployment_gate_table_t *refmem_application_model_get_deployment_gate(void)
{
    return &s_deployment_gate;
}

const refmem_connection_quality_table_t *refmem_application_model_get_connection_quality(void)
{
    return &s_connection_quality;
}

const refmem_application_model_snapshot_t *refmem_application_model_get_snapshot(void)
{
    if (!s_initialized) {
        (void)refmem_application_model_init();
    }
    return &s_snapshot;
}
