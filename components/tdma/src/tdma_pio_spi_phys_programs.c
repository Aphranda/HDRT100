/* PIO program persona manager implementation. */
#include "tdma_pio_spi_phys_programs.h"

#include <stddef.h>

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "resource_arbiter.h"
#include "sync_io.h"
#include "tdma_pio_spi.pio.h"
#include "tdma_state_machine_resources.h"

/* Keep the migrated implementation readable while making every former
 * file-local offset an explicit field of the owner context. */
#define s_tdma_pio_spi_flight_origin_clock_offset (*manager->flight_origin_clock_offset)
#define s_tdma_pio_spi_flight_origin_data_offset (*manager->flight_origin_data_offset)
#define s_tdma_pio_spi_flight_origin_data_capture_offset (*manager->flight_origin_data_capture_offset)
#define s_tdma_pio_spi_flight_origin_rtt_offset (*manager->flight_origin_rtt_offset)
#define s_tdma_pio_spi_flight_data_follower_offset (*manager->flight_data_follower_offset)
#define s_tdma_pio_spi_flight_process_follower_offset (*manager->flight_process_follower_offset)
#define s_tdma_pio_spi_flight_control_forward_offset (*manager->flight_control_forward_offset)
#define s_tdma_pio_spi_flight_clock_latch_offset (*manager->flight_clock_latch_offset)
#define s_tdma_pio_spi_tx_offset (*manager->tx_offset)
#define s_tdma_pio_spi_rx_offset (*manager->rx_offset)
#define s_tdma_pio_spi_clk_forward_offset (*manager->clk_forward_offset)
#define s_tdma_pio_spi_marker_forward_offset (*manager->marker_forward_offset)
#define s_tdma_pio_spi_clk_burst_offset (*manager->clk_burst_offset)
#define s_tdma_pio_spi_clk_capture_offset (*manager->clk_capture_offset)
#define s_tdma_pio_spi_clk_coded_tx_offset (*manager->clk_coded_tx_offset)
#define s_tdma_pio_spi_clk_oversample_offset (*manager->clk_oversample_offset)
#define s_tdma_pio_spi_marker_origin_offset (*manager->marker_origin_offset)
#define s_tdma_pio_spi_marker_capture_offset (*manager->marker_capture_offset)
#define s_tdma_pio_spi_data_train_source_offset (*manager->data_train_source_offset)
#define s_tdma_pio_spi_data_train_sink_offset (*manager->data_train_sink_offset)
#define s_tdma_pio_spi_sck_train_trigger_offset (*manager->sck_train_trigger_offset)
#define s_tdma_pio_spi_sck_train_source_offset (*manager->sck_train_source_offset)
#define s_tdma_pio_spi_sck_train_sink_offset (*manager->sck_train_sink_offset)
#define s_tdma_pio_spi_cal_tx_offset (*manager->cal_tx_offset)
#define s_tdma_pio_spi_cal_capture_offset (*manager->cal_capture_offset)
#define s_tdma_pio_spi_p3_initiator_offset (*manager->p3_initiator_offset)
#define s_tdma_pio_spi_p3_responder_offset (*manager->p3_responder_offset)
#define s_tdma_pio_spi_p3_capture_offset (*manager->p3_capture_offset)
#define s_tdma_pio_spi_p3_responder_capture_offset (*manager->p3_responder_capture_offset)
#define s_tdma_pio_spi_program_persona (*manager->program_persona)
#define s_tdma_pio_spi_tx_dma_channel (*manager->tx_dma_channel)
#define s_tdma_pio_spi_rx_dma_channel (*manager->rx_dma_channel)

static const char *const TDMA_FLIGHT_RESOURCE_OWNER = "TDMA_FLIGHT_PIO";
static const char *const TDMA_MAINTENANCE_RESOURCE_OWNER =
    "TDMA_MAINTENANCE_PIO";

static bool tdma_pio_spi_programs_is_flight_persona(
    tdma_pio_spi_program_persona_t persona)
{
    return persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN ||
           persona == TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER ||
           persona ==
               TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER;
}

static bool tdma_pio_spi_programs_ensure_maintenance_sms_claimed(
    tdma_pio_spi_program_manager_t *manager)
{
    if (manager == NULL || manager->sms_claimed == NULL) {
        return false;
    }
    if (*manager->sms_claimed) {
        return true;
    }
    if (pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM) ||
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM) ||
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM) ||
        pio_sm_is_claimed(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM)) {
        return false;
    }
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
    pio_sm_claim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
    *manager->sms_claimed = true;
    return true;
}

static bool tdma_pio_spi_programs_ensure_flight_sms_claimed(
    tdma_pio_spi_program_manager_t *manager)
{
    if (manager == NULL || manager->flight_sms_claimed == NULL) {
        return false;
    }
    if (*manager->flight_sms_claimed) {
        return true;
    }
    const uint tx_sms[] = {
        BOARD_TDMA_TX_CONTROL_OUT_SM,
        BOARD_TDMA_TX_RTT_EVIDENCE_SM,
        BOARD_TDMA_TX_CLOCK_LATCH_SM,
        BOARD_TDMA_TX_DATA_CAPTURE_SM,
    };
    const uint rx_sms[] = {
        BOARD_TDMA_RX_RESERVED_CONTROL_SM,
        BOARD_TDMA_RX_RESERVED_EVIDENCE_SM,
        BOARD_TDMA_RX_DATA_FLIGHT_SM,
        BOARD_TDMA_RX_CLOCK_LATCH_SM,
    };
    for (size_t index = 0u; index < sizeof(tx_sms) / sizeof(tx_sms[0]);
         index++) {
        if (pio_sm_is_claimed(BOARD_TDMA_TX_PIO, tx_sms[index])) {
            return false;
        }
    }
    for (size_t index = 0u; index < sizeof(rx_sms) / sizeof(rx_sms[0]);
         index++) {
        if (pio_sm_is_claimed(BOARD_TDMA_RX_PIO, rx_sms[index])) {
            return false;
        }
    }
    for (size_t index = 0u; index < sizeof(tx_sms) / sizeof(tx_sms[0]);
         index++) {
        pio_sm_claim(BOARD_TDMA_TX_PIO, tx_sms[index]);
    }
    for (size_t index = 0u; index < sizeof(rx_sms) / sizeof(rx_sms[0]);
         index++) {
        pio_sm_claim(BOARD_TDMA_RX_PIO, rx_sms[index]);
    }
    *manager->flight_sms_claimed = true;
    return true;
}

static void tdma_pio_spi_programs_release_maintenance_sms(
    tdma_pio_spi_program_manager_t *manager)
{
    if (manager == NULL || manager->sms_claimed == NULL ||
        !*manager->sms_claimed) {
        return;
    }
    pio_sm_unclaim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_unclaim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_unclaim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_CAPTURE_SM);
    pio_sm_unclaim(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_RTT_SM);
    *manager->sms_claimed = false;
}

static void tdma_pio_spi_programs_release_flight_sms(
    tdma_pio_spi_program_manager_t *manager)
{
    if (manager == NULL || manager->flight_sms_claimed == NULL ||
        !*manager->flight_sms_claimed) {
        return;
    }
    const uint tx_sms[] = {
        BOARD_TDMA_TX_CONTROL_OUT_SM,
        BOARD_TDMA_TX_RTT_EVIDENCE_SM,
        BOARD_TDMA_TX_CLOCK_LATCH_SM,
        BOARD_TDMA_TX_DATA_CAPTURE_SM,
    };
    const uint rx_sms[] = {
        BOARD_TDMA_RX_RESERVED_CONTROL_SM,
        BOARD_TDMA_RX_RESERVED_EVIDENCE_SM,
        BOARD_TDMA_RX_DATA_FLIGHT_SM,
        BOARD_TDMA_RX_CLOCK_LATCH_SM,
    };
    for (size_t index = 0u; index < sizeof(tx_sms) / sizeof(tx_sms[0]);
         index++) {
        pio_sm_unclaim(BOARD_TDMA_TX_PIO, tx_sms[index]);
    }
    for (size_t index = 0u; index < sizeof(rx_sms) / sizeof(rx_sms[0]);
         index++) {
        pio_sm_unclaim(BOARD_TDMA_RX_PIO, rx_sms[index]);
    }
    *manager->flight_sms_claimed = false;
}

static bool tdma_pio_spi_programs_resume_sync_io(tdma_pio_spi_phys_t *phys)
{
    const bool resumed = sync_io_resume_after_tdma_flight();
    if (!resumed && phys != NULL) {
        phys->snapshot.program_switch_fail_count++;
        phys->snapshot.last_error =
            TDMA_PIO_SPI_PHYS_ERROR_PERSONA_RESOURCE;
    }
    return resumed;
}

static bool tdma_pio_spi_programs_claim_resources(
    tdma_pio_spi_program_manager_t *manager,
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona)
{
    if (manager == NULL || phys == NULL ||
        manager->maintenance_resources_claimed == NULL) {
        return false;
    }
    if (persona == TDMA_PIO_SPI_PROGRAM_PERSONA_NONE) {
        return true;
    }

    const bool flight = tdma_pio_spi_programs_is_flight_persona(persona);
    bool claimed_here = false;
    if (flight) {
        if (*manager->maintenance_resources_claimed) {
            return false;
        }
        if (!phys->flight_resource_claimed) {
            if (!resource_arbiter_acquire_owned(
                    TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK,
                    TDMA_FLIGHT_RESOURCE_OWNER)) {
                return false;
            }
            phys->flight_resource_claimed = true;
            claimed_here = true;
        }
        if (!sync_io_suspend_for_tdma_flight()) {
            if (claimed_here) {
                resource_arbiter_release_owned(
                    TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK,
                    TDMA_FLIGHT_RESOURCE_OWNER);
                phys->flight_resource_claimed = false;
            }
            return false;
        }
    } else {
        if (phys->flight_resource_claimed) {
            return false;
        }
        if (!*manager->maintenance_resources_claimed) {
            if (!resource_arbiter_acquire_owned(
                    TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK,
                    TDMA_MAINTENANCE_RESOURCE_OWNER)) {
                return false;
            }
            *manager->maintenance_resources_claimed = true;
            claimed_here = true;
        }
    }

    const bool sms_claimed = flight
        ? tdma_pio_spi_programs_ensure_flight_sms_claimed(manager)
        : tdma_pio_spi_programs_ensure_maintenance_sms_claimed(manager);
    if (sms_claimed) {
        return true;
    }
    if (flight) {
        tdma_pio_spi_programs_release_flight_sms(manager);
        if (phys->flight_resource_claimed) {
            resource_arbiter_release_owned(
                TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK,
                TDMA_FLIGHT_RESOURCE_OWNER);
            phys->flight_resource_claimed = false;
        }
        (void)tdma_pio_spi_programs_resume_sync_io(phys);
    } else if (claimed_here) {
        resource_arbiter_release_owned(
            TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK,
            TDMA_MAINTENANCE_RESOURCE_OWNER);
        *manager->maintenance_resources_claimed = false;
    }
    return false;
}

static void tdma_pio_spi_programs_publish_lifecycle(
    const tdma_pio_spi_program_manager_t *manager,
    tdma_pio_spi_phys_t *phys)
{
    if (manager == NULL || phys == NULL) {
        return;
    }
    phys->snapshot.program_lifecycle_state = manager->lifecycle.state;
    phys->snapshot.program_target_persona = manager->lifecycle.target_persona;
    phys->snapshot.program_previous_persona =
        manager->lifecycle.previous_persona;
    phys->snapshot.program_transition_seq = manager->lifecycle.transition_seq;
    phys->snapshot.program_lifecycle_error = manager->lifecycle.last_error;
}

static bool tdma_pio_spi_programs_transition(
    tdma_pio_spi_program_manager_t *manager,
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_persona_event_t event,
    tdma_pio_spi_program_persona_t persona)
{
    const bool accepted = tdma_pio_spi_persona_fsm_dispatch(
        &manager->lifecycle, event, (uint32_t)persona);
    tdma_pio_spi_programs_publish_lifecycle(manager, phys);
    return accepted;
}

bool tdma_pio_spi_programs_ensure_sms_claimed(
    tdma_pio_spi_program_manager_t *manager)
{
    if (manager == NULL || manager->maintenance_resources_claimed == NULL) {
        return false;
    }
    bool claimed_here = false;
    if (!*manager->maintenance_resources_claimed) {
        if (!resource_arbiter_acquire_owned(
                TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK,
                TDMA_MAINTENANCE_RESOURCE_OWNER)) {
            return false;
        }
        *manager->maintenance_resources_claimed = true;
        claimed_here = true;
    }
    if (tdma_pio_spi_programs_ensure_maintenance_sms_claimed(manager)) {
        return true;
    }
    if (claimed_here) {
        resource_arbiter_release_owned(
            TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK,
            TDMA_MAINTENANCE_RESOURCE_OWNER);
        *manager->maintenance_resources_claimed = false;
    }
    return false;
}

void tdma_pio_spi_programs_release_resources(
    tdma_pio_spi_program_manager_t *manager,
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona)
{
    if (manager == NULL || phys == NULL ||
        manager->maintenance_resources_claimed == NULL) {
        return;
    }
    if (tdma_pio_spi_programs_is_flight_persona(persona)) {
        tdma_pio_spi_programs_release_flight_sms(manager);
        if (phys->flight_resource_claimed) {
            resource_arbiter_release_owned(
                TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK,
                TDMA_FLIGHT_RESOURCE_OWNER);
            phys->flight_resource_claimed = false;
        }
        (void)tdma_pio_spi_programs_resume_sync_io(phys);
    } else if (*manager->maintenance_resources_claimed) {
        tdma_pio_spi_programs_release_maintenance_sms(manager);
        resource_arbiter_release_owned(
            TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK,
            TDMA_MAINTENANCE_RESOURCE_OWNER);
        *manager->maintenance_resources_claimed = false;
    }
}

bool tdma_pio_spi_programs_transfer_resources(
    tdma_pio_spi_program_manager_t *manager,
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t previous,
    tdma_pio_spi_program_persona_t target)
{
    const bool previous_flight =
        tdma_pio_spi_programs_is_flight_persona(previous);
    const bool target_flight =
        tdma_pio_spi_programs_is_flight_persona(target);
    if (previous != TDMA_PIO_SPI_PROGRAM_PERSONA_NONE &&
        target != TDMA_PIO_SPI_PROGRAM_PERSONA_NONE &&
        previous_flight == target_flight) {
        return tdma_pio_spi_programs_claim_resources(
            manager, phys, target);
    }
    if (previous != TDMA_PIO_SPI_PROGRAM_PERSONA_NONE) {
        tdma_pio_spi_programs_release_resources(
            manager, phys, previous);
    }
    return tdma_pio_spi_programs_claim_resources(manager, phys, target);
}

static bool tdma_pio_spi_phys_load_flight_clock_latch_program(
    tdma_pio_spi_program_manager_t *manager,
    PIO pio)
{
    if (pio == NULL || !pio_can_add_program(
            pio,
            &tdma_pio_spi_flight_clock_latch_program)) {
        return false;
    }
    *manager->flight_clock_latch_offset = (uint)pio_add_program(
        pio, &tdma_pio_spi_flight_clock_latch_program);
    return true;
}

static bool tdma_pio_spi_phys_load_normal_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_tx_byte_program)) return false;
    *manager->tx_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_tx_byte_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_rx_byte_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_tx_byte_program,
                           *manager->tx_offset);
        return false;
    }
    *manager->rx_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_rx_byte_program);
    return true;
}

static bool tdma_pio_spi_phys_load_coarse_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_forward_program)) return false;
    *manager->clk_forward_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_clk_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_burst_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           *manager->clk_forward_offset);
        return false;
    }
    *manager->clk_burst_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_clk_burst_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_burst_program,
                           *manager->clk_burst_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           *manager->clk_forward_offset);
        return false;
    }
    *manager->clk_capture_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_clk_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_cal_loopback_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_tx_program)) return false;
    *manager->cal_tx_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_tx_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_cal_loopback_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_cal_loopback_tx_program,
                           *manager->cal_tx_offset);
        return false;
    }
    *manager->cal_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_cal_loopback_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_coded_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_forward_program)) return false;
    *manager->clk_forward_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_coded_tx_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           *manager->clk_forward_offset);
        return false;
    }
    *manager->clk_coded_tx_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_coded_tx_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_clk_oversample_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_coded_tx_program,
                           *manager->clk_coded_tx_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_clk_forward_program,
                           *manager->clk_forward_offset);
        return false;
    }
    *manager->clk_oversample_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_clk_oversample_program);
    return true;
}

static bool tdma_pio_spi_phys_load_marker_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_forward_program)) return false;
    *manager->marker_forward_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_forward_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_origin_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_forward_program,
                           *manager->marker_forward_offset);
        return false;
    }
    *manager->marker_origin_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_origin_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_capture_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           *manager->marker_origin_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_forward_program,
                           *manager->marker_forward_offset);
        return false;
    }
    *manager->marker_capture_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_capture_program);
    return true;
}

static bool tdma_pio_spi_phys_load_data_train_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_marker_origin_program)) return false;
    *manager->marker_origin_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_marker_origin_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_data_train_source_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           *manager->marker_origin_offset);
        return false;
    }
    *manager->data_train_source_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_data_train_source_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_data_train_sink_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_data_train_source_program,
                           *manager->data_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_marker_origin_program,
                           *manager->marker_origin_offset);
        return false;
    }
    *manager->data_train_sink_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_data_train_sink_program);
    return true;
}

static bool tdma_pio_spi_phys_load_sck_train_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_sck_train_trigger_program)) {
        return false;
    }
    *manager->sck_train_trigger_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_sck_train_trigger_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_sck_train_source_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_trigger_program,
                           *manager->sck_train_trigger_offset);
        return false;
    }
    *manager->sck_train_source_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_sck_train_source_program);
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_sck_train_sink_program)) {
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_source_program,
                           *manager->sck_train_source_offset);
        pio_remove_program(BOARD_TDMA_SPI_PIO,
                           &tdma_pio_spi_sck_train_trigger_program,
                           *manager->sck_train_trigger_offset);
        return false;
    }
    *manager->sck_train_sink_offset = (uint)pio_add_program(
        BOARD_TDMA_SPI_PIO, &tdma_pio_spi_sck_train_sink_program);
    return true;
}

static bool tdma_pio_spi_phys_load_flight_origin_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    const PIO tx_pio = BOARD_TDMA_TX_PIO;
    const PIO rx_pio = BOARD_TDMA_RX_PIO;
    if (!pio_can_add_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_clock_rx_program)) {
        return false;
    }
    s_tdma_pio_spi_flight_origin_clock_offset = (uint)pio_add_program(
        tx_pio, &tdma_pio_spi_flight_origin_clock_rx_program);
    if (!pio_can_add_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_data_capture_program)) {
        pio_remove_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        return false;
    }
    s_tdma_pio_spi_flight_origin_data_capture_offset =
        (uint)pio_add_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_data_capture_program);
    if (!pio_can_add_program(
            rx_pio,
            &tdma_pio_spi_flight_origin_data_tx_program)) {
        pio_remove_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_data_capture_program,
            s_tdma_pio_spi_flight_origin_data_capture_offset);
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
            &tdma_pio_spi_flight_origin_data_capture_program,
            s_tdma_pio_spi_flight_origin_data_capture_offset);
        pio_remove_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        return false;
    }
    s_tdma_pio_spi_flight_origin_rtt_offset = (uint)pio_add_program(
        tx_pio, &tdma_pio_spi_flight_origin_rtt_program);
    if (!tdma_pio_spi_phys_load_flight_clock_latch_program(
            manager, tx_pio)) {
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
            &tdma_pio_spi_flight_origin_data_capture_program,
            s_tdma_pio_spi_flight_origin_data_capture_offset);
        pio_remove_program(
            tx_pio,
            &tdma_pio_spi_flight_origin_clock_rx_program,
            s_tdma_pio_spi_flight_origin_clock_offset);
        return false;
    }
    return true;
}

static bool tdma_pio_spi_phys_load_flight_follower_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    const PIO tx_pio = BOARD_TDMA_TX_PIO;
    const PIO rx_pio = BOARD_TDMA_RX_PIO;
    if (!pio_can_add_program(tx_pio,
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
    if (!tdma_pio_spi_phys_load_flight_clock_latch_program(
            manager, rx_pio)) {
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

static bool tdma_pio_spi_phys_load_flight_process_follower_programs(
    tdma_pio_spi_program_manager_t *manager)
{
    const PIO tx_pio = BOARD_TDMA_TX_PIO;
    const PIO rx_pio = BOARD_TDMA_RX_PIO;
    if (!pio_can_add_program(tx_pio,
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
    if (!tdma_pio_spi_phys_load_flight_clock_latch_program(
            manager, rx_pio)) {
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

static bool tdma_pio_spi_phys_load_p3_initiator_programs(
    tdma_pio_spi_program_manager_t *manager)
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

static bool tdma_pio_spi_phys_load_p3_responder_programs(
    tdma_pio_spi_program_manager_t *manager)
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

static bool tdma_pio_spi_phys_load_p3_reference_programs(
    tdma_pio_spi_program_manager_t *manager)
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
    tdma_pio_spi_program_manager_t *manager,
    tdma_pio_spi_program_persona_t persona)
{
    switch (persona) {
    case TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL:
        return tdma_pio_spi_phys_load_normal_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE:
        return tdma_pio_spi_phys_load_coarse_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CAL_LOOPBACK:
        return tdma_pio_spi_phys_load_cal_loopback_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE:
        return tdma_pio_spi_phys_load_p3_reference_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_CODED:
        return tdma_pio_spi_phys_load_coded_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_INITIATOR:
        return tdma_pio_spi_phys_load_p3_initiator_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_RESPONDER:
        return tdma_pio_spi_phys_load_p3_responder_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_INITIATOR:
        return tdma_pio_spi_phys_load_p3_initiator_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_P3_CS_RESPONDER:
        return tdma_pio_spi_phys_load_p3_responder_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_MARKER:
        return tdma_pio_spi_phys_load_marker_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_TRAIN:
        return tdma_pio_spi_phys_load_data_train_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_SCK_TRAIN:
        return tdma_pio_spi_phys_load_sck_train_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN:
        return tdma_pio_spi_phys_load_flight_origin_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER:
        return tdma_pio_spi_phys_load_flight_follower_programs(manager);
    case TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_PROCESS_FOLLOWER:
        return tdma_pio_spi_phys_load_flight_process_follower_programs(manager);
    default:
        return false;
    }
}

static void tdma_pio_spi_phys_unload_programs(
    tdma_pio_spi_program_manager_t *manager)
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
            &tdma_pio_spi_flight_origin_data_capture_program,
            s_tdma_pio_spi_flight_origin_data_capture_offset);
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

static void tdma_pio_spi_programs_rollback(
    tdma_pio_spi_program_manager_t *manager,
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t previous,
    tdma_pio_spi_program_persona_t failed_target,
    bool previous_resources_held)
{
    tdma_pio_spi_programs_release_resources(
        manager, phys, failed_target);
    bool restored = previous == TDMA_PIO_SPI_PROGRAM_PERSONA_NONE;
    if (!restored && tdma_pio_spi_programs_transfer_resources(
                         manager, phys,
                         TDMA_PIO_SPI_PROGRAM_PERSONA_NONE, previous)) {
        if (tdma_pio_spi_phys_load_programs(manager, previous)) {
            s_tdma_pio_spi_program_persona = previous;
            restored = true;
            if (!previous_resources_held) {
                tdma_pio_spi_programs_release_resources(
                    manager, phys, previous);
            }
        } else {
            tdma_pio_spi_programs_release_resources(
                manager, phys, previous);
        }
    }
    (void)tdma_pio_spi_programs_transition(
        manager, phys,
        restored ? TDMA_PIO_SPI_PERSONA_EVENT_ROLLBACK_LOADED
                 : TDMA_PIO_SPI_PERSONA_EVENT_ROLLBACK_FAILED,
        failed_target);
    phys->snapshot.program_persona =
        (uint32_t)s_tdma_pio_spi_program_persona;
}

static bool tdma_pio_spi_programs_current_persona_quiesced(
    const tdma_pio_spi_program_manager_t *manager,
    const tdma_pio_spi_phys_t *phys)
{
    if (manager == NULL || phys == NULL || manager->program_persona == NULL ||
        manager->maintenance_resources_claimed == NULL) {
        return false;
    }
    const tdma_pio_spi_program_persona_t current =
        *manager->program_persona;
    if (tdma_pio_spi_programs_is_flight_persona(current) &&
        phys->flight_resource_claimed) {
        const uint32_t tx_sm_mask =
            (1u << BOARD_TDMA_TX_CONTROL_OUT_SM) |
            (1u << BOARD_TDMA_TX_RTT_EVIDENCE_SM) |
            (1u << BOARD_TDMA_TX_CLOCK_LATCH_SM) |
            (1u << BOARD_TDMA_TX_DATA_CAPTURE_SM);
        const uint32_t rx_sm_mask =
            (1u << BOARD_TDMA_RX_RESERVED_CONTROL_SM) |
            (1u << BOARD_TDMA_RX_RESERVED_EVIDENCE_SM) |
            (1u << BOARD_TDMA_RX_DATA_FLIGHT_SM) |
            (1u << BOARD_TDMA_RX_CLOCK_LATCH_SM);
        if ((BOARD_TDMA_TX_PIO->ctrl & tx_sm_mask) != 0u ||
            (BOARD_TDMA_RX_PIO->ctrl & rx_sm_mask) != 0u) {
            return false;
        }
    } else if (!tdma_pio_spi_programs_is_flight_persona(current) &&
               *manager->maintenance_resources_claimed) {
        const uint32_t maintenance_sm_mask =
            (1u << BOARD_TDMA_SPI_MASTER_SM) |
            (1u << BOARD_TDMA_SPI_SLAVE_SM) |
            (1u << BOARD_TDMA_SPI_CAPTURE_SM) |
            (1u << BOARD_TDMA_SPI_RTT_SM);
        if ((BOARD_TDMA_SPI_PIO->ctrl & maintenance_sm_mask) != 0u) {
            return false;
        }
    }
    return (*manager->tx_dma_channel < 0 ||
            !dma_channel_is_busy((uint)*manager->tx_dma_channel)) &&
           (*manager->rx_dma_channel < 0 ||
            !dma_channel_is_busy((uint)*manager->rx_dma_channel));
}

bool tdma_pio_spi_programs_select(
    tdma_pio_spi_program_manager_t *manager,
    tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_program_persona_t persona)
{
    if (manager == NULL || phys == NULL ||
        manager->program_persona == NULL || manager->sms_claimed == NULL ||
        manager->flight_sms_claimed == NULL ||
        manager->maintenance_resources_claimed == NULL ||
        manager->tx_dma_channel == NULL || manager->rx_dma_channel == NULL) {
        if (phys != NULL) {
            phys->snapshot.last_error =
                TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT;
        }
        return false;
    }
    if (!tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_REQUEST, persona)) {
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY;
        return false;
    }
    if (persona <= TDMA_PIO_SPI_PROGRAM_PERSONA_NONE ||
        persona > TDMA_PIO_SPI_PROGRAM_PERSONA_MAX) {
        (void)tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_INVALID, persona);
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT;
        return false;
    }
    if (!tdma_pio_spi_programs_current_persona_quiesced(manager, phys)) {
        phys->snapshot.program_switch_fail_count++;
        (void)tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_BUSY, persona);
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY;
        return false;
    }
    if (s_tdma_pio_spi_program_persona == persona) {
        if (!tdma_pio_spi_programs_transfer_resources(
                manager, phys, persona, persona)) {
            phys->snapshot.program_switch_fail_count++;
            (void)tdma_pio_spi_programs_transition(
                manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_BUSY, persona);
            phys->snapshot.last_error =
                TDMA_PIO_SPI_PHYS_ERROR_PERSONA_RESOURCE;
            return false;
        }
        phys->snapshot.program_persona = (uint32_t)persona;
        (void)tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_RETAIN, persona);
        return true;
    }
    if (!tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_VALID, persona) ||
        !tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_QUIESCED, persona)) {
        phys->snapshot.program_switch_fail_count++;
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY;
        return false;
    }
    const tdma_pio_spi_program_persona_t previous =
        s_tdma_pio_spi_program_persona;
    const bool previous_resources_held =
        tdma_pio_spi_programs_is_flight_persona(previous)
            ? phys->flight_resource_claimed
            : *manager->maintenance_resources_claimed;
    tdma_pio_spi_phys_unload_programs(manager);
    if (!tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_UNLOADED, persona)) {
        phys->snapshot.program_switch_fail_count++;
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_PERSONA_BUSY;
        return false;
    }
    if (!tdma_pio_spi_programs_transfer_resources(
            manager, phys, previous, persona)) {
        phys->snapshot.program_switch_fail_count++;
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_PERSONA_RESOURCE;
        (void)tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_LOAD_FAILED, persona);
        tdma_pio_spi_programs_rollback(
            manager, phys, previous, persona, previous_resources_held);
        return false;
    }
    if (!tdma_pio_spi_phys_load_programs(manager, persona)) {
        phys->snapshot.program_switch_fail_count++;
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_PROGRAM_LOAD;
        (void)tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_LOAD_FAILED, persona);
        tdma_pio_spi_programs_rollback(
            manager, phys, previous, persona, previous_resources_held);
        return false;
    }
    s_tdma_pio_spi_program_persona = persona;
    if (!tdma_pio_spi_programs_transition(
            manager, phys, TDMA_PIO_SPI_PERSONA_EVENT_LOADED, persona)) {
        phys->snapshot.program_switch_fail_count++;
        phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_PROGRAM_LOAD;
        return false;
    }
    phys->snapshot.program_persona = (uint32_t)persona;
    phys->snapshot.program_switch_count++;
    return true;
}
