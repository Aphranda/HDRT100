from pathlib import Path

from tools.state_machine_resource_check import state_machine_resource_check


ROOT = Path(__file__).resolve().parents[2]


def test_directional_resource_contract_is_valid() -> None:
    failures = state_machine_resource_check.check(
        ROOT / "boards/rp2350_trig/inc/board_config.h",
        ROOT / "components/tdma/src/tdma_pio_spi.pio",
    )
    assert failures == []


def test_directional_pin_semantics_reject_swapped_data_port(tmp_path: Path) -> None:
    board = tmp_path / "board_config.h"
    board_text = (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(
        encoding="utf-8"
    )
    board.write_text(
        board_text.replace(
            "#define BOARD_TDMA_TX_DATA_IN_PIN BOARD_TDMA_SPI_UPLINK_RX_PIN",
            "#define BOARD_TDMA_TX_DATA_IN_PIN BOARD_TDMA_SPI_DOWNLINK_TX_PIN",
        ),
        encoding="utf-8",
    )
    pio = tmp_path / "tdma_pio_spi.pio"
    pio.write_text(
        (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
            encoding="utf-8"
        ),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(board, pio)
    assert any("direction mismatch: BOARD_TDMA_TX_DATA_IN_PIN" in item
               for item in failures)


def test_directional_contract_rejects_missing_crossed_direction(tmp_path: Path) -> None:
    board = tmp_path / "board_config.h"
    board.write_text(
        (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(encoding="utf-8")
    pio.write_text(
        pio_text.replace(
            ".program tdma_pio_spi_directional_tx\n.wrap_target\n"
            "    pull block\n    out pins, 2\n    wait 1 gpio 0\n    in pins, 1",
            ".program tdma_pio_spi_directional_tx\n.wrap_target\n"
            "    pull block\n    out pins, 2\n    wait 1 gpio 0",
        ),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(board, pio)
    assert "directional TX is missing in pins" in failures


def test_flight_data_sm_must_publish_unload_bytes(
    tmp_path: Path,
) -> None:
    board = tmp_path / "board_config.h"
    board.write_text(
        (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(
            encoding="utf-8"),
        encoding="utf-8",
    )
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    pio.write_text(
        pio_text.replace("    push noblock\n    out null, 24",
                         "    out null, 24", 1),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(board, pio)
    assert "flight raw follower is missing unload push" in failures


def test_process_flight_data_sm_must_publish_unload_bytes(tmp_path: Path) -> None:
    board = tmp_path / "board_config.h"
    board.write_text(
        (ROOT / "boards/rp2350_trig/inc/board_config.h").read_text(
            encoding="utf-8"),
        encoding="utf-8",
    )
    pio = tmp_path / "tdma_pio_spi.pio"
    pio_text = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    pio.write_text(
        pio_text.replace(
            "    mov y, isr\n    push noblock",
            "    mov y, isr",
        ),
        encoding="utf-8",
    )
    failures = state_machine_resource_check.check(board, pio)
    assert "flight process follower is missing unload push" in failures


def test_flight_capture_patches_actual_wait_instructions() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    init = source.split(
        "static inline void tdma_pio_spi_flight_data_capture_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    assert "instr_mem[offset + 0u]" in init
    assert "instr_mem[offset + 2u]" in init
    assert "instr_mem[offset + 4u]" in init
    assert "instr_mem[offset + 3u]" not in init
    assert "instr_mem[offset + 5u]" not in init


def test_calibration_transition_unloads_flight_programs_from_declared_pios() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8")
    transition = source.split(
        "static bool tdma_pio_spi_phys_cal_unload_source_step", 1,
    )[1].split("static bool tdma_pio_spi_phys_cal_load_p3_step", 1)[0]
    assert "count = 4u" in transition
    assert "count = 3u" in transition
    assert "pio_remove_program(BOARD_TDMA_TX_PIO" in transition
    assert "pio_remove_program(BOARD_TDMA_RX_PIO" in transition
    assert "tdma_pio_spi_flight_data_capture_program" not in transition


def test_flight_setup_initializes_cross_pio_inputs_without_outputs() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    for init_name, required in (
        ("tdma_pio_spi_flight_control_forward_program_init",
         ("pio_gpio_init(pio, rx_csn_pin)",
          "pio_gpio_init(pio, rx_sck_pin)")),
        ("tdma_pio_spi_flight_data_follower_program_init",
         ("pio_gpio_init(pio, rx_data_pin)",
          "pio_gpio_init(pio, rx_sck_pin)")),
        ("tdma_pio_spi_flight_process_follower_program_init",
         ("pio_gpio_init(pio, rx_data_pin)",
          "pio_gpio_init(pio, rx_csn_pin)",
          "pio_gpio_init(pio, rx_sck_pin)")),
    ):
        body = source.split(f"static inline void {init_name}", 1)[1].split(
            "static inline void", 1)[0]
        for token in required:
            assert token in body


def test_calibration_switch_ready_has_directional_flight_gate() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8")
    ready = source.split(
        "static bool tdma_pio_spi_phys_cal_persona_switch_ready", 1,
    )[1].split("static bool tdma_pio_spi_phys_cal_unload_source_step", 1)[0]
    assert "tdma_pio_spi_phys_is_flight_persona()" in ready
    assert "BOARD_TDMA_TX_PIO->ctrl" in ready
    assert "BOARD_TDMA_RX_PIO->ctrl" in ready


def test_flight_claim_reserves_dma_gpio_irq_and_dreq_classes() -> None:
    source = "\n".join(
        (ROOT / "components/tdma/src" / name).read_text(encoding="utf-8")
        for name in (
            "tdma_pio_spi_phys.c",
            "tdma_pio_spi_phys_persona.c",
        )
    )
    resources = (ROOT / "components/tdma/inc/tdma_state_machine_resources.h").read_text(
        encoding="utf-8")
    assert "TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK" in source
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


def test_maintenance_claim_uses_same_arbiter_projection() -> None:
    source = "\n".join(
        (ROOT / "components/tdma/src" / name).read_text(encoding="utf-8")
        for name in (
            "tdma_pio_spi_phys.c",
            "tdma_pio_spi_phys_persona.c",
        )
    )
    resources = (ROOT / "components/tdma/inc/tdma_state_machine_resources.h").read_text(
        encoding="utf-8")
    assert "TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK" in resources
    assert "TDMA_MAINTENANCE_RESOURCE_OWNER" in source
    assert "s_tdma_pio_spi_maintenance_resources_claimed" in source
    assert "resource_arbiter_acquire_owned(" in source
    assert "resource_arbiter_release_owned(" in source


def test_maintenance_projection_covers_shared_runtime_classes() -> None:
    resources = (ROOT / "components/tdma/inc/tdma_state_machine_resources.h").read_text(
        encoding="utf-8")
    mask = resources.split(
        "#define TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK", 1
    )[1].split("\n\n", 1)[0]
    for token in (
        "RESOURCE_ARBITER_RESOURCE_PIO2",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DMA_OUTPUT",
        "RESOURCE_ARBITER_RESOURCE_TDMA_IRQ",
        "RESOURCE_ARBITER_RESOURCE_TDMA_DREQ",
        "RESOURCE_ARBITER_RESOURCE_TDMA_GPIO",
    ):
        assert token in mask


def test_flight_arm_transfers_persona_before_claiming_overlapping_resources() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys.c").read_text(
        encoding="utf-8"
    )
    arm = source.split("bool tdma_pio_spi_phys_arm", 1)[1].split(
        "void tdma_pio_spi_phys_disarm", 1
    )[0]
    assert arm.index("tdma_pio_spi_phys_select_program_persona") < arm.index(
        "tdma_pio_spi_phys_claim_flight_resources"
    )
    assert "TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK" not in arm


def test_persona_selection_claims_flight_owner_before_sm_install() -> None:
    source = (
        ROOT / "components/tdma/src/tdma_pio_spi_phys_persona.c"
    ).read_text(encoding="utf-8")
    selection = source.split(
        "bool tdma_pio_spi_phys_select_program_persona", 1
    )[1].split(
        "bool tdma_pio_spi_phys_claim_flight_resources", 1
    )[0]
    target = selection.split("const bool target_claimed", 1)[1]
    assert "tdma_pio_spi_phys_claim_flight_resources(phys)" in target
    assert target.index("tdma_pio_spi_phys_claim_flight_resources(phys)") < target.index(
        "tdma_pio_spi_phys_ensure_flight_sms_claimed()"
    )
    assert "tdma_pio_spi_phys_release_flight_resources(phys);" in target


def test_tdma_ring_arm_preserves_selected_flight_persona() -> None:
    source = (
        ROOT / "components/distributed_refmem/src/distributed_refmem.c"
    ).read_text(encoding="utf-8")
    arm = source.split(
        "bool distributed_refmem_tdma_ring_arm", 1
    )[1].split(
        "distributed_refmem_tdma_arm_result_t", 1
    )[0]
    map_config = arm.index("tdma_service_configure_flight_map")
    ring_arm = arm.index("tdma_service_ring_arm(owner)")
    assert map_config < ring_arm
    assert "tdma_runtime_owner_set_flight_process_image_mode(true)" not in arm
    assert "Do not overwrite that selection here" in arm


def test_process_follower_patch_points_follow_replace_program_layout() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    init = source.split(
        "static inline void tdma_pio_spi_flight_process_follower_program_init",
        1,
    )[1].split("static inline void", 1)[0]
    assert "instr_mem[offset + 14u]" in init
    assert "instr_mem[offset + 16u]" in init
    assert "instr_mem[offset + 11u]" not in init
    assert "instr_mem[offset + 13u]" not in init


def test_process_replace_reloads_msb_aligned_byte() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi.pio").read_text(
        encoding="utf-8")
    replace = source.split("flight_process_replace:", 1)[1].split(
        "flight_process_pass:", 1)[0]
    assert "out x, 8" in replace
    assert "mov osr, x" in replace
    assert "out null, 24" in replace


def test_origin_transport_uses_bit_control_and_byte_data_counts() -> None:
    source = (ROOT / "components/tdma/src/tdma_pio_spi_phys_transport.c").read_text(
        encoding="utf-8")
    assert "pio_sm_put(control_pio, control_sm, clock_bits - 1u)" in source
    assert "pio_sm_put(data_pio, data_sm, clock_bytes - 1u)" in source
