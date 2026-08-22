from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "middleware/scpi_port/inc/scpi_ota_commands.h"
SOURCE = ROOT / "middleware/scpi_port/src/scpi_ota_commands.c"
SCHEMA = ROOT / "config/persistence_schema_registry.json"
BOARD_VALIDATOR = ROOT / "tools/ota_board_validate/ota_board_validate.py"
SCPI_SERIAL = ROOT / "tools/scpi_common/scpi_serial.py"


def test_ota_journal_query_has_independent_namespace() -> None:
    header = HEADER.read_text(encoding="utf-8")
    assert '"SYSTem:OTA:JOURnal?"' in header
    assert ".callback = scpi_cmd_ota_journal_q" in header
    assert header.count('"SYSTem:OTA:JOURnal?"') == 1


def test_ota_journal_query_projects_stable_owner_fields() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index("scpi_result_t scpi_cmd_ota_journal_q")
    end = source.index("\n}\n", start)
    body = source[start:end]
    fields = [
        "snapshot.valid",
        "snapshot.result",
        "snapshot.sequence",
        "snapshot.checkpoint.session_id",
        "snapshot.checkpoint.generation",
        "snapshot.checkpoint.token",
        "snapshot.checkpoint.object_id",
        "snapshot.checkpoint.durable_offset",
        "snapshot.checkpoint.total_size",
        "snapshot.checkpoint.package_crc32",
        "snapshot.checkpoint.chunk_crc32",
        "snapshot.checkpoint.durable_crc32",
    ]
    positions = [body.index(field) for field in fields]
    assert positions == sorted(positions)
    assert body.count("SCPI_ResultUInt32") == len(fields)


def test_ota_journal_query_is_wired_into_schema_and_hil_tools() -> None:
    schema = SCHEMA.read_text(encoding="utf-8")
    board_validator = BOARD_VALIDATOR.read_text(encoding="utf-8")
    scpi_serial = SCPI_SERIAL.read_text(encoding="utf-8")
    assert '"object_type":"OTA_JOURNAL"' in schema
    assert '"diagnostic_projection":"SYST:OTA:JOUR?"' in schema
    assert '"SYST:OTA:JOUR?"' in board_validator
    assert '"SYST:OTA:JOUR?"' in scpi_serial
    assert "_csv_uints_match(text, 12)" in scpi_serial
