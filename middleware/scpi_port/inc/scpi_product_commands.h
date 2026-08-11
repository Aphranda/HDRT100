#ifndef SCPI_PRODUCT_COMMANDS_H
#define SCPI_PRODUCT_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_product_result_accepted(scpi_t *context);
scpi_result_t scpi_product_run_last_q(scpi_t *context);
scpi_result_t scpi_product_run_summary_q(scpi_t *context);
scpi_result_t scpi_product_page_block_q(scpi_t *context);
scpi_result_t scpi_product_count_zero_q(scpi_t *context);
scpi_result_t scpi_product_trigger_parameter_q(scpi_t *context);
scpi_result_t scpi_product_angle_sweep_q(scpi_t *context);
scpi_result_t scpi_product_angle_pulse_q(scpi_t *context);
scpi_result_t scpi_product_angle_position_q(scpi_t *context);
scpi_result_t scpi_product_angle_breakpoint_q(scpi_t *context);
scpi_result_t scpi_product_sequence_q(scpi_t *context);
scpi_result_t scpi_product_sequence_map_q(scpi_t *context);
scpi_result_t scpi_product_sequence_check_q(scpi_t *context);
scpi_result_t scpi_product_sequence_active_q(scpi_t *context);
scpi_result_t scpi_product_trigger_state_q(scpi_t *context);
scpi_result_t scpi_product_switch_q(scpi_t *context);
scpi_result_t scpi_product_cal_link_q(scpi_t *context);
scpi_result_t scpi_product_cal_delay_q(scpi_t *context);
scpi_result_t scpi_product_cal_result_q(scpi_t *context);
scpi_result_t scpi_product_cal_list_q(scpi_t *context);
scpi_result_t scpi_product_cal_active_q(scpi_t *context);
scpi_result_t scpi_product_cal_meta_q(scpi_t *context);
scpi_result_t scpi_product_cal_health_q(scpi_t *context);
scpi_result_t scpi_product_cal_limit_q(scpi_t *context);
scpi_result_t scpi_product_sync_state_q(scpi_t *context);
scpi_result_t scpi_product_sync_parameter_q(scpi_t *context);
scpi_result_t scpi_product_sync_health_q(scpi_t *context);
scpi_result_t scpi_product_sync_node_q(scpi_t *context);
scpi_result_t scpi_product_sync_check_q(scpi_t *context);
scpi_result_t scpi_product_sync_list_q(scpi_t *context);
scpi_result_t scpi_product_sync_active_q(scpi_t *context);
scpi_result_t scpi_product_sync_quality_q(scpi_t *context);
scpi_result_t scpi_product_sync_version_q(scpi_t *context);
scpi_result_t scpi_product_sync_override_q(scpi_t *context);
scpi_result_t scpi_product_sync_coef_q(scpi_t *context);
scpi_result_t scpi_product_permission_q(scpi_t *context);
scpi_result_t scpi_product_role_q(scpi_t *context);

#define SCPI_PRODUCT_SYSTEM_LOG_COMMANDS \
    {.pattern = "SYSTem:RUN:LAST?", .callback = scpi_product_run_last_q}, \
    {.pattern = "SYSTem:RUN:SUMMary?", .callback = scpi_product_run_summary_q}, \
    {.pattern = "SYSTem:RUN:LOG?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:LOG:PAGE?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:TRACe:DATA?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:SNAPshot:DATA?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:T2:DATA?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:FAULT:CLEAr", .callback = scpi_product_result_accepted}

#define SCPI_PRODUCT_SYSTEM_PERMISSION_COMMANDS \
    {.pattern = "SYSTem:SCPI:PERMission?", .callback = scpi_product_permission_q}, \
    {.pattern = "SYSTem:SCPI:PERMission", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYSTem:SCPI:ROLE?", .callback = scpi_product_role_q}, \
    {.pattern = "SYSTem:SCPI:ROLE", .callback = scpi_product_result_accepted}

#define SCPI_PRODUCT_BUSINESS_COMMANDS \
    {.pattern = "CONFigure:TRIGger", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:TRIGger:PARameter?", .callback = scpi_product_trigger_parameter_q}, \
    {.pattern = "CONFigure:ANGLe:SWEEp", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:ANGLe:SWEEp?", .callback = scpi_product_angle_sweep_q}, \
    {.pattern = "CONFigure:ANGLe:PULSe", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:ANGLe:PULSe?", .callback = scpi_product_angle_pulse_q}, \
    {.pattern = "READ:ANGLe:POSition?", .callback = scpi_product_angle_position_q}, \
    {.pattern = "CONFigure:ANGLe:BPOint", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:ANGLe:BPOint:CLEAr", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:ANGLe:BPOint?", .callback = scpi_product_angle_breakpoint_q}, \
    {.pattern = "CONFigure:SEQuence", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:SEQuence?", .callback = scpi_product_sequence_q}, \
    {.pattern = "READ:SEQuence:MAP?", .callback = scpi_product_sequence_map_q}, \
    {.pattern = "READ:SEQuence:CHECk?", .callback = scpi_product_sequence_check_q}, \
    {.pattern = "CONFigure:SEQuence:ACTive", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:SEQuence:ACTive?", .callback = scpi_product_sequence_active_q}, \
    {.pattern = "CONFigure:SWITch#", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:SWITch#?", .callback = scpi_product_switch_q}, \
    {.pattern = "READ:RUN:SUMMary?", .callback = scpi_product_run_summary_q}, \
    {.pattern = "READ:T2:COUNt?", .callback = scpi_product_count_zero_q}, \
    {.pattern = "READ:T2:DATA?", .callback = scpi_product_page_block_q}, \
    {.pattern = "READ:STATistics?", .callback = scpi_product_sync_quality_q}

#define SCPI_PRODUCT_CAL_SYNC_COMMANDS \
    {.pattern = "CONFigure:CALibration:LINK:ADD", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:SET", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:DELete", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:CALibration:LINK?", .callback = scpi_product_cal_link_q}, \
    {.pattern = "CALibration:STARt", .callback = scpi_product_cal_result_q}, \
    {.pattern = "READ:CALibration:STATe?", .callback = scpi_product_cal_result_q}, \
    {.pattern = "READ:CALibration:RESult?", .callback = scpi_product_cal_result_q}, \
    {.pattern = "CONFigure:CALibration:PARameter:ADD", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:CALibration:PARameter:SET", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:CALibration:PARameter:DELete", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:CALibration:PARameter?", .callback = scpi_product_cal_delay_q}, \
    {.pattern = "CALibration:SAVE", .callback = scpi_product_result_accepted}, \
    {.pattern = "CALibration:LOAD", .callback = scpi_product_result_accepted}, \
    {.pattern = "CALibration:ACTivate", .callback = scpi_product_result_accepted}, \
    {.pattern = "CALibration:ROLLback", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:CALibration:LIST?", .callback = scpi_product_cal_list_q}, \
    {.pattern = "READ:CALibration:ACTive?", .callback = scpi_product_cal_active_q}, \
    {.pattern = "CONFigure:CALibration:META", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:CALibration:META?", .callback = scpi_product_cal_meta_q}, \
    {.pattern = "CONFigure:CALibration:LIMit", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:CALibration:HEALth?", .callback = scpi_product_cal_health_q}, \
    {.pattern = "SYSTem:CALibration:LIMit:OVERRide", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYSTem:CALibration:LIMit:OVERRide?", .callback = scpi_product_cal_limit_q}, \
    {.pattern = "SYSTem:CALibration:LIMit:DEFAult", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYNC:CHECk", .callback = scpi_product_sync_check_q}, \
    {.pattern = "SYNC:STARt", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYNC:STOP", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYNC:RELock", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYNC:HOLDover", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:SYNC:STATe?", .callback = scpi_product_sync_state_q}, \
    {.pattern = "READ:SYNC:PARameter?", .callback = scpi_product_sync_parameter_q}, \
    {.pattern = "READ:SYNC:HEALth?", .callback = scpi_product_sync_health_q}, \
    {.pattern = "READ:SYNC:NODE?", .callback = scpi_product_sync_node_q}, \
    {.pattern = "READ:SYNC:LINK?", .callback = scpi_product_cal_link_q}, \
    {.pattern = "READ:SYNC:CHECk?", .callback = scpi_product_sync_check_q}, \
    {.pattern = "CONFigure:SYNC:CALibration", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:SYNC:RING", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:SYNC:DPLL", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:SYNC:GATE", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:SYNC:LIMit", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYNC:SAVE", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYNC:LOAD", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYNC:ACTivate", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYNC:ROLLback", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:SYNC:LIST?", .callback = scpi_product_sync_list_q}, \
    {.pattern = "READ:SYNC:ACTive?", .callback = scpi_product_sync_active_q}, \
    {.pattern = "READ:SYNC:QUALity?", .callback = scpi_product_sync_quality_q}, \
    {.pattern = "READ:SYNC:VERSion?", .callback = scpi_product_sync_version_q}, \
    {.pattern = "SYSTem:SYNC:DPLL:TUNE", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYSTem:SYNC:DPLL:COEFficient", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYSTem:SYNC:DPLL:OVERRide?", .callback = scpi_product_sync_override_q}, \
    {.pattern = "SYSTem:SYNC:DPLL:COEFficient?", .callback = scpi_product_sync_coef_q}, \
    {.pattern = "SYSTem:SYNC:DPLL:DEFAult", .callback = scpi_product_result_accepted}

#define SCPI_PRODUCT_TRIGGER_COMMANDS \
    {.pattern = "TRIGger:STARt", .callback = scpi_product_result_accepted}, \
    {.pattern = "TRIGger:STOP", .callback = scpi_product_result_accepted}, \
    {.pattern = "TRIGger:PAUSe", .callback = scpi_product_result_accepted}, \
    {.pattern = "TRIGger:CONTinue", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:TRIGger:STATe?", .callback = scpi_product_trigger_state_q}

#endif
