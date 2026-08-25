#!/usr/bin/env python3
"""Check docs freshness (loop 1) and contract registry (loop 2).

Companion to tools/docs_check/docs_check.py. Pure stdlib.

Loops:
  loop 1 (freshness): top-level doc must be refreshed within FRESHNESS_DAYS
                       of the newest dated domain doc.
  loop 2 (registry):  contract registry rows must have unique ids, existing
                       clause_loc files and existing code_anchor files.

Exit code 1 on any FAIL (used by pre-commit and pytest).
"""
from __future__ import annotations

import argparse
import datetime
import os
import re
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path

FRESHNESS_DAYS = 7
REGISTRY_REL = Path("docs/check/DOCS_REGISTRY.md")
TOP_DOC_REL = Path("docs/arch/HAOFV_ARCHITECTURE.md")
GATE_MARKER_REL = Path(".git/doc-verify-last")
BYPASS_WINDOW_S = 120

# Self-referential plugin sync: the checker checks its own snapshots.
PLUGIN_DIR_REL = Path(".agents/skills/doc-self-regression")
HARNESS_PLUGIN_DIR = Path(
    os.environ.get(
        "DOC_SKILL_HARNESS_DIR",
        r"D:\Aphranda\npm\deepseek-harness\.agents\skills\doc-self-regression",
    )
)
PLUGIN_FILES = (
    "SKILL.md",
    "README.md",
    "doc_regression_check.py",
    "DOCS_REGISTRY.template.md",
)

# Governance/temp dirs are not "domain content" for freshness purposes.
FRESHNESS_EXCLUDE_DIRS = {"archive", "legacy", "check", "temp"}
# Never scan these trees for anchors.
SCAN_EXCLUDE_DIRS = {"build", ".git", "node_modules", "third_party"}

DATE_RE = re.compile(r"Last updated:\s*(\d{4})-(\d{2})-(\d{2})")
ROW_RE = re.compile(
    r"^\|\s*(TDMA|VDC|REFMEM|RTOS|INTERFACE|HARDWARE|ARCH|DOCS|CALIBRATION)-[A-Z0-9]+-\d+\s*\|"
)
CLAUSE_ROW_RE = re.compile(r"^\|\s*HAOFV-\d+\s*\|")
STATUS_RE = re.compile(r"^Status:\s*(\w+)", re.MULTILINE)
HARD_CONSTRAINTS_HEADING = "### 顶层安全硬约束"

# Loop 3: docs must not hand-write #define numbers (single source of truth).
DEFINE_RE = re.compile(r"#define\s+([A-Z_][A-Z0-9_]*)\s+(\d+)")
CODE_EXT = {".c", ".h"}


@dataclass
class Result:
    failures: list[str]
    warnings: list[str]

    def fail(self, msg: str) -> None:
        self.failures.append(msg)
        print(f"FAIL {msg}")

    def warn(self, msg: str) -> None:
        self.warnings.append(msg)
        print(f"WARN {msg}")

    def ok(self, msg: str) -> None:
        print(f"OK   {msg}")


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def parse_date(text: str) -> tuple[int, int, int] | None:
    m = DATE_RE.search(text)
    if not m:
        return None
    y, mo, d = int(m.group(1)), int(m.group(2)), int(m.group(3))
    try:
        datetime.date(y, mo, d)
    except ValueError:
        return None  # malformed date: ignore, do not crash
    return y, mo, d


def days_between(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    return (datetime.date(*b) - datetime.date(*a)).days


def is_scope_affected(scope: set[str], rel: str) -> bool:
    return not scope or rel in scope or any(rel.startswith(s) for s in scope)


def check_freshness(root: Path, result: Result, scope: set[str]) -> None:
    top = root / TOP_DOC_REL
    if not top.exists():
        result.fail(f"{TOP_DOC_REL} missing")
        return
    if not is_scope_affected(scope, TOP_DOC_REL.as_posix()):
        return
    top_date = parse_date(read_text(top))
    if top_date is None:
        result.fail(f"{TOP_DOC_REL}: missing 'Last updated: YYYY-MM-DD'")
        return

    newest: tuple[int, int, int] | None = None
    newest_file = ""
    for doc in (root / "docs").rglob("*.md"):
        rel = doc.relative_to(root).as_posix()
        if doc == top:
            continue
        if any(part in FRESHNESS_EXCLUDE_DIRS for part in doc.parts):
            continue
        if doc.parts.count("docs") > 1:
            continue  # docs/docs governance subdir
        d = parse_date(read_text(doc))
        if d is None:
            continue
        if newest is None or d > newest:
            newest, newest_file = d, rel

    if newest is None:
        result.ok("freshness: no dated domain docs found")
        return
    gap = days_between(top_date, newest)
    if gap > FRESHNESS_DAYS:
        result.fail(
            f"freshness: top doc {gap}d older than {newest_file} "
            f"({newest[0]}-{newest[1]:02d}-{newest[2]:02d}); "
            f"refresh {TOP_DOC_REL} within {FRESHNESS_DAYS}d"
        )
    else:
        result.ok("freshness: top doc within window")


def registry_rows(text: str) -> list[list[str]]:
    rows = []
    for ln in text.splitlines():
        if ROW_RE.match(ln):
            rows.append([c.strip() for c in ln.split("|")])
    return rows


def check_registry(root: Path, result: Result) -> None:
    reg = root / REGISTRY_REL
    if not reg.exists():
        result.fail(f"{REGISTRY_REL} missing (create it per spec)")
        return
    text = read_text(reg)

    # P3: rows that look like contract rows but do not match ROW_RE (e.g. a
    # double-segment id like TDMA-FLIGHT-BITMAP-01) are silently skipped by
    # registry_rows; surface them as WARN instead of silently ignoring.
    for ln in text.splitlines():
        if not ln.lstrip().startswith("|") or ROW_RE.match(ln):
            continue
        cells = [c.strip() for c in ln.split("|")]
        if len(cells) < 10:
            continue
        cid = cells[1]
        if cid in ("contract_id", "---"):
            continue
        result.warn(
            f"{REGISTRY_REL}: row id '{cid}' does not match ROW_RE "
            "(not counted/validated; check id format)"
        )

    rows = registry_rows(text)
    if not rows:
        result.fail(f"{REGISTRY_REL}: no contract rows found")
        return

    ids = [r[1] for r in rows]
    dupes = sorted({i for i in ids if ids.count(i) > 1})
    if dupes:
        result.fail(f"{REGISTRY_REL}: duplicate contract_id {dupes}")
    else:
        result.ok(f"registry: {len(rows)} contracts, ids unique")

    # Pre-index the file tree once (loop 2 anchor existence).
    anchor_index = {
        p.name: p
        for p in root.rglob("*")
        if p.is_file() and not any(d in SCAN_EXCLUDE_DIRS for d in p.parts)
    }

    for r in rows:
        # cells: ['', id, domain, contract, ver, clause_loc, code_anchor, check, registered, status, '']
        if len(r) < 10:
            result.fail(f"{REGISTRY_REL}: malformed row: {'|'.join(r)[:80]}")
            continue
        clause_loc = r[5]
        code_anchor = r[6]
        doc_rel = clause_loc.split(":")[0]
        doc_path = root / doc_rel
        if not doc_path.exists():
            result.fail(f"{REGISTRY_REL}: clause_loc file missing: {clause_loc}")
        else:
            doc_status = parse_doc_status(doc_path)
            if doc_status not in ("Active", "Frozen"):
                result.fail(
                    f"{REGISTRY_REL}: clause_loc doc {doc_rel} Status="
                    f"{doc_status or 'missing'} (must be Active/Frozen)"
                )
        if code_anchor not in anchor_index:
            result.fail(f"{REGISTRY_REL}: code_anchor not found: {code_anchor}")


def parse_doc_status(path: Path) -> str | None:
    m = STATUS_RE.search(read_text(path))
    return m.group(1) if m else None


def top_hard_constraints(top_text: str) -> list[str]:
    """Extract constraint names from the top-level hard-constraint table."""
    m = re.search(rf"{re.escape(HARD_CONSTRAINTS_HEADING)}\n(.*?)(\n## )", top_text, re.S)
    if not m:
        return []
    names = []
    for ln in m.group(1).splitlines():
        row = re.match(r"^\| ([^|]+?) \|", ln)
        if row:
            name = row.group(1).strip()
            if name and name != "约束":
                names.append(name)
    return names


def clause_rows(reg_text: str) -> list[list[str]]:
    return [
        [c.strip() for c in ln.split("|")]
        for ln in reg_text.splitlines()
        if CLAUSE_ROW_RE.match(ln)
    ]


def check_orphan_clauses(root: Path, result: Result) -> None:
    """Loop 2 extension: every top hard constraint must have a clause row."""
    top = root / TOP_DOC_REL
    reg = root / REGISTRY_REL
    if not top.exists() or not reg.exists():
        result.fail("orphan: top doc or registry missing")
        return
    names = top_hard_constraints(read_text(top))
    if not names:
        result.fail(f"orphan: no hard constraints found under {HARD_CONSTRAINTS_HEADING}")
        return
    clauses = clause_rows(read_text(reg))
    clause_text = " ".join(r[2] for r in clauses if len(r) > 2)
    orphans = [n for n in names if n not in clause_text]
    if orphans:
        result.fail(f"orphan clauses: {orphans} not covered in registry 条款落点表")
    else:
        result.ok(f"orphan: {len(names)} top constraints all covered")


def check_escape_hatch(root: Path, result: Result) -> None:
    """C3: a commit significantly newer than the last pre-commit gate run
    suggests `git commit --no-verify` was used. WARN, not FAIL."""
    marker = root / GATE_MARKER_REL
    if not marker.exists():
        result.warn(
            "escape-hatch: no gate marker (.git/doc-verify-last); "
            "run pre-commit once to initialize"
        )
        return
    try:
        marker_time = int(marker.read_text(encoding="utf-8", errors="ignore").strip())
        commit_time = int(
            subprocess_run(root, ["git", "log", "-1", "--format=%ct"])
        )
    except (ValueError, OSError):
        result.warn("escape-hatch: cannot read marker or git log")
        return
    if commit_time - marker_time > BYPASS_WINDOW_S:
        result.warn(
            f"escape-hatch: last commit is {commit_time - marker_time}s newer "
            "than the last pre-commit gate run; commit may have bypassed "
            "with --no-verify"
        )
    else:
        result.ok("escape-hatch: last commit covered by pre-commit gate")


def subprocess_run(root: Path, args: list[str]) -> str:
    import subprocess

    out = subprocess.run(args, cwd=root, capture_output=True, text=True)
    return out.stdout.strip()


def code_define_map(root: Path) -> dict[str, str]:
    defines: dict[str, str] = {}
    for p in root.rglob("*"):
        if p.suffix not in CODE_EXT:
            continue
        if any(d in SCAN_EXCLUDE_DIRS for d in p.parts):
            continue
        for name, val in DEFINE_RE.findall(read_text(p)):
            defines.setdefault(name, val)
    return defines


def check_constants(root: Path, result: Result, doc_dirs: list[str]) -> None:
    """Loop 3: doc `#define X N` must match code. Lines marked 快照 are exempt."""
    code_map = code_define_map(root)
    checked = 0
    for d in doc_dirs:
        base = root / d
        if not base.exists():
            result.warn(f"constants: doc dir missing: {d}")
            continue
        for doc in base.rglob("*.md"):
            lines = read_text(doc).splitlines()
            for i, ln in enumerate(lines):
                m = DEFINE_RE.search(ln)
                if not m:
                    continue
                ctx = " ".join(lines[max(0, i - 1): i + 1])
                if "快照" in ctx or "非事实源" in ctx:
                    continue
                checked += 1
                name, val = m.group(1), m.group(2)
                if name in code_map:
                    if code_map[name] != val:
                        result.fail(
                            f"constants: {doc.relative_to(root)} #{name} "
                            f"doc={val} code={code_map[name]}"
                        )
                else:
                    result.warn(
                        f"constants: {doc.relative_to(root)} #{name} "
                        "not found in code (unverifiable)"
                    )
    if checked == 0:
        result.ok("constants: no #define literals found in scanned docs")
    else:
        result.ok(f"constants: checked {checked} #define literals")


def check_crosscheck(root: Path, result: Result) -> None:
    """Loop 4 (verify-doc-crosscheck): a contract's key term should appear in
    its code_anchor file. Heuristic, so misses are WARN not FAIL."""
    reg = root / REGISTRY_REL
    if not reg.exists():
        result.fail(f"{REGISTRY_REL} missing (create it per spec)")
        return
    rows = registry_rows(read_text(reg))
    if not rows:
        result.warn("crosscheck: no contract rows")
        return
    for r in rows:
        if len(r) < 10:
            continue
        contract_text, code_anchor = r[3], r[6]
        tokens = re.findall(r"[A-Za-z_]{4,}", contract_text)
        if not tokens:
            continue
        keyword = max(tokens, key=len)
        anchor = root / code_anchor
        if not anchor.exists():
            continue  # anchor existence is loop 2's job
        if keyword not in read_text(anchor):
            result.warn(
                f"crosscheck: contract '{r[1]}' keyword '{keyword}' "
                f"not found in anchor {code_anchor}"
            )
    result.ok(f"crosscheck: {len(rows)} contracts checked")


def check_skill_sync(root: Path, result: Result) -> None:
    """Self-reference: the checker's own snapshots must match the live script."""
    live = root / "tools" / "doc_regression_check.py"
    proj = root / PLUGIN_DIR_REL
    if not live.exists():
        result.warn("skill-sync: live script tools/doc_regression_check.py missing")
        return
    if not proj.exists():
        result.fail("skill-sync: project skill dir missing (run --skill-sync)")
        return

    if (proj / "doc_regression_check.py").read_bytes() != live.read_bytes():
        result.fail("skill-sync: project skill script differs from tools/ (run --skill-sync)")

    if HARNESS_PLUGIN_DIR.exists():
        for name in PLUGIN_FILES:
            src, dst = proj / name, HARNESS_PLUGIN_DIR / name
            if not src.exists() or not dst.exists() or src.read_bytes() != dst.read_bytes():
                result.fail(f"skill-sync: harness plugin {name} differs (run --skill-sync)")
                break
        else:
            result.ok("skill-sync: all plugin snapshots in sync")
    else:
        result.ok("skill-sync: harness plugin dir not present (skipped)")


def sync_skill_plugin(root: Path, result: Result) -> None:
    """Copy live script + registry template into project skill dir, then mirror
    the whole plugin dir to the harness copy.

    The harness copy is only written when the harness plugin dir already exists
    or DOC_SKILL_HARNESS_DIR is explicitly set; otherwise it is skipped so we
    never create phantom directories at a stale default path."""
    live = root / "tools" / "doc_regression_check.py"
    proj = root / PLUGIN_DIR_REL
    proj.mkdir(parents=True, exist_ok=True)
    if live.exists():
        shutil.copy2(live, proj / "doc_regression_check.py")
    reg = root / REGISTRY_REL
    if reg.exists():
        shutil.copy2(reg, proj / "DOCS_REGISTRY.template.md")

    harness_configured = "DOC_SKILL_HARNESS_DIR" in os.environ
    if HARNESS_PLUGIN_DIR.exists() or harness_configured:
        HARNESS_PLUGIN_DIR.mkdir(parents=True, exist_ok=True)
        for name in PLUGIN_FILES:
            src = proj / name
            if src.exists():
                shutil.copy2(src, HARNESS_PLUGIN_DIR / name)
        result.ok(
            f"skill-sync: synced to {proj.relative_to(root)} and {HARNESS_PLUGIN_DIR}"
        )
    else:
        result.ok(
            f"skill-sync: synced to {proj.relative_to(root)}; "
            "harness dir not present and DOC_SKILL_HARNESS_DIR unset (skipped)"
        )


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--freshness", action="store_true", help="only loop 1")
    parser.add_argument("--registry", action="store_true", help="only loop 2")
    parser.add_argument(
        "--log-check", action="store_true", help="only escape-hatch (C3) check"
    )
    parser.add_argument(
        "--skill-sync",
        action="store_true",
        help="sync live checker + registry template into the skill plugin snapshots",
    )
    parser.add_argument(
        "--constants",
        nargs="*",
        default=None,
        metavar="DOCDIR",
        help="loop 3: check doc `#define` numbers vs code (dirs default: docs)",
    )
    parser.add_argument(
        "--crosscheck",
        action="store_true",
        help="loop 4: contract keyword must appear in code_anchor file (WARN)",
    )
    parser.add_argument(
        "--scope",
        type=str,
        default="",
        help="space-separated relative paths to limit checks to (e.g. git diff)",
    )
    args = parser.parse_args()
    root = args.root.resolve()
    scope = {s for s in args.scope.split() if s}

    result = Result(failures=[], warnings=[])
    if args.skill_sync:
        sync_skill_plugin(root, result)
        if result.failures:
            return 1
        result.ok("doc_regression passed")
        return 0

    if args.constants is not None:
        check_constants(root, result, args.constants or ["docs"])
        if result.failures:
            print(
                f"SUMMARY FAIL failures={len(result.failures)} "
                f"warnings={len(result.warnings)}"
            )
            return 1
        result.ok("doc_regression passed")
        return 0

    if args.crosscheck:
        check_crosscheck(root, result)
        if result.failures:
            print(
                f"SUMMARY FAIL failures={len(result.failures)} "
                f"warnings={len(result.warnings)}"
            )
            return 1
        result.ok("doc_regression passed")
        return 0

    run_all = not (args.freshness or args.registry or args.log_check)
    run_freshness = args.freshness or run_all
    run_registry = args.registry or run_all
    run_log = args.log_check
    if run_freshness:
        check_freshness(root, result, scope)
    if run_registry:
        check_registry(root, result)
        check_orphan_clauses(root, result)
    if run_log:
        check_escape_hatch(root, result)
    if run_all:
        check_skill_sync(root, result)

    if result.failures:
        print(f"SUMMARY FAIL failures={len(result.failures)} warnings={len(result.warnings)}")
        return 1
    result.ok("doc_regression passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
