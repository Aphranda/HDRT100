"""Execute unchanged activation owner and SCPI bodies with observable boundaries."""
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def function(source, name):
    match = re.search(r"^(?:static )?[\w_]+ " + name + r"\([^;]*?\n\{", source, re.M)
    assert match, name
    opening = source.index("{", match.start())
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def test_per_attempt_activation_and_quality_classification(tmp_path):
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    names = ["distributed_refmem_quality_gate_check", "distributed_refmem_quality_gate_ready",
             "distributed_refmem_activation_nack_reason", "distributed_refmem_activate_staging_locked",
             "distributed_refmem_activate_staging_checked", "distributed_refmem_activate_staging",
             "distributed_refmem_get_activation_diagnostic"]
    (tmp_path / "activation_owner.inc").write_text(
        "\n\n".join(function(source, name) for name in names), encoding="utf-8")
    scpi = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    node = (ROOT / "middleware/scpi_port/src/scpi_sequence_node_commands.c").read_text(encoding="utf-8")
    (tmp_path / "activation_scpi.inc").write_text("\n\n".join(function(scpi, name) for name in [
        "scpi_refmem_result_activation_diagnostic", "scpi_cmd_refmem_load_activation_status_q",
        "scpi_cmd_refmem_load_activate", "scpi_refmem_result_table_image_descriptor"]) +
        "\n\n" + function(node, "scpi_sequence_node_activate"), encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    library = ROOT / "third_party/scpi-parser/libscpi"
    includes = [tmp_path, ROOT / "tests/unit/host_stubs", ROOT / "config", ROOT / "osal/inc",
                ROOT / "boards/rp2350_trig/inc", library / "inc", ROOT / "middleware/scpi_port/inc"]
    includes += sorted((ROOT / "components").glob("*/inc"))
    exe = tmp_path / "activation.exe"
    command = [compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
        "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections", "-DSCPI_USER_CONFIG=1",
        *[f"-I{path}" for path in includes], str(ROOT / "tests/unit/test_refmem_activation_diagnostic.c"),
        str(ROOT / "components/distributed_refmem/src/refmem_quality.c"),
        *[str(path) for path in sorted((library / "src").glob("*.c"))],
        *(["-Wno-error=attributes"] if os.name == "nt" else ["-lm"]), "-o", str(exe)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "activation diagnostics passed" in result.stdout
