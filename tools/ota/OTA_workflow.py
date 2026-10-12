"""OTA workflow: put one firmware build on the four bench boards, addressable per step.

Same shape as tools/vdc_priority_trace/DPLL60_workflow.py and
tools/sync_trigger/t2_workflow.py - numbered steps, each runnable alone, in ranges
or continuously, with per-step timestamps, durations and named verdicts, a durable
log, and JSON/JS data for the data-driven A4 report (docs/ota/OTA_FLOW.html).

  --list                       step table
  --method ota|picotool|tdma   flashing route (default ota)
  --step 4                     one step
  --steps 1-3 / 0,2,5-7        an arbitrary selection, in order
  --steps all | --full         the continuous chain
  --no-build / --package P     reuse an existing package instead of building
  --interactive                wait for Enter between steps

Routes:
  ota       USB CDC stream OTA through tools/ota/ota_multi_update (4096 B blocks).
            The route the acceptance credential is built on; keeps calibration.
  picotool  UF2 through tools/picotool_flash (BOOTSEL), one board at a time. Loads
            the FACTORY image (bootloader + slot A + boot control); fast inner-loop
            route, but needs --allow-factory-image because on this bench it has
            cost the stored calibration before (step 6 checks it either way).
  tdma      RESERVED for streaming OTA over the TDMA ring (one host port, peers fed
            through the ring data plane). Not implemented: steps 3/4 report it as
            reserved and fail, so it can never read as a successful flash.

This is a THIN ORCHESTRATION: building is tools/cmake_build_auto, transport is
ota_multi_update / picotool_flash, and the OTA acceptance gate is
p3_hardware_acceptance.validate_ota. It reimplements no flashing logic.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "hardware_acceptance" / "p3_bench.json"
LOG_DIR = ROOT / "out" / "HardwareAcceptance"
DEFAULT_BUILD_DIR = ROOT / "out" / "build" / "dpll60-latest"
HTML_VIEW = ROOT / "docs" / "ota" / "OTA_FLOW.html"

# Fallback bench order (NO.1..NO.4) when the bench config does not name the boards.
BENCH_BOARDS = ("0010071E65B5CB38", "FB276192BEF9CCE1",
                "2BD5090FE009FA2A", "A1E549202D18ED6A")

METHODS = {
    "ota": {"label": "USB CDC 流式 OTA", "implemented": True,
            "tool": "tools/ota/ota_multi_update/ota_multi_update.py",
            "note": "4096 B 块、A/B 槽 + 提交；验收凭证所依赖的路径，保留标定"},
    "picotool": {"label": "picotool UF2（BOOTSEL）", "implemented": True,
                 "tool": "tools/picotool_flash/picotool_flash.py",
                 "note": "加载 FACTORY 镜像（bootloader + 槽 A + boot control），"
                         "需 --allow-factory-image；本台架曾因此丢失已存标定"},
    "tdma": {"label": "TDMA 环上流式 OTA（预留）", "implemented": False,
             "tool": "",
             "note": "预留：单一上位机端口，经 TDMA 数据面向从板流式分发；尚未实现"},
}

STEP_TITLES = {
    0: "构建（cmake_build_auto；--no-build / --package 时复用产物）",
    1: "产物校验（OTA 包魔数 / 构建号 / UF2 / SHA256）",
    2: "刷写前快照（四板身份 / 构建号 / 标定持久化）",
    3: "刷写方式前置（ota 4096 块 / picotool FACTORY 确认 / tdma 预留）",
    4: "刷写（按 --method 执行）",
    5: "刷写后复验（四板重枚举 + 构建号 + OTA 摘要门限）",
    6: "标定保持校验（持久化记录与刷写前一致）",
    7: "摘要与收据（ota-workflow-summary.json）",
}
STEP_FUNCTIONS = {}

# READ:CALibration:TRAINing:STAGe:PERSistence? (tools/calibration_ring_validate/
# trn03_stage.py PERSISTENCE_QUERY_FIELDS).
PERSISTENCE_FIELDS = ("tag", "persisted_valid", "loaded", "restore_pending",
                      "reject_reason", "record_generation", "record_sequence",
                      "payload_crc32", "calibration_generation", "topology_generation",
                      "topology_crc32", "profile_crc32", "schedule_crc32")
# The identity of a stored calibration: these must survive the flash unchanged.
PERSISTENCE_IDENTITY = ("persisted_valid", "record_generation", "payload_crc32",
                        "calibration_generation", "topology_crc32", "profile_crc32",
                        "schedule_crc32")


def step(number):
    def register(function):
        STEP_FUNCTIONS[number] = function
        return function
    return register


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def p3_module():
    """The acceptance module owns the package reader and the OTA gate."""
    sys.path.insert(0, str(ROOT / "tools" / "hardware_acceptance"))
    import p3_hardware_acceptance as p3                                   # noqa: PLC0415
    return p3


class Context:
    def __init__(self, args):
        self.args = args
        self.method = args.method
        self.log_dir = LOG_DIR
        self.log_dir.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        self.log_path = self.log_dir / f"ota_steps-{stamp}.log"
        self.out_dir = self.log_dir / time.strftime("%Y%m%d") / f"ota-{time.strftime('%H%M%S')}"
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.records = []
        self.failures = []
        self.warnings = []
        self.config = json.loads(CONFIG.read_text(encoding="utf-8")) if CONFIG.exists() else {}
        self.board_ids = [str(value) for value in
                          self.config.get("p3_board_ids_in_physical_order", BENCH_BOARDS)]
        self.build_dir = Path(args.build_dir).resolve() if args.build_dir else DEFAULT_BUILD_DIR
        self.package = (Path(args.package).resolve() if args.package
                        else self.build_dir / "DHRT100_UPDATE.pkg")
        self.uf2 = self.build_dir / "DHRT100_FACTORY.uf2"
        self.build_id = None
        self.artifacts = {}
        self.before = {}
        self.after = {}
        self.flash_summary = None
        self.flash_attempted = False

    def log(self, message):
        print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)

    def run(self, command, log_name):
        """Run a child process, tee its output into the run directory."""
        path = self.out_dir / log_name
        with path.open("w", encoding="utf-8") as handle:
            completed = subprocess.run(command, cwd=str(ROOT), stdout=handle,
                                       stderr=subprocess.STDOUT, check=False)
        return completed.returncode, path

    def serial_args(self):
        return SimpleNamespace(board_ids=list(self.board_ids), baud=115200, timeout=8.0,
                               read_timeout=0.02, settle=0.02, keep_open=True,
                               short_open=False, action_timeout=0.5)

    def snapshot_boards(self, wait_s=0.0):
        """Identity, build and calibration persistence of the bench set.

        wait_s polls for the exact set: a freshly flashed board re-enumerates for a
        moment (and picotool leaves it in BOOTSEL until the application starts).
        """
        sys.path.insert(0, str(ROOT))
        from tools.tdma_ring_monitor.tdma_start_ring import (            # noqa: PLC0415
            board_command, close_persistent_connections, discover)
        ns = self.serial_args()
        deadline = time.monotonic() + wait_s
        boards = discover(ns)
        while set(self.board_ids) - set(boards) and time.monotonic() < deadline:
            close_persistent_connections()
            time.sleep(1.0)
            boards = discover(ns)
        rows = {}
        try:
            for uid in self.board_ids:
                board = boards.get(uid)
                if board is None:
                    rows[uid] = {"present": False}
                    continue
                raw = board_command(board, "READ:CALibration:TRAINing:STAGe:PERSistence?", ns)
                rows[uid] = {"present": True, "port": board.port, "idn": board.idn,
                             "build": board.build, "persistence": parse_persistence(raw),
                             "persistence_raw": raw.strip()}
        finally:
            close_persistent_connections()
        return rows


def parse_persistence(raw):
    row = [value.strip().strip('"') for value in next(csv.reader([raw]), [])]
    if len(row) != len(PERSISTENCE_FIELDS) or row[0] != "TRN03NVS":
        return None
    parsed = {"tag": row[0]}
    for name, value in zip(PERSISTENCE_FIELDS[1:], row[1:]):
        try:
            parsed[name] = int(value, 0)
        except ValueError:
            return None
    return parsed


def method_reserved(ctx, number):
    """Steps that would touch the transport refuse a reserved route by name."""
    info = METHODS[ctx.method]
    if info["implemented"]:
        return False
    ctx.failures.append(f"Step {number}: method '{ctx.method}' is reserved"
                        f" ({info['label']}) and not implemented; nothing was flashed")
    return True


@step(0)
def step_0_build(ctx):
    """Incremental build into the shared build directory (no recompile when clean)."""
    if ctx.args.no_build or ctx.args.package:
        ctx.log(f"  build skipped ({'--package' if ctx.args.package else '--no-build'});"
                f" reusing {ctx.package}")
        ctx.warnings.append("Step 0 build skipped by request: the package may not match"
                            " the working tree")
        return
    code, log = ctx.run([sys.executable, str(ROOT / "tools/cmake_build_auto/cmake_build_auto.py"),
                         "--root", str(ROOT), "--build-dir", str(ctx.build_dir)], "build.log")
    ctx.log(f"  cmake_build_auto exit {code} (log {log.name})")
    if code != 0:
        tail = [line for line in log.read_text(encoding="utf-8", errors="replace").splitlines()
                if "overflow" in line or "error" in line.lower()][:3]
        ctx.failures.append(f"Step 0 build failed (exit {code}): {' | '.join(tail) or log.name}")


@step(1)
def step_1_artifacts(ctx):
    """The package the OTA route sends, the UF2 the picotool route loads."""
    p3 = p3_module()
    if not ctx.package.is_file():
        ctx.failures.append(f"Step 1 OTA package missing: {ctx.package}")
        return
    try:
        ctx.build_id = p3._read_package_build_id(ctx.package)            # noqa: SLF001
    except Exception as exc:                                              # noqa: BLE001
        ctx.failures.append(f"Step 1 invalid OTA package: {exc}")
        return
    ctx.artifacts["package"] = {
        "path": str(ctx.package.relative_to(ROOT)).replace("\\", "/")
        if ctx.package.is_relative_to(ROOT) else str(ctx.package),
        "sha256": sha256(ctx.package), "bytes": ctx.package.stat().st_size,
        "modified": time.strftime("%Y-%m-%d %H:%M:%S",
                                  time.localtime(ctx.package.stat().st_mtime))}
    ctx.log(f"  package build {ctx.build_id} sha {ctx.artifacts['package']['sha256'][:12]}"
            f" modified {ctx.artifacts['package']['modified']}")
    if ctx.method == "picotool":
        if not ctx.uf2.is_file():
            ctx.failures.append(f"Step 1 UF2 missing for picotool: {ctx.uf2}")
            return
        ctx.artifacts["uf2"] = {"path": str(ctx.uf2.relative_to(ROOT)).replace("\\", "/"),
                                "sha256": sha256(ctx.uf2), "bytes": ctx.uf2.stat().st_size}
        ctx.log(f"  uf2 sha {ctx.artifacts['uf2']['sha256'][:12]}")


@step(2)
def step_2_before(ctx):
    """What the boards run and what calibration they hold, before anything is sent."""
    ctx.before = ctx.snapshot_boards()
    (ctx.out_dir / "boards-before.json").write_text(
        json.dumps(ctx.before, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    missing = [uid for uid, row in ctx.before.items() if not row.get("present")]
    for uid, row in ctx.before.items():
        if row.get("present"):
            persistence = row.get("persistence") or {}
            ctx.log(f"  {uid} {row['port']} build {row['build']}"
                    f" | calibration valid {persistence.get('persisted_valid', '?')}"
                    f" loaded {persistence.get('loaded', '?')}"
                    f" gen {persistence.get('calibration_generation', '?')}"
                    f" crc {persistence.get('payload_crc32', '?')}")
    if missing:
        ctx.failures.append(f"Step 2 boards missing before flashing: {missing}")
    if ctx.build_id and all(row.get("build") == ctx.build_id
                            for row in ctx.before.values() if row.get("present")):
        # PROJECT_BUILD_ID is cached at CMake configure time, so incremental builds keep
        # the same id: an equal id before and after cannot prove the new image landed.
        ctx.warnings.append(f"Step 2 boards already report build {ctx.build_id}: the build id"
                            f" is CMake-cached (not unique per image), so step 5 cannot"
                            f" distinguish old from new by id alone")


@step(3)
def step_3_method_preflight(ctx):
    """Route-specific preconditions, refused by name rather than failing mid-flash."""
    info = METHODS[ctx.method]
    ctx.log(f"  method {ctx.method}: {info['label']} - {info['note']}")
    if method_reserved(ctx, 3):
        return
    tool = ROOT / info["tool"]
    if not tool.is_file():
        ctx.failures.append(f"Step 3 transport tool missing: {info['tool']}")
        return
    if ctx.method == "picotool" and not ctx.args.allow_factory_image:
        ctx.failures.append("Step 3 picotool loads DHRT100_FACTORY.uf2 (bootloader + slot A +"
                            " boot control); pass --allow-factory-image to accept that, or"
                            " use --method ota to keep the stored calibration")


@step(4)
def step_4_flash(ctx):
    """Send the image over the selected route."""
    if method_reserved(ctx, 4):
        return
    if any(record["status"] == "FAIL" and record["step"] in (1, 3) for record in ctx.records):
        ctx.failures.append("Step 4 not attempted: an earlier artifact/method check failed")
        return
    if not ctx.build_id:
        ctx.failures.append("Step 4 not attempted: no package build id (run step 1)")
        return
    ctx.flash_attempted = True
    if ctx.method == "ota":
        ota_dir = ctx.out_dir / "ota-four-board"
        command = [sys.executable, str(ROOT / METHODS["ota"]["tool"]), str(ctx.package),
                   "--expected-board-count", str(len(ctx.board_ids)),
                   "--expected-build", ctx.build_id, "--out-dir", str(ota_dir),
                   "--block-size", "4096"]
        for uid in ctx.board_ids:
            command.extend(["--serial-number", uid])
        code, log = ctx.run(command, "ota.log")
        ctx.log(f"  ota_multi_update exit {code} (log {log.name})")
        summary_path = ota_dir / "summary.json"
        if summary_path.exists():
            ctx.flash_summary = json.loads(summary_path.read_text(encoding="utf-8"))
        if code != 0:
            ctx.failures.append(f"Step 4 OTA exited {code}; see {log.name}")
        return
    helper = ROOT / METHODS["picotool"]["tool"]
    flash_dir = ctx.out_dir / "flash-picotool"
    flash_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for uid in ctx.board_ids:
        transcript = flash_dir / f"{uid}.txt"
        started = time.perf_counter()
        completed = subprocess.run([sys.executable, str(helper), str(ctx.uf2),
                                    "--serial-number", uid, "--retries", "3",
                                    "--settle", "2.0", "--out", str(transcript)],
                                   cwd=str(ROOT), text=True, capture_output=True)
        elapsed = round(time.perf_counter() - started, 1)
        rows.append({"serial_number": uid, "returncode": completed.returncode,
                     "elapsed_s": elapsed})
        ctx.log(f"  picotool {uid} rc {completed.returncode} {elapsed:.1f}s")
    ctx.flash_summary = {"schema": "HAOFV_PICOTOOL_FLASH_EVIDENCE_V1", "boards": rows,
                         "failed_count": sum(1 for row in rows if row["returncode"] != 0)}
    (flash_dir / "summary.json").write_text(
        json.dumps(ctx.flash_summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if ctx.flash_summary["failed_count"]:
        ctx.failures.append(f"Step 4 picotool failed on"
                            f" {[r['serial_number'] for r in rows if r['returncode']]}")


@step(5)
def step_5_verify(ctx):
    """Exact board set back online, on the package build, OTA gate met."""
    if not METHODS[ctx.method]["implemented"]:
        ctx.warnings.append(f"Step 5 skipped: method '{ctx.method}' is reserved")
        return
    ctx.after = ctx.snapshot_boards(wait_s=60.0)
    (ctx.out_dir / "boards-after.json").write_text(
        json.dumps(ctx.after, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    missing = [uid for uid, row in ctx.after.items() if not row.get("present")]
    if missing:
        ctx.failures.append(f"Step 5 boards did not come back: {missing}")
    wrong = {uid: row["build"] for uid, row in ctx.after.items()
             if row.get("present") and ctx.build_id and row["build"] != ctx.build_id}
    if wrong:
        ctx.failures.append(f"Step 5 boards not on package build {ctx.build_id}: {wrong}")
    for uid, row in ctx.after.items():
        if row.get("present"):
            ctx.log(f"  {uid} {row['port']} build {row['build']}")
    if ctx.method == "ota" and ctx.flash_attempted:
        try:
            p3_module().validate_ota(ctx.flash_summary or {}, ctx.board_ids, ctx.build_id)
            ctx.log("  OTA summary gate: passed (all boards updated, 4096-byte blocks)")
        except Exception as exc:                                          # noqa: BLE001
            ctx.failures.append(f"Step 5 OTA summary gate: {exc}")


@step(6)
def step_6_calibration(ctx):
    """The stored calibration must be the same record after the flash."""
    if not METHODS[ctx.method]["implemented"]:
        ctx.warnings.append(f"Step 6 skipped: method '{ctx.method}' is reserved")
        return
    if not ctx.before:
        ctx.warnings.append("Step 6 no pre-flash snapshot (step 2 not run): retention unproven")
        return
    after = ctx.after or ctx.snapshot_boards(wait_s=30.0)
    lost = {}
    for uid in ctx.board_ids:
        old = (ctx.before.get(uid) or {}).get("persistence")
        new = (after.get(uid) or {}).get("persistence")
        if old is None:
            continue
        if new is None:
            lost[uid] = "persistence page unreadable after flash"
            continue
        changed = [name for name in PERSISTENCE_IDENTITY if old.get(name) != new.get(name)]
        if changed or new.get("persisted_valid") != 1:
            lost[uid] = f"changed {changed or ['persisted_valid']}"
        ctx.log(f"  {uid} calibration valid {new.get('persisted_valid')}"
                f" loaded {new.get('loaded')} gen {new.get('calibration_generation')}"
                f" crc {new.get('payload_crc32')}"
                f" {'UNCHANGED' if uid not in lost else 'CHANGED'}")
    if lost:
        ctx.failures.append(f"Step 6 stored calibration not retained: {lost}; the bench"
                            f" needs recalibration before DPLL60/T2")


@step(7)
def step_7_summary(ctx):
    """One machine-readable record of what was flashed, how, and what it did."""
    summary = {
        "schema": "HDRT100_OTA_WORKFLOW_SUMMARY_V1",
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "method": ctx.method,
        "method_label": METHODS[ctx.method]["label"],
        "build_id": ctx.build_id,
        "artifacts": ctx.artifacts,
        "boards": {uid: {"before": (ctx.before.get(uid) or {}).get("build"),
                         "after": (ctx.after.get(uid) or {}).get("build")}
                   for uid in ctx.board_ids},
        "flash_summary": ctx.flash_summary,
        "failures": list(ctx.failures),
        "warnings": list(ctx.warnings),
        "passed": not ctx.failures,
        "note": "workflow record, not an acceptance credential",
    }
    path = ctx.out_dir / "ota-workflow-summary.json"
    path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    ctx.log(f"  summary -> {path.relative_to(ROOT)}")


def inject_html_view(payload) -> str:
    """Write the payload into the HTML view's inline data block (see DPLL60_workflow)."""
    if not HTML_VIEW.exists():
        return ""
    text = HTML_VIEW.read_text(encoding="utf-8")
    start = "<!-- OTA-DATA-START -->"
    end = "<!-- OTA-DATA-END -->"
    if start not in text or end not in text:
        return ""
    block = (f'{start}\n<script id="ota-flow-data" type="application/json">'
             f'{json.dumps(payload, ensure_ascii=False)}</script>\n{end}')
    head, _, rest = text.partition(start)
    _, _, tail = rest.partition(end)
    HTML_VIEW.write_text(head + block + tail, encoding="utf-8", newline="")
    return str(HTML_VIEW.relative_to(ROOT)).replace("\\", "/")


def refresh_html_view() -> int:
    """Re-inject the newest run's data (no bench access, no steps)."""
    path = LOG_DIR / "ota_flow.json"
    if not path.exists():
        print(f"no data yet: {path} - run some steps first")
        return 2
    payload = json.loads(path.read_text(encoding="utf-8"))
    target = inject_html_view(payload)
    print(f"refreshed {target or '(HTML view not found)'} from {path.name}"
          f" (generated {payload.get('generated')}, failed {payload.get('failed') or 'none'})")
    return 0


def emit_flow_data(ctx, selection):
    by_id = {record["step"]: record for record in ctx.records}
    payload = {
        "schema": "HDRT100_OTA_FLOW_V1",
        "generated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "method": ctx.method,
        "methods": [{"id": key, **value} for key, value in METHODS.items()],
        "build_id": ctx.build_id,
        "package_sha256": (ctx.artifacts.get("package") or {}).get("sha256"),
        "selection": list(selection),
        "failed": [record["step"] for record in ctx.records if record["status"] == "FAIL"],
        "warnings": [record["warning"] for record in ctx.records if record.get("warning")],
        "log": str(ctx.log_path.relative_to(ROOT)).replace("\\", "/"),
        "boards": [{"id": uid,
                    "before": (ctx.before.get(uid) or {}).get("build"),
                    "after": (ctx.after.get(uid) or {}).get("build"),
                    "calibration_before": ((ctx.before.get(uid) or {}).get("persistence")
                                           or {}).get("payload_crc32"),
                    "calibration_after": ((ctx.after.get(uid) or {}).get("persistence")
                                          or {}).get("payload_crc32")}
                   for uid in ctx.board_ids],
        "steps": [{
            "id": number,
            "title": STEP_TITLES[number],
            "planned": number in selection,
            "status": (by_id[number]["status"] if number in by_id
                       else ("pending" if number in selection else "not-run")),
            "seconds": round(by_id[number]["seconds"], 1) if number in by_id else None,
            "reason": (by_id[number]["reason"] or "") if number in by_id else "",
            "warning": (by_id[number].get("warning") or "") if number in by_id else "",
        } for number in sorted(STEP_TITLES)],
    }
    for name in ("ota_flow.json", "ota_flow_data.js"):
        path = LOG_DIR / name
        text = json.dumps(payload, ensure_ascii=False, indent=2)
        path.write_text(text if name.endswith(".json") else f"window.OTA_FLOW = {text};\n",
                        encoding="utf-8")
    ctx.log("flow data: out/HardwareAcceptance/ota_flow.json · ota_flow_data.js")
    target = inject_html_view(payload)
    ctx.log(f"html view refreshed: {target or '(not found)'}")


def parse_selection(text):
    if text is None or text.strip().lower() in ("all", "full", "*"):
        return sorted(STEP_TITLES)
    selection = set()
    for part in text.replace(" ", "").split(","):
        if not part:
            continue
        if "-" in part:
            low, _, high = part.partition("-")
            selection.update(range(int(low), int(high) + 1))
        else:
            selection.add(int(part))
    return sorted(selection)


def run_steps(args, selection):
    ctx = Context(args)
    original = sys.stdout
    handle = ctx.log_path.open("a", encoding="utf-8")
    sys.stdout = _Tee(original, handle)
    try:
        ctx.log(f"method: {ctx.method}  steps: {selection}  out: {ctx.out_dir}")
        for number in selection:
            function = STEP_FUNCTIONS.get(number)
            if function is None:
                ctx.log(f"=== Step {number} has no implementation")
                continue
            ctx.log(f"=== Step {number} START  {STEP_TITLES[number]}")
            started = time.time()
            before = len(ctx.failures)
            before_warn = len(ctx.warnings)
            try:
                function(ctx)
            except Exception as exc:                                      # noqa: BLE001
                ctx.failures.append(f"Step {number} raised {type(exc).__name__}: {exc}")
            duration = time.time() - started
            new = ctx.failures[before:]
            new_warnings = ctx.warnings[before_warn:]
            # Three states, as in DPLL60_workflow: OK / warn (non-blocking) / FAIL.
            status = "FAIL" if new else ("warn" if new_warnings else "OK")
            reason = new[0] if new else (new_warnings[0] if new_warnings else "")
            ctx.records.append({"step": number, "title": STEP_TITLES[number],
                                "seconds": duration, "status": status,
                                "reason": new[0] if new else "",
                                "warning": new_warnings[0] if new_warnings else ""})
            ctx.log(f"=== Step {number} END    {status.upper()}  {duration:.1f}s"
                    f"{'  reason: ' + reason if reason else ''}")
            if args.interactive:
                try:
                    if input(f"Step {number} done. Enter=continue, q=stop: ").strip()[:1] == "q":
                        break
                except EOFError:
                    pass
        ctx.log("step summary:")
        for record in ctx.records:
            ctx.log(f"  {record['step']:>3}  {record['seconds']:>6.1f}s  {record['status']:<4}"
                    f"  {record['title'][:34]:<34} {record['reason'][:56]}")
        ctx.log(f"failed steps: "
                f"{[r['step'] for r in ctx.records if r['status'] == 'FAIL'] or 'none'}")
        emit_flow_data(ctx, selection)
    finally:
        sys.stdout = original
        handle.close()
    return 1 if any(r["status"] == "FAIL" for r in ctx.records) else 0


class _Tee:
    def __init__(self, *streams):
        self.streams = streams

    def write(self, data):
        for stream in self.streams:
            try:
                stream.write(data)
            except Exception:                                             # noqa: BLE001
                pass
        return len(data)

    def flush(self):
        for stream in self.streams:
            try:
                stream.flush()
            except Exception:                                             # noqa: BLE001
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--list", action="store_true", help="print the step table and exit")
    parser.add_argument("--method", choices=sorted(METHODS), default="ota",
                        help="flashing route; 'tdma' is reserved (not implemented)")
    parser.add_argument("--step", type=int, default=None, help="run exactly one step")
    parser.add_argument("--steps", default=None, help="step selection, e.g. '1-3' or '0,2,5-7'")
    parser.add_argument("--full", action="store_true", help="run every step in order")
    parser.add_argument("--no-build", action="store_true",
                        help="skip step 0's build and reuse the build directory's package")
    parser.add_argument("--package", default=None,
                        help="send this DHRT100_UPDATE.pkg instead of the build output")
    parser.add_argument("--build-dir", default=None,
                        help=f"build directory (default {DEFAULT_BUILD_DIR.relative_to(ROOT)})")
    parser.add_argument("--allow-factory-image", action="store_true",
                        help="picotool route: accept loading DHRT100_FACTORY.uf2")
    parser.add_argument("--interactive", action="store_true",
                        help="wait for Enter between steps")
    parser.add_argument("--refresh-html", action="store_true",
                        help="re-inject the newest run's data into the HTML view and exit")
    args = parser.parse_args()
    if args.refresh_html:
        return refresh_html_view()
    if args.list:
        for key, info in METHODS.items():
            print(f"  method {key:<9} {'' if info['implemented'] else '[reserved] '}"
                  f"{info['label']}")
        for number in sorted(STEP_TITLES):
            print(f"{number:>3}  {STEP_TITLES[number]}")
        return 0
    if args.step is not None:
        return run_steps(args, [args.step])
    if args.steps is not None:
        return run_steps(args, parse_selection(args.steps))
    if args.full:
        return run_steps(args, parse_selection("all"))
    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
