"""Guard the instruction-memory writes used when ARM installs the follower.

Resolve public PIO labels independently of the C init function, then check
each write still targets a WAIT for the intended edge. Mutations model the
dispatch growth that previously left handwritten patch offsets vulnerable.
"""

from pathlib import Path
import re

import pytest


ROOT = Path(__file__).resolve().parents[2]
PIO_SOURCE = ROOT / "components/tdma/src/tdma_pio_spi.pio"
PROGRAM = "tdma_pio_spi_flight_process_follower"


def _check_patch_targets(source: str) -> dict[str, int]:
    body = re.search(
        rf"^\.program {PROGRAM}\s*$([\s\S]*?)^\.wrap\s*$",
        source, re.MULTILINE,
    )
    assert body is not None
    instructions: list[str] = []
    labels: dict[str, int] = {}
    for raw_line in body.group(1).splitlines():
        line = raw_line.split(";", 1)[0].strip()
        if line.startswith("public ") and line.endswith(":"):
            labels[line[len("public "):-1]] = len(instructions)
        elif line and not line.startswith(".") and not line.endswith(":"):
            instructions.append(line)

    init = re.search(
        rf"static inline void {PROGRAM}_program_init\([\s\S]*?\n\}}", source,
    )
    assert init is not None
    patches = re.findall(
        r"pio->instr_mem\[offset\s*\+\s*([^\]]+)\]\s*=([^;]+);",
        init.group(0),
    )
    expected = {
        ("true", "rx_csn_pin"): "wait 1 gpio 0",
        ("true", "rx_sck_pin"): "wait 1 gpio 1",
        ("false", "rx_sck_pin"): "wait 0 gpio 1",
    }
    assert len(patches) == len(expected)
    remaining = dict(expected)
    positions = {}
    for target, value in patches:
        symbol = re.fullmatch(rf"{PROGRAM}_offset_(\w+)", target.strip())
        assert symbol is not None, "patch offset must come from pioasm"
        label = symbol.group(1)
        assert label in labels, "patch must use an exported instruction label"
        edge = re.search(r"pio_encode_wait_gpio\((true|false),\s*(\w+)\)", value)
        assert edge is not None
        key = edge.groups()
        assert key in remaining, "missing or duplicate edge patch"
        assert instructions[labels[label]] == remaining.pop(key), "patch overwrites wrong instruction"
        if key == ("true", "rx_sck_pin"):
            assert "pio_encode_delay(data_phase_delay_cycles)" in value
        positions[label] = labels[label]
    assert not remaining
    return positions


def test_repository_patches_target_the_intended_wait_edges() -> None:
    _check_patch_targets(PIO_SOURCE.read_text(encoding="utf-8"))


def test_dispatch_insertion_relocates_all_patch_targets() -> None:
    source = PIO_SOURCE.read_text(encoding="utf-8")
    original = _check_patch_targets(source)
    source = source.replace("flight_process_command:\n", "flight_process_command:\n    nop\n", 1)
    shifted = _check_patch_targets(source)
    assert shifted == {name: offset + 1 for name, offset in original.items()}


@pytest.mark.parametrize("label", ["wait_csn_high", "wait_sck_high", "wait_sck_low"])
def test_handwritten_offset_is_rejected(label: str) -> None:
    source = PIO_SOURCE.read_text(encoding="utf-8")
    offset = _check_patch_targets(source)[label]
    source = source.replace(f"{PROGRAM}_offset_{label}", f"{offset}u", 1)
    with pytest.raises(AssertionError, match="patch offset must come from pioasm"):
        _check_patch_targets(source)


@pytest.mark.parametrize("label", ["wait_csn_high", "wait_sck_high", "wait_sck_low"])
def test_label_detached_from_wait_is_rejected(label: str) -> None:
    source = PIO_SOURCE.read_text(encoding="utf-8")
    source = source.replace(f"public {label}:\n", f"public {label}:\n    nop\n", 1)
    with pytest.raises(AssertionError, match="patch overwrites wrong instruction"):
        _check_patch_targets(source)


def test_wrong_edge_symbol_is_rejected() -> None:
    source = PIO_SOURCE.read_text(encoding="utf-8")
    source = source.replace(f"{PROGRAM}_offset_wait_sck_high", f"{PROGRAM}_offset_wait_sck_low", 1)
    with pytest.raises(AssertionError, match="patch overwrites wrong instruction"):
        _check_patch_targets(source)
