from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_v2_journal_uses_disjoint_completion_and_checkpoint_regions():
    source = (ROOT / "components/ota_manager/src/ota_journal.c").read_text(
        encoding="utf-8"
    )

    assert "OTA_JOURNAL_COMPLETION_REGION_SIZE" in source
    assert "OTA_JOURNAL_CHECKPOINT_REGION_OFFSET" in source
    assert "ota_journal_region_range_valid" in source
    assert "flash_transaction_journal_init(&s_completion_store" in source
    assert "flash_transaction_ao_set_completion_lease(&s_completion_lease)" in source
    assert "flash_transaction_ao_journal_program" in source
    assert "flash_transaction_ao_journal_erase" in source
    assert "flash_transaction_ao_execute(" not in source


def test_v2_durable_init_precedes_metadata_dependent_stream_open():
    ota_ao = (ROOT / "components/ota_manager/src/ota_ao.c").read_text(
        encoding="utf-8"
    )
    durable_call = ota_ao.index("portable_ota_port_durable_init()")
    metadata_load = ota_ao.index("ota_metadata_load(&metadata)")
    assert durable_call < metadata_load


def test_stream_surface_reuses_durable_init_instead_of_reinitializing_store():
    source = (ROOT / "middleware/portable_ota_port/src/portable_ota_core_port.c").read_text(
        encoding="utf-8"
    )

    assert "bool portable_ota_port_durable_init(void)" in source
    assert "!portable_ota_port_durable_init() ||" in source
    assert source.count("ota_journal_init()") == 1
