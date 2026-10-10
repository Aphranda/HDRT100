#!/usr/bin/env python3
"""按新鲜度窗口迁移 tools/ 条目到域目录（含旧路径兼容 wrapper）。

用法: python tools/scratch/migrate_batch.py <from-date> <to-date> [--limit N]
约定: 只移动不删除；跳过已迁移/禁止移动集/7 天内修改项；域取自 tool_registry.json 的 domain。
"""
import json, pathlib, subprocess, sys, datetime

TOOLS = pathlib.Path("tools")
FORB = {"docs_check","doc_regression_check.py","checks","hardware_acceptance","tool_governance","common",
        "domains","workflows","tests","bench","flash_map","archive","scratch","README.md","INDEX.md",
        "tool_registry.json","tool_registry.schema.json","tool_layout.json","_ref_audit.json",
        "_freshness.json","_migration_map.json","_batch_paths.json","_n1_paths.json"}
DOM = {"TOOLS": "project"}
HEUR = [("dpll","dpll"),("vdc","vdc"),("refmem","refmem"),("sd_","storage"),("ota","ota"),
        ("biss","communication"),("scpi","interface"),("trigger","trigger"),("tdma","tdma"),
        ("calibration","calibration"),("flash","flash"),("analyzer","project"),("watchdog","watchdog"),
        ("rtos","rtos"),("monitor","project"),("run_","project")]


def git(*args):
    r = subprocess.run(["git", *args], capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()


def domain_for(reg, name):
    for t in reg["tools"]:
        for f in t.get("files", []):
            fp = f.get("path", "")
            if fp.startswith(f"tools/{name}/") or fp == f"tools/{name}":
                d = t.get("domain", "TOOLS")
                return DOM.get(d, d.lower())
    n = name.lower()
    for key, dom in HEUR:
        if key in n:
            return dom
    return "project"


def main() -> int:
    lo, hi = sys.argv[1], sys.argv[2]
    limit = 14
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])
    reg = json.loads((TOOLS / "tool_registry.json").read_text(encoding="utf-8"))
    migrated = {m["old"].split("/")[-1] for m in
                json.loads((TOOLS / "_migration_map.json").read_text(encoding="utf-8"))}
    fresh = json.loads((TOOLS / "_freshness.json").read_text(encoding="utf-8"))
    today = datetime.date.today()
    batch = sorted((e["mtime"], e["name"], e["dir"]) for e in fresh
                   if lo <= e["mtime"] <= hi and e["name"] not in migrated
                   and e["name"] not in FORB
                   and (today - datetime.date.fromisoformat(e["mtime"])).days >= 7)
    if not batch:
        print("no candidates in window"); return 0
    mapping, paths = [], []
    for _mt, name, isdir in batch[:limit]:
        src = TOOLS / name
        if not src.exists():
            continue
        dom = domain_for(reg, name)
        (TOOLS / dom).mkdir(parents=True, exist_ok=True)
        rc, out = git("mv", str(src), str(TOOLS / dom / name))
        if rc != 0:
            print("FAIL", name, out[:60]); continue
        mapping.append({"old": f"tools/{name}", "new": f"tools/{dom}/{name}"})
        paths += [f"tools/{dom}", f"tools/{name}"]
        if isdir:
            w = TOOLS / name; w.mkdir(parents=True, exist_ok=True)
            (w / "README.md").write_text(
                f"# 兼容转发（已迁移）\n\n已迁移到 `tools/{dom}/{name}/`；本目录仅兼容层。\n",
                encoding="utf-8")
            for f in sorted((TOOLS / dom / name).glob("*.py")):
                (w / f.name).write_text(
                    '#!/usr/bin/env python3\n"""兼容转发（迁移期保留）。"""\nimport pathlib, runpy, sys\n'
                    f'_T = pathlib.Path(__file__).resolve().parents[1] / "{dom}" / "{name}" / "{f.name}"\n'
                    'sys.argv[0] = str(_T)\nrunpy.run_path(str(_T), run_name="__main__")\n',
                    encoding="utf-8")
        elif pathlib.Path(name).suffix == ".py":
            (TOOLS / name).write_text(
                '#!/usr/bin/env python3\n"""兼容转发（迁移期保留）。"""\nimport pathlib, runpy, sys\n'
                f'_T = pathlib.Path(__file__).resolve().parent / "{dom}" / "{name}"\n'
                'sys.argv[0] = str(_T)\nrunpy.run_path(str(_T), run_name="__main__")\n', encoding="utf-8")
        else:
            (TOOLS / name).write_text(
                f'# 兼容转发到 tools/{dom}/{name}\n& "$PSScriptRoot\\{dom}\\{name}" @args\n',
                encoding="utf-8")
    mp = TOOLS / "_migration_map.json"
    mp.write_text(json.dumps(json.loads(mp.read_text(encoding="utf-8")) + mapping,
                             ensure_ascii=False, indent=1), encoding="utf-8")
    audit = TOOLS / "archive" / "REFERENCE_AUDIT.md"
    audit.write_text(audit.read_text(encoding="utf-8").rstrip("\n") +
                     f"\n\n### batch {lo}…{hi}（{today}）\n\n- 范围：{len(mapping)} 项\n" +
                     "".join(f"  - `{m['old']}` → `{m['new']}`\n" for m in mapping) +
                     "- 兼容层：旧路径 wrapper\n- 验证：文档门禁\n", encoding="utf-8")
    (TOOLS / "_batch_paths.json").write_text(json.dumps(sorted(set(paths)), ensure_ascii=False),
                                             encoding="utf-8")
    print(f"moved {len(mapping)} items in {lo}…{hi}")
    for m in mapping:
        print("  ", m["old"], "->", m["new"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
