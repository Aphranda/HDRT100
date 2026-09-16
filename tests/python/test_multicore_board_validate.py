"""The runtime-protection version is the live RefMem layout ABI."""
import pytest

from tools.multicore_board_validate import multicore_board_validate as validate


def _responses(core_version=2, protection_version=2):
    return {
        "SYST:CORE:VECTor?": [core_version, 7, 2, 0, 1, 0, 8, 2, 0, 0, 123, 0, 0],
        "SYST:PROTection:STATus?": [protection_version, 7, 1, 1, 1, 0, 0, 0, 0, 9, 3, 3, 3, 0, 0, 2, 15, 0, 123, 0, 0],
    }


@pytest.mark.parametrize("core_version,protection_version,passed", [(2, 2, True), (1, 2, False), (2, 1, False), (3, 2, False), (2, 3, False)])
def test_protection_requires_current_layout(monkeypatch, core_version, protection_version, passed):
    responses = _responses(core_version, protection_version)
    monkeypatch.setattr(validate, "_query", lambda ser, query, timeout: ",".join(map(str, responses[query])))
    result, _ = validate.test_runtime_protection_tables(None, 1.0)
    assert result is passed


@pytest.mark.parametrize("query,index,value", [
    ("SYST:CORE:VECTor?", 4, 0), ("SYST:CORE:VECTor?", 6, 0),
    ("SYST:PROTection:STATus?", 4, 0), ("SYST:PROTection:STATus?", 13, 1),
    ("SYST:PROTection:STATus?", 18, 0),
])
def test_current_layout_retains_owner_and_lockout_gates(monkeypatch, query, index, value):
    responses = _responses()
    responses[query][index] = value
    monkeypatch.setattr(validate, "_query", lambda ser, command, timeout: ",".join(map(str, responses[command])))
    result, _ = validate.test_runtime_protection_tables(None, 1.0)
    assert not result
