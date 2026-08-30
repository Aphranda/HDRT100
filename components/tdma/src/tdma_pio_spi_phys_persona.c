#include "tdma_pio_spi_phys_internal.h"

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "resource_arbiter.h"
#include "sync_io.h"
#include "tdma_state_machine_resources.h"
#include "tdma_pio_spi.pio.h"

static bool s_tdma_pio_spi_sms_claimed;
static bool s_tdma_pio_spi_flight_sms_claimed;
static bool s_tdma_pio_spi_maintenance_resources_claimed;
static tdma_pio_spi_role_t s_tdma_pio_spi_program_role;
static const char *const TDMA_FLIGHT_RESOURCE_OWNER = "TDMA_FLIGHT_PIO";
static const char *const TDMA_MAINTENANCE_RESOURCE_OWNER =
    "TDMA_MAINTENANCE_PIO";

bool tdma_pio_spi_phys_ensure_sms_claimed(void)
{
    if (s_tdma_pio_spi_sms_claimed) return true;
    if (!resource_arbiter_acquire_owned(
            TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK,
            TDMA_MAINTENANCE_RESOURCE_OWNER)) {
        return false;
    }
    s_tdma_pio_spi_maintenance_resources_claimed = true;
    if (pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM) ||
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM) ||
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM) ||
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM)) {
        resource_arbiter_release_owned(
            TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK,
            TDMA_MAINTENANCE_RESOURCE_OWNER);
        s_tdma_pio_spi_maintenance_resources_claimed = false;
        return false;
    }
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
    s_tdma_pio_spi_sms_claimed = true;
    return true;
}

/* Maintenance and flight both use the RX PIO block on this board, but they
 * use different logical personas.  Claims are therefore transferred at the
 * persona boundary; leaving the maintenance claims resident would make a
 * stopped NORMAL persona look like a flight resource conflict. */
void tdma_pio_spi_phys_release_sms_claimed(void)
{
    if (!s_tdma_pio_spi_sms_claimed) {
        return;
    }
    const uint sms[] = {
        BOARD_TDMA_SPI_MASTER_SM,
        BOARD_TDMA_SPI_SLAVE_SM,
        BOARD_TDMA_SPI_CAPTURE_SM,
        BOARD_TDMA_SPI_RTT_SM,
    };
    for (size_t i = 0u; i < sizeof(sms) / sizeof(sms[0]); ++i) {
        pio_sm_unclaim(BOARD_TDMA_SPI_PIO, sms[i]);
    }
    s_tdma_pio_spi_sms_claimed = false;
    if (s_tdma_pio_spi_maintenance_resources_claimed) {
        resource_arbiter_release_owned(
            TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK,
            TDMA_MAINTENANCE_RESOURCE_OWNER);
        s_tdma_pio_spi_maintenance_resources_claimed = false;
    }
}
/* Flight resources are claimed per PIO block, rather than by the legacy
 * maintenance SM pair.  Claim all declared roles up front so a later
 * persona cannot silently steal the evidence or capture endpoint. */
bool tdma_pio_spi_phys_ensure_flight_sms_claimed(void)
{
    if (s_tdma_pio_spi_flight_sms_claimed) {
        return true;
    }
    const uint tx_sms[] = {
        BOARD_TDMA_TX_CLK_OUT_SM,
        BOARD_TDMA_TX_SYNC_OUT_SM,
        BOARD_TDMA_TX_DATA_IN_FORWARD_SM,
        BOARD_TDMA_TX_DATA_IN_CAPTURE_SM,
    };
    const uint rx_sms[] = {
        BOARD_TDMA_RX_CLK_IN_SM,
        BOARD_TDMA_RX_SYNC_IN_SM,
        BOARD_TDMA_RX_DATA_OUT_SM,
        BOARD_TDMA_RX_EVIDENCE_IN_SM,
    };
    for (size_t i = 0u; i < sizeof(tx_sms) / sizeof(tx_sms[0]); ++i) {
        if (pio_sm_is_claimed(BOARD_TDMA_TX_PIO, tx_sms[i])) {
            return false;
        }
    }
    for (size_t i = 0u; i < sizeof(rx_sms) / sizeof(rx_sms[0]); ++i) {
        if (pio_sm_is_claimed(BOARD_TDMA_RX_PIO, rx_sms[i])) {
            return false;
        }
    }
    for (size_t i = 0u; i < sizeof(tx_sms) / sizeof(tx_sms[0]); ++i) {
        pio_sm_claim(BOARD_TDMA_TX_PIO, tx_sms[i]);
    }
    for (size_t i = 0u; i < sizeof(rx_sms) / sizeof(rx_sms[0]); ++i) {
        pio_sm_claim(BOARD_TDMA_RX_PIO, rx_sms[i]);
    }
    s_tdma_pio_spi_flight_sms_claimed = true;
    return true;
}

bool tdma_pio_spi_phys_flight_sms_claimed(void)
{
    return s_tdma_pio_spi_flight_sms_claimed;
}

static bool tdma_pio_spi_phys_load_flight_clock_latch_program(PIO pio)
{
    if (pio == NULL || !pio_can_add_program(
            pio, &tdma_pio_spi_flight_clock_latch_program)) {
        return false;
    }
    s_tdma_pio_spi_flight_clock_latch_offset = (uint)pio_add_program(
        pio, &tdma_pio_spi_flight_clock_latch_program);
    return true;
}

static bool tdma_pio_spi_phys_load_flight_origin_programs(void)
{
    /* TX and RX flight controllers own separate PIO blocks. The DATA SM is
     * also the RX unload endpoint; do not install a second DATA sampler on
     * the opposite block because that would remux the same physical input. */
    const PIO tx_pio = BOARD_TDMA_TX_PIO;
    const PIO rx_pio = BOARD_TDMA_RX_PIO;
    if (!tdma_pio_spi_phys_ensure_flight_sms_claimed()) {
        return false;
    }
    if (!pio_can_add_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_clock_rx_program)) {
        return false;
    }
    s_tdma_pio_spi_flight_origin_clock_offset = (uint)pio_add_program(
        tx_pio, &tdma_pio_spi_flight_origin_clock_rx_program);
    if (!pio_can_add_program(
            rx_pio,
            &tdma_pio_spi_flight_origin_data_tx_program)) {
        pio_remove_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        return false;
    }
    s_tdma_pio_spi_flight_origin_data_offset = (uint)pio_add_program(
        rx_pio, &tdma_pio_spi_flight_origin_data_tx_program);
    if (!pio_can_add_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_rtt_program)) {
        pio_remove_program(
            rx_pio,
            &tdma_pio_spi_flight_origin_data_tx_program,
            s_tdma_pio_spi_flight_origin_data_offset);
        pio_remove_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        return false;
    }
    s_tdma_pio_spi_flight_origin_rtt_offset = (uint)pio_add_program(
        tx_pio, &tdma_pio_spi_flight_origin_rtt_program);
    if (!tdma_pio_spi_phys_load_flight_clock_latch_program(tx_pio)) {
        pio_remove_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_rtt_program,
            s_tdma_pio_spi_flight_origin_rtt_offset);
        pio_remove_program(
            rx_pio,
            &tdma_pio_spi_flight_origin_data_tx_program,
            s_tdma_pio_spi_flight_origin_data_offset);
        pio_remove_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        return false;
    }
    return true;
}

static bool tdma_pio_spi_phys_load_flight_follower_programs(void)
{
    const PIO tx_pio = BOARD_TDMA_TX_PIO;
    const PIO rx_pio = BOARD_TDMA_RX_PIO;
    if (!tdma_pio_spi_phys_ensure_flight_sms_claimed() ||
        !pio_can_add_program(tx_pio,
                             &tdma_pio_spi_flight_control_forward_program)) {
        return false;
    }
    s_tdma_pio_spi_flight_control_forward_offset = (uint)pio_add_program(
        tx_pio,
        &tdma_pio_spi_flight_control_forward_program);
    if (!pio_can_add_program(rx_pio,
                             &tdma_pio_spi_flight_data_follower_program)) {
        pio_remove_program(tx_pio,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        return false;
    }
    s_tdma_pio_spi_flight_data_follower_offset = (uint)pio_add_program(
        rx_pio,
        &tdma_pio_spi_flight_data_follower_program);
    if (!tdma_pio_spi_phys_load_flight_clock_latch_program(rx_pio)) {
        pio_remove_program(rx_pio,
                           &tdma_pio_spi_flight_data_follower_program,
                           s_tdma_pio_spi_flight_data_follower_offset);
        pio_remove_program(tx_pio,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        return false;
    }
    return true;
}

static bool tdma_pio_spi_phys_load_flight_process_follower_programs(void)
{
    const PIO tx_pio = BOARD_TDMA_TX_PIO;
    const PIO rx_pio = BOARD_TDMA_RX_PIO;
    if (!tdma_pio_spi_phys_ensure_flight_sms_claimed() ||
        !pio_can_add_program(tx_pio,
                             &tdma_pio_spi_flight_control_forward_program)) {
        return false;
    }
    s_tdma_pio_spi_flight_control_forward_offset = (uint)pio_add_program(
        tx_pio,
        &tdma_pio_spi_flight_control_forward_program);
    if (!pio_can_add_program(
            rx_pio,
            &tdma_pio_spi_flight_process_follower_program)) {
        pio_remove_program(tx_pio,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        return false;
    }
    s_tdma_pio_spi_flight_process_follower_offset = (uint)pio_add_program(
        rx_pio,
        &tdma_pio_spi_flight_process_follower_program);
    if (!tdma_pio_spi_phys_load_flight_clock_latch_program(rx_pio)) {
        pio_remove_program(
            rx_pio,
            &tdma_pio_spi_flight_process_follower_program,
            s_tdma_pio_spi_flight_process_follower_offset);
        pio_remove_program(tx_pio,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        return false;
    }
    return true;
}

static bool tdma_pio_spi_phys_load_p3_initiator_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_initiator_program)) return false;
    s_tdma_pio_spi_p3_initiator_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_initiator_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        return false;
    }
    s_tdma_pio_spi_p3_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_p3_responder_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_responder_program)) return false;
    s_tdma_pio_spi_p3_responder_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_responder_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_responder_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        return false;
    }
    s_tdma_pio_spi_p3_responder_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_responder_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_p3_reference_programs(void)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_initiator_program)) return false;
    s_tdma_pio_spi_p3_initiator_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_initiator_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_p3_responder_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        return false;
    }
    s_tdma_pio_spi_p3_responder_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_p3_responder_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        return false;
    }
    s_tdma_pio_spi_p3_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_programs(
    tdma_pio_spi_program_persona_t persona)
{
    switch (persona) {
    case TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL:
        return tdma_pio_spi_phys_load_normal_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE:
        return tdma_pio_spi_phys_load_coarse_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CAL_LOOPBACK:
        return tdma_pio_spi_phys_load_cal_loopback_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE:
        return tdma_pio_spi_phys_load_p3_reference_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED:
        return tdma_pio_spi_phys_load_coded_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR:
        return tdma_pio_spi_phys_load_p3_initiator_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER:
        return tdma_pio_spi_phys_load_p3_responder_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR:
        return tdma_pio_spi_phys_load_p3_initiator_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER:
        return tdma_pio_spi_phys_load_p3_responder_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER:
        return tdma_pio_spi_phys_load_marker_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN:
        return tdma_pio_spi_phys_load_data_train_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN:
        return tdma_pio_spi_phys_load_sck_train_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN:
        return tdma_pio_spi_phys_load_flight_origin_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER:
        return tdma_pio_spi_phys_load_flight_follower_programs();
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER:
        return tdma_pio_spi_phys_load_flight_process_follower_programs();
    default:
        return false;
    }
}

static void tdma_pio_spi_phys_unload_programs(void)
{
    switch (s_tdma_pio_spi_program_persona) {
    case TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_rx_byte_program,
                           s_tdma_pio_spi_rx_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_tx_byte_program,
                           s_tdma_pio_spi_tx_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_capture_program,
                           s_tdma_pio_spi_clk_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_burst_program,
                           s_tdma_pio_spi_clk_burst_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CAL_LOOPBACK:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_capture_program,
                           s_tdma_pio_spi_cal_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_tx_program,
                           s_tdma_pio_spi_cal_tx_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_capture_program,
                           s_tdma_pio_spi_p3_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_oversample_program,
                           s_tdma_pio_spi_clk_oversample_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_coded_tx_program,
                           s_tdma_pio_spi_clk_coded_tx_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           s_tdma_pio_spi_clk_forward_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_capture_program,
                           s_tdma_pio_spi_p3_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_capture_program,
                           s_tdma_pio_spi_p3_responder_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_capture_program,
                           s_tdma_pio_spi_p3_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_initiator_program,
                           s_tdma_pio_spi_p3_initiator_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_capture_program,
                           s_tdma_pio_spi_p3_responder_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_p3_responder_program,
                           s_tdma_pio_spi_p3_responder_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_capture_program,
                           s_tdma_pio_spi_marker_capture_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_forward_program,
                           s_tdma_pio_spi_marker_forward_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_data_train_sink_program,
                           s_tdma_pio_spi_data_train_sink_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_data_train_source_program,
                           s_tdma_pio_spi_data_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           s_tdma_pio_spi_marker_origin_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN:
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_sink_program,
                           s_tdma_pio_spi_sck_train_sink_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_source_program,
                           s_tdma_pio_spi_sck_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_trigger_program,
                           s_tdma_pio_spi_sck_train_trigger_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN:
        pio_remove_program(
            BOARD_TDMA_TX_PIO,
            &tdma_pio_spi_flight_clock_latch_program,
            s_tdma_pio_spi_flight_clock_latch_offset);
        pio_remove_program(
            BOARD_TDMA_TX_PIO,
            &tdma_pio_spi_flight_origin_rtt_program,
            s_tdma_pio_spi_flight_origin_rtt_offset);
        pio_remove_program(
            BOARD_TDMA_RX_PIO,
            &tdma_pio_spi_flight_origin_data_tx_program,
            s_tdma_pio_spi_flight_origin_data_offset);
        pio_remove_program(
            BOARD_TDMA_TX_PIO,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER:
        pio_remove_program(
            BOARD_TDMA_RX_PIO,
            &tdma_pio_spi_flight_clock_latch_program,
            s_tdma_pio_spi_flight_clock_latch_offset);
        pio_remove_program(BOARD_TDMA_RX_PIO,
                           &tdma_pio_spi_flight_data_follower_program,
                           s_tdma_pio_spi_flight_data_follower_offset);
        pio_remove_program(BOARD_TDMA_TX_PIO,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        break;
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER:
        pio_remove_program(
            BOARD_TDMA_RX_PIO,
            &tdma_pio_spi_flight_clock_latch_program,
            s_tdma_pio_spi_flight_clock_latch_offset);
        pio_remove_program(
            BOARD_TDMA_RX_PIO,
            &tdma_pio_spi_flight_process_follower_program,
            s_tdma_pio_spi_flight_process_follower_offset);
        pio_remove_program(BOARD_TDMA_TX_PIO,
                           &tdma_pio_spi_flight_control_forward_program,
                           s_tdma_pio_spi_flight_control_forward_offset);
        break;
    default:
        break;
    }
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
}

bool tdma_pio_spi_phys_select_program_persona(
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona)
{
    if (phys == NULL || persona <= TDMA_PIO_SPI_PROGRAM_PERSONA_NONE ||
        persona > TDMA_PIO_SPI_PROGRAM_PERSONA_MAX) {
        return false;
    }
    s_tdma_pio_spi_program_role = phys->role;
    const bool flight_persona =
        persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
        persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER ||
        persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
    const uint32_t maintenance_sm_mask =
        (1u << BOARD_TDMA_SPI_MASTER_SM) |
        (1u << BOARD_TDMA_SPI_SLAVE_SM) |
        (1u << BOARD_TDMA_SPI_CAPTURE_SM) |
        (1u << BOARD_TDMA_SPI_RTT_SM);
    const uint32_t flight_tx_sm_mask =
        (1u << BOARD_TDMA_TX_CLK_OUT_SM) |
        (1u << BOARD_TDMA_TX_SYNC_OUT_SM) |
        (1u << BOARD_TDMA_TX_DATA_IN_FORWARD_SM) |
        (1u << BOARD_TDMA_TX_DATA_IN_CAPTURE_SM);
    const uint32_t flight_rx_sm_mask =
        (1u << BOARD_TDMA_RX_CLK_IN_SM) |
        (1u << BOARD_TDMA_RX_SYNC_IN_SM) |
        (1u << BOARD_TDMA_RX_DATA_OUT_SM) |
        (1u << BOARD_TDMA_RX_EVIDENCE_IN_SM);
    const bool current_flight_persona =
        tdma_pio_spi_phys_is_flight_persona();
    const bool current_sm_busy = current_flight_persona
        ? ((BOARD_TDMA_TX_PIO->ctrl & flight_tx_sm_mask) != 0u ||
           (BOARD_TDMA_RX_PIO->ctrl & flight_rx_sm_mask) != 0u)
        : ((BOARD_TDMA_SPI_PIO->ctrl & maintenance_sm_mask) != 0u);
    if (current_sm_busy ||
        (s_tdma_pio_spi_tx_dma_channel >= 0 &&
         dma_channel_is_busy((uint)s_tdma_pio_spi_tx_dma_channel)) ||
        (s_tdma_pio_spi_rx_dma_channel >= 0 &&
         dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel))) {
        tdma_pio_spi_phys_set_error(
            phys, TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY);
        phys->snapshot.program_switch_fail_count++;
        return false;
    }
    if (s_tdma_pio_spi_program_persona == persona) {
        const bool claimed = flight_persona
            ? (s_tdma_pio_spi_flight_sms_claimed &&
               phys->flight_resource_claimed)
            : s_tdma_pio_spi_sms_claimed;
        if (!claimed) {
            /* A selected persona must never expose claimed SMs without the
             * matching arbiter owner.  ARM calls this path before its
             * idempotent resource check, so taking the claim here closes the
             * transition window for direct persona users as well. */
            const bool resources_claimed_before =
                phys->flight_resource_claimed;
            const bool resource_ok = flight_persona
                ? tdma_pio_spi_phys_claim_flight_resources(phys)
                : true;
            const bool sm_ok = resource_ok &&
                (flight_persona
                     ? tdma_pio_spi_phys_ensure_flight_sms_claimed()
                     : tdma_pio_spi_phys_ensure_sms_claimed());
            if (!sm_ok) {
                if (flight_persona && !resources_claimed_before) {
                    tdma_pio_spi_phys_release_flight_resources(phys);
                }
                tdma_pio_spi_phys_set_error(
                    phys, TDMA_PIO_SPI_PHYS_ERROR_PERSONA_RESOURCE);
                phys->snapshot.program_switch_fail_count++;
                return false;
            }
        }
        phys->snapshot.program_persona = (uint32_t)persona;
        return true;
    }
    const tdma_pio_spi_program_persona_t previous =
        s_tdma_pio_spi_program_persona;
    bool sync_wave_suspended = false;
    if (flight_persona) {
        if (!sync_io_suspend_wave_for_tdma()) {
            tdma_pio_spi_phys_set_error(
                phys, TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY);
            phys->snapshot.program_switch_fail_count++;
            return false;
        }
        sync_wave_suspended = true;
    }
    tdma_pio_spi_phys_unload_programs();
    if (current_flight_persona && !flight_persona) {
        tdma_pio_spi_phys_release_flight_resources(phys);
        (void)sync_io_resume_wave_after_tdma();
    } else if (!current_flight_persona) {
        /* A previous failed maintenance transition may have left the claim
         * bit set after its program set was already unloaded. */
        tdma_pio_spi_phys_release_sms_claimed();
    }
    const bool target_claimed = flight_persona
        ? (tdma_pio_spi_phys_claim_flight_resources(phys) &&
           tdma_pio_spi_phys_ensure_flight_sms_claimed())
        : tdma_pio_spi_phys_ensure_sms_claimed();
    if (!target_claimed) {
        if (sync_wave_suspended) {
            (void)sync_io_resume_wave_after_tdma();
        }
        if (flight_persona) {
            /* claim_flight_resources() may have succeeded before a PIO SM
             * claim failed; release both halves before restoring the prior
             * persona. */
            tdma_pio_spi_phys_release_flight_resources(phys);
        }
        /* Restore the old claim before attempting the old program set. */
        if (current_flight_persona) {
            (void)tdma_pio_spi_phys_claim_flight_resources(phys);
            (void)tdma_pio_spi_phys_ensure_flight_sms_claimed();
        } else if (previous != TDMA_PIO_SPI_PROGRAM_PERSONA_NONE) {
            (void)tdma_pio_spi_phys_ensure_sms_claimed();
        }
        if (previous != TDMA_PIO_SPI_PROGRAM_PERSONA_NONE) {
            (void)tdma_pio_spi_phys_load_programs(previous);
            s_tdma_pio_spi_program_persona = previous;
        }
        tdma_pio_spi_phys_set_error(
            phys, TDMA_PIO_SPI_PHYS_ERROR_PERSONA_RESOURCE);
        phys->snapshot.program_switch_fail_count++;
        phys->snapshot.program_persona =
            (uint32_t)s_tdma_pio_spi_program_persona;
        return false;
    }
    if (!tdma_pio_spi_phys_load_programs(persona)) {
        if (sync_wave_suspended) {
            (void)sync_io_resume_wave_after_tdma();
        }
        tdma_pio_spi_phys_set_error(
            phys, TDMA_PIO_SPI_PHYS_ERROR_PROGRAM_LOAD);
        phys->snapshot.program_switch_fail_count++;
        if (flight_persona) {
            tdma_pio_spi_phys_release_flight_resources(phys);
        } else {
            tdma_pio_spi_phys_release_sms_claimed();
        }
        if (previous != TDMA_PIO_SPI_PROGRAM_PERSONA_NONE &&
            (current_flight_persona
                 ? tdma_pio_spi_phys_claim_flight_resources(phys) &&
                   tdma_pio_spi_phys_ensure_flight_sms_claimed()
                 : tdma_pio_spi_phys_ensure_sms_claimed()) &&
            tdma_pio_spi_phys_load_programs(previous)) {
            s_tdma_pio_spi_program_persona = previous;
        }
        phys->snapshot.program_persona =
            (uint32_t)s_tdma_pio_spi_program_persona;
        return false;
    }
    s_tdma_pio_spi_program_persona = persona;
    phys->snapshot.program_persona = (uint32_t)persona;
    phys->snapshot.program_switch_count++;
    return true;
}




bool tdma_pio_spi_phys_claim_flight_resources(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return false;
    }
    if (phys->flight_resource_claimed) {
        return true;
    }
    const uint32_t resources = TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK;
    if (!resource_arbiter_acquire_owned(resources,
                                        TDMA_FLIGHT_RESOURCE_OWNER)) {
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_RESOURCE_CONFLICT;
        phys->snapshot.program_switch_fail_count++;
        return false;
    }
    phys->flight_resource_claimed = true;
    return true;
}

void tdma_pio_spi_phys_release_flight_resources(
    tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return;
    }
    if (phys->flight_resource_claimed) {
        resource_arbiter_release_owned(
            TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK,
            TDMA_FLIGHT_RESOURCE_OWNER);
        phys->flight_resource_claimed = false;
    }
    if (s_tdma_pio_spi_flight_sms_claimed) {
        const uint tx_sms[] = {
            BOARD_TDMA_TX_CLK_OUT_SM,
            BOARD_TDMA_TX_SYNC_OUT_SM,
            BOARD_TDMA_TX_DATA_IN_FORWARD_SM,
            BOARD_TDMA_TX_DATA_IN_CAPTURE_SM,
        };
        const uint rx_sms[] = {
            BOARD_TDMA_RX_CLK_IN_SM,
            BOARD_TDMA_RX_SYNC_IN_SM,
            BOARD_TDMA_RX_DATA_OUT_SM,
            BOARD_TDMA_RX_EVIDENCE_IN_SM,
        };
        for (size_t i = 0u; i < sizeof(tx_sms) / sizeof(tx_sms[0]); ++i) {
            pio_sm_unclaim(BOARD_TDMA_TX_PIO, tx_sms[i]);
        }
        for (size_t i = 0u; i < sizeof(rx_sms) / sizeof(rx_sms[0]); ++i) {
            pio_sm_unclaim(BOARD_TDMA_RX_PIO, rx_sms[i]);
        }
        s_tdma_pio_spi_flight_sms_claimed = false;
    }
}
