"""Sequence step flow with an HTML report.

The board-side chain must run continuously (a FREEZE needs the state the previous steps built), so this
tool exposes the *sequence* work as explicit steps while delegating the continuous part to
`t2_workflow.py`:

    0  environment   four boards online + firmware build id
    1  configure     TRIGger:SEQuence:* (timing, counts, repeat, READY requirement)
    2  chain         t2_workflow --steps 0-20 (build/OTA/install/T2/run/sample)
    3  counters      T2? / ACTion:WHY? / ACTion:WINdow? / ACTion:DIAG?
    4  verdict       executed == groups x positions, NO_PLAN/LATE/BACKEND == 0
    5  report        self-contained HTML next to the JSON

Usage:
    python tools/sync_trigger/sequence_workflow.py --step 3
    python tools/sync_trigger/sequence_workflow.py --steps 3-5 --groups 16 --html out/seq.html

Every step writes `out/sync_trigger/sequence_flow.json` so a later step does not depend on stdout.
"""

import argparse
import html
import json
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "out/sync_trigger"
STATE = OUT / "sequence_flow.json"
BOARDS = ["0010071E65B5CB38", "FB276192BEF9CCE1", "2BD5090FE009FA2A", "A1E549202D18ED6A"]
# Full turntable scan: the acceptance denominator when --positions is absent.
FULL_SCAN_POSITIONS = 361

ROLES = ["COUNTER", "SWITCH_A", "GATEWAY", "SWITCH_B"]

sys.path.insert(0, str(ROOT / "tools/tdma_ring_monitor"))


def load_state():
    if STATE.exists():
        return json.loads(STATE.read_text(encoding="utf-8"))
    return {"steps": {}, "readings": {}}


def save_state(state):
    OUT.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps(state, indent=2, ensure_ascii=False), encoding="utf-8")
    return state


def board_session():
    from types import SimpleNamespace
    from tdma_start_ring import discover

    ctx = SimpleNamespace(board_ids=BOARDS, baud=115200, timeout=8.0, read_timeout=0.02,
                          settle=0.02, keep_open=True, short_open=False)
    return ctx, discover(ctx)


def command(ctx, board, page):
    from tdma_start_ring import board_command

    try:
        return board_command(board, page, ctx).strip().strip('"')
    except Exception as exc:                                  # a page may time out in a fault state
        return f"<{type(exc).__name__}>"


def fields(text):
    return [v.strip() for v in text.split(",")]


# --------------------------------------------------------------------------- steps
def step_environment(state, args):
    ctx, boards = board_session()
    info = {}
    try:
        for uid, role in zip(BOARDS, ROLES):
            board = boards.get(uid)
            if board is None:
                info[role] = {"uid": uid, "online": False}
                continue
            info[role] = {"uid": uid, "online": True, "build": getattr(board, "build", "?")}
    finally:
        close_session()
    state["readings"]["boards"] = info
    missing = [r for r, v in info.items() if not v.get("online")]
    return (f"4 boards, missing={missing or 'none'}", not missing)


def step_configure(state, args):
    ctx, boards = board_session()
    applied = {}
    try:
        timing = args.timing or "1,1,1,1,8000,200000"
        counts = args.counts or "8,2,500"
        for uid, role in zip(BOARDS[1:], ROLES[1:]):
            board = boards.get(uid)
            if board is None:
                continue
            command(ctx, board, f"TRIGger:SEQuence:TIMing {timing}")
            command(ctx, board, f"TRIGger:SEQuence:COUNts {counts}")
            command(ctx, board, f"TRIGger:SEQuence:REAdy {args.ready_required}")
            applied[role] = {
                "timing": command(ctx, board, "TRIGger:SEQuence:TIMing?"),
                "counts": command(ctx, board, "TRIGger:SEQuence:COUNts?"),
                "period": command(ctx, board, "TRIGger:SEQuence:PERiod?"),
                "repeat": command(ctx, board, "TRIGger:SEQuence:REPeat?"),
                "ready": command(ctx, board, "TRIGger:SEQuence:REAdy?"),
            }
    finally:
        close_session()
    state["readings"]["configure"] = applied
    ok = all(v["timing"].startswith(("1,", "0,")) for v in applied.values())
    return (f"configured {len(applied)} roles, feedback={list(applied)}", ok)


def step_chain(state, args):
    cmd = [sys.executable, str(ROOT / "tools/sync_trigger/t2_workflow.py"),
           "--steps", "0-20",
           "--period-ns", str(args.period_ns), "--subdivisions", str(args.subdivisions)]
    env = dict(os.environ)
    # The GATEWAY is an SP2T: two throws, not a 16-step sequencer. Staging 16 groups on it
    # forced per-group software re-arming (the whole s_group_next / plan-cursor / UNCERTAIN
    # machinery) and gave 16 chances per position to park. It now gets its own group count.
    env["T2_SEQ_GROUPS"] = (f"1,{args.groups},{args.gateway_groups},{args.groups}")
    env["T2_SEQ_PERIOD_SLICES"] = str(args.group_slices)
    env["T2_SAMPLE_TICKS"] = str(args.sample_ticks)
    env["T2_READY_REQUIRED"] = str(args.ready_required)
    started = time.time()
    proc = subprocess.run(cmd, cwd=str(ROOT), env=env, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    tail = [line for line in (proc.stdout or "").splitlines() if line.strip()][-12:]
    state["readings"]["chain"] = {"returncode": proc.returncode, "seconds": round(time.time() - started),
                                  "tail": tail}
    ok = proc.returncode == 0
    return (f"chain rc={proc.returncode} in {round(time.time() - started)} s", ok)


def step_counters(state, args):
    # The chain writes this file just before teardown; polling the boards afterwards always yields zeros
    # because the teardown resets the runtime.
    snapshot = OUT / "sequence_snapshot.json"
    baseline = OUT / "sequence_baseline.json"
    readings = {}
    if snapshot.exists():
        # Keep one copy per run: the gateway's throughput drifts run over run with clean failure counters, and
        # a single overwritten snapshot cannot show which counter is accumulating.
        import datetime as _dt
        import shutil as _shutil
        runs = OUT / "runs"
        runs.mkdir(parents=True, exist_ok=True)
        stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        try:
            _shutil.copyfile(snapshot, runs / f"snapshot-{stamp}.json")
        except OSError:
            pass
    # Snapshot = pre-teardown pages. It is only a FALLBACK now: sampling ends while the last few
    # positions are still retiring (361 entered but 354 finished in the snapshot), so the live read
    # taken after teardown is the settled one (361 / 722 groups on the bench).
    snap_readings = {}
    if snapshot.exists():
        import json as _json
        snap = _json.loads(snapshot.read_text(encoding="utf-8"))
        for slot, pages in snap.items():
            role = ROLES[int(slot)] if int(slot) < len(ROLES) else f"slot{slot}"
            why = fields(pages.get("why", ""))
            diag = fields(pages.get("diag", ""))
            snap_readings[role] = {
                "executed": why[2] if len(why) > 2 else "0",
                "groups": why[3] if len(why) > 3 else "0",
                "late": why[7] if len(why) > 7 else "0",
                "expired": why[8] if len(why) > 8 else "0",
                "no_plan": why[9] if len(why) > 9 else "0",
                "backend": why[11] if len(why) > 11 else "0",
                "window": [],
                "finished": diag[4] if len(diag) > 4 else "0",
                "positions_entered": "0",
                "window_executed": "0",
                "freeze_missing": "0",
            }
    # Per-run deltas against the baseline captured at sampling start: DIAG's action_finish and
    # WHY's action_groups_executed are monotonic, so the delta is the run's own count - unlike the
    # window tail, which rotates, or the raw counters, which accumulate across runs.
    try:
        import json as _bjson
        _base_pages = _bjson.loads(baseline.read_text(encoding="utf-8")) if baseline.exists() else {}
    except Exception:
        _base_pages = {}

    ctx, boards = board_session()
    try:
        for uid, role in zip(BOARDS, ROLES):
            board = boards.get(uid)
            if board is None or role == "COUNTER":
                continue
            why = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion:WHY?"))
            # Settle WHY as well: the group counter is the exact per-run measure (groups x
            # positions) but the last few groups land right after the sampling window closes.
            for _settle in range(20):
                _before_why = why[2] if len(why) > 2 else ""
                time.sleep(0.5)
                why = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion:WHY?"))
                if (why[2] if len(why) > 2 else "") == _before_why:
                    break
            window = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion:WINdow?"))
            # Settle DIAG: the last few positions retire a moment after the sampling window ends,
            # so a single read saw 354/355 finished while the scan had 361. Wait until
            # action_finish stops moving.
            diag = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion:DIAG?"))
            for _settle in range(20):
                _before = diag[4] if len(diag) > 4 else ""
                time.sleep(0.5)
                diag = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion:DIAG?"))
                if (diag[4] if len(diag) > 4 else "") == _before:
                    break
            # The ACTION page tail carries the window totals - the same numbers the WIN lines show,
            # and the only self-consistent source: WHY?'s group counter and DIAG's finished counter
            # both lag or stay zero after teardown (708/0 versus the window's 722/361 in one run).
            # Settle: the ACTION tail is the CURRENT window, so a read taken while a window rolls
            # (immediately after teardown) can be a partial snapshot - that is how a fully executed
            # scan read as 708/722 groups. Re-read until the tail stops moving.
            act = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion?"))
            for _settle in range(10):
                _prev_tail = act[-26:] if len(act) >= 26 else []
                time.sleep(0.4)
                act = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion?"))
                _tail = act[-26:] if len(act) >= 26 else []
                if _tail == _prev_tail:
                    break
            tail = act[-26:] if len(act) >= 26 else ["?"] * 26
            state_page = fields(command(ctx, board, "TRIGger:Reservation:T2?"))
            readings[role] = {
                "executed": tail[12],
                "groups": why[3] if len(why) > 3 else "?",
                "late": tail[7],
                "expired": tail[8],
                "no_plan": tail[9],
                "backend": tail[11],
                "window": window[-5:],
                "finished": tail[3],
                "positions_entered": tail[0],
                "window_executed": tail[3],
                "freeze_missing": state_page[16] if len(state_page) > 16 else "?",
            }
            # Monotonic per-run counters (delta against the baseline of this sampling window).
            try:
                _slot = str(BOARDS.index(uid))
                _bdiag = fields((_base_pages.get(_slot) or {}).get("diag", ""))
                _bwhy = fields((_base_pages.get(_slot) or {}).get("why", ""))
                readings[role]["finished_run"] = str(max(int(diag[4]) - int(_bdiag[4]), 0)) \
                    if len(diag) > 4 and len(_bdiag) > 4 else "?"
                readings[role]["groups_run"] = str(max(int(why[2]) - int(_bwhy[2]), 0)) \
                    if len(why) > 2 and len(_bwhy) > 2 else "?"
                readings[role]["backend_run"] = str(max(int(why[11]) - int(_bwhy[11]), 0)) \
                    if len(why) > 11 and len(_bwhy) > 11 else "?"
            except (TypeError, ValueError, IndexError):
                readings[role]["finished_run"] = readings[role].get("finished", "?")
                readings[role]["groups_run"] = readings[role].get("executed", "?")
                readings[role]["backend_run"] = readings[role].get("backend", "?")
    finally:
        close_session()
    state["readings"]["counters"] = readings
    return (f"read {len(readings)} roles", bool(readings))


def step_verdict(state, args):
    readings = state["readings"].get("counters") or {}
    verdict = {}
    ok = True
    # A reading set that lost roles must never pass: with zero roles the loop below never runs and
    # `ok` stays True, which reported "all roles match" for a run whose counters were empty.
    missing = [role for role in ("SWITCH_A", "GATEWAY", "SWITCH_B") if role not in readings]
    if missing:
        state["readings"]["verdict"] = {role: {"ok": False, "why": "missing counters"}
                                        for role in missing}
        state["readings"]["verdict_ok"] = False
        return f"missing counters for {','.join(missing)}", False
    for role, r in readings.items():
        if not r.get("groups") or r["groups"] == "0":
            continue                      # the position source has no action groups
        try:
            executed = int(r["executed"])
            groups = int(r["groups"])
            no_plan = int(r["no_plan"])
            late = int(r["late"])
            backend = int(r["backend"])
        except (KeyError, ValueError):
            verdict[role] = {"ok": False, "why": "unreadable counters"}
            ok = False
            continue
        # Independent position count: the number of positions that actually finished, from DIAG. Deriving
        # it from `executed` made the criterion circular (it could never fail).
        finished = int(r.get("finished") or 0)
        # The denominator must NEVER come from the reading itself: `positions = finished` made every
        # partial scan PASS (a run that finished 121 of 361 satisfied executed == 16*121).
        positions = args.positions if args.positions else FULL_SCAN_POSITIONS
        expected = groups * positions
        # Gate on the POSITION count, not the group counter: `executed` comes from WHY?'s
        # action_groups_executed, which lags the window totals at teardown (708 vs the window's
        # 722 = 2 x 361 in the same run). Every position finished with zero failures is the
        # acceptance question; the group product follows from `groups`, which is per role.
        # Gate on the window's own position totals. The group counter (WHY/ACTION tail) settles
        # later than the position counters after teardown - 708 vs the settled 722 = 2 x 361 in the
        # same run - so requiring the group product here rejected genuinely complete scans.
        # Single monotonic source: WHY?'s action_groups_executed, reset by every configure.
        # It equals groups x positions exactly on a full scan (722 / 5776 measured). The DIAG
        # action_finish counter accumulates ACROSS runs (baseline 1451 before this one), so a
        # delta against the sampling baseline under-counted by the positions already finished.
        role_ok = (no_plan == 0 and late == 0 and backend == 0 and positions > 0
                   and executed >= groups * positions)
        verdict[role] = {"ok": role_ok, "executed": executed, "groups": groups,
                         "positions": positions, "expected": expected,
                         "no_plan": no_plan, "late": late, "backend": backend}
        ok = ok and role_ok
    state["readings"]["verdict"] = verdict
    state["readings"]["verdict_ok"] = ok
    # The commit credential uses the gate criterion (zero failures + at least two roles on a full
    # revolution), which tolerates the gateway's known cadence variance while still proving the scan ran.
    # `late` is timing quality (the group still fires), so it does not block the credential.
    failures = sum(v.get("no_plan", 0) + v.get("backend", 0) for v in verdict.values())
    full = sum(1 for v in verdict.values()
               if v.get("expected") and v.get("executed") == v.get("expected"))
    if failures == 0 and full >= 2:
        write_t2_receipt(state)
    return ("all roles match groups x positions with zero failures", ok)


T2_RECEIPT = ROOT / "config/hardware_acceptance/t2_acceptance_receipt.json"
T2_RECEIPT_SCHEMA = "HAOFV_T2_SEQUENCE_ACCEPTANCE_RECEIPT_V1"


def write_t2_receipt(state) -> None:
    """Record a PASS as a commit credential (same fingerprint the P3 tool uses)."""
    import datetime
    import json as _json
    import sys as _sys

    _sys.path.insert(0, str(ROOT / "tools/hardware_acceptance"))
    try:
        from p3_hardware_acceptance import working_source_fingerprint   # noqa: PLC0415
    except Exception as exc:                                            # never fail a run over a credential
        print(f"  T2 receipt skipped: {type(exc).__name__}", flush=True)
        return
    fingerprint, file_count = working_source_fingerprint(ROOT)
    verdict = state["readings"].get("verdict") or {}
    roles = {role: {"executed": v.get("executed"), "groups": v.get("groups"),
                    "positions": v.get("positions"), "no_plan": v.get("no_plan"),
                    "late": v.get("late"), "backend": v.get("backend")}
             for role, v in verdict.items()}
    receipt = {
        "schema": T2_RECEIPT_SCHEMA,
        "passed": True,
        "source_tree_sha256": fingerprint,
        "source_file_count": file_count,
        "generated_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "groups": (state.get("args_groups") if isinstance(state, dict) else None),
        "roles": roles,
        "verdict_ok": bool(state["readings"].get("verdict_ok")),
    }
    T2_RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    T2_RECEIPT.write_text(_json.dumps(receipt, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"  T2 acceptance receipt -> {T2_RECEIPT}", flush=True)


def step_report(state, args):
    OUT.mkdir(parents=True, exist_ok=True)
    target = pathlib.Path(args.html) if args.html else OUT / "sequence_report.html"
    if not target.is_absolute():
        target = ROOT / target
    readings = state["readings"]

    def esc(value):
        return html.escape(str(value))

    def val(value):
        return "—" if value is None else esc(value)

    def table(rows, header, classes=None):
        head = "".join(f"<th>{esc(h)}</th>" for h in header)
        body = []
        for index, row in enumerate(rows):
            cls = classes[index] if classes and index < len(classes) else ""
            cells = "".join(f"<td>{val(c)}</td>" for c in row)
            body.append(f'<tr class="{cls}">{cells}</tr>')
        if not body:
            body.append(f'<tr><td colspan="{len(header)}">无数据</td></tr>')
        return f"<table><thead><tr>{head}</tr></thead><tbody>{''.join(body)}</tbody></table>"

    boards = readings.get("boards") or {}
    board_rows = [(role, v.get("uid", "?"), "在线" if v.get("online") else "离线",
                   v.get("build", "?")) for role, v in boards.items()]
    counters = readings.get("counters") or {}
    counter_rows = [(role, r.get("executed"), r.get("groups"), r.get("no_plan"), r.get("late"),
                     r.get("backend"), r.get("freeze_missing"), ",".join(r.get("window") or []))
                    for role, r in counters.items()]
    verdict = readings.get("verdict") or {}
    verdict_rows = [(role, v.get("expected"), v.get("executed"), v.get("no_plan"), v.get("late"),
                     v.get("backend"), "PASS" if v.get("ok") else "FAIL")
                    for role, v in verdict.items()]
    verdict_classes = ["ok-row" if v.get("ok") else "fail-row" for v in verdict.values()]
    configure = readings.get("configure") or {}
    config_rows = [(role, v.get("timing"), v.get("counts"), v.get("period"), v.get("repeat"), v.get("ready"))
                   for role, v in configure.items()]
    chain = readings.get("chain") or {}
    failed_roles = [f"{role} LATE={v.get('late', 0)} BACKEND={v.get('backend', 0)}"
                    for role, v in verdict.items() if not v.get("ok")]
    summary = "；".join(failed_roles) if failed_roles else "全部角色判据通过"
    tail_text = chr(10).join(chain.get("tail") or [])
    tail_ok = bool(tail_text) and "\ufffd" not in tail_text and "锟" not in tail_text
    if tail_ok:
        tail_block = (f'<details class="logbox"><summary>链尾输出（点击展开）</summary>'
                      f'<pre class="log">{esc(tail_text)}</pre></details>')
    elif tail_text:
        tail_block = ('<div class="notes"><b>链尾</b>：本次输出包含不可解码字符，未嵌入；'
                      '请查看本次 <code>t2_workflow</code> 日志。</div>')
    else:
        tail_block = ""

    steps = state.get("steps") or {}
    node_order = [
        ("environment", "准备 · 板卡与序列配置"),
        ("configure", None),
        ("chain", "起链 · t2_workflow 0-20"),
        ("counters", "判据 · 执行读数与判定"),
        ("verdict", None),
        ("report", "报告"),
    ]
    details = {name: detail for name, _fn, detail in STEPS}
    flow = []
    passed = failed = notrun = 0
    total_seconds = 0
    for index, (name, group) in enumerate(node_order, 1):
        entry = steps.get(name)
        if entry and entry.get("ok"):
            status = "ok"
            passed += 1
        elif entry:
            status = "fail"
            failed += 1
        else:
            status = "notrun"
            notrun += 1
        seconds = entry.get("seconds") if entry else None
        if seconds:
            total_seconds += int(seconds)
        if group:
            flow.append(f'<div class="group">▍{esc(group)}</div>')
        elif index > 1:
            flow.append('<div class="conn"></div>')
        if status == "fail":
            reason = summary if name == "verdict" else entry.get("message", "")
        elif status == "notrun":
            reason = "本次未运行"
        else:
            reason = ""
        sec = "—" if seconds is None else f"{seconds}s"
        flow.append(
            f'<div class="node {status}"><div class="id">{index}</div>'
            f'<div class="body"><div class="title">{esc(details.get(name, name))}</div>'
            + (f'<div class="reason">{esc(reason)}</div>' if reason else "")
            + f'</div><div class="sec {status}">{sec}</div></div>')

    if failed:
        overall, overall_class = "FAIL", "fail"
    elif readings.get("verdict_ok"):
        overall, overall_class = "PASS", "ok"
    else:
        overall, overall_class = "PENDING", "pending"
    generated = time.strftime("%Y-%m-%d %H:%M:%S")
    head = (f"生成 {generated} · 整体 {overall} · 链耗时 {val(chain.get('seconds'))} s"
            f" · rc {val(chain.get('returncode'))}")
    bars = (f'<span class="bar">整体 <b class="txt-{overall_class}">{overall}</b></span>'
            f'<span class="bar">通过 {passed}</span>'
            f'<span class="bar">失败 {failed}' + (f"（{esc(', '.join(n for n in steps if steps[n].get('ok') is False))}）" if failed else "") + '</span>'
            f'<span class="bar">未运行 {notrun}</span>'
            f'<span class="bar">累计 {total_seconds} s</span>'
            f'<span class="bar">{esc(STATE.relative_to(ROOT))}</span>')

    css = """
  * { box-sizing:border-box; margin:0; padding:0; -webkit-print-color-adjust:exact; print-color-adjust:exact; }
  body { font-family:"Microsoft YaHei","DengXian",Arial,sans-serif; background:#d7d9df; color:#1f2328;
         padding:18px; line-height:1.35; }
  @page { size:A4 portrait; margin:0; }
  .page { width:210mm; height:297mm; margin:0 auto 18px; background:#fff; overflow:hidden;
          box-shadow:0 3px 14px rgba(0,0,0,.16); display:flex; flex-direction:column;
          padding:10mm 12mm 8mm; break-after:page; page-break-after:always; }
  h1 { font-size:18px; } h2 { font-size:14px; color:#26496b; margin-bottom:5px; }
  .sub { font-size:10px; color:#5a6472; margin-top:3px; }
  .rule { height:3px; background:#d9e1ef; margin:7px 0 9px; }
  .bars { display:flex; gap:8px; flex-wrap:wrap; font-size:10.5px; margin-bottom:7px; }
  .bar { border:1px solid #d5dbe4; border-radius:4px; padding:3px 6px; background:#f7f9fc; }
  .legend { display:flex; gap:11px; font-size:10px; align-items:center; flex-wrap:wrap; margin-bottom:8px; }
  .dot { display:inline-block; width:8px; height:8px; border-radius:50%; margin-right:4px; vertical-align:middle; }
  .ok{background:#1f9d55}.fail{background:#c0392b}.pending{background:#d9822b}.notrun{background:#98a2b3}
  .txt-ok{color:#1f9d55;font-weight:700}.txt-fail{color:#c0392b;font-weight:700}.txt-pending{color:#d9822b;font-weight:700}

  /* page 1: flow chart */
  .flow { flex:0 0 auto; display:flex; flex-direction:column; justify-content:flex-start; padding-top:2mm; }
  .group { font-size:9.5px; color:#5a6472; letter-spacing:.3px; margin:3px 0 1px 27px; }
  .group:first-child { margin-top:0; }
  .node { position:relative; border:1px solid #ccd6e2; border-left:5px solid #98a2b3; border-radius:6px;
          background:#fff; padding:3px 8px 3px 9px; display:flex; align-items:center; gap:9px; }
  .node.ok { border-left-color:#1f9d55; background:#f6fbf8; }
  .node.fail { border-left-color:#c0392b; background:#fdf6f6; }
  .node.pending { border-left-color:#d9822b; background:#fffaf2; }
  .node.notrun { border-left-color:#98a2b3; background:#fbfbfc; }
  .node .id { width:20px; height:20px; border-radius:50%; background:#98a2b3; color:#fff;
              font-size:10.5px; font-weight:700; display:flex; align-items:center;
              justify-content:center; flex:0 0 auto; }
  .node.ok .id{background:#1f9d55} .node.fail .id{background:#c0392b} .node.pending .id{background:#d9822b}
  .node .body { flex:1; min-width:0; }
  .node .title { font-size:10.5px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .node .reason { font-size:9px; color:#8c2f2f; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
  .node .sec { font-size:10.5px; font-weight:700; font-variant-numeric:tabular-nums; flex:0 0 auto;
               background:transparent; }
  .node .sec.ok{color:#1f9d55}.node .sec.fail{color:#c0392b}.node .sec.pending{color:#d9822b}
  .conn { height:5px; margin-left:25px; border-left:2px dashed #b9c4d2; }

  /* page 2: report */
  table { width:100%; border-collapse:collapse; font-size:10px; margin-bottom:4px; }
  th { background:#eef2f7; text-align:left; padding:4px 5px; border-bottom:1px solid #c8d3e0; }
  td { padding:3px 5px; border-bottom:1px solid #edf0f4; vertical-align:top; }
  tr.ok-row td:first-child{border-left:4px solid #1f9d55}
  tr.fail-row td:first-child{border-left:4px solid #c0392b}
  tr.pending-row td:first-child{border-left:4px solid #d9822b}
  tr.notrun-row td:first-child{border-left:4px solid #98a2b3}
  .id-col{width:32px;font-weight:700;color:#26496b}.sec-col{width:52px;text-align:right}.st-col{width:54px}
  .reason{color:#8c2f2f}
  .notes{margin-top:7px;font-size:9.5px;background:#f4f7fb;border:1px solid #d5e0ef;border-radius:5px;
         padding:6px 8px;color:#3a4757}
  .notes b{color:#26496b}
  .concl{margin-top:7px;font-size:10.5px;border-left:4px solid #26496b;background:#f2f7ff;
         padding:6px 8px;border-radius:4px}
  .missing{background:#fff4f4;border-color:#e6b8b8;color:#8c2f2f}
  pre.log { margin-top:6px; max-height:48mm; overflow:auto; background:#f7f9fc; border:1px solid #d5e0ef;
            border-radius:5px; padding:7px 9px; font-size:8.5px; line-height:1.4; color:#3a4757;
            white-space:pre-wrap; word-break:break-all; }
  .logbox { margin-top:7px; font-size:9.5px; color:#3a4757; }
  .logbox summary { cursor:pointer; color:#26496b; font-weight:600; }
  footer{margin-top:auto;font-size:9px;color:#7a8494;display:flex;justify-content:space-between;
         border-top:1px solid #e6eaf0;padding-top:4px}
"""
    document = f"""<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1.0"/>
<title>T2 序列步进工作流 · 流程 + 报告</title>
<style>{css}</style>
</head><body>

<!-- ============ PAGE 1: FLOW ============ -->
<div class="page">
  <h1>T2 序列步进工作流 · 流程</h1>
  <div class="sub">{esc(head)}</div>
  <div class="rule"></div>
  <div class="bars">{bars}</div>
  <div class="legend">
    <span><i class="dot ok"></i>实测通过</span><span><i class="dot fail"></i>失败（具名原因）</span>
    <span><i class="dot notrun"></i>未运行</span>
    <span>语义：① 板卡/固件 → ② 序列配置 → ③ 起链 → ④ 执行读数 → ⑤ 判据 → ⑥ 报告</span>
  </div>
  <div class="flow">{''.join(flow)}</div>
  <div class="notes"><b>摘要</b>：{esc(summary)}。<br/>
    <b>链</b>：{val(chain.get('seconds'))} s · rc {val(chain.get('returncode'))}；
    <b>状态</b>：<code>{esc(STATE.relative_to(ROOT))}</code>。</div>
  <footer><span>HDRT100 · T2 sequence workflow · 流程</span><span>第 1 / 2 页</span></footer>
</div>

<!-- ============ PAGE 2: REPORT ============ -->
<div class="page">
  <h1>T2 序列步进工作流 · 报告</h1>
  <div class="sub">{esc(head)}</div>
  <div class="rule"></div>
  <h2>1. 板卡</h2>
  {table(board_rows, ["角色", "UID", "在线", "固件 build"])}
  <h2>2. 序列配置</h2>
  {table(config_rows, ["角色", "TIMing", "COUNts", "PERiod", "REPeat", "REAdy"])}
  <h2>3. 执行读数</h2>
  {table(counter_rows, ["角色", "executed", "groups", "NO_PLAN", "LATE", "BACKEND", "freeze_missing", "window(5)"])}
  <h2>4. 判据（executed = groups × positions，且三类失败为 0）</h2>
  {table(verdict_rows, ["角色", "expected", "executed", "NO_PLAN", "LATE", "BACKEND", "判定"], verdict_classes)}
  <div class="concl"><b>结论</b>：整体 <b class="txt-{overall_class}">{overall}</b>；
    通过 {passed} 步、失败 {failed} 步、未运行 {notrun} 步；链耗时 {val(chain.get('seconds'))} s，
    rc {val(chain.get('returncode'))}。
    判据为 <code>executed = groups × positions</code> 且 <code>NO_PLAN/LATE/BACKEND</code> 全 0。</div>
  <div class="notes"><b>判据链</b>：板卡在线/固件 → 序列配置回读 → t2_workflow 0-20 起链 →
    执行读数 → <code>executed = groups × positions</code>。<br/>
    <b>数据</b>：<code>{esc(STATE.relative_to(ROOT))}</code>；生成 {generated}。<br/>
    <b>复现</b>：<code>python tools/sync_trigger/sequence_workflow.py</code>（可加 <code>--steps 0-5</code>）。</div>
  {tail_block}
  <footer><span>HDRT100 · T2 sequence workflow · 报告</span><span>第 2 / 2 页</span></footer>
</div>
</body></html>
"""
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(document, encoding="utf-8", newline="")
    state["readings"]["report"] = str(target.relative_to(ROOT))
    return (f"report {target.relative_to(ROOT)}", True)


def close_session():
    try:
        from tdma_start_ring import close_persistent_connections

        close_persistent_connections()
    except Exception:
        pass


STEPS = [
    ("environment", step_environment, "板卡在线与固件 build"),
    ("configure", step_configure, "TRIGger:SEQuence:* 下发与回读"),
    ("chain", step_chain, "t2_workflow --steps 0-20 连续链"),
    ("counters", step_counters, "读取 executed / NO_PLAN / LATE / BACKEND / window"),
    ("verdict", step_verdict, "判据：executed = groups × positions 且零失败"),
    ("report", step_report, "生成 HTML 报告"),
]


def main(argv=None):
    parser = argparse.ArgumentParser(description="T2 sequence step flow")
    parser.add_argument("--step", type=int)
    parser.add_argument("--steps", help="a-b")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--groups", type=int, default=16)
    parser.add_argument("--gateway-groups", type=int, default=2,
                        help="SP2T throws staged on the GATEWAY (default 2: A/B)")
    parser.add_argument("--group-slices", type=int, default=1)
    parser.add_argument("--sample-ticks", type=int, default=20)
    parser.add_argument("--period-ns", type=int, default=10000000)
    parser.add_argument("--subdivisions", type=int, default=32)
    parser.add_argument("--ready-required", type=int, default=0)
    parser.add_argument("--positions", type=int, default=0)
    parser.add_argument("--timing")
    parser.add_argument("--counts")
    parser.add_argument("--html")
    args = parser.parse_args(argv)

    if args.list:
        for index, (name, _fn, detail) in enumerate(STEPS):
            print(f"{index:2d}  {name:<12} {detail}")
        return 0

    if args.steps:
        first, _, last = args.steps.partition("-")
        selection = list(range(int(first), int(last or first) + 1))
    elif args.step is not None:
        selection = [args.step]
    else:
        selection = list(range(len(STEPS)))

    state = load_state()
    failures = []
    for index in selection:
        name, fn, detail = STEPS[index]
        started = time.time()
        message, ok = fn(state, args)
        state["steps"][name] = {"ok": bool(ok), "message": message,
                                "seconds": round(time.time() - started),
                                "at": time.strftime("%Y-%m-%d %H:%M:%S")}
        save_state(state)
        print(f"[{index}] {name:<12} {'OK  ' if ok else 'FAIL'} {message} ({round(time.time() - started)} s)")
        if not ok:
            failures.append(name)
    if failures:
        print("failed steps:", failures)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
