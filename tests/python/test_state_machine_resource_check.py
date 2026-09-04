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


def test_rx_endpoint_contract_rejects_missing_follower_unload(
    tmp_path: Path,
) -> None:
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (
        ROOT / "components/tdma/src/tdma_pio_spi.pio"
    ).read_text(encoding="utf-8")
    pio.write_text(
        pio_text.replace(
            "    mov osr, isr\n    push noblock\n    out null, 24",
            "    mov osr, isr\n    nop\n    out null, 24",
            1,
        ),
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        pio,
    )

    assert "flight DATA follower is missing business RX unload push" in failures


def test_rx_endpoint_contract_rejects_process_fifo_join(tmp_path: Path) -> None:
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (
        ROOT / "components/tdma/src/tdma_pio_spi.pio"
    ).read_text(encoding="utf-8")
    pio.write_text(
        pio_text.replace(
            "    sm_config_set_out_shift(&c, false, false, 32u);\n"
            "    sm_config_set_clkdiv(&c, 1.0f);\n"
            "    pio_sm_init(pio, sm, offset, &c);",
            "    sm_config_set_out_shift(&c, false, false, 32u);\n"
            "    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);\n"
            "    sm_config_set_clkdiv(&c, 1.0f);\n"
            "    pio_sm_init(pio, sm, offset, &c);",
            1,
        ),
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        pio,
    )

    assert (
        "process-image DATA follower must keep independent TX/RX FIFOs"
        in failures
    )


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


def test_normal_adapter_contract_models_combined_control_tx_and_data_rx() -> None:
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")

    for token in (
        "TDMA_STATE_MACHINE_NORMAL_COMM_RESOURCE_MASK",
        "tdma_state_machine_normal_comm_contract_t",
        ".control_tx_pio",
        ".data_rx_pio",
        ".control_tx_sm",
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


def test_flight_resource_view_uses_runtime_persona_roles() -> None:
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")
    phys = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8"
    )

    for role in (
        "tx_control_out_sm",
        "tx_rtt_evidence_sm",
        "tx_clock_latch_sm",
        "tx_data_capture_sm",
    ):
        assert role in resources
        assert f"flight_resources.{role}" in phys
    for endpoint in (
        "rx_endpoints.data_output",
        "rx_endpoints.data_unload",
        "rx_endpoints.clock_evidence",
    ):
        assert f"flight_resources.{endpoint}" in phys
    assert "tdma_state_machine_rx_endpoint_contract_t rx_endpoints" in resources
    helper_block = phys.split(
        "static PIO tdma_pio_spi_phys_control_pio", 1
    )[1].split("static void tdma_pio_spi_phys_clk_train_write_begin", 1)[0]
    assert "BOARD_TDMA_TX_CLK_OUT_SM" not in helper_block
    assert "BOARD_TDMA_TX_SYNC_OUT_SM" not in helper_block
    assert "BOARD_TDMA_TX_DATA_IN_FORWARD_SM" not in helper_block
    assert "BOARD_TDMA_RX_DATA_OUT_SM" not in helper_block


def test_rx_endpoint_contract_declares_fifo_dma_and_single_consumer() -> None:
    resources = (
        ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
    ).read_text(encoding="utf-8")

    for token in (
        "tdma_state_machine_fifo_endpoint_t",
        "tdma_state_machine_rx_endpoint_contract_t",
        "TDMA_STATE_MACHINE_FIFO_TX",
        "TDMA_STATE_MACHINE_FIFO_RX",
        "TDMA_STATE_MACHINE_ENDPOINT_OWNER_DMA",
        "TDMA_STATE_MACHINE_ENDPOINT_OWNER_CORE1",
        "TDMA_STATE_MACHINE_DREQ_TX",
        "TDMA_STATE_MACHINE_DREQ_RX",
        "TDMA_STATE_MACHINE_DREQ_NONE",
        "BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL",
        "BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL",
        "TDMA_STATE_MACHINE_DMA_CHANNEL_NONE",
        ".business_rx_consumer_count = 1u",
    ):
        assert token in resources


def test_rx_endpoint_contract_rejects_multiple_business_consumers(
    tmp_path: Path,
) -> None:
    resources = tmp_path / "tdma_state_machine_resources.h"
    resources.write_text(
        (
            ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
        ).read_text(encoding="utf-8").replace(
            ".business_rx_consumer_count = 1u",
            ".business_rx_consumer_count = 2u",
            1,
        ),
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
        resources=resources,
    )

    assert "RX DATA business FIFO must have exactly one consumer" in failures


def test_rx_endpoint_contract_rejects_shared_clock_evidence_sm(
    tmp_path: Path,
) -> None:
    resources = tmp_path / "tdma_state_machine_resources.h"
    resources.write_text(
        (
            ROOT / "components/tdma/inc/tdma_state_machine_resources.h"
        ).read_text(encoding="utf-8").replace(
            ".sm = BOARD_TDMA_RX_CLOCK_LATCH_SM",
            ".sm = BOARD_TDMA_RX_DATA_FLIGHT_SM",
            1,
        ),
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
        resources=resources,
    )

    assert "clock evidence must use its dedicated RX PIO SM" in failures


def test_rx_endpoint_contract_rejects_rx_dma_dreq_direction(
    tmp_path: Path,
) -> None:
    phys = tmp_path / "tdma_pio_spi_phys.c"
    phys.write_text(
        (
            ROOT / "components/tdma/src/tdma_pio_spi_phys.c"
        ).read_text(encoding="utf-8").replace(
            "pio_get_dreq(capture_pio, capture_sm, false)",
            "pio_get_dreq(capture_pio, capture_sm, true)",
            1,
        ),
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
        phys=phys,
    )

    assert "business RX DMA must use the RX DREQ" in failures


def test_rx_endpoint_contract_rejects_tx_fifo_as_rx_dma_source(
    tmp_path: Path,
) -> None:
    phys = tmp_path / "tdma_pio_spi_phys.c"
    phys.write_text(
        (
            ROOT / "components/tdma/src/tdma_pio_spi_phys.c"
        ).read_text(encoding="utf-8").replace(
            "&capture_pio->rxf[capture_sm]",
            "&capture_pio->txf[capture_sm]",
            1,
        ),
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
        phys=phys,
    )

    assert "business RX DMA must read the declared RX FIFO" in failures


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
    release_helper = phys.split(
        "static void tdma_pio_spi_phys_release_flight_resources", 1
    )[1].split("static void tdma_pio_spi_phys_enable_sm_pair", 1)[0]
    assert "s_tdma_pio_spi_program_persona" in release_helper
    assert "TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN" not in release_helper


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
        "tdma_pio_spi_programs_ensure_maintenance_sms_claimed"
    )
    assert "resource_arbiter_release_owned" in ensure
    release = programs.split(
        "void tdma_pio_spi_programs_release_resources", 1
    )[1].split("bool tdma_pio_spi_programs_transfer_resources", 1)[0]
    assert "tdma_pio_spi_programs_release_flight_sms" in release
    assert "tdma_pio_spi_programs_release_maintenance_sms" in release
    flight_unclaim = release.index("tdma_pio_spi_programs_release_flight_sms")
    flight_owner = release.index("if (phys->flight_resource_claimed)")
    maintenance_owner = release.index(
        "else if (*manager->maintenance_resources_claimed)"
    )
    maintenance_unclaim = release.index(
        "tdma_pio_spi_programs_release_maintenance_sms", maintenance_owner
    )
    assert flight_unclaim < flight_owner < maintenance_owner
    assert maintenance_owner < maintenance_unclaim


def test_flight_program_load_and_unload_never_use_legacy_pio_alias() -> None:
    programs = (
        ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c"
    ).read_text(encoding="utf-8")
    for function_name, next_name in (
        ("tdma_pio_spi_phys_load_flight_origin_programs",
         "tdma_pio_spi_phys_load_flight_follower_programs"),
        ("tdma_pio_spi_phys_load_flight_follower_programs",
         "tdma_pio_spi_phys_load_flight_process_follower_programs"),
        ("tdma_pio_spi_phys_load_flight_process_follower_programs",
         "tdma_pio_spi_phys_load_p3_initiator_programs"),
    ):
        function = programs.split(f"static bool {function_name}", 1)[1].split(
            f"static bool {next_name}", 1)[0]
        assert "BOARD_TDMA_SPI_PIO" not in function
        assert "BOARD_TDMA_TX_PIO" in function
        assert "BOARD_TDMA_RX_PIO" in function

    unload = programs.split(
        "static void tdma_pio_spi_phys_unload_programs", 1
    )[1].split("static bool tdma_pio_spi_phys_load_programs", 1)[0]
    for persona, next_marker in (
        ("FLIGHT_ORIGIN", "FLIGHT_FOLLOWER"),
        ("FLIGHT_FOLLOWER", "FLIGHT_PROCESS_FOLLOWER"),
        ("FLIGHT_PROCESS_FOLLOWER", None),
    ):
        case_tail = unload.split(
            f"case TDMA_PIO_SPI_PROGRAM_PERSONA_{persona}:", 1
        )[1]
        case = (case_tail.split(
            f"case TDMA_PIO_SPI_PROGRAM_PERSONA_{next_marker}:", 1)[0]
            if next_marker is not None else case_tail.split("default:", 1)[0])
        assert "BOARD_TDMA_SPI_PIO" not in case


def test_sync_io_flight_claim_has_no_legacy_handoff() -> None:
    programs = (
        ROOT / "components/tdma/src/tdma_pio_spi_phys_programs.c"
    ).read_text(encoding="utf-8")
    claim = programs.split(
        "static bool tdma_pio_spi_programs_claim_resources", 1
    )[1].split("static void tdma_pio_spi_programs_publish_lifecycle", 1)[0]
    assert "sync_io_suspend_for_tdma_flight" not in claim
    assert "sync_io_resume_after_tdma_flight" not in claim
    assert "tdma_pio_spi_programs_ensure_flight_sms_claimed" in claim
    assert "resource_arbiter_acquire_owned" in claim

    sync_io = (ROOT / "components/sync_io/src/sync_io.c").read_text(
        encoding="utf-8")
    assert "sync_io_suspend_for_tdma_flight" not in sync_io
    assert "sync_io_resume_after_tdma_flight" not in sync_io
    assert "tdma_flight_suspended" not in sync_io


def test_sync_io_pio0_wave_output_is_independent_of_tdma() -> None:
    model_sched = (
        ROOT / "components/sync_io/src/sync_io_model_sched.c"
    ).read_text(encoding="utf-8")
    sync_io = (ROOT / "components/sync_io/src/sync_io.c").read_text(
        encoding="utf-8"
    )

    output_arm = model_sched.split(
        "bool sync_io_output_pulse_schedule_arm(", 1
    )[1].split("bool sync_io_output_pulse_schedule_arm_ns", 1)[0]
    output_arm_ns = model_sched.split(
        "bool sync_io_output_pulse_schedule_arm_ns", 1
    )[1].split(
        "bool sync_io_sma_observer_pulse_schedule_arm_periodic_ns", 1
    )[0]
    for function in (output_arm, output_arm_ns):
        assert "BOARD_SYNC_PIO_FAST" in function
        assert "BOARD_SYNC_PIO0_WAVE_OUTPUT_SM" in function
        assert "BOARD_SYNC_PIO_WAVE" not in function
        assert "sync_io_core_tdma_flight_suspended" not in function

    observer_arm = model_sched.split(
        "bool sync_io_sma_observer_pulse_schedule_arm_periodic_ns(", 1
    )[1].split("void sync_io_model_pulse_schedule_disarm", 1)[0]
    assert "BOARD_SYNC_PIO_FAST" in observer_arm
    assert "BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM" in observer_arm
    assert "BOARD_SYNC_PIO_WAVE" not in observer_arm

    init = sync_io.split("bool sync_io_init(", 1)[1]
    assert '"sma_observer"' not in init

    completion = model_sched.split(
        "static void sync_io_model_update_completion", 1
    )[1].split(
        "static bool sync_io_pulse_schedule_arm_on_pin_common", 1
    )[0]
    assert "sync_io_wave_output_manager_release();" in completion

    for function_name in (
        "sync_io_model_pulse_schedule_arm",
        "sync_io_model_pulse_schedule_arm_ns",
        "sync_io_model_pulse_schedule_arm_periodic_ns",
    ):
        body = model_sched.split(f"bool {function_name}", 1)[1]
        body = body.split("\n}", 1)[0]
        assert "BOARD_SYNC_PIO_WAVE" not in body
        assert "DREQ_PIO1_TX0" not in body

    cleanup = model_sched.split(
        "static void sync_io_wave_output_cleanup", 1
    )[1].split("static void sync_io_wave_output_manager_init", 1)[0]
    assert "hardware_owned = s_wave_output_sm_claimed" in cleanup
    assert "if (hardware_owned && s_model_pulse.pio != NULL)" in cleanup
    assert "if (hardware_owned)" in cleanup


def test_tdma_flight_rx_unload_and_tx_load_are_directional() -> None:
    engine_h = (
        ROOT / "components/tdma/inc/tdma_flight_engine.h"
    ).read_text(encoding="utf-8")
    engine_c = (
        ROOT / "components/tdma/src/tdma_flight_engine.c"
    ).read_text(encoding="utf-8")
    adapter = (
        ROOT / "components/tdma/src/tdma_pio_spi_ring_adapter.c"
    ).read_text(encoding="utf-8")

    assert "tdma_flight_engine_unload_t" in engine_h
    assert "tdma_flight_engine_rx_unload(" in engine_h
    assert "tdma_flight_engine_rx_commit(" in engine_h
    assert "tdma_flight_engine_tx_load(" in engine_h

    tx_load = engine_c.split(
        "bool tdma_flight_engine_tx_load(", 1
    )[1].split("bool tdma_flight_engine_inspect_input", 1)[0]
    assert "tdma_flight_engine_commit_input" not in tx_load
    assert "tdma_flight_engine_inspect_input" not in tx_load

    rx_unload = engine_c.split(
        "bool tdma_flight_engine_rx_unload(", 1
    )[1].split("bool tdma_flight_engine_expected_input_mask", 1)[0]
    assert "tdma_flight_engine_inspect_input(" in rx_unload
    assert "tdma_flight_engine_tx_load" not in rx_unload

    rx_commit = engine_c.split(
        "bool tdma_flight_engine_rx_commit(", 1
    )[1].split("bool tdma_flight_engine_get_snapshot", 1)[0]
    assert "tdma_flight_engine_commit_input(" in rx_commit

    assert adapter.count("tdma_flight_engine_tx_load(") >= 4
    assert "tdma_flight_engine_rx_unload(" in adapter
    assert "tdma_flight_engine_rx_commit(" in adapter
    assert "tdma_flight_engine_apply_preclassified(" not in adapter
    assert "tdma_flight_engine_commit_input(" not in adapter


def test_sync_io_static_gate_rejects_legacy_output_pio(tmp_path: Path) -> None:
    model = tmp_path / "sync_io_model_sched.c"
    model_text = (ROOT / "components/sync_io/src/sync_io_model_sched.c").read_text(
        encoding="utf-8"
    )
    prefix, suffix = model_text.split(
        "bool sync_io_output_pulse_schedule_arm(", 1
    )
    function, tail = suffix.split(
        "bool sync_io_output_pulse_schedule_arm_ns", 1
    )
    function = function.replace(
        "BOARD_SYNC_PIO_FAST,\n"
        "        BOARD_SYNC_PIO0_WAVE_OUTPUT_SM,\n"
        "        DREQ_PIO0_TX0 + BOARD_SYNC_PIO0_WAVE_OUTPUT_SM,",
        "BOARD_SYNC_PIO_WAVE,\n"
        "        BOARD_SYNC_MODEL_SCHED_SM,\n"
        "        DREQ_PIO1_TX0 + BOARD_SYNC_MODEL_SCHED_SM,",
        1,
    )
    model.write_text(
        prefix + "bool sync_io_output_pulse_schedule_arm(" + function +
        "bool sync_io_output_pulse_schedule_arm_ns" + tail,
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
        sync_model=model,
    )
    assert "SYNC WAVE_OUTPUT arm contains forbidden BOARD_SYNC_PIO_WAVE" in failures


def test_sync_io_static_gate_rejects_reintroduced_tdma_handoff(
    tmp_path: Path,
) -> None:
    core = tmp_path / "sync_io.c"
    core_text = (ROOT / "components/sync_io/src/sync_io.c").read_text(
        encoding="utf-8"
    )
    core.write_text(
        core_text + "\nbool sync_io_suspend_for_tdma_flight(void) { return true; }\n",
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
        sync_core=core,
    )
    assert (
        "legacy PIO1 TDMA handoff symbol remains in SYNC_IO: "
        "sync_io_suspend_for_tdma_flight" in failures
    )


def test_sync_io_static_gate_rejects_schedule_without_capture_guard(
    tmp_path: Path,
) -> None:
    model = tmp_path / "sync_io_model_sched.c"
    model.write_text(
        (
            ROOT / "components/sync_io/src/sync_io_model_sched.c"
        ).read_text(encoding="utf-8").replace(
            "sync_io_core_capture_is_running() ||",
            "false ||",
            1,
        ),
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
        sync_model=model,
    )

    assert (
        "SYNC output schedule common arm is missing "
        "sync_io_core_capture_is_running"
        in failures
    )


def test_sync_io_static_gate_rejects_private_schedule_workspace(
    tmp_path: Path,
) -> None:
    model = tmp_path / "sync_io_model_sched.c"
    model.write_text(
        (
            ROOT / "components/sync_io/src/sync_io_model_sched.c"
        ).read_text(encoding="utf-8").replace(
            "s_model_pulse.words = sync_io_shared_workspace;",
            "s_model_pulse.words = private_schedule_workspace;",
            1,
        ),
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
        sync_model=model,
    )

    assert (
        "SYNC output schedule common arm is missing "
        "s_model_pulse.words = sync_io_shared_workspace"
        in failures
    )


def test_sync_io_static_gate_rejects_schedule_replacement_before_disarm(
    tmp_path: Path,
) -> None:
    model = tmp_path / "sync_io_model_sched.c"
    model.write_text(
        (
            ROOT / "components/sync_io/src/sync_io_model_sched.c"
        ).read_text(encoding="utf-8").replace(
            "sync_io_model_pulse_schedule_disarm();\n\n"
            "    /* The schedule shares the capture DMA workspace.  Both APIs reject an\n"
            "     * active peer, so assigning the workspace here cannot race a DMA owner. */\n"
            "    s_model_pulse.words = sync_io_shared_workspace;",
            "s_model_pulse.words = sync_io_shared_workspace;\n"
            "    sync_io_model_pulse_schedule_disarm();",
            1,
        ),
        encoding="utf-8",
    )

    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
        sync_model=model,
    )

    assert (
        "SYNC output schedule arm must reject active capture, disarm the old "
        "schedule, bind the shared workspace, then start the PIO0 persona"
        in failures
    )


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
    assert "previous_resources_held" in rollback
    assert "if (!previous_resources_held)" in rollback
    assert rollback.index(
        "tdma_pio_spi_programs_release_resources", restore_program
    ) > restore_program
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
        "test_tdma_rx_endpoint_contract",
        "tdma_state_machine_rx_endpoint_contract_valid",
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
