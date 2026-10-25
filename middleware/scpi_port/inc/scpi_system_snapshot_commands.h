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
scpi_result_t scpi_cmd_refmem_cmd_submit(scpi_t *context);
scpi_result_t scpi_cmd_refmem_cmd_submit_type(scpi_t *context);
scpi_result_t scpi_cmd_refmem_cmd_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_cmd_drop(scpi_t *context);
scpi_result_t scpi_cmd_refmem_lease_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_idle_window(scpi_t *context);
scpi_result_t scpi_cmd_refmem_idle_window_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_config_begin(scpi_t *context);
scpi_result_t scpi_cmd_refmem_config_commit(scpi_t *context);
scpi_result_t scpi_cmd_refmem_config_abort(scpi_t *context);
scpi_result_t scpi_cmd_refmem_config_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_node_control_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_nodes_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_cmdframe_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_cmd_reply_q(scpi_t *context);
scpi_result_t scpi_cmd_ctrl_execute(scpi_t *context);
scpi_result_t scpi_cmd_ctrl_status_q(scpi_t *context);
scpi_result_t scpi_cmd_ctrl_catalog_q(scpi_t *context);
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
scpi_result_t scpi_cmd_refmem_sync_tdma_vdc_rx_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_tdma_vdc_flight_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_vdc_feedback_rx_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_vdc_feedback_tx_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_vdc_reference_proof_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_session(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_session_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_sync(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_sync_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_match(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_match_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_match_status_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_status_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_health_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_delay(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_delay_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_delay_default(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_delay_recall(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_delay_store(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_timing(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_timing_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_timing_default(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_timing_recall(scpi_t *context);
scpi_result_t scpi_cmd_vdc_output_timing_store(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_config(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_config_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_enable(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_enable_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_status_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_default(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_recall(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_store(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_discipline(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_discipline_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_discipline_config(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_discipline_config_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_discipline_active_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_discipline_default(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_discipline_recall(scpi_t *context);
scpi_result_t scpi_cmd_vdc_reference_discipline_store(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_baseline(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_baseline_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_baseline_default(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_baseline_recall(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_baseline_store(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_trace_arm(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_trace_phase_arm(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_trace_summary_phase_arm(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_trace_summary_origin_arm(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_guard_phase_arm(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_guard_origin_arm(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_guard_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_phase(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_phase_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_follow_phase_status_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_trace_origin_arm(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_trace_stop(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_trace_release(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_trace_status_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_trace_read_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_tx_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_tx_gap(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_tx_gap_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_tx_gap_status_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_probe(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_auto(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_auto_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_reference(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_reference_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_boundary_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_local_follow(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_local_follow_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_model_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_fixed_output(scpi_t *context);
#include "scpi_trigger_grid_commands.h"
#include "scpi_trigger_cursor_commands.h"
#include "scpi_trigger_deployment_commands.h"
scpi_result_t scpi_cmd_vdc_run_output(scpi_t *context);
scpi_result_t scpi_cmd_vdc_run_output_stop(scpi_t *context);
scpi_result_t scpi_cmd_vdc_run_output_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_fixed_output_stop(scpi_t *context);
scpi_result_t scpi_cmd_vdc_fixed_output_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_subdivision_output(scpi_t *context);
scpi_result_t scpi_cmd_vdc_subdivision_output_stop(scpi_t *context);
scpi_result_t scpi_cmd_vdc_subdivision_output_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_subdivision_window(scpi_t *context);
scpi_result_t scpi_cmd_vdc_subdivision_window_stop(scpi_t *context);
scpi_result_t scpi_cmd_vdc_subdivision_window_q(scpi_t *context);
#include "scpi_vdc_output_slices.h"
#include "scpi_action_probe.h"
#include "scpi_trigger_counter_commands.h"
#include "scpi_trigger_gateway_commands.h"
#include "scpi_trigger_switch_commands.h"
#include "scpi_trigger_action_commands.h"
#include "scpi_trigger_action_execute.h"
#include "scpi_trigger_template_commands.h"
#include "scpi_trigger_seat_commands.h"
#include "scpi_trigger_t2_runtime_commands.h"
scpi_result_t scpi_cmd_vdc_feedback_bridge_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_feedback_match_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_tdma_abort(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_flight_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_sync_flight_position_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_fifo_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_priority_rx_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_priority_rx_budget_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_priority_rx_timing_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_priority_rx_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_priority_rx_record_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_priority_rx_consumer_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_fifo_reset(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_mode(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_seat_rotation(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_seat_rotation_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_seat_counters_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_seat_health_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_mode_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_clock_evidence(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_clock_evidence_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_clock_evidence_status_q(
    scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_load_mask(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_load_mask_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_schedule_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_period(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_period_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_profile_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_profile_peak_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_profile_run_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_profile_other_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_profile_steady_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_profile_rx_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_rx_start_cut_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_profile_reset(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_crc_diagnostic_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_process_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_tx(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_flight_rx_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_local(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_topology(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_loop_delay(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_loop_delay_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_diagnostic(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_diagnostic_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_origin_cadence_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_burst(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_burst_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_burst_status_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_geometry(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_geometry_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_arm(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_arm_status_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_status_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_config_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_record_arm(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_record_status_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_record_cancel(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_record_save(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_record_read_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_train(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_train_status_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_start(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_stop(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_log(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_ring_log_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_long_frame_tx(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_long_frame_rx_q(scpi_t *context);
scpi_result_t scpi_cmd_system_tdma_long_frame_status_q(scpi_t *context);
scpi_result_t scpi_cmd_core_vector_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_vdc_vector_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_vdc_priority_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_dpll_vector_q(scpi_t *context);
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
    {.pattern = "SYSTem:REFMEM:CMD:SUBmit", .callback = scpi_cmd_refmem_cmd_submit}, \
    {.pattern = "SYSTem:REFMEM:CMD:SUBmit:TYPe", .callback = scpi_cmd_refmem_cmd_submit_type}, \
    {.pattern = "SYSTem:REFMEM:CMD:STATus?", .callback = scpi_cmd_refmem_cmd_status_q}, \
{.pattern = "SYSTem:REFMEM:CMD:DROP", .callback = scpi_cmd_refmem_cmd_drop}, \
{.pattern = "SYSTem:REFMEM:LEASE?", .callback = scpi_cmd_refmem_lease_q}, \
{.pattern = "SYSTem:REFMEM:IDLE:WINDow", .callback = scpi_cmd_refmem_idle_window}, \
{.pattern = "SYSTem:REFMEM:IDLE:WINDow?", .callback = scpi_cmd_refmem_idle_window_q}, \
{.pattern = "SYSTem:REFMEM:CONFig:BEGIN", .callback = scpi_cmd_refmem_config_begin}, \
{.pattern = "SYSTem:REFMEM:CONFig:COMMit", .callback = scpi_cmd_refmem_config_commit}, \
{.pattern = "SYSTem:REFMEM:CONFig:ABORt", .callback = scpi_cmd_refmem_config_abort}, \
{.pattern = "SYSTem:REFMEM:CONFig:STATus?", .callback = scpi_cmd_refmem_config_status_q}, \
{.pattern = "SYSTem:REFMEM:NODE:CONTrol?", .callback = scpi_cmd_refmem_node_control_q}, \
{.pattern = "SYSTem:REFMEM:NODES?", .callback = scpi_cmd_refmem_nodes_q}, \
{.pattern = "SYSTem:REFMEM:CMDFRame?", .callback = scpi_cmd_refmem_cmdframe_q}, \
{.pattern = "SYSTem:REFMEM:CMD:REPLy?", .callback = scpi_cmd_refmem_cmd_reply_q}, \
{.pattern = "SYSTem:CTRL:EXECute", .callback = scpi_cmd_ctrl_execute}, \
{.pattern = "SYSTem:CTRL:STATus?", .callback = scpi_cmd_ctrl_status_q}, \
{.pattern = "SYSTem:CTRL:CATalog?", .callback = scpi_cmd_ctrl_catalog_q}, \
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
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:VDC:RX?", .callback = scpi_cmd_refmem_sync_tdma_vdc_rx_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:VDC:FLIGHT?", .callback = scpi_cmd_refmem_sync_tdma_vdc_flight_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:VDC:FEEDback:RX?", .callback = scpi_cmd_refmem_vdc_feedback_rx_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:VDC:FEEDback:TX?", .callback = scpi_cmd_refmem_vdc_feedback_tx_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:VDC:FEEDback:PROof?", .callback = scpi_cmd_refmem_vdc_reference_proof_q}, \
    {.pattern = "SYSTem:VDC:FEEDback:SESSion", .callback = scpi_cmd_vdc_feedback_session}, \
    {.pattern = "SYSTem:VDC:FEEDback:SESSion?", .callback = scpi_cmd_vdc_feedback_session_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:SYNC", .callback = scpi_cmd_vdc_priority_sync}, \
    {.pattern = "SYSTem:VDC:PRIORity:SYNC?", .callback = scpi_cmd_vdc_priority_sync_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:MATCH", .callback = scpi_cmd_vdc_priority_match}, \
    {.pattern = "SYSTem:VDC:PRIORity:MATCH?", .callback = scpi_cmd_vdc_priority_match_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:MATCH:STATus?", .callback = scpi_cmd_vdc_priority_match_status_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow", .callback = scpi_cmd_vdc_priority_follow}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow?", .callback = scpi_cmd_vdc_priority_follow_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:STATus?", .callback = scpi_cmd_vdc_priority_follow_status_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:HEALth?", .callback = scpi_cmd_vdc_priority_follow_health_q}, \
    {.pattern = "SYSTem:VDC:OUTPut:DELay", .callback = scpi_cmd_vdc_output_delay}, \
    {.pattern = "SYSTem:VDC:OUTPut:DELay?", .callback = scpi_cmd_vdc_output_delay_q}, \
    {.pattern = "SYSTem:VDC:OUTPut:DELay:DEFAult", .callback = scpi_cmd_vdc_output_delay_default}, \
    {.pattern = "SYSTem:VDC:OUTPut:DELay:RECall", .callback = scpi_cmd_vdc_output_delay_recall}, \
    {.pattern = "SYSTem:VDC:OUTPut:DELay:STORe", .callback = scpi_cmd_vdc_output_delay_store}, \
    {.pattern = "SYSTem:VDC:OUTPut:TIMing", .callback = scpi_cmd_vdc_output_timing}, \
    {.pattern = "SYSTem:VDC:OUTPut:TIMing?", .callback = scpi_cmd_vdc_output_timing_q}, \
    {.pattern = "SYSTem:VDC:OUTPut:TIMing:DEFAult", .callback = scpi_cmd_vdc_output_timing_default}, \
    {.pattern = "SYSTem:VDC:OUTPut:TIMing:RECall", .callback = scpi_cmd_vdc_output_timing_recall}, \
    {.pattern = "SYSTem:VDC:OUTPut:TIMing:STORe", .callback = scpi_cmd_vdc_output_timing_store}, \
    {.pattern = "SYSTem:VDC:REFerence:CONFigure", .callback = scpi_cmd_vdc_reference_config}, \
    {.pattern = "SYSTem:VDC:REFerence:CONFigure?", .callback = scpi_cmd_vdc_reference_config_q}, \
    {.pattern = "SYSTem:VDC:REFerence:ENABle", .callback = scpi_cmd_vdc_reference_enable}, \
    {.pattern = "SYSTem:VDC:REFerence:ENABle?", .callback = scpi_cmd_vdc_reference_enable_q}, \
    {.pattern = "SYSTem:VDC:REFerence:STATus?", .callback = scpi_cmd_vdc_reference_status_q}, \
    {.pattern = "SYSTem:VDC:REFerence:DEFAult", .callback = scpi_cmd_vdc_reference_default}, \
    {.pattern = "SYSTem:VDC:REFerence:RECall", .callback = scpi_cmd_vdc_reference_recall}, \
    {.pattern = "SYSTem:VDC:REFerence:STORe", .callback = scpi_cmd_vdc_reference_store}, \
    {.pattern = "SYSTem:VDC:REFerence:DISCipline", .callback = scpi_cmd_vdc_reference_discipline}, \
    {.pattern = "SYSTem:VDC:REFerence:DISCipline?", .callback = scpi_cmd_vdc_reference_discipline_q}, \
    {.pattern = "SYSTem:VDC:REFerence:DISCipline:CONFigure", .callback = scpi_cmd_vdc_reference_discipline_config}, \
    {.pattern = "SYSTem:VDC:REFerence:DISCipline:CONFigure?", .callback = scpi_cmd_vdc_reference_discipline_config_q}, \
    {.pattern = "SYSTem:VDC:REFerence:DISCipline:ACTive?", .callback = scpi_cmd_vdc_reference_discipline_active_q}, \
    {.pattern = "SYSTem:VDC:REFerence:DISCipline:DEFAult", .callback = scpi_cmd_vdc_reference_discipline_default}, \
    {.pattern = "SYSTem:VDC:REFerence:DISCipline:RECall", .callback = scpi_cmd_vdc_reference_discipline_recall}, \
    {.pattern = "SYSTem:VDC:REFerence:DISCipline:STORe", .callback = scpi_cmd_vdc_reference_discipline_store}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:BASEline", .callback = scpi_cmd_vdc_priority_follow_baseline}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:BASEline?", .callback = scpi_cmd_vdc_priority_follow_baseline_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:BASEline:DEFAult", .callback = scpi_cmd_vdc_priority_follow_baseline_default}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:BASEline:RECall", .callback = scpi_cmd_vdc_priority_follow_baseline_recall}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:BASEline:STORe", .callback = scpi_cmd_vdc_priority_follow_baseline_store}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:ARM", .callback = scpi_cmd_vdc_priority_trace_arm}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:PHASe", .callback = scpi_cmd_vdc_priority_trace_phase_arm}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:SUMMary:PHASe", .callback = scpi_cmd_vdc_priority_trace_summary_phase_arm}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:SUMMary:ORIGin", .callback = scpi_cmd_vdc_priority_trace_summary_origin_arm}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:GUARd:PHASe", .callback = scpi_cmd_vdc_priority_guard_phase_arm}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:GUARd:ORIGin", .callback = scpi_cmd_vdc_priority_guard_origin_arm}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:GUARd?", .callback = scpi_cmd_vdc_priority_guard_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:PHASe", .callback = scpi_cmd_vdc_priority_follow_phase}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:PHASe?", .callback = scpi_cmd_vdc_priority_follow_phase_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:FOLLow:PHASe:STATus?", .callback = scpi_cmd_vdc_priority_follow_phase_status_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:ORIGin", .callback = scpi_cmd_vdc_priority_trace_origin_arm}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:STOP", .callback = scpi_cmd_vdc_priority_trace_stop}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:RELease", .callback = scpi_cmd_vdc_priority_trace_release}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:STATus?", .callback = scpi_cmd_vdc_priority_trace_status_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:TRACe:READ?", .callback = scpi_cmd_vdc_priority_trace_read_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:TX?", .callback = scpi_cmd_vdc_priority_tx_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:TX:GAP", .callback = scpi_cmd_vdc_priority_tx_gap}, \
    {.pattern = "SYSTem:VDC:PRIORity:TX:GAP?", .callback = scpi_cmd_vdc_priority_tx_gap_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:TX:GAP:STATus?", .callback = scpi_cmd_vdc_priority_tx_gap_status_q}, \
    {.pattern = "SYSTem:VDC:PRIORity:RX?", .callback = scpi_cmd_vdc_priority_rx_q}, \
    {.pattern = "SYSTem:VDC:FEEDback:PROBe", .callback = scpi_cmd_vdc_feedback_probe}, \
    {.pattern = "SYSTem:VDC:FEEDback:AUTO", .callback = scpi_cmd_vdc_feedback_auto}, \
    {.pattern = "SYSTem:VDC:FEEDback:AUTO?", .callback = scpi_cmd_vdc_feedback_auto_q}, \
    {.pattern = "SYSTem:VDC:FEEDback:REFerence", .callback = scpi_cmd_vdc_feedback_reference}, \
    {.pattern = "SYSTem:VDC:FEEDback:REFerence?", .callback = scpi_cmd_vdc_feedback_reference_q}, \
    {.pattern = "SYSTem:VDC:FEEDback:BOUNDary?", .callback = scpi_cmd_vdc_feedback_boundary_q}, \
    {.pattern = "SYSTem:VDC:FEEDback:LOCALfollow", .callback = scpi_cmd_vdc_feedback_local_follow}, \
    {.pattern = "SYSTem:VDC:FEEDback:LOCALfollow?", .callback = scpi_cmd_vdc_feedback_local_follow_q}, \
    {.pattern = "SYSTem:VDC:FEEDback:MODel?", .callback = scpi_cmd_vdc_feedback_model_q}, \
    {.pattern = "TRIGger:REServation:DEPloyment:STAGe", .callback = scpi_cmd_trigger_deployment_stage}, \
    {.pattern = "TRIGger:REServation:DEPloyment:ACTivate", .callback = scpi_cmd_trigger_deployment_activate}, \
    {.pattern = "TRIGger:REServation:DEPloyment:CLEar", .callback = scpi_cmd_trigger_deployment_clear}, \
    {.pattern = "TRIGger:REServation:DEPloyment?", .callback = scpi_cmd_trigger_deployment_q}, \
    {.pattern = "TRIGger:REServation:DEPloyment:PLAN?", .callback = scpi_cmd_trigger_deployment_plan_q}, \
    {.pattern = "TRIGger:REServation:GRID:CONFigure", .callback = scpi_cmd_trigger_grid_configure}, \
    {.pattern = "TRIGger:REServation:GRID:CLEar", .callback = scpi_cmd_trigger_grid_clear}, \
    {.pattern = "TRIGger:REServation:GRID?", .callback = scpi_cmd_trigger_grid_q}, \
    {.pattern = "TRIGger:REServation:GRID:PLAN?", .callback = scpi_cmd_trigger_grid_plan_q}, \
    {.pattern = "TRIGger:REServation:GRID:ANCHor", .callback = scpi_cmd_trigger_grid_anchor}, \
    {.pattern = "TRIGger:REServation:GRID:ANCHor?", .callback = scpi_cmd_trigger_grid_anchor_q}, \
    {.pattern = "TRIGger:REServation:GRID:ANCHor:PLAN?", .callback = scpi_cmd_trigger_grid_anchor_plan_q}, \
    {.pattern = "TRIGger:REServation:CURSor?", .callback = scpi_cmd_trigger_cursor_q}, \
    {.pattern = "TRIGger:REServation:TIMing?", .callback = scpi_cmd_trigger_timing_q}, \
    {.pattern = "TRIGger:REServation:SOURce:CONFigure", .callback = scpi_cmd_trigger_sim_source_configure}, \
    {.pattern = "TRIGger:REServation:SOURce:STARt", .callback = scpi_cmd_trigger_sim_source_start}, \
    {.pattern = "TRIGger:REServation:SOURce:STOP", .callback = scpi_cmd_trigger_sim_source_stop}, \
    {.pattern = "TRIGger:REServation:SOURce:RELease", .callback = scpi_cmd_trigger_sim_source_release}, \
    {.pattern = "TRIGger:REServation:SOURce?", .callback = scpi_cmd_trigger_sim_source_q}, \
    {.pattern = "SYSTem:VDC:OUTPut:FIXed", .callback = scpi_cmd_vdc_fixed_output}, \
    {.pattern = "SYSTem:VDC:OUTPut:RUN", .callback = scpi_cmd_vdc_run_output}, \
    {.pattern = "SYSTem:VDC:OUTPut:RUN:STOP", .callback = scpi_cmd_vdc_run_output_stop}, \
    {.pattern = "SYSTem:VDC:OUTPut:RUN?", .callback = scpi_cmd_vdc_run_output_q}, \
    {.pattern = "SYSTem:VDC:OUTPut:FIXed:STOP", .callback = scpi_cmd_vdc_fixed_output_stop}, \
    {.pattern = "SYSTem:VDC:OUTPut:FIXed?", .callback = scpi_cmd_vdc_fixed_output_q}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision", .callback = scpi_cmd_vdc_subdivision_output}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision:STOP", .callback = scpi_cmd_vdc_subdivision_output_stop}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision?", .callback = scpi_cmd_vdc_subdivision_output_q}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision:WINdow", .callback = scpi_cmd_vdc_subdivision_window}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision:WINdow:STOP", .callback = scpi_cmd_vdc_subdivision_window_stop}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision:WINdow?", .callback = scpi_cmd_vdc_subdivision_window_q}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision:AFTER:STATus?", .callback = scpi_cmd_vdc_subdivision_after_sync_q}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision:AFTER:ENABle", .callback = scpi_cmd_vdc_subdivision_after_sync}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision:AFTER:CONFigure", .callback = scpi_cmd_vdc_subdivision_after_sync_configure}, \
    {.pattern = "SYSTem:VDC:OUTPut:SUBDivision:AFTER:RUN?", .callback = scpi_cmd_vdc_subdivision_after_sync_run_q}, \
    {.pattern = "TRIGger:REServation:PROBe:CONFigure", .callback = scpi_cmd_action_probe_configure}, \
    {.pattern = "TRIGger:REServation:PROBe:SEND", .callback = scpi_cmd_action_probe_send}, \
    {.pattern = "TRIGger:REServation:PROBe:CLEar", .callback = scpi_cmd_action_probe_clear}, \
    {.pattern = "TRIGger:REServation:PROBe?", .callback = scpi_cmd_action_probe_q}, \
    {.pattern = "TRIGger:REServation:COUNter:LOAD", .callback = scpi_cmd_trigger_counter_load}, \
    {.pattern = "TRIGger:REServation:COUNter:STARt", .callback = scpi_cmd_trigger_counter_start}, \
    {.pattern = "TRIGger:REServation:COUNter:STOP", .callback = scpi_cmd_trigger_counter_stop}, \
    {.pattern = "TRIGger:REServation:COUNter:CLEar", .callback = scpi_cmd_trigger_counter_clear}, \
    {.pattern = "TRIGger:REServation:COUNter:UNLoad", .callback = scpi_cmd_trigger_counter_unload}, \
    {.pattern = "TRIGger:REServation:COUNter?", .callback = scpi_cmd_trigger_counter_q}, \
    {.pattern = "TRIGger:REServation:GATEway:LOAD", .callback = scpi_cmd_trigger_gateway_load}, \
    {.pattern = "TRIGger:REServation:GATEway:STOP", .callback = scpi_cmd_trigger_gateway_stop}, \
    {.pattern = "TRIGger:REServation:GATEway:UNLoad", .callback = scpi_cmd_trigger_gateway_unload}, \
    {.pattern = "TRIGger:REServation:GATEway?", .callback = scpi_cmd_trigger_gateway_q}, \
    {.pattern = "TRIGger:REServation:SWITch:LOAD", .callback = scpi_cmd_trigger_switch_load}, \
    {.pattern = "TRIGger:REServation:SWITch:STAGe", .callback = scpi_cmd_trigger_switch_stage}, \
    {.pattern = "TRIGger:REServation:SWITch:APPLy", .callback = scpi_cmd_trigger_switch_apply}, \
    {.pattern = "TRIGger:REServation:SWITch:STOP", .callback = scpi_cmd_trigger_switch_stop}, \
    {.pattern = "TRIGger:REServation:SWITch:UNLoad", .callback = scpi_cmd_trigger_switch_unload}, \
    {.pattern = "TRIGger:REServation:SWITch?", .callback = scpi_cmd_trigger_switch_q}, \
    {.pattern = "TRIGger:REServation:ACTion:INSTall", .callback = scpi_cmd_trigger_action_install}, \
    {.pattern = "TRIGger:REServation:ACTion:CLEar", .callback = scpi_cmd_trigger_action_clear}, \
    {.pattern = "TRIGger:REServation:ACTion?", .callback = scpi_cmd_trigger_action_q}, \
    {.pattern = "TRIGger:REServation:ACTion:ROW?", .callback = scpi_cmd_trigger_action_row_q}, \
    {.pattern = "TRIGger:REServation:ACTion:EXECute:PREPare", .callback = scpi_cmd_trigger_action_prepare}, \
    {.pattern = "TRIGger:REServation:ACTion:EXECute:ARM", .callback = scpi_cmd_trigger_action_arm}, \
    {.pattern = "TRIGger:REServation:ACTion:EXECute:STOP", .callback = scpi_cmd_trigger_action_stop}, \
    {.pattern = "TRIGger:REServation:ACTion:EXECute:RELease", .callback = scpi_cmd_trigger_action_release}, \
    {.pattern = "TRIGger:REServation:ACTion:EXECute?", .callback = scpi_cmd_trigger_action_execute_q}, \
    {.pattern = "TRIGger:REServation:ACTion:EXECute:NOW?", .callback = scpi_cmd_trigger_action_now_q}, \
    {.pattern = "TRIGger:REServation:TEMPLate:INSTall", .callback = scpi_cmd_trigger_template_install}, \
    {.pattern = "TRIGger:REServation:TEMPLate:CLEar", .callback = scpi_cmd_trigger_template_clear}, \
    {.pattern = "TRIGger:REServation:TEMPLate:ACTivate", .callback = scpi_cmd_trigger_template_activate}, \
    {.pattern = "TRIGger:REServation:TEMPLate:RELease", .callback = scpi_cmd_trigger_template_release}, \
    {.pattern = "TRIGger:REServation:TEMPLate?", .callback = scpi_cmd_trigger_template_q}, \
{.pattern = "TRIGger:REServation:SEAT:MOUNT", .callback = scpi_cmd_trigger_seat_mount}, \
    {.pattern = "TRIGger:REServation:SEAT:MOUNT?", .callback = scpi_cmd_trigger_seat_mount_q}, \
    {.pattern = "TRIGger:REServation:SEAT:CLEar", .callback = scpi_cmd_trigger_seat_clear}, \
        {.pattern = "TRIGger:REServation:T2:CONFigure", .callback = scpi_cmd_trigger_t2_configure}, \
    {.pattern = "TRIGger:REServation:T2:MOUNT", .callback = scpi_cmd_trigger_t2_start}, \
    {.pattern = "TRIGger:REServation:T2:UNMOUNT", .callback = scpi_cmd_trigger_t2_stop}, \
    {.pattern = "TRIGger:REServation:T2?", .callback = scpi_cmd_trigger_t2_q}, \
    {.pattern = "TRIGger:REServation:T2:CURSor?", .callback = scpi_cmd_trigger_t2_cursor_q}, \
    {.pattern = "TRIGger:REServation:T2:ACTion?", .callback = scpi_cmd_trigger_t2_action_q}, \
    {.pattern = "TRIGger:REServation:T2:ACTion:WHY?", .callback = scpi_cmd_trigger_t2_action_why_q}, \
    {.pattern = "TRIGger:REServation:T2:ACTion:WINdow?", .callback = scpi_cmd_trigger_t2_action_window_q}, \
    {.pattern = "TRIGger:REServation:T2:ACTion:DIAG?", .callback = scpi_cmd_trigger_t2_action_diag_q}, \
{.pattern = "TRIGger:REServation:T2:ACTion:ARMBranch?", .callback = scpi_cmd_trigger_t2_action_arm_branch_q}, \
    {.pattern = "TRIGger:REServation:T2:ACTion:GWDIAG?", .callback = scpi_cmd_trigger_t2_gateway_diag_q}, \
    {.pattern = "TRIGger:REServation:T2:SCAN?", .callback = scpi_cmd_trigger_t2_scan_q}, \
    {.pattern = "TRIGger:REServation:T2:ANNOUNCE", .callback = scpi_cmd_trigger_t2_announce}, \
    {.pattern = "TRIGger:REServation:T2:ANNOUNCE?", .callback = scpi_cmd_trigger_t2_announce_q}, \
    {.pattern = "TRIGger:RESERVation:T2:DRAIN", .callback = scpi_cmd_trigger_t2_drain}, \
    {.pattern = "TRIGger:RESERVation:T2:DRAIN?", .callback = scpi_cmd_trigger_t2_drain_q}, \
    {.pattern = "TRIGger:SEQuence:TIMing", .callback = scpi_cmd_trigger_sequence_timing}, \
    {.pattern = "TRIGger:SEQuence:TIMing?", .callback = scpi_cmd_trigger_sequence_timing_q}, \
    {.pattern = "TRIGger:SEQuence:COUNts", .callback = scpi_cmd_trigger_sequence_counts}, \
    {.pattern = "TRIGger:SEQuence:COUNts?", .callback = scpi_cmd_trigger_sequence_counts_q}, \
    {.pattern = "TRIGger:SEQuence:PERiod?", .callback = scpi_cmd_trigger_sequence_period_q}, \
    {.pattern = "TRIGger:SEQuence:REAdy", .callback = scpi_cmd_trigger_sequence_ready}, \
{.pattern = "TRIGger:REServation:T2:SEQ:PILot", .callback = scpi_cmd_trigger_t2_seq_pilot}, \
{.pattern = "TRIGger:REServation:T2:SEQ:MODE", .callback = scpi_cmd_trigger_t2_seq_mode}, \
{.pattern = "TRIGger:REServation:T2:SEQ:MODE?", .callback = scpi_cmd_trigger_t2_seq_mode_q}, \
{.pattern = "TRIGger:REServation:T2:SEQ:PILot?", .callback = scpi_cmd_trigger_t2_seq_pilot_q}, \
    {.pattern = "TRIGger:SEQuence:REAdy?", .callback = scpi_cmd_trigger_sequence_ready_q}, \
    {.pattern = "TRIGger:SEQuence:CODE", .callback = scpi_cmd_trigger_sequence_code}, \
    {.pattern = "TRIGger:SEQuence:CODE?", .callback = scpi_cmd_trigger_sequence_code_q}, \
    {.pattern = "CONFigure:SEQuence:REPeat", .callback = scpi_cmd_trigger_sequence_repeat}, \
    {.pattern = "TRIGger:SEQuence:REPeat?", .callback = scpi_cmd_trigger_sequence_repeat_q}, \
    {.pattern = "SYSTem:VDC:FEEDback:BRIDge?", .callback = scpi_cmd_vdc_feedback_bridge_q}, \
    {.pattern = "SYSTem:VDC:FEEDback:MATCh?", .callback = scpi_cmd_vdc_feedback_match_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:TDMA:ABORt", .callback = scpi_cmd_refmem_sync_tdma_abort}, \
    {.pattern = "SYSTem:REFMEM:SYNC:FLIGHT?", .callback = scpi_cmd_refmem_sync_flight_q}, \
    {.pattern = "SYSTem:REFMEM:SYNC:FLIGHT:POSition?", .callback = scpi_cmd_refmem_sync_flight_position_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:FIFO?", .callback = scpi_cmd_system_tdma_flight_fifo_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:PRIORity?", .callback = scpi_cmd_system_tdma_priority_rx_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:PRIORity:BUDGet?", .callback = scpi_cmd_system_tdma_priority_rx_budget_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:PRIORity:TIMing?", .callback = scpi_cmd_system_tdma_priority_rx_timing_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:PRIORity:RECord?", .callback = scpi_cmd_system_tdma_priority_rx_record_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:PRIORity:CONSumed?", .callback = scpi_cmd_system_tdma_priority_rx_consumer_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:FIFO:RESet", .callback = scpi_cmd_system_tdma_flight_fifo_reset}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:MODE?", .callback = scpi_cmd_system_tdma_flight_mode_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:MODE", .callback = scpi_cmd_system_tdma_flight_mode}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:CLOCK:EVIDence?", .callback = scpi_cmd_system_tdma_flight_clock_evidence_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:CLOCK:EVIDence", .callback = scpi_cmd_system_tdma_flight_clock_evidence}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:CLOCK:EVIDence:STATus?", .callback = scpi_cmd_system_tdma_flight_clock_evidence_status_q}, \
    {.pattern = "SYSTem:TDMA:SEAT:ROTation", .callback = scpi_cmd_system_tdma_seat_rotation}, \
    {.pattern = "SYSTem:TDMA:SEAT:ROTation?", .callback = scpi_cmd_system_tdma_seat_rotation_q}, \
    {.pattern = "SYSTem:TDMA:SEAT:COUNters?", .callback = scpi_cmd_system_tdma_seat_counters_q}, \
    {.pattern = "SYSTem:TDMA:SEAT:HEALth?", .callback = scpi_cmd_system_tdma_seat_health_q}, \
    {.pattern = "SYSTem:TDMA:LOAD:MASK?", .callback = scpi_cmd_system_tdma_load_mask_q}, \
    {.pattern = "SYSTem:TDMA:LOAD:MASK", .callback = scpi_cmd_system_tdma_load_mask}, \
    {.pattern = "SYSTem:TDMA:SCHEDule?", .callback = scpi_cmd_system_tdma_schedule_q}, \
    {.pattern = "SYSTem:TDMA:PERiod", .callback = scpi_cmd_system_tdma_period}, \
    {.pattern = "SYSTem:TDMA:PERiod?", .callback = scpi_cmd_system_tdma_period_q}, \
    {.pattern = "SYSTem:TDMA:PROFile?", .callback = scpi_cmd_system_tdma_profile_q}, \
    {.pattern = "SYSTem:TDMA:PROFile:PEAK?", .callback = scpi_cmd_system_tdma_profile_peak_q}, \
    {.pattern = "SYSTem:TDMA:PROFile:RUN?", .callback = scpi_cmd_system_tdma_profile_run_q}, \
    {.pattern = "SYSTem:TDMA:PROFile:OTHer?", .callback = scpi_cmd_system_tdma_profile_other_q}, \
    {.pattern = "SYSTem:TDMA:PROFile:STEady?", .callback = scpi_cmd_system_tdma_profile_steady_q}, \
    {.pattern = "SYSTem:TDMA:PROFile:RX?", .callback = scpi_cmd_system_tdma_profile_rx_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:RX:CUT?", .callback = scpi_cmd_system_tdma_rx_start_cut_q}, \
    {.pattern = "SYSTem:TDMA:PROFile:RESet", .callback = scpi_cmd_system_tdma_profile_reset}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:CRC:DIAGnostic?", .callback = scpi_cmd_system_tdma_flight_crc_diagnostic_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:PROCess?", .callback = scpi_cmd_system_tdma_flight_process_q}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:TX", .callback = scpi_cmd_system_tdma_flight_tx}, \
    {.pattern = "SYSTem:TDMA:FLIGHT:RX?", .callback = scpi_cmd_system_tdma_flight_rx_q}, \
    {.pattern = "SYSTem:TDMA:RING:LOCAL", .callback = scpi_cmd_system_tdma_ring_local}, \
    {.pattern = "SYSTem:TDMA:RING:TOPology", .callback = scpi_cmd_system_tdma_ring_topology}, \
    {.pattern = "SYSTem:TDMA:RING:LOOP:DELay", .callback = scpi_cmd_system_tdma_ring_loop_delay}, \
    {.pattern = "SYSTem:TDMA:RING:LOOP:DELay?", .callback = scpi_cmd_system_tdma_ring_loop_delay_q}, \
    {.pattern = "SYSTem:TDMA:RING:DIAGnostic", .callback = scpi_cmd_system_tdma_ring_diagnostic}, \
    {.pattern = "SYSTem:TDMA:RING:DIAGnostic?", .callback = scpi_cmd_system_tdma_ring_diagnostic_q}, \
    {.pattern = "SYSTem:TDMA:RING:ORIGin:CADEnce?", .callback = scpi_cmd_system_tdma_ring_origin_cadence_q}, \
    {.pattern = "SYSTem:TDMA:RING:BURSt", .callback = scpi_cmd_system_tdma_ring_burst}, \
    {.pattern = "SYSTem:TDMA:RING:BURSt?", .callback = scpi_cmd_system_tdma_ring_burst_q}, \
    {.pattern = "SYSTem:TDMA:RING:BURSt:STATus?", .callback = scpi_cmd_system_tdma_ring_burst_status_q}, \
    {.pattern = "SYSTem:TDMA:RING:GEOMetry", .callback = scpi_cmd_system_tdma_ring_geometry}, \
    {.pattern = "SYSTem:TDMA:RING:GEOMetry?", .callback = scpi_cmd_system_tdma_ring_geometry_q}, \
    {.pattern = "SYSTem:TDMA:RING:ARM:STATus?", .callback = scpi_cmd_system_tdma_ring_arm_status_q}, \
    {.pattern = "SYSTem:TDMA:RING:STATus?", .callback = scpi_cmd_system_tdma_ring_status_q}, \
    {.pattern = "SYSTem:TDMA:RING:CONFig?", .callback = scpi_cmd_system_tdma_ring_config_q}, \
    {.pattern = "SYSTem:TDMA:RECord:ARM", .callback = scpi_cmd_system_tdma_record_arm}, \
    {.pattern = "SYSTem:TDMA:RECord:STATus?", .callback = scpi_cmd_system_tdma_record_status_q}, \
    {.pattern = "SYSTem:TDMA:RECord:CANCel", .callback = scpi_cmd_system_tdma_record_cancel}, \
    {.pattern = "SYSTem:TDMA:RECord:SAVE", .callback = scpi_cmd_system_tdma_record_save}, \
    {.pattern = "SYSTem:TDMA:RECord:READ?", .callback = scpi_cmd_system_tdma_record_read_q}, \
    {.pattern = "SYSTem:TDMA:RING:ARM", .callback = scpi_cmd_system_tdma_ring_arm}, \
    {.pattern = "SYSTem:TDMA:RING:TRAIN", .callback = scpi_cmd_system_tdma_ring_train}, \
    {.pattern = "SYSTem:TDMA:RING:TRAIN:STATus?", .callback = scpi_cmd_system_tdma_ring_train_status_q}, \
    {.pattern = "SYSTem:TDMA:RING:STARt", .callback = scpi_cmd_system_tdma_ring_start}, \
    {.pattern = "SYSTem:TDMA:RING:STOP", .callback = scpi_cmd_system_tdma_ring_stop}, \
    {.pattern = "SYSTem:TDMA:RING:LOG?", .callback = scpi_cmd_system_tdma_ring_log_q}, \
    {.pattern = "SYSTem:TDMA:RING:LOG", .callback = scpi_cmd_system_tdma_ring_log}, \
    {.pattern = "SYSTem:TDMA:LONGframe:TX", .callback = scpi_cmd_system_tdma_long_frame_tx}, \
    {.pattern = "SYSTem:TDMA:LONGframe:RX?", .callback = scpi_cmd_system_tdma_long_frame_rx_q}, \
    {.pattern = "SYSTem:TDMA:LONGframe:STATus?", .callback = scpi_cmd_system_tdma_long_frame_status_q}, \
    {.pattern = "SYSTem:CORE:VECTOR?", .callback = scpi_cmd_core_vector_q}, \
    {.pattern = "SYSTem:REFMEM:VDC:VECtor?", .callback = scpi_cmd_refmem_vdc_vector_q}, \
    {.pattern = "SYSTem:REFMEM:VDC:PRIORity?", .callback = scpi_cmd_refmem_vdc_priority_q}, \
    {.pattern = "SYSTem:REFMEM:DPLL:VECtor?", .callback = scpi_cmd_refmem_dpll_vector_q}, \
    {.pattern = "SYSTem:PROTection:STATus?", .callback = scpi_cmd_runtime_protection_q}, \
    {.pattern = "SYSTem:MODE:TABle?", .callback = scpi_cmd_system_mode_table_q}, \
    {.pattern = "SYSTem:RESource:TABle?", .callback = scpi_cmd_resource_arbiter_table_q}, \
    {.pattern = "SYSTem:FAULT:TABle?", .callback = scpi_cmd_fault_code_table_q}

#endif
