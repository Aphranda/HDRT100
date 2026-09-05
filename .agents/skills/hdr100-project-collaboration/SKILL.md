---
name: hdr100-project-collaboration
description: Use for any work in the Distributed Hard Real-Time Trigger System (HDRT100 / RP2350_TRIG) repository — planning, doc changes, firmware/tool/test changes, or reviews. Loads the project operating rules: AGENTS.md doc self-regression system, docs/code separation, HAOFV owner boundaries, verification gates, and P3 hardware-acceptance obligation. Invoke when the task touches this repo or its docs corpus.
---

# HDRT100 Project Collaboration

This skill encodes how to work correctly in the Distributed Hard Real-Time Trigger System repository (device DHRT100, historical build name RP2350_TRIG, hardware RP2350/RP2350B, architecture HAOFV). It applies to the main working agent and to the reviewer that audits it. AGENTS.md at the repo root is the authoritative operating manual; this skill is the fast-loading summary and must never contradict it.

## 1. Read first (onboarding)

| File | Purpose |
|---|---|
| `AGENTS.md` | Constant agent orders: doc self-regression system, hard constraints, workflows, current status. |
| `docs/README.md` | Docs index and domain routing (which domain owns which topic). |
| `.agents/skills/doc-self-regression/SKILL.md` | Full doc-governance rules + gate commands for any `.md` change. |
| `docs/check/DOCS_REGISTRY.md` | Contract registry (only source of truth for registered contracts). |

## 2. The doc self-regression system in 30 seconds

Three loops + gates + cross-review:

```
Loop 1 freshness : top-level doc (docs/arch/HAOFV_ARCHITECTURE.md) must reflect newly frozen
                   domain contracts within 7 days, else the checker FAILs
Loop 2 registry   : a domain doc freezing a cross-domain contract must add a row to
                   docs/check/DOCS_REGISTRY.md (unique contract_id, real clause_loc + code_anchor)
Gate             : git pre-commit runs both checkers; any FAIL blocks the commit
Cross-review     : registry status changes are never self-approved (C11)
```

### Hard constraints (violation = gate FAIL)

1. 5 metadata fields (Status/Domain/Canonical/Related/Last updated) within 7 lines after the title; keep the 5 Required fields first.
2. Hard numbers are not hand-written in docs: reference the code symbol, register, or mark as "snapshot, not source of truth".
3. Any new `.md` must be named `<PREFIX>_<TOPIC>_<SUFFIX>.md` (whitelist) AND indexed in `docs/README.md`.
4. Registered contracts can only be `superseded`, never physically deleted.
5. Editing any document requires updating its `Last updated` (legal `YYYY-MM-DD`).
6. Temporary content (review snapshots / drafts) is never registered as a contract.

### Standard doc workflow

1. Freeze contract -> add registry row (unique id, anchors real — confirm the code file exists before registering).
2. Edit doc -> update `Last updated`; reference symbols instead of copying numbers.
3. Verify -> run all gates in section 4; all must be green.
4. Commit -> pre-commit runs automatically (FAIL first, fix first).

Common traps (from implementation experience): extra metadata fields push Required fields out of the window; a guessed anchor file name often does not exist (tdma_ring_profile.h is actually `tdma_profile.h`); changing the checker signature requires updating its pytest; after editing `tools/doc_regression_check.py` run `python tools/doc_regression_check.py --skill-sync`; dates must be real YYYY-MM-DD.

## 3. Workflow lanes and separation

HDRT100 commits docs/contract changes separately from code changes (auditable history). Work is split into independent task packages so parallel workers do not collide:

- DOCS lane: `docs/**`, `.md` files — obey the self-regression rules above.
- FIRMWARE lane: `application/`, `boards/`, `components/`, `drivers/`, `middleware/`, `config/`, `bootloader/`, PIO programs, build/hook files — HAOFV owner boundaries apply and the P3 hardware-acceptance gate applies before commit.
- TOOL/TEST lane: `tools/`, `tests/` — verify with host C unit tests / pytest; note whether HIL/hardware is required.

HAOFV non-negotiables the worker and reviewer must check:

- Flash erase/program is core0-only; core1 parks/lockout-acks before flash writes (XIP dual-core safety).
- core1 realtime plane never touches FatFs / USB / SCPI / OTA / LCD / long logging / dynamic memory.
- Shared cross-core fields need a unique writer + seqlock / double buffer / `__atomic` / DMB.
- Function Block actions return immediately (`FB_RESULT_BUSY` + `next_state=self`); no blocking waits inside an action.
- Registry/clause status changes require C11 cross-review — the reviewer agent or an independent second party.

## 3.1 Recommended operating shape: two parallel workers + one reviewer

The simplest reliable shape for this project is two agents doing work in parallel plus one reviewer over their combined output:

```
You (main session) -> assign task A to worker (instance 1) and task B to worker (instance 2)
  - both workers run in parallel; keep them in disjoint file scopes so they cannot collide
  - wait for both to finish
  - then spawn the reviewer over the combined diff and evidence from both workers
```

- Two `worker` instances (`.agents/agents/worker.toml`, nickname_candidates worker-alpha / worker-beta) handle independent task packages; give each a disjoint file scope.
- One `reviewer` instance (`.agents/agents/reviewer.toml`) audits the combined result, records findings, and ends with `accept` / `revise` / `escalate`.
- If a task has no safe parallel split, a single worker suffices; do not force parallelism on dependent work.
- Example prompt: "Run worker-alpha on the docs change and worker-beta on the firmware fix in parallel (disjoint scopes), wait for both, then spawn the reviewer on the combined diff."

## 4. Verification commands (run after any change)

```powershell
python tools/docs_check/docs_check.py --strict-names
python tools/doc_regression_check.py
python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -p no:cacheprovider --basetemp <writable-tmp>
sh .githooks/pre-commit
```

Notes:

- The pre-commit hook depends on `git config core.hooksPath .githooks` (reconfigure after clone/move); `sh .githooks/pre-commit` validates manually.
- pytest needs a writable `--basetemp`; a stale `out/pytest/runs` can fail cleanup on Windows.
- The `TDMA-FLIGHT-BITMAP-01` registry WARN is pre-existing (legacy double-segment id), not a regression.
- `python tools/doc_regression_check.py --log-check` is the weekly escape-hatch audit.
- After firmware/PIO/build/tool/hook changes, the P3 hardware acceptance gate must run before commit:
  `python tools/hardware_acceptance/p3_hardware_acceptance.py run` (receipt bound to the staged source fingerprint; manual reports or replays do not pass). `check-staged` verifies at commit time.
- Build presets: release = `pico2-release`; firmware changes should at least compile via `python tools/cmake_build_auto/cmake_build_auto.py --preset pico2-release --build-dir <local>` when the SDK is available.

## 5. Roles: two parallel workers + one reviewer

| Agent | Sandbox | Lane | Ends with |
|---|---|---|---|
| worker (`.agents/agents/worker.toml`, 2 instances: worker-alpha / worker-beta) | workspace-write | runs an independent doc/firmware/tool task package; scopes must be disjoint | changed files + verification evidence |
| reviewer (`.agents/agents/reviewer.toml`) | read-only | audits the combined output of the workers, records findings, reports back on completion | audit record + `accept` / `revise` / `escalate` |
| main session (default) | as granted | assigns task packages, waits for workers, spawns reviewer | orchestrated result |

How to use (Codex never auto-decomposes — you must ask):

```
Run worker-alpha on task A and worker-beta on task B in parallel (disjoint file scopes).
Wait for both to finish, then spawn the reviewer on the combined diff and evidence.
Reviewer stays read-only: audit record with file:line findings, end with accept / revise / escalate.
```

- Workers only write inside their assigned scope; reviewer never edits files and never self-approves (C11 cross-review).
- Parallelism is bounded by `[agents] max_threads` in `.agents/config.toml` (default 6) and nesting by `max_depth` (default 1).
- If a task cannot be split into independent packages, use a single worker; do not force parallelism on dependent work.

## 6. Quick reference

- System/architecture boundary: read `docs/arch/HAOFV_ARCHITECTURE.md` first.
- Find the owner domain for a topic: `docs/README.md` domain table.
- Evaluation/oversight domain: `docs/evaluation/` (value evaluation + tracking).
- Registry and governance: `docs/check/`.
- This repository's remote is GitHub (Aphranda/HDRT100); the branch under active work may be `refactor/tdma-phy-split-p3-gated` or `feature/rtos-multicore-haofv` — confirm with the user before committing/pushing.
