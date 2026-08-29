#include "tdma_pio_spi_phys_internal.h"

#include "board_config.h"
#include "hardware/pio.h"
#include "tdma_pio_spi.pio.h"

/* Maintenance-persona PIO program loading.  Each loader adds a complete
 * program set and rolls back already-added programs on capacity failure. */
bool tdma_pio_spi_phys_load_normal_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_tx_byte_program)) return false;
    s_tdma_pio_spi_tx_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_tx_byte_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_rx_byte_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_tx_byte_program,
                           s_tdma_pio_spi_tx_offset);
        return false;
    }
    s_tdma_pio_spi_rx_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_rx_byte_program);
    return true;
}

bool tdma_pio_spi_phys_load_coarse_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_forward_program)) return false;
    s_tdma_pio_spi_clk_forward_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_burst_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        return false;
    }
    s_tdma_pio_spi_clk_burst_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_burst_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_burst_program,
                           s_tdma_pio_spi_clk_burst_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        return false;
    }
    s_tdma_pio_spi_clk_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_capture_program);
    return true;
}

bool tdma_pio_spi_phys_load_cal_loopback_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_tx_program)) return false;
    s_tdma_pio_spi_cal_tx_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_tx_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_tx_program,
                           s_tdma_pio_spi_cal_tx_offset);
        return false;
    }
    s_tdma_pio_spi_cal_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_capture_program);
    return true;
}

bool tdma_pio_spi_phys_load_coded_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_forward_program)) return false;
    s_tdma_pio_spi_clk_forward_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_coded_tx_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        return false;
    }
    s_tdma_pio_spi_clk_coded_tx_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_coded_tx_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_oversample_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_coded_tx_program,
                           s_tdma_pio_spi_clk_coded_tx_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        return false;
    }
    s_tdma_pio_spi_clk_oversample_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_oversample_program);
    return true;
}

bool tdma_pio_spi_phys_load_marker_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_forward_program)) return false;
    s_tdma_pio_spi_marker_forward_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_origin_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_forward_program,
                           s_tdma_pio_spi_marker_forward_offset);
        return false;
    }
    s_tdma_pio_spi_marker_origin_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_origin_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_forward_program,
                           s_tdma_pio_spi_marker_forward_offset);
        return false;
    }
    s_tdma_pio_spi_marker_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_capture_program);
    return true;
}

bool tdma_pio_spi_phys_load_data_train_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_origin_program)) return false;
    s_tdma_pio_spi_marker_origin_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_origin_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_data_train_source_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        return false;
    }
    s_tdma_pio_spi_data_train_source_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_data_train_source_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_data_train_sink_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_data_train_source_program,
                           s_tdma_pio_spi_data_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        return false;
    }
    s_tdma_pio_spi_data_train_sink_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_data_train_sink_program);
    return true;
}

bool tdma_pio_spi_phys_load_sck_train_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_sck_train_trigger_program)) {
        return false;
    }
    s_tdma_pio_spi_sck_train_trigger_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_sck_train_trigger_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_sck_train_source_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_trigger_program,
                           s_tdma_pio_spi_sck_train_trigger_offset);
        return false;
    }
    s_tdma_pio_spi_sck_train_source_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_sck_train_source_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_sck_train_sink_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_source_program,
                           s_tdma_pio_spi_sck_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_trigger_program,
                           s_tdma_pio_spi_sck_train_trigger_offset);
        return false;
    }
    s_tdma_pio_spi_sck_train_sink_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_sck_train_sink_program);
    return true;
}
