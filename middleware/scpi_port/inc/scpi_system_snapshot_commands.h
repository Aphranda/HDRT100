#ifndef SCPI_SYSTEM_SNAPSHOT_COMMANDS_H
#define SCPI_SYSTEM_SNAPSHOT_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_refmem_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_node_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_board_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_claim_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_claim_evidence_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_load_sd(scpi_t *context);
scpi_result_t scpi_cmd_refmem_load_node(scpi_t *context);
scpi_result_t scpi_cmd_refmem_load_board(scpi_t *context);
scpi_result_t scpi_cmd_refmem_load_activate(scpi_t *context);
scpi_result_t scpi_cmd_refmem_load_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_load_board_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_table_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_table_image_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_table_view_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_quality_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_init(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_hello_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_epoch_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_delta_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_ack_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_fence_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_quality_frame_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_rx(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_mirror_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_ack_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_fence_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_quality_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_peer_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_quality_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_adapter_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_auto(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_auto_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_tdma_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_tdma_node_tx(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_tdma_tx(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_tdma_rx(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_tdma_frame_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_tdma_vdc_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_tdma_abort(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_flight_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_fifo_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_process_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_tx(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_rx_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_local(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_topology(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_loop_delay(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_loop_delay_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_arm(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_arm_status_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_train(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_train_status_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_start(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_stop(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_log(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_log_q(scpi_t *context);
scpi_result_t scpi_cmd_core_vector_q(scpi_t *context);
scpi_result_t scpi_cmd_runtime_protection_q(scpi_t *context);
scpi_result_t scpi_cmd_config_gate_status_q(scpi_t *context);
scpi_result_t scpi_cmd_config_ack_q(scpi_t *context);
scpi_result_t scpi_cmd_config_nack_reason_q(scpi_t *context);
scpi_result_t scpi_cmd_command_ack_q(scpi_t *context);
scpi_result_t scpi_cmd_command_nack_reason_q(scpi_t *context);
scpi_result_t scpi_cmd_scpi_run_allow_q(scpi_t *context);
scpi_result_t scpi_cmd_config_role_q(scpi_t *context);
scpi_result_t scpi_cmd_config_loop_q(scpi_t *context);
scpi_result_t scpi_cmd_config_action_q(scpi_t *context);
scpi_result_t scpi_cmd_config_calibration_q(scpi_t *context);
scpi_result_t scpi_cmd_system_mode_table_q(scpi_t *context);
scpi_result_t scpi_cmd_resource_arbiter_table_q(scpi_t *context);
scpi_result_t scpi_cmd_fault_code_table_q(scpi_t *context);

#define SCPI_SYSTEM_SNAPSHOT_COMMANDS \
    {.pattern = "SYSTem:CONFigure:STAT?", .callback = scpi_cmd_config_gate_status_q}, \
    {.pattern = "SYSTem:CONFigure:ROLE?", .callback = scpi_cmd_config_role_q}, \
    {.pattern = "SYSTem:CONFigure:LOOP?", .callback = scpi_cmd_config_loop_q}, \
    {.pattern = "SYSTem:CONFigure:ACT?", .callback = scpi_cmd_config_action_q}, \
    {.pattern = "SYSTem:CONFigure:CAL?", .callback = scpi_cmd_config_calibration_q}, \
    {.pattern = "SYSTem:CONFigure:ACK?", .callback = scpi_cmd_config_ack_q}, \
    {.pattern = "SYSTem:CONFigure:NACK?", .callback = scpi_cmd_config_nack_reason_q}, \
    {.pattern = "SYSTem:COMMand:ACK?", .callback = scpi_cmd_command_ack_q}, \
    {.pattern = "SYSTem:COMMand:NACK?", .callback = scpi_cmd_command_nack_reason_q}, \
    {.pattern = "SYSTem:SCPI:RUN:ALLOW?", .callback = scpi_cmd_scpi_run_allow_q}, \
    {.pattern = "SYSTem:REFMEM:STATus?", .callback = scpi_cmd_refmem_status_q}, \
    {.pattern = "SYSTem:REFMEM:NODE?", .callback = scpi_cmd_refmem_node_q}, \
    {.pattern = "SYSTem:REFMEM:BOARD?", .callback = scpi_cmd_refmem_board_q}, \
    {.pattern = "SYSTem:REFMEM:CLAIM?", .callback = scpi_cmd_refmem_claim_q}, \
    {.pattern = "SYSTem:REFMEM:CLAIM:EVIDence?", .callback = scpi_cmd_refmem_claim_evidence_q}, \
    {.pattern = "SYSTem:REFMEM:LOAD:SD", .callback = scpi_cmd_refmem_load_sd}, \
    {.pattern = "SYSTem:REFMEM:LOAD:NODE", .callback = scpi_cmd_refmem_load_node}, \
    {.pattern = "SYSTem:REFMEM:LOAD:BOARD", .callback = scpi_cmd_refmem_load_board}, \
    {.pattern = "SYSTem:REFMEM:LOAD:ACTivate", .callback = scpi_cmd_refmem_load_activate}, \
    {.pattern = "SYSTem:REFMEM:LOAD:STATus?", .callback = scpi_cmd_refmem_load_status_q}, \
    {.pattern = "SYSTem:REFMEM:LOAD:BOARD:STATus?", .callback = scpi_cmd_refmem_load_board_status_q}, \
    {.pattern = "SYSTem:REFMEM:TABle:IMAGe?", .callback = scpi_cmd_refmem_table_image_q}, \
    {.pattern = "SYSTem:REFMEM:TABle:VIEW?", .callback = scpi_cmd_refmem_table_view_q}, \
    {.pattern = "SYSTem:REFMEM:TABle?", .callback = scpi_cmd_refmem_table_q}, \
    {.pattern = "SYSTem:REFMEM:QUALity?", .callback = scpi_cmd_refmem_quality_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:INITialize", .callback = scpi_cmd_refmem_sync_init}, \
    {.pattern = "SYSTem:REFMEM:SYNC:HELLo?", .callback = scpi_cmd_refmem_sync_hello_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:EPOCh?", .callback = scpi_cmd_refmem_sync_epoch_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:DELTa?", .callback = scpi_cmd_refmem_sync_delta_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:ACK?", .callback = scpi_cmd_refmem_sync_ack_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:FENCe?", .callback = scpi_cmd_refmem_sync_fence_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:QUALity:FRAMe?", .callback = scpi_cmd_refmem_sync_quality_frame_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:RX", .callback = scpi_cmd_refmem_sync_rx}, \
    {.pattern = "SYSTem:REFMEM:SYNC:MIRRor?", .callback = scpi_cmd_refmem_sync_mirror_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:ACK:STATus?", .callback = scpi_cmd_refmem_sync_ack_status_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:FENCe:STATus?", .callback = scpi_cmd_refmem_sync_fence_status_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:QUALity:STATus?", .callback = scpi_cmd_refmem_sync_quality_status_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:PEER?", .callback = scpi_cmd_refmem_sync_peer_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:QUALity?", .callback = scpi_cmd_refmem_sync_quality_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:ADAPter?", .callback = scpi_cmd_refmem_sync_adapter_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:AUTO", .callback = scpi_cmd_refmem_sync_auto}, \
    {.pattern = "SYSTem:REFMEM:SYNC:AUTO?", .callback = scpi_cmd_refmem_sync_auto_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:STATus?", .callback = scpi_cmd_refmem_sync_tdma_status_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:NODE:TX", .callback = scpi_cmd_refmem_sync_tdma_node_tx}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:TX", .callback = scpi_cmd_refmem_sync_tdma_tx}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:RX", .callback = scpi_cmd_refmem_sync_tdma_rx}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:FRAMe?", .callback = scpi_cmd_refmem_sync_tdma_frame_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:VDC?", .callback = scpi_cmd_refmem_sync_tdma_vdc_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:ABORt", .callback = scpi_cmd_refmem_sync_tdma_abort}, \
    {.pattern = "SYSTem:REFMEM:SYNC:FLIGHT?", .callback = scpi_cmd_refmem_sync_flight_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:FIFO?", .callback = scpi_cmd_system_tdma_flight_fifo_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:PROCess?", .callback = scpi_cmd_system_tdma_flight_process_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:TX", .callback = scpi_cmd_system_tdma_flight_tx}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:RX?", .callback = scpi_cmd_system_tdma_flight_rx_q}, \
    {.pattern = "SYSTem:TDMA:RING:LOCAL", .callback = scpi_cmd_system_tdma_ring_local}, \
    {.pattern = "SYSTem:TDMA:RING:TOPology", .callback = scpi_cmd_system_tdma_ring_topology}, \
    {.pattern = "SYSTem:TDMA:RING:LOOP:DELay", .callback = scpi_cmd_system_tdma_ring_loop_delay}, \
    {.pattern = "SYSTem:TDMA:RING:LOOP:DELay?", .callback = scpi_cmd_system_tdma_ring_loop_delay_q}, \
    {.pattern = "SYSTem:TDMA:RING:ARM:STATus?", .callback = scpi_cmd_system_tdma_ring_arm_status_q}, \
    {.pattern = "SYSTem:TDMA:RING:ARM", .callback = scpi_cmd_system_tdma_ring_arm}, \
    {.pattern = "SYSTem:TDMA:RING:TRAIN", .callback = scpi_cmd_system_tdma_ring_train}, \
    {.pattern = "SYSTem:TDMA:RING:TRAIN:STATus?", .callback = scpi_cmd_system_tdma_ring_train_status_q}, \
    {.pattern = "SYSTem:TDMA:RING:START", .callback = scpi_cmd_system_tdma_ring_start}, \
    {.pattern = "SYSTem:TDMA:RING:STOP", .callback = scpi_cmd_system_tdma_ring_stop}, \
    {.pattern = "SYSTem:TDMA:RING:LOG?", .callback = scpi_cmd_system_tdma_ring_log_q}, \
    {.pattern = "SYSTem:TDMA:RING:LOG", .callback = scpi_cmd_system_tdma_ring_log}, \
    {.pattern = "SYSTem:CORE:VECTOR?", .callback = scpi_cmd_core_vector_q}, \
    {.pattern = "SYSTem:PROTection:STATus?", .callback = scpi_cmd_runtime_protection_q}, \
    {.pattern = "SYSTem:MODE:TABle?", .callback = scpi_cmd_system_mode_table_q}, \
    {.pattern = "SYSTem:RESource:TABle?", .callback = scpi_cmd_resource_arbiter_table_q}, \
    {.pattern = "SYSTem:FAULT:TABle?", .callback = scpi_cmd_fault_code_table_q}

#endif
