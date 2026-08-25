from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
PIO_SOURCE = ROOT / "components" / "tdma" / "src" / "tdma_pio_spi.pio"


def _program_instructions(source: str, name: str) -> list[str]:
    match = re.search(
        rf"^\.program {re.escape(name)}\s*$([\s\S]*?)(?=^\.program |^% c-sdk)",
        source,
        re.MULTILINE,
    )
    assert match is not None
    instructions = []
    for raw_line in match.group(1).splitlines():
        line = raw_line.split(";", 1)[0].strip()
        if not line or line.startswith(".") or line.endswith(":"):
            continue
        instructions.append(line)
    return instructions


def test_data_train_source_patch_positions_match_pio_program() -> None:
    source = PIO_SOURCE.read_text(encoding="utf-8")
    instructions = _program_instructions(
        source, "tdma_pio_spi_data_train_source")

    assert instructions[3].startswith("wait 1 gpio")
    assert instructions[4].startswith("wait 0 gpio")
    assert instructions[5].startswith("jmp x--")
    assert instructions[6] == "nop"
    assert instructions[7].startswith("out pins")

    assert "DATA_TRAIN_SOURCE_WAIT_HIGH_INSTRUCTION = 3u" in source
    assert "DATA_TRAIN_SOURCE_WAIT_LOW_INSTRUCTION = 4u" in source
    assert "DATA_TRAIN_SOURCE_PHASE_DELAY_INSTRUCTION = 6u" in source
    assert "offset + DATA_TRAIN_SOURCE_WAIT_HIGH_INSTRUCTION" in source
    assert "offset + DATA_TRAIN_SOURCE_WAIT_LOW_INSTRUCTION" in source
    assert "offset + DATA_TRAIN_SOURCE_PHASE_DELAY_INSTRUCTION" in source
