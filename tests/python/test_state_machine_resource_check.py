from pathlib import Path

from tools.state_machine_resource_check import state_machine_resource_check


ROOT = Path(__file__).resolve().parents[2]


def test_directional_resource_contract_is_valid() -> None:
    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
    )
    assert failures == []


def test_directional_contract_rejects_mixed_data_program(tmp_path: Path) -> None:
    board = tmp_path / "board_config.h"
    board.write_text(
        (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(encoding="utf-8")
    pio.write_text(
        pio_text.replace(
            ".program tdma_pio_spi_directional_data_rx\n.wrap_target\n"
            "    wait 1 gpio 0\n    in pins, 1",
            ".program tdma_pio_spi_directional_data_rx\n.wrap_target\n"
            "    wait 1 gpio 0\n    out pins, 1",
        ),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(board, pio)
    assert "directional DATA RX contains out pins" in failures


def test_directional_contract_rejects_crossed_role_as_legacy_alias(
    tmp_path: Path,
) -> None:
    board = tmp_path / "board_config.h"
    board_text = (
        ROOT / "boards/rp2350_trig/inc/board_config.h"
    ).read_text(encoding="utf-8")
    board.write_text(
        board_text.replace(
            "#define BOARD_TDMA_TX_DATA_SM 1u",
            "#define BOARD_TDMA_TX_DATA_SM 2u",
        ),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(
        board,
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
    )
    assert "BOARD_TDMA_TX_DATA_SM: expected 1, got 2" in failures


def test_flight_claim_reserves_directional_runtime_resources() -> None:
    programs = (
        ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c"
    ).read_text(
        encoding="utf-8"
    )
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")

    assert "TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK" in programs
    for token in (
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_OUTPUT",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_FORWARD",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_SYNC_EDGE",
        "RESOURCE_ARBITER_RESOURCE_TDMA_GPIO",
        "RESOURCE_ARBITER_RESOURCE_TDMA_IRQ",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DREQ",
    ):
        assert token in resources


def test_normal_adapter_contract_models_parallel_clock_tx_and_data_rx() -> None:
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")

    for token in (
        "TDMA_STATE_MACHINE_NORMAL_COMM_RESOURCE_MASK",
        "tdma_state_machine_normal_comm_contract_t",
        ".clock_tx_pio",
        ".sync_tx_pio",
        ".data_rx_pio",
        ".clock_tx_sm",
        ".sync_tx_sm",
        ".data_rx_sm",
        ".data_rx_clock_pin",
        ".data_rx_sync_pin",
        ".clock_tx_dma",
        ".data_rx_dma",
    ):
        assert token in resources

    assert "TDMA_STATE_MACHINE_TX_PIO_RESOURCE" in resources
    assert "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE" in resources
    assert "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_SYNC_EDGE" in resources
    assert "tdma_adapter_comm_fsm" in (
        ROOT / "components/tdma/src/tdma_adapter_comm_fsm.c"
    ).read_text(encoding="utf-8")


def test_normal_adapter_contract_does_not_introduce_data_tx_or_serial_join():
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")
    fsm = (
        ROOT / "components/tdma/inc/tdma_adapter_comm_fsm.h"
    ).read_text(encoding="utf-8")

    normal_contract = resources.split(
        "typedef struct {", 1
    )[1].split(
        "tdma_state_machine_normal_comm_contract", 1
    )[0]
    assert "data_tx" not in normal_contract.lower()
    assert "full_duplex" not in normal_contract.lower()
    assert "CLOCK_TX_STARTED" in fsm
    assert "DATA_RX_STARTED" in fsm
    assert "CLOCK_TX_COMPLETED" in fsm
    assert "DATA_RX_COMPLETED" in fsm


def test_flight_claim_is_released_on_arm_failure_and_stop() -> None:
    phys = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8"
    )
    arm = phys.split("bool tdma_pio_spi_phys_arm(void *context", 1)[1].split(
        "void tdma_pio_spi_phys_disarm", 1
    )[0]
    disarm = phys.split("void tdma_pio_spi_phys_disarm", 1)[1].split(
        "static bool tdma_pio_spi_phys_tx_put", 1
    )[0]

    assert "tdma_pio_spi_phys_claim_flight_resources" not in arm
    assert arm.count("tdma_pio_spi_phys_release_flight_resources(phys)") == 5
    assert "tdma_pio_spi_phys_release_flight_resources(phys)" in disarm


def test_maintenance_persona_has_independent_resource_owner() -> None:
    board = (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(
        encoding="utf-8"
    )
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")
    programs = (
        ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c"
    ).read_text(encoding="utf-8")

    assert "BOARD_TDMA_SPI_PIO_BLOCK_ID" in board
    assert "TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK" in resources
    for token in (
        "TDMA_STATE_MACHINE_MAINTENANCE_PIO_RESOURCE",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_OUTPUT",
        "RESOURCE_ARBITER_RESOURCE_TDMA_GPIO",
        "RESOURCE_ARBITER_RESOURCE_TDMA_IRQ",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DREQ",
    ):
        assert token in resources
    assert '"TDMA_MAINTENANCE_PIO"' in programs
    ensure = programs.split(
        "bool tdma_pio_spi_programs_ensure_sms_claimed", 1
    )[1].split("void tdma_pio_spi_programs_release_resources", 1)[0]
    assert ensure.index("resource_arbiter_acquire_owned") < ensure.index(
        "tdma_pio_spi_programs_ensure_hardware_sms_claimed"
    )
    assert "resource_arbiter_release_owned" in ensure
    release = programs.split(
        "void tdma_pio_spi_programs_release_resources", 1
    )[1].split("bool tdma_pio_spi_programs_transfer_resources", 1)[0]
    assert release.count("tdma_pio_spi_programs_release_hardware_sms") == 2
    flight_owner = release.index("if (phys->flight_resource_claimed)")
    flight_unclaim = release.index(
        "tdma_pio_spi_programs_release_hardware_sms", flight_owner
    )
    maintenance_owner = release.index(
        "else if (*manager->maintenance_resources_claimed)"
    )
    maintenance_unclaim = release.index(
        "tdma_pio_spi_programs_release_hardware_sms", maintenance_owner
    )
    assert flight_owner < flight_unclaim < maintenance_owner
    assert maintenance_owner < maintenance_unclaim


def test_persona_resource_transfer_is_quiesced_and_rollback_is_owned() -> None:
    programs = (
        ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c"
    ).read_text(encoding="utf-8")
    select = programs.split("bool tdma_pio_spi_programs_select", 1)[1]
    unload = select.index("tdma_pio_spi_phys_unload_programs")
    transfer = select.index(
        "tdma_pio_spi_programs_transfer_resources", unload
    )
    load = select.index("tdma_pio_spi_phys_load_programs", transfer)
    assert unload < transfer < load

    rollback = programs.split(
        "static void tdma_pio_spi_programs_rollback", 1
    )[1].split("bool tdma_pio_spi_programs_select", 1)[0]
    release = rollback.index("tdma_pio_spi_programs_release_resources")
    restore_owner = rollback.index(
        "tdma_pio_spi_programs_transfer_resources", release
    )
    restore_program = rollback.index(
        "tdma_pio_spi_phys_load_programs", restore_owner
    )
    assert release < restore_owner < restore_program
    assert "TDMA_PIO_SPI_PERSONA_EVENT_ROLLBACK_FAILED" in rollback


def test_calibration_transfers_owner_only_after_incremental_unload() -> None:
    service = (
        ROOT / "components/tdma/src/tdma_pio_spi_phys_cal_service.inc"
    ).read_text(encoding="utf-8")

    ready = service.split(
        "static bool tdma_pio_spi_phys_cal_persona_switch_ready", 1
    )[1].split("static bool tdma_pio_spi_phys_cal_unload_source_step", 1)[0]
    assert "ensure_sms_claimed" not in ready
    assert service.count("source_persona") == 4
    assert service.count("tdma_pio_spi_programs_transfer_resources") == 2
    assert "TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL" in service
    assert "TDMA_PIO_SPI_PROGRAM_PERSONA_P3_REFERENCE" in service
    assert "tdma_pio_spi_phys_release_flight_resources" not in service
    start = service.split(
        "    if (phys->cal_loopback_transition ==\n"
        "        TDMA_PIO_SPI_CAL_TRANSITION_START_UNLOAD) {", 1
    )[1].split(
        "    if (phys->cal_loopback_transition ==\n"
        "        TDMA_PIO_SPI_CAL_TRANSITION_START_LOAD) {", 1
    )[0]
    unload = start.index("tdma_pio_spi_phys_cal_unload_source_step")
    transfer = start.index("tdma_pio_spi_programs_transfer_resources")
    assert unload < transfer


def test_host_aggregate_runs_resource_and_persona_runtime_gates() -> None:
    aggregate = (ROOT / "tools/tests/run_host_unit_tests.ps1").read_text(
        encoding="utf-8"
    )
    resource_gate = aggregate.index('"run_resource_arbiter_tests.ps1"')
    persona_gate = aggregate.index(
        '"run_tdma_pio_spi_persona_fsm_tests.ps1"'
    )
    assert resource_gate < persona_gate

    resource_test = (ROOT / "tests/unit/test_resource_arbiter.c").read_text(
        encoding="utf-8"
    )
    for token in (
        "TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK",
        "TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK",
        "RESOURCE_ARBITER_RESOURCE_TDMA_GPIO",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DREQ",
        "last_conflict_resources",
        "last_conflict_owner",
        "last_conflict_holder",
        "test_tdma_conflict_recovery_has_no_partial_lease",
    ):
        assert token in resource_test
