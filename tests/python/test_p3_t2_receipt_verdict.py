"""M0.5：`check_staged` 的 T2 凭证分支必须校验 `verdict_ok`。

背景：`sequence_workflow.py` 在 verdict 之前写 receipt，`passed` 与 `verdict_ok`
是两个字段。实测出现过 `passed=true / verdict_ok=false`（GATEWAY 判据失败），
而 `check_staged` 只检查 `passed`，于是**判据失败的运行仍能授权固件提交**。

本测试是"固定判据"式守卫（与 `test_t2_configure_signature` 同类）：它钉住
T2 分支必须同时检查 `verdict_ok`，防止该条件被后续重构悄悄删掉。
"""
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tools/hardware_acceptance/workflow_steps.py"

ANCHOR = 't2_path = root / "config/hardware_acceptance/t2_acceptance_receipt.json"'


def _t2_branch() -> str:
    text = SOURCE.read_text(encoding="utf-8")
    start = text.find(ANCHOR)
    assert start >= 0, "T2 receipt 分支的锚点缺失：check_staged 结构已变，请更新本测试"
    return text[start:start + 900]


def test_t2_receipt_branch_requires_verdict_ok() -> None:
    branch = _t2_branch()
    assert 't2.get("verdict_ok") is True' in branch, (
        "T2 凭证分支必须要求 verdict_ok is True：否则 verdict 失败的一次运行"
        "（passed=true / verdict_ok=false）仍会放行代码提交"
    )


def test_t2_receipt_branch_still_checks_passed_and_fingerprint() -> None:
    branch = _t2_branch()
    for required in ('t2.get("passed") is True',
                     't2.get("source_tree_sha256") == fingerprint',
                     't2.get("source_file_count") == file_count'):
        assert required in branch, f"T2 凭证分支丢失了必要条件：{required}"


def _condition_source() -> str:
    """取出 T2 分支的布尔条件原文（`if (...)` 里括号内的部分）。

    结束锚点用条件自身的最后一个比较，而不是找 `):`——分支内的
    `print(f"OK   hardware acceptance (T2 sequence): ...")` 也含 `):`，
    按它切会把表达式截断（实测 SyntaxError）。
    """
    branch = _t2_branch()
    start = branch.index('if (t2.get("schema")') + len("if (")
    tail = 't2.get("source_file_count") == file_count'
    end = branch.index(tail, start) + len(tail)
    return branch[start:end]


def _accepts(t2: dict, fingerprint: str = "fp", file_count: int = 3) -> bool:
    """用被测源码里的真实条件求值，而不是在测试里重写一遍。

    条件原文跨多行，必须重新包一层括号才有续行语义（原代码是靠 `if (` 的
    括号续行的；直接 eval 会 SyntaxError，这是本轮踩到的第二个坑）。
    """
    return bool(eval("(" + _condition_source() + ")",  # noqa: S307 - 来自受测文件
                     {"t2": t2, "fingerprint": fingerprint,
                      "file_count": file_count}))


GOOD = {
    "schema": "HAOFV_T2_SEQUENCE_ACCEPTANCE_RECEIPT_V1",
    "passed": True,
    "verdict_ok": True,
    "source_tree_sha256": "fp",
    "source_file_count": 3,
}


def test_failed_verdict_is_rejected() -> None:
    bad = dict(GOOD, verdict_ok=False)
    assert _accepts(bad) is False, "verdict_ok=false 的运行不得授权提交"


def test_missing_verdict_field_is_rejected() -> None:
    legacy = dict(GOOD)
    legacy.pop("verdict_ok")
    assert _accepts(legacy) is False, "缺 verdict_ok 字段的旧 receipt 不得授权提交"


def test_clean_run_is_accepted() -> None:
    assert _accepts(dict(GOOD)) is True, "判据通过且指纹匹配时必须放行"
