import datetime as dt
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "ram_budget_check" / "ram_budget_check.py"
SPEC = importlib.util.spec_from_file_location("ram_budget_check", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
ram_budget_check = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ram_budget_check)


def test_default_min_free_is_the_48k_release_gate() -> None:
    assert ram_budget_check.DEFAULT_MIN_FREE_BYTES == 48 * 1024
    args = ram_budget_check.parse_args([])
    assert ram_budget_check.minimum_free_bytes(args) == 48 * 1024


def test_debug_profile_uses_the_32k_gate() -> None:
    args = ram_budget_check.parse_args(["--profile", "debug"])
    assert ram_budget_check.minimum_free_bytes(args) == 32 * 1024
    assert ram_budget_check.profile_blocks_on_shortfall(args) is False


def test_release_profile_blocks_on_shortfall() -> None:
    args = ram_budget_check.parse_args([])
    assert ram_budget_check.profile_blocks_on_shortfall(args) is True


def test_explicit_min_free_overrides_debug_profile() -> None:
    args = ram_budget_check.parse_args(
        ["--profile", "debug", "--min-free", str(40 * 1024)])
    assert ram_budget_check.minimum_free_bytes(args) == 40 * 1024


def write_license(path: Path, **overrides: object) -> None:
    today = dt.date.today()
    data = {
        "schema": ram_budget_check.LICENSE_SCHEMA,
        "issued_on": today.isoformat(),
        "expires_on": (today + dt.timedelta(days=7)).isoformat(),
        "formal_min_free_bytes": 96 * 1024,
        "licensed_min_free_bytes": 95 * 1024,
        "reason": "DPLL mainline development",
    }
    data.update(overrides)
    path.write_text(json.dumps(data), encoding="utf-8")


def test_load_temporary_license_accepts_current_license(tmp_path: Path) -> None:
    path = tmp_path / "license.json"
    write_license(path)

    loaded = ram_budget_check.load_temporary_license(path, 96 * 1024)

    assert loaded["licensed_min_free_bytes"] == 95 * 1024


def test_load_temporary_license_rejects_expired_license(tmp_path: Path) -> None:
    path = tmp_path / "license.json"
    yesterday = dt.date.today() - dt.timedelta(days=1)
    write_license(path, issued_on=yesterday.isoformat(),
                  expires_on=yesterday.isoformat())

    try:
        ram_budget_check.load_temporary_license(path, 96 * 1024)
    except SystemExit as exc:
        assert "not currently valid" in str(exc)
    else:
        raise AssertionError("expired license was accepted")


def test_load_temporary_license_binds_formal_threshold(tmp_path: Path) -> None:
    path = tmp_path / "license.json"
    write_license(path)

    try:
        ram_budget_check.load_temporary_license(path, 97 * 1024)
    except SystemExit as exc:
        assert "threshold mismatch" in str(exc)
    else:
        raise AssertionError("threshold-mismatched license was accepted")
