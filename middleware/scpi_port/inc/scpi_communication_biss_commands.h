#ifndef SCPI_COMMUNICATION_BISS_COMMANDS_H
#define SCPI_COMMUNICATION_BISS_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_biss_anchor_bits(scpi_t *context);
scpi_result_t scpi_cmd_biss_anchor_bits_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_anchor_mask(scpi_t *context);
scpi_result_t scpi_cmd_biss_anchor_mask_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_anchor_offset(scpi_t *context);
scpi_result_t scpi_cmd_biss_anchor_offset_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_anchor_value(scpi_t *context);
scpi_result_t scpi_cmd_biss_anchor_value_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_clock(scpi_t *context);
scpi_result_t scpi_cmd_biss_clock_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_bits(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_bits_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_cover_bits(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_cover_bits_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_cover_offset(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_cover_offset_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_error(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_gate(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_gate_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_init(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_init_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_invert(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_invert_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_offset(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_offset_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_polynomial(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_polynomial_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_xor(scpi_t *context);
scpi_result_t scpi_cmd_biss_crc_xor_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_configure(scpi_t *context);
scpi_result_t scpi_cmd_biss_device(scpi_t *context);
scpi_result_t scpi_cmd_biss_device_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_error_bit(scpi_t *context);
scpi_result_t scpi_cmd_biss_error_bit_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_frame_bits(scpi_t *context);
scpi_result_t scpi_cmd_biss_frame_bits_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_frame_rx(scpi_t *context);
scpi_result_t scpi_cmd_biss_latency_offset(scpi_t *context);
scpi_result_t scpi_cmd_biss_latency_offset_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_pins_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_position_bits(scpi_t *context);
scpi_result_t scpi_cmd_biss_position_bits_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_position_modulo(scpi_t *context);
scpi_result_t scpi_cmd_biss_position_modulo_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_position_offset(scpi_t *context);
scpi_result_t scpi_cmd_biss_position_offset_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_pulse_in(scpi_t *context);
scpi_result_t scpi_cmd_biss_role(scpi_t *context);
scpi_result_t scpi_cmd_biss_role_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_delay(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_delay_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_edge(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_edge_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_scan(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_scan_end(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_scan_end_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_scan_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_scan_start(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_scan_start_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_scan_step(scpi_t *context);
scpi_result_t scpi_cmd_biss_sample_scan_step_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_status_gate(scpi_t *context);
scpi_result_t scpi_cmd_biss_status_gate_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_status_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_target(scpi_t *context);
scpi_result_t scpi_cmd_biss_target_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_timeout_inject(scpi_t *context);
scpi_result_t scpi_cmd_biss_timeout_us(scpi_t *context);
scpi_result_t scpi_cmd_biss_timeout_us_q(scpi_t *context);
scpi_result_t scpi_cmd_biss_warning_bit(scpi_t *context);
scpi_result_t scpi_cmd_biss_warning_bit_q(scpi_t *context);

#define SCPI_COMMUNICATION_BISS_COMMANDS \
    {.pattern = "COMMunication:BISS:CONFigure", .callback = scpi_cmd_biss_configure}, \
    {.pattern = "COMMunication:BISS:ROLE", .callback = scpi_cmd_biss_role}, \
    {.pattern = "COMMunication:BISS:ROLE?", .callback = scpi_cmd_biss_role_q}, \
    {.pattern = "COMMunication:BISS:DEVice", .callback = scpi_cmd_biss_device}, \
    {.pattern = "COMMunication:BISS:DEVice?", .callback = scpi_cmd_biss_device_q}, \
    {.pattern = "COMMunication:BISS:CLOCk", .callback = scpi_cmd_biss_clock}, \
    {.pattern = "COMMunication:BISS:CLOCk?", .callback = scpi_cmd_biss_clock_q}, \
    {.pattern = "COMMunication:BISS:FRAMe:BITS", .callback = scpi_cmd_biss_frame_bits}, \
    {.pattern = "COMMunication:BISS:FRAMe:BITS?", .callback = scpi_cmd_biss_frame_bits_q}, \
    {.pattern = "COMMunication:BISS:POSition:OFFSet", .callback = scpi_cmd_biss_position_offset}, \
    {.pattern = "COMMunication:BISS:POSition:OFFSet?", .callback = scpi_cmd_biss_position_offset_q}, \
    {.pattern = "COMMunication:BISS:POSition:BITS", .callback = scpi_cmd_biss_position_bits}, \
    {.pattern = "COMMunication:BISS:POSition:BITS?", .callback = scpi_cmd_biss_position_bits_q}, \
    {.pattern = "COMMunication:BISS:POSition:MODulo", .callback = scpi_cmd_biss_position_modulo}, \
    {.pattern = "COMMunication:BISS:POSition:MODulo?", .callback = scpi_cmd_biss_position_modulo_q}, \
    {.pattern = "COMMunication:BISS:SAMPle:EDGE", .callback = scpi_cmd_biss_sample_edge}, \
    {.pattern = "COMMunication:BISS:SAMPle:EDGE?", .callback = scpi_cmd_biss_sample_edge_q}, \
    {.pattern = "COMMunication:BISS:SAMPle:DELay", .callback = scpi_cmd_biss_sample_delay}, \
    {.pattern = "COMMunication:BISS:SAMPle:DELay?", .callback = scpi_cmd_biss_sample_delay_q}, \
    {.pattern = "COMMunication:BISS:SAMPle:SCAN", .callback = scpi_cmd_biss_sample_scan}, \
    {.pattern = "COMMunication:BISS:SAMPle:SCAN?", .callback = scpi_cmd_biss_sample_scan_q}, \
    {.pattern = "COMMunication:BISS:SAMPle:SCAN:STARt", .callback = scpi_cmd_biss_sample_scan_start}, \
    {.pattern = "COMMunication:BISS:SAMPle:SCAN:STARt?", .callback = scpi_cmd_biss_sample_scan_start_q}, \
    {.pattern = "COMMunication:BISS:SAMPle:SCAN:END", .callback = scpi_cmd_biss_sample_scan_end}, \
    {.pattern = "COMMunication:BISS:SAMPle:SCAN:END?", .callback = scpi_cmd_biss_sample_scan_end_q}, \
    {.pattern = "COMMunication:BISS:SAMPle:SCAN:STEP", .callback = scpi_cmd_biss_sample_scan_step}, \
    {.pattern = "COMMunication:BISS:SAMPle:SCAN:STEP?", .callback = scpi_cmd_biss_sample_scan_step_q}, \
    {.pattern = "COMMunication:BISS:TIMEout", .callback = scpi_cmd_biss_timeout_us}, \
    {.pattern = "COMMunication:BISS:TIMEout?", .callback = scpi_cmd_biss_timeout_us_q}, \
    {.pattern = "COMMunication:BISS:ANCHor:OFFSet", .callback = scpi_cmd_biss_anchor_offset}, \
    {.pattern = "COMMunication:BISS:ANCHor:OFFSet?", .callback = scpi_cmd_biss_anchor_offset_q}, \
    {.pattern = "COMMunication:BISS:ANCHor:BITS", .callback = scpi_cmd_biss_anchor_bits}, \
    {.pattern = "COMMunication:BISS:ANCHor:BITS?", .callback = scpi_cmd_biss_anchor_bits_q}, \
    {.pattern = "COMMunication:BISS:ANCHor:MASK", .callback = scpi_cmd_biss_anchor_mask}, \
    {.pattern = "COMMunication:BISS:ANCHor:MASK?", .callback = scpi_cmd_biss_anchor_mask_q}, \
    {.pattern = "COMMunication:BISS:ANCHor:VALue", .callback = scpi_cmd_biss_anchor_value}, \
    {.pattern = "COMMunication:BISS:ANCHor:VALue?", .callback = scpi_cmd_biss_anchor_value_q}, \
    {.pattern = "COMMunication:BISS:ERRor:BIT", .callback = scpi_cmd_biss_error_bit}, \
    {.pattern = "COMMunication:BISS:ERRor:BIT?", .callback = scpi_cmd_biss_error_bit_q}, \
    {.pattern = "COMMunication:BISS:WARNing:BIT", .callback = scpi_cmd_biss_warning_bit}, \
    {.pattern = "COMMunication:BISS:WARNing:BIT?", .callback = scpi_cmd_biss_warning_bit_q}, \
    {.pattern = "COMMunication:BISS:STATus:GATE", .callback = scpi_cmd_biss_status_gate}, \
    {.pattern = "COMMunication:BISS:STATus:GATE?", .callback = scpi_cmd_biss_status_gate_q}, \
    {.pattern = "COMMunication:BISS:CRC:OFFSet", .callback = scpi_cmd_biss_crc_offset}, \
    {.pattern = "COMMunication:BISS:CRC:OFFSet?", .callback = scpi_cmd_biss_crc_offset_q}, \
    {.pattern = "COMMunication:BISS:CRC:BITS", .callback = scpi_cmd_biss_crc_bits}, \
    {.pattern = "COMMunication:BISS:CRC:BITS?", .callback = scpi_cmd_biss_crc_bits_q}, \
    {.pattern = "COMMunication:BISS:CRC:COVer:OFFSet", .callback = scpi_cmd_biss_crc_cover_offset}, \
    {.pattern = "COMMunication:BISS:CRC:COVer:OFFSet?", .callback = scpi_cmd_biss_crc_cover_offset_q}, \
    {.pattern = "COMMunication:BISS:CRC:COVer:BITS", .callback = scpi_cmd_biss_crc_cover_bits}, \
    {.pattern = "COMMunication:BISS:CRC:COVer:BITS?", .callback = scpi_cmd_biss_crc_cover_bits_q}, \
    {.pattern = "COMMunication:BISS:CRC:POLYnomial", .callback = scpi_cmd_biss_crc_polynomial}, \
    {.pattern = "COMMunication:BISS:CRC:POLYnomial?", .callback = scpi_cmd_biss_crc_polynomial_q}, \
    {.pattern = "COMMunication:BISS:CRC:INIT", .callback = scpi_cmd_biss_crc_init}, \
    {.pattern = "COMMunication:BISS:CRC:INIT?", .callback = scpi_cmd_biss_crc_init_q}, \
    {.pattern = "COMMunication:BISS:CRC:XOR", .callback = scpi_cmd_biss_crc_xor}, \
    {.pattern = "COMMunication:BISS:CRC:XOR?", .callback = scpi_cmd_biss_crc_xor_q}, \
    {.pattern = "COMMunication:BISS:CRC:INVert", .callback = scpi_cmd_biss_crc_invert}, \
    {.pattern = "COMMunication:BISS:CRC:INVert?", .callback = scpi_cmd_biss_crc_invert_q}, \
    {.pattern = "COMMunication:BISS:CRC:GATE", .callback = scpi_cmd_biss_crc_gate}, \
    {.pattern = "COMMunication:BISS:CRC:GATE?", .callback = scpi_cmd_biss_crc_gate_q}, \
    {.pattern = "COMMunication:BISS:LATency:OFFSet", .callback = scpi_cmd_biss_latency_offset}, \
    {.pattern = "COMMunication:BISS:LATency:OFFSet?", .callback = scpi_cmd_biss_latency_offset_q}, \
    {.pattern = "COMMunication:BISS:TARGet", .callback = scpi_cmd_biss_target}, \
    {.pattern = "COMMunication:BISS:TARGet?", .callback = scpi_cmd_biss_target_q}, \
    {.pattern = "COMMunication:BISS:PINs?", .callback = scpi_cmd_biss_pins_q}, \
    {.pattern = "COMMunication:BISS:PULSe", .callback = scpi_cmd_biss_pulse_in}, \
    {.pattern = "COMMunication:BISS:FRAMe", .callback = scpi_cmd_biss_frame_rx}, \
    {.pattern = "COMMunication:BISS:CRC:ERRor", .callback = scpi_cmd_biss_crc_error}, \
    {.pattern = "COMMunication:BISS:TIMEout:INJect", .callback = scpi_cmd_biss_timeout_inject}, \
    {.pattern = "COMMunication:BISS:STATus?", .callback = scpi_cmd_biss_status_q}

#endif
