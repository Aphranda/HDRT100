"""Guard the host harness against one-machine Pico SDK paths.

``tests/harness`` builds the real VDC units for the *developer* machine with
``PICO_PLATFORM=host``, so its CMakeLists must resolve the Pico SDK for whoever
runs it: an explicit ``PICO_SDK_PATH`` (environment or ``-D``) first, then the
per-user Pico VS Code extension install. A hard-coded home directory from
another machine makes ``test_vdc_host_harness.py`` fail at configure time on
every other checkout, which is what these checks prevent from coming back.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tests" / "harness" / "CMakeLists.txt"


def harness_text() -> str:
    return HARNESS.read_text(encoding="utf-8")


def test_harness_cmake_pins_no_machine_home_directory() -> None:
    text = harness_text()
    assert not re.search(r"[A-Za-z]:[\\/]Users[\\/][^\"'\s]+", text), (
        "tests/harness/CMakeLists.txt must not hard-code a Windows user home")
    assert not re.search(r"/home/[^\"'\s]+", text), (
        "tests/harness/CMakeLists.txt must not hard-code a POSIX user home")


def test_harness_cmake_resolves_the_sdk_per_user() -> None:
    text = harness_text()
    # Both home conventions, and the environment override, stay reachable.
    assert "$ENV{USERPROFILE}" in text and "$ENV{HOME}" in text
    assert "$ENV{PICO_SDK_PATH}" in text
    # The fallback is the per-user Pico VS Code extension install, and a
    # candidate only counts when it really is a Pico SDK tree.
    assert ".pico-sdk" in text
    assert "external/pico_sdk_import.cmake" in text
    # No SDK left: stop the configure with the instruction instead of a
    # confusing "include could not find requested file" further down.
    assert "FATAL_ERROR" in text


def test_harness_cmake_keeps_the_host_platform_and_repo_root() -> None:
    text = harness_text()
    assert 'set(PICO_PLATFORM host CACHE STRING "" FORCE)' in text
    assert "set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)" in text
