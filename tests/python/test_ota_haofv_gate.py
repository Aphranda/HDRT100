from pathlib import Path

from tools.ota_haofv_gate.ota_haofv_gate import run


def test_ota_haofv_gate_accepts_current_build() -> None:
    root = Path(__file__).resolve().parents[2]
    dis = root / "out/build/pico2-rtos-multicore-smoke/DHRT100.dis"
    if not dis.exists():
        return
    report = run(root, dis)
    assert report["passed"], report["failures"]


def test_ota_haofv_gate_rejects_page_sized_stack_frame(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[2]
    dis = tmp_path / "bad.dis"
    dis.write_text(
        "1000 <pota_bcb_scan_step>:\n"
        " push {lr}\n sub sp, #256\n\n"
        "1001 <pota_bcb_txn_begin_from_selection>:\n"
        " push {lr}\n\n"
        "1002 <ota_metadata_mark_pending_step>:\n"
        " push {lr}\n\n"
        "1003 <body_crc32>:\n"
        " push {lr}\n",
        encoding="utf-8",
    )
    report = run(root, dis)
    assert not report["passed"]
    assert any("pota_bcb_scan_step stack frame" in item
               for item in report["failures"])
