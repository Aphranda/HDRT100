"""Invariants for the host fact-source helpers (N5.0.11a).

These tests do not exercise production behaviour.  They prove the *plumbing*
the migration depends on: every archived fragment name resolves to a live unit
that ships in the device build, the extractor picks definitions rather than
declarations or call sites, and no helper can silently fall back to the audit
archive.
"""
from pathlib import Path

import pytest

import c_text
import scpi_units
import tdma_units
import vdc_fragment_functions
import vdc_units

ROOT = Path(__file__).resolve().parents[2]
CMAKE = ROOT / "CMakeLists.txt"


def test_extractor_prefers_the_definition_over_the_declaration():
    text = (
        "bool foo(int value);\n"
        "static bool foo(int value)\n"
        "{\n"
        "    return value > 0;\n"
        "}\n"
        "static void caller(void) { assert(foo(1)); }\n"
    )
    body = c_text.function_body(text, "foo")
    assert body.startswith("static bool foo(int value)")
    assert body.endswith("}")


def test_extractor_rejects_a_call_site():
    text = "static void caller(void)\n{\n    if (foo(1)) { return; }\n}\n"
    with pytest.raises(KeyError):
        c_text.function_body(text, "foo")


def test_strip_prologue_leaves_the_first_declaration():
    text = '#include "a.h"\n\n#include "b.h"\n/* body */\nbool x(void) { return true; }\n'
    body = c_text.strip_prologue(text)
    assert body.startswith("/* body */")


@pytest.mark.parametrize("fragment", sorted(vdc_units.FRAGMENT_TO_UNIT))
def test_vdc_fragment_maps_to_a_live_unit(fragment):
    unit = vdc_units.unit_of(fragment)
    path = vdc_units.SRC / f"{unit}.c"
    assert path.is_file(), path
    assert "archive" not in path.as_posix()
    assert vdc_units.body_for(fragment).strip()
    assert f"components/vdc_dpll_manager/src/{unit}.c" in CMAKE.read_text(encoding="utf-8")


@pytest.mark.parametrize("unit,symbol", [
    ("vdc_priority_tx", "vdc_priority_tx_origin_trace_eligible_core1"),
    ("vdc_boundary", "vdc_boundary_capture_offer_core1"),
    ("vdc_priority_follow", "vdc_dpll_manager_set_priority_match"),
    ("vdc_output", "vdc_fixed_output_service_core0"),
    ("vdc_dpll_feedback", "vdc_dpll_manager_feedback_prepare_core0"),
    ("vdc_reference_config", "vdc_dpll_manager_set_reference_discipline"),
])
def test_vdc_units_expose_live_symbols(unit, symbol):
    body = vdc_units.function_body(unit, symbol)
    assert symbol in body.splitlines()[0]
    assert "{" in body


@pytest.mark.parametrize("fragment", sorted(scpi_units.FRAGMENT_TO_UNIT))
def test_scpi_fragment_has_a_unit_header_and_build_entry(fragment):
    source = scpi_units.source_path(fragment)
    header = scpi_units.header_path(fragment)
    assert source.is_file(), source
    assert header.is_file(), header
    assert "archive" not in source.as_posix()
    assert "scpi_cmd_" in header.read_text(encoding="utf-8")
    assert f"middleware/scpi_port/src/{source.name}" in CMAKE.read_text(encoding="utf-8")


def test_scpi_units_define_their_command_callbacks():
    callbacks = scpi_units.callbacks("scpi_trigger_switch_commands")
    assert "scpi_cmd_trigger_switch_load" in callbacks
    assert "scpi_cmd_trigger_switch_q" in callbacks
    assert scpi_units.body("scpi_trigger_switch_commands").strip()


@pytest.mark.parametrize("symbol,unit", [
    ("tdma_pio_spi_phys_disarm", "tdma_pio_spi_phys_arm.c"),
    ("tdma_pio_spi_phys_clk_train_terminal_core1", "tdma_pio_spi_phys_arm.c"),
])
def test_tdma_symbols_resolve_to_one_live_unit(symbol, unit):
    assert tdma_units.unit_with(symbol).name == unit
    assert symbol in tdma_units.function_body(symbol)


@pytest.mark.parametrize("module", [c_text, scpi_units, tdma_units, vdc_units,
                                    vdc_fragment_functions])
def test_helpers_never_read_the_audit_archive(module):
    source = Path(module.__file__).read_text(encoding="utf-8")
    assert "archive/" not in source
    assert "_inc_split_v1" not in source


def test_typedef_body_extracts_a_single_declaration():
    text = "typedef struct {\n    int a;\n} foo_t;\n\ntypedef struct {\n    int b;\n} bar_t;\n"
    assert c_text.typedef_body(text, "foo_t").startswith("typedef struct {")
    assert c_text.typedef_body(text, "foo_t").strip().endswith("} foo_t;")
    assert "bar_t" not in c_text.typedef_body(text, "foo_t")


def test_fragment_slice_carries_the_live_unit_body():
    code = vdc_units.fragment_slice("vdc_priority_tx")
    assert "vdc_priority_tx_work_t" in code          # type hoisted by the split
    assert "vdc_priority_tx_core1" in code           # provider entry point
    assert "priority_gap_publish" in code            # merged unit body
    assert "archive" not in code
