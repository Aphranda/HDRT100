"""Lock the persisted core1 realtime phase table and its v1/v2 compatibility.

The component only talks to the journal through drv_flash/FlashTransaction, so
the behavioural lock is the host unit test compiled the same way
tools/tests/run_product_config_tests.ps1 does it; the source checks below lock
the record contract the unit test and its mirror encode.
"""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]

HEADER = ROOT / "components/product_config/inc/product_config.h"
SOURCE = ROOT / "components/product_config/src/product_config.c"
UNIT_SOURCE = ROOT / "tests/unit/test_product_config.c"

MAX_PHASES = 10
# reserved[] word map of the persisted realtime table (see the source comment).
MARKER_WORD = 10
CYCLE_WORD = 11
PHASE_COUNT_WORD = 12
START_WORD = 13
END_WORD = 23
WCET_WORD = 33
RESERVED_WORDS = 43

INCLUDES = [
    "components/product_config/inc",
    "components/flash_transaction/inc",
    "components/ota_manager/inc",
    "drivers/mcu/flash/inc",
    "config",
]


def _flatten(text: str) -> str:
    return re.sub(r"\s+", " ", text.replace("\\\n", " "))


def _function(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return source[start:end]


def _numeric_define(text: str, name: str) -> int:
    match = re.search(r"^#define\s+" + name + r"\s+(\d+)u?", text, re.M)
    assert match, name
    return int(match.group(1))


def test_realtime_table_word_map_matches_the_unit_test_mirror() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    unit = UNIT_SOURCE.read_text(encoding="utf-8")

    assert _numeric_define(source, "PRODUCT_CONFIG_REALTIME_PROFILE_MARKER_WORD") \
        == MARKER_WORD
    assert _numeric_define(source, "PRODUCT_CONFIG_REALTIME_PROFILE_CYCLE_WORD") \
        == CYCLE_WORD
    assert _numeric_define(
        source, "PRODUCT_CONFIG_REALTIME_PROFILE_PHASE_COUNT_WORD") \
        == PHASE_COUNT_WORD
    assert _numeric_define(source, "PRODUCT_CONFIG_REALTIME_PROFILE_START_WORD") \
        == START_WORD
    mirror = {
        "TEST_REALTIME_MARKER_WORD": MARKER_WORD,
        "TEST_REALTIME_CYCLE_WORD": CYCLE_WORD,
        "TEST_REALTIME_PHASE_COUNT_WORD": PHASE_COUNT_WORD,
        "TEST_REALTIME_START_WORD": START_WORD,
        "TEST_REALTIME_END_WORD": END_WORD,
        "TEST_REALTIME_WCET_WORD": WCET_WORD,
        "TEST_RECORD_RESERVED_WORDS": RESERVED_WORDS,
    }
    assert {name: _numeric_define(unit, name) for name in mirror} == mirror

    flat = _flatten(source)
    # The later arrays are derived from the first one, never re-spelled.
    assert ("#define PRODUCT_CONFIG_REALTIME_PROFILE_END_WORD "
            "(PRODUCT_CONFIG_REALTIME_PROFILE_START_WORD + "
            "PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES)") in flat
    assert ("#define PRODUCT_CONFIG_REALTIME_PROFILE_WCET_WORD "
            "(PRODUCT_CONFIG_REALTIME_PROFILE_END_WORD + "
            "PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES)") in flat
    assert ("#define PRODUCT_CONFIG_RESERVED_WORDS "
            "(PRODUCT_CONFIG_REALTIME_PROFILE_WCET_WORD + "
            "PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES)") in flat
    # 30 window words plus marker/cycle/phase_count need 43 reserved words; the
    # header still publishes the phase count the record must carry.
    assert WCET_WORD + MAX_PHASES == RESERVED_WORDS
    assert "PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES 10u" in \
        HEADER.read_text(encoding="utf-8")
    assert ("_Static_assert(PRODUCT_CONFIG_REALTIME_PROFILE_WCET_WORD + "
            "PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES == "
            "sizeof(((product_config_record_t *)0)->reserved) / "
            "sizeof(uint32_t),") in flat
    assert ("_Static_assert(sizeof(product_config_record_t) <= "
            "PRODUCT_CONFIG_SLOT_SIZE,") in flat


def test_record_versions_and_legacy_crc_domain() -> None:
    source = SOURCE.read_text(encoding="utf-8")

    assert _numeric_define(source, "PRODUCT_CONFIG_VERSION") == 3
    assert _numeric_define(source, "PRODUCT_CONFIG_VERSION_V2") == 2
    assert _numeric_define(source, "PRODUCT_CONFIG_VERSION_LEGACY") == 1
    # ASCII marker in the style of the DPLL markers: "RTPF".
    assert "#define PRODUCT_CONFIG_REALTIME_PROFILE_VALID 0x52545046u" in source

    # The deployed 64-byte prefix keeps its own struct and its own CRC domain.
    legacy = _function(source, "/* Deployed record of versions 1 and 2",
                       "} product_config_legacy_record_t;")
    assert "uint32_t reserved[10];" in legacy
    assert "_Static_assert(sizeof(product_config_legacy_record_t) == 64u," in source

    legacy_crc = _function(source, "static uint32_t product_config_legacy_crc32(",
                           "static bool product_config_legacy_record_is_valid(")
    assert "legacy.crc32 = 0u;" in legacy_crc
    assert "ota_crc32_compute((const uint8_t *)&legacy, sizeof(legacy))" in legacy_crc

    valid = _function(source, "static bool product_config_legacy_record_is_valid(",
                      "/* RAM-only promotion")
    assert "PRODUCT_CONFIG_VERSION_LEGACY" in valid
    assert "PRODUCT_CONFIG_VERSION_V2" in valid


def test_legacy_promotion_is_ram_only_and_init_never_writes() -> None:
    source = SOURCE.read_text(encoding="utf-8")

    promote = _function(source, "/* RAM-only promotion",
                        "/* Validates exactly the current layout")
    assert "record->version = PRODUCT_CONFIG_VERSION;" in promote
    # The realtime words are never carried over from a prefix that had none.
    assert "memcpy(record->reserved, legacy.reserved, sizeof(legacy.reserved));" \
        in promote
    assert "product_config_store" not in promote
    assert "product_config_flash_execute" not in promote

    find_latest = _function(source, "static bool product_config_find_latest(",
                            "static bool product_config_store(")
    assert "product_config_legacy_record_is_valid(" in find_latest
    assert "product_config_legacy_record_promote(" in find_latest
    assert "product_config_flash_execute" not in find_latest

    init = _function(source, "bool product_config_init(void)",
                     "bool product_config_get_usb_mode")
    assert "product_config_store" not in init
    assert "product_config_flash_execute" not in init
    assert "drv_flash" not in init


def test_realtime_accessors_keep_their_guards() -> None:
    source = SOURCE.read_text(encoding="utf-8")

    getter = _function(source, "bool product_config_get_realtime_profile(",
                       "bool product_config_set_realtime_profile(")
    assert "profile == NULL" in getter
    assert "!product_config_record_is_valid(&s_product_config)" in getter
    assert "!product_config_realtime_profile_is_valid(&s_product_config)" in getter
    # A refused read must not touch the caller's profile.
    assert "*profile = decoded;" in getter
    assert getter.index("*profile = decoded;") > getter.index("decoded.cycle_cycles")

    setter = _function(source, "bool product_config_set_realtime_profile(",
                       "bool product_config_clear_realtime_profile(void)")
    assert "product_config_realtime_profile_is_storable(profile)" in setter
    assert "record.sequence++;" in setter
    assert "record.crc32 = product_config_crc32(&record);" in setter
    assert "product_config_store(&record) &&" in setter
    assert "product_config_realtime_profiles_equal(&readback, profile)" in setter

    clear = _function(source, "bool product_config_clear_realtime_profile(void)",
                      "bool product_config_get_dpll_servo_profile(")
    assert "product_config_record_clear_realtime_profile(&record);" in clear
    assert "!product_config_get_realtime_profile(&readback)" in clear

    storable = _function(source, "static bool product_config_realtime_profile_is_storable(",
                         "static void product_config_record_set_realtime_profile(")
    assert "profile->phase_count == PRODUCT_CONFIG_REALTIME_PROFILE_MAX_PHASES" in storable
    assert "profile->cycle_cycles != 0u" in storable
    # Admission of the phase arithmetic is the caller's job.
    for judgement in ("end_cycle", "wcet_cycles", "start_cycle"):
        assert judgement not in storable

    cleared = _function(source, "/* The marker is cleared and nothing else:",
                        "static bool product_config_realtime_profiles_equal(")
    assert "PRODUCT_CONFIG_REALTIME_PROFILE_MARKER_WORD] = 0u;" in cleared
    assert cleared.count("reserved[") == 1


def test_host_unit_test_executes_the_realtime_profile_cases() -> None:
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler, "A host gcc or clang compiler is required"

    unit = UNIT_SOURCE.read_text(encoding="utf-8")
    for case in ("test_realtime_profile_is_absent_until_stored",
                 "test_realtime_profile_round_trips_through_flash",
                 "test_realtime_profile_stored_layout_and_clear",
                 "test_realtime_profile_rejects_incomplete_or_empty_table",
                 "test_realtime_profile_never_judges_phase_arithmetic",
                 "test_realtime_record_guards_reject_malformed_tables",
                 "test_v2_record_is_accepted_and_keeps_realtime_absent"):
        assert f"    {case}();" in unit, case

    # Same build directory and flags as tools/tests/run_product_config_tests.ps1.
    build = ROOT / "out/pytest/build-product-config-tests"
    build.mkdir(parents=True, exist_ok=True)
    exe = build / ("test_product_config.exe" if os.name == "nt"
                   else "test_product_config")
    command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
               *[f"-I{ROOT / path}" for path in INCLUDES],
               str(UNIT_SOURCE), str(SOURCE), "-o", str(exe)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=180)
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "product config host unit tests passed" in result.stdout
