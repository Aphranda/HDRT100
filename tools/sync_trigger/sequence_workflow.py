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
    env["T2_SEQ_GROUPS"] = f"1,{args.groups},{args.groups},{args.groups}"
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
    readings = {}
    if snapshot.exists():
        import json as _json
        snap = _json.loads(snapshot.read_text(encoding="utf-8"))
        for slot, pages in snap.items():
            role = ROLES[int(slot)] if int(slot) < len(ROLES) else f"slot{slot}"
            why = fields(pages.get("why", ""))
            window = fields(pages.get("window", ""))
            diag = fields(pages.get("diag", ""))
            readings[role] = {
                "executed": why[2] if len(why) > 2 else "?",
                "groups": why[3] if len(why) > 3 else "?",
                "late": why[7] if len(why) > 7 else "?",
                "expired": why[8] if len(why) > 8 else "?",
                "no_plan": why[9] if len(why) > 9 else "?",
                "backend": why[11] if len(why) > 11 else "?",
                "window": window[-5:],
                "finished": diag[4] if len(diag) > 4 else "?",
                "source": "snapshot",
            }
        state["readings"]["counters"] = readings
        return (f"read {len(readings)} roles from the snapshot", bool(readings))
    ctx, boards = board_session()
    try:
        for uid, role in zip(BOARDS, ROLES):
            board = boards.get(uid)
            if board is None or role == "COUNTER":
                continue
            why = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion:WHY?"))
            window = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion:WINdow?"))
            diag = fields(command(ctx, board, "TRIGger:REServation:T2:ACTion:DIAG?"))
            state_page = fields(command(ctx, board, "TRIGger:REServation:T2?"))
            readings[role] = {
                "executed": why[2] if len(why) > 2 else "?",
                "groups": why[3] if len(why) > 3 else "?",
                "late": why[7] if len(why) > 7 else "?",
                "expired": why[8] if len(why) > 8 else "?",
                "no_plan": why[9] if len(why) > 9 else "?",
                "backend": why[11] if len(why) > 11 else "?",
                "window": window[-5:],
                # DIAG: result(2) + schema + action_advance + action_finish + ...
                "finished": diag[4] if len(diag) > 4 else "?",
                "freeze_missing": state_page[16] if len(state_page) > 16 else "?",
            }
    finally:
        close_session()
    state["readings"]["counters"] = readings
    return (f"read {len(readings)} roles", bool(readings))


def step_verdict(state, args):
    readings = state["readings"].get("counters") or {}
    verdict = {}
    ok = True
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
        positions = args.positions if args.positions else finished
        expected = groups * positions
        role_ok = (no_plan == 0 and late == 0 and backend == 0 and positions > 0
                   and executed == expected)
        verdict[role] = {"ok": role_ok, "executed": executed, "groups": groups,
                         "positions": positions, "expected": expected,
                         "no_plan": no_plan, "late": late, "backend": backend}
        ok = ok and role_ok
    state["readings"]["verdict"] = verdict
    state["readings"]["verdict_ok"] = ok
    return ("all roles match groups x positions with zero failures", ok)


def step_report(state, args):
    OUT.mkdir(parents=True, exist_ok=True)
    target = pathlib.Path(args.html) if args.html else OUT / "sequence_report.html"
    if not target.is_absolute():
        target = ROOT / target
    readings = state["readings"]

    def esc(value):
        return html.escape(str(value))

    def table(rows, header):
        head = "".join(f"<th>{esc(h)}</th>" for h in header)
        body = "".join("<tr>" + "".join(f"<td>{esc(c)}</td>" for c in row) + "</tr>" for row in rows)
        return f"<table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table>"

    boards = readings.get("boards") or {}
    board_rows = [(role, v.get("uid", "?"), v.get("online"), v.get("build", "?"))
                  for role, v in boards.items()]
    counters = readings.get("counters") or {}
    counter_rows = [(role, r.get("executed"), r.get("groups"), r.get("no_plan"), r.get("late"),
                     r.get("backend"), r.get("freeze_missing"), ",".join(r.get("window") or []))
                    for role, r in counters.items()]
    verdict = readings.get("verdict") or {}
    verdict_rows = [(role, v.get("expected"), v.get("executed"), v.get("no_plan"), v.get("late"),
                     v.get("backend"), "PASS" if v.get("ok") else "FAIL") for role, v in verdict.items()]
    configure = readings.get("configure") or {}
    config_rows = [(role, v.get("timing"), v.get("counts"), v.get("period"), v.get("repeat"), v.get("ready"))
                   for role, v in configure.items()]
    chain = readings.get("chain") or {}

    overall = "PASS" if readings.get("verdict_ok") else "PENDING"
    document = f"""<!doctype html>
<html lang="zh"><head><meta charset="utf-8">
<title>T2 序列步进报告</title>
<style>
body{{font-family:Segoe UI,Helvetica,Arial,sans-serif;margin:24px;color:#0f172a}}
h1{{font-size:20px}} h2{{font-size:16px;margin-top:24px}}
table{{border-collapse:collapse;margin:8px 0;font-size:13px}}
th,td{{border:1px solid #cbd5e1;padding:4px 8px;text-align:left}}
th{{background:#eef2f7}} .ok{{color:#15803d;font-weight:600}} .bad{{color:#b91c1c;font-weight:600}}
code{{background:#f1f5f9;padding:1px 4px;border-radius:3px}}
</style></head><body>
<h1>T2 多动作序列 · 步进报告</h1>
<p>整体判定：<span class="{'ok' if overall == 'PASS' else 'bad'}">{esc(overall)}</span>
 · 链耗时 {esc(chain.get('seconds', '?'))} s · rc {esc(chain.get('returncode', '?'))}</p>

<h2>1. 板卡</h2>{table(board_rows, ["角色", "UID", "在线", "固件 build"])}

<h2>2. 序列配置</h2>{table(config_rows, ["角色", "TIMing", "COUNts", "PERiod", "REPeat", "REAdy"])}

<h2>3. 执行读数</h2>{table(counter_rows, ["角色", "executed", "groups", "NO_PLAN", "LATE", "BACKEND",
                                          "freeze_missing", "window(5)"])}

<h2>4. 判据（executed = groups × positions，且三类失败为 0）</h2>
{table(verdict_rows, ["角色", "expected", "executed", "NO_PLAN", "LATE", "BACKEND", "判定"])}

<h2>5. 链尾输出</h2><pre>{esc(chr(10).join(chain.get('tail') or []))}</pre>
<p style="color:#64748b;font-size:12px">生成时间 {time.strftime('%Y-%m-%d %H:%M:%S')} ·
 状态文件 <code>{esc(STATE.relative_to(ROOT))}</code></p>
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
