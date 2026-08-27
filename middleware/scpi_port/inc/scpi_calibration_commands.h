#ifndef SCPI_CALIBRATION_COMMANDS_H
#define SCPI_CALIBRATION_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_port_internal.h"

scpi_result_t scpi_calibration_link_q(scpi_t *context);
scpi_result_t scpi_calibration_parameter_q(scpi_t *context);
scpi_result_t scpi_calibration_result_q(scpi_t *context);
scpi_result_t scpi_calibration_list_q(scpi_t *context);
scpi_result_t scpi_calibration_active_q(scpi_t *context);
scpi_result_t scpi_calibration_meta_q(scpi_t *context);
scpi_result_t scpi_calibration_health_q(scpi_t *context);
scpi_result_t scpi_calibration_limit_q(scpi_t *context);
scpi_result_t scpi_calibration_loopback_start(scpi_t *context);
scpi_result_t scpi_calibration_loopback_stop(scpi_t *context);
scpi_result_t scpi_calibration_loopback_q(scpi_t *context);
scpi_result_t scpi_calibration_bias_start(scpi_t *context);
scpi_result_t scpi_calibration_bias_stop(scpi_t *context);
scpi_result_t scpi_calibration_bias_q(scpi_t *context);
scpi_result_t scpi_calibration_save(scpi_t *context);
scpi_result_t scpi_calibration_activate(scpi_t *context);
scpi_result_t scpi_calibration_rollback(scpi_t *context);
scpi_result_t scpi_calibration_clear(scpi_t *context);
scpi_result_t scpi_calibration_clk_coded_start(scpi_t *context);
scpi_result_t scpi_calibration_clk_coded_stop(scpi_t *context);
scpi_result_t scpi_calibration_clk_coded_q(scpi_t *context);
scpi_result_t scpi_calibration_marker_arm(scpi_t *context);
scpi_result_t scpi_calibration_marker_inject(scpi_t *context);
scpi_result_t scpi_calibration_marker_stop(scpi_t *context);
scpi_result_t scpi_calibration_marker_q(scpi_t *context);
scpi_result_t scpi_calibration_marker_capture_save(scpi_t *context);
scpi_result_t scpi_calibration_data_arm(scpi_t *context);
scpi_result_t scpi_calibration_data_inject(scpi_t *context);
scpi_result_t scpi_calibration_data_stop(scpi_t *context);
scpi_result_t scpi_calibration_data_q(scpi_t *context);
scpi_result_t scpi_calibration_data_capture_save(scpi_t *context);
scpi_result_t scpi_calibration_sck_arm(scpi_t *context);
scpi_result_t scpi_calibration_sck_inject(scpi_t *context);
scpi_result_t scpi_calibration_sck_stop(scpi_t *context);
scpi_result_t scpi_calibration_sck_q(scpi_t *context);
scpi_result_t scpi_calibration_sck_capture_save(scpi_t *context);
scpi_result_t scpi_calibration_ring_capture_save(scpi_t *context);
scpi_result_t scpi_calibration_ring_capture_latch(scpi_t *context);
scpi_result_t scpi_calibration_ring_capture_q(scpi_t *context);
scpi_result_t scpi_calibration_training_stage_begin(scpi_t *context);
scpi_result_t scpi_calibration_training_stage_link(scpi_t *context);
scpi_result_t scpi_calibration_training_stage_q(scpi_t *context);
scpi_result_t scpi_calibration_training_stage_link_q(scpi_t *context);
scpi_result_t scpi_calibration_training_stage_clear(scpi_t *context);
scpi_result_t scpi_calibration_topology_probe(scpi_t *context);
scpi_result_t scpi_calibration_path_candidate_begin(scpi_t *context);
scpi_result_t scpi_calibration_path_candidate_link(scpi_t *context);
scpi_result_t scpi_calibration_path_candidate_finalize(scpi_t *context);
scpi_result_t scpi_calibration_path_candidate_clear(scpi_t *context);
scpi_result_t scpi_calibration_path_candidate_q(scpi_t *context);
scpi_result_t scpi_calibration_path_candidate_link_q(scpi_t *context);
scpi_result_t scpi_calibration_path_active_q(scpi_t *context);
scpi_result_t scpi_calibration_path_rollback_q(scpi_t *context);
scpi_result_t scpi_calibration_p3_start(scpi_t *context);
scpi_result_t scpi_calibration_p3_stop(scpi_t *context);
scpi_result_t scpi_calibration_p3_q(scpi_t *context);
scpi_result_t scpi_calibration_sma_cable_phase_q(scpi_t *context);
scpi_result_t scpi_calibration_sma_cable_source_start(scpi_t *context);
scpi_result_t scpi_calibration_sma_cable_source_stop(scpi_t *context);
scpi_result_t scpi_calibration_sma_cable_validator_q(scpi_t *context);
scpi_result_t scpi_calibration_sma_cable_coarse_q(scpi_t *context);

#define SCPI_CALIBRATION_COMMANDS \
    {.pattern = "CONFigure:CALibration:LINK:ADD", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:UPDate", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:DELete", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:CLEAr", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:LINK?", .callback = scpi_calibration_link_q}, \
    {.pattern = "CALibration:STARt", .callback = scpi_calibration_result_q}, \
    {.pattern = "CALibration:LOOPback:STARt", .callback = scpi_calibration_loopback_start}, \
    {.pattern = "CALibration:LOOPback:STOP", .callback = scpi_calibration_loopback_stop}, \
    {.pattern = "READ:CALibration:LOOPback?", .callback = scpi_calibration_loopback_q}, \
    {.pattern = "CALibration:BIAS:STARt", .callback = scpi_calibration_bias_start}, \
    {.pattern = "CALibration:BIAS:STOP", .callback = scpi_calibration_bias_stop}, \
    {.pattern = "READ:CALibration:BIAS?", .callback = scpi_calibration_bias_q}, \
    {.pattern = "CALibration:CLOCk:CODEd:STARt", .callback = scpi_calibration_clk_coded_start}, \
    {.pattern = "CALibration:CLOCk:CODEd:STOP", .callback = scpi_calibration_clk_coded_stop}, \
    {.pattern = "READ:CALibration:CLOCk:CODEd?", .callback = scpi_calibration_clk_coded_q}, \
    {.pattern = "CALibration:MARKer:ARM", .callback = scpi_calibration_marker_arm}, \
    {.pattern = "CALibration:MARKer:STARt", .callback = scpi_calibration_marker_arm}, \
    {.pattern = "CALibration:MARKer:INJect", .callback = scpi_calibration_marker_inject}, \
    {.pattern = "CALibration:MARKer:STOP", .callback = scpi_calibration_marker_stop}, \
    {.pattern = "READ:CALibration:MARKer?", .callback = scpi_calibration_marker_q}, \
    {.pattern = "CALibration:MARKer:CAPTure:SAVE", .callback = scpi_calibration_marker_capture_save}, \
    {.pattern = "CALibration:DATA:ARM", .callback = scpi_calibration_data_arm}, \
    {.pattern = "CALibration:DATA:INJect", .callback = scpi_calibration_data_inject}, \
    {.pattern = "CALibration:DATA:STOP", .callback = scpi_calibration_data_stop}, \
    {.pattern = "READ:CALibration:DATA?", .callback = scpi_calibration_data_q}, \
    {.pattern = "CALibration:DATA:CAPTure:SAVE", .callback = scpi_calibration_data_capture_save}, \
    {.pattern = "CALibration:SCK:ARM", .callback = scpi_calibration_sck_arm}, \
    {.pattern = "CALibration:SCK:INJect", .callback = scpi_calibration_sck_inject}, \
    {.pattern = "CALibration:SCK:STOP", .callback = scpi_calibration_sck_stop}, \
    {.pattern = "READ:CALibration:SCK?", .callback = scpi_calibration_sck_q}, \
    {.pattern = "CALibration:SCK:CAPTure:SAVE", .callback = scpi_calibration_sck_capture_save}, \
    {.pattern = "CALibration:RING:CAPTure:SAVE", .callback = scpi_calibration_ring_capture_save}, \
    {.pattern = "CALibration:RING:CAPTure:LATCh", .callback = scpi_calibration_ring_capture_latch}, \
    {.pattern = "READ:CALibration:RING:CAPTure?", .callback = scpi_calibration_ring_capture_q}, \
    {.pattern = "CALibration:TRAINing:STAGe:BEGin", .callback = scpi_calibration_training_stage_begin}, \
    {.pattern = "CALibration:TRAINing:STAGe:LINK", .callback = scpi_calibration_training_stage_link}, \
    {.pattern = "READ:CALibration:TRAINing:STAGe?", .callback = scpi_calibration_training_stage_q}, \
    {.pattern = "READ:CALibration:TRAINing:STAGe:LINK?", .callback = scpi_calibration_training_stage_link_q}, \
    {.pattern = "CALibration:TRAINing:STAGe:CLEar", .callback = scpi_calibration_training_stage_clear}, \
    {.pattern = "CALibration:TOPology:PROBe", .callback = scpi_calibration_topology_probe}, \
    {.pattern = "CALibration:PATH:CANDidate:BEGin", .callback = scpi_calibration_path_candidate_begin}, \
    {.pattern = "CALibration:PATH:CANDidate:LINK", .callback = scpi_calibration_path_candidate_link}, \
    {.pattern = "CALibration:PATH:CANDidate:FINalize", .callback = scpi_calibration_path_candidate_finalize}, \
    {.pattern = "CALibration:PATH:CANDidate:CLEar", .callback = scpi_calibration_path_candidate_clear}, \
    {.pattern = "READ:CALibration:PATH:CANDidate?", .callback = scpi_calibration_path_candidate_q}, \
    {.pattern = "READ:CALibration:PATH:CANDidate:LINK?", .callback = scpi_calibration_path_candidate_link_q}, \
    {.pattern = "READ:CALibration:PATH:ACTive?", .callback = scpi_calibration_path_active_q}, \
    {.pattern = "READ:CALibration:PATH:ROLLback?", .callback = scpi_calibration_path_rollback_q}, \
    {.pattern = "CALibration:P3:STARt", .callback = scpi_calibration_p3_start}, \
    {.pattern = "CALibration:P3:STOP", .callback = scpi_calibration_p3_stop}, \
    {.pattern = "READ:CALibration:P3?", .callback = scpi_calibration_p3_q}, \
    {.pattern = "READ:CALibration:SMA:CABLe:PHASe?", .callback = scpi_calibration_sma_cable_phase_q}, \
    {.pattern = "CALibration:SMA:CABLe:SOURce:STARt", .callback = scpi_calibration_sma_cable_source_start}, \
    {.pattern = "CALibration:SMA:CABLe:SOURce:STOP", .callback = scpi_calibration_sma_cable_source_stop}, \
    {.pattern = "READ:CALibration:SMA:CABLe:VALidator?", .callback = scpi_calibration_sma_cable_validator_q}, \
    {.pattern = "READ:CALibration:SMA:CABLe:COARse?", .callback = scpi_calibration_sma_cable_coarse_q}, \
    {.pattern = "READ:CALibration:STATe?", .callback = scpi_calibration_result_q}, \
    {.pattern = "READ:CALibration:RESult?", .callback = scpi_calibration_result_q}, \
    {.pattern = "CONFigure:CALibration:PARameter:ADD", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:PARameter:SET", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:PARameter:DELete", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:PARameter?", .callback = scpi_calibration_parameter_q}, \
    {.pattern = "CALibration:SAVE", .callback = scpi_calibration_save}, \
    {.pattern = "CALibration:STOP", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:LOAD", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:ACTivate", .callback = scpi_calibration_activate}, \
    {.pattern = "CALibration:ROLLback", .callback = scpi_calibration_rollback}, \
    {.pattern = "CALibration:CLEAr", .callback = scpi_calibration_clear}, \
    {.pattern = "READ:CALibration:LIST?", .callback = scpi_calibration_list_q}, \
    {.pattern = "READ:CALibration:ACTive?", .callback = scpi_calibration_active_q}, \
    {.pattern = "CONFigure:CALibration:META", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:META?", .callback = scpi_calibration_meta_q}, \
    {.pattern = "CONFigure:CALibration:LIMit", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:HEALth?", .callback = scpi_calibration_health_q}, \
    {.pattern = "SYSTem:CALibration:LIMit:OVERRide", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYSTem:CALibration:LIMit:OVERRide?", .callback = scpi_calibration_limit_q}, \
    {.pattern = "SYSTem:CALibration:LIMit:DEFAult", .callback = scpi_port_result_accepted}

#endif
