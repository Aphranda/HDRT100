"""Regression checks for the DPLL observer's TDMA DATA-path contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def _function(source: str, start: str, end: str) -> str:
    return source.split(start, 1)[1].split(end, 1)[0]


def test_vdc_path_tables_follow_reverse_tdma_data_flow() -> None:
    source = (ROOT / "components" / "vdc_dpll_manager" / "src" /
              "vdc_dpll_manager.c").read_text(encoding="utf-8")

    calibrated = _function(
        source, "static bool vdc_dpll_manager_build_calibration_path_table(",
        "bool vdc_dpll_manager_publish_calibration_path_snapshot(")
    assert "source_slot_id = link->destination_node" in calibrated
    assert "reference_slot_id = link->source_node" in calibrated
    assert ("entry->direction = "
            "VDC_PATH_DELAY_DIRECTION_TDMA_DATA_REVERSE" in calibrated)

    provisional = _function(
        source,
        "static bool vdc_dpll_manager_build_provisional_training_path_table(",
        "bool vdc_dpll_manager_activate_tdma_provisional_training(void)")
    assert "entry->source_slot_id = link->data_source_node" in provisional
    assert ("entry->reference_slot_id = link->data_destination_node" in
            provisional)
    assert ("entry->direction = "
            "VDC_PATH_DELAY_DIRECTION_TDMA_DATA_REVERSE" in provisional)
    assert "entry->writer = link->data_source_node" in provisional
