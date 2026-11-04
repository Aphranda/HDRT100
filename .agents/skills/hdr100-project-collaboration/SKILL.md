---
name: hdr100-project-collaboration
description: Use for any work in the Distributed Hard Real-Time Trigger System (HDRT100 / RP2350_TRIG) repository — planning, doc changes, firmware/tool/test changes, or reviews. Entry points, lanes, and HAOFV owner boundaries; the authoritative rules live in AGENTS.md and docs/check/, this skill never restates them.
---

# HDRT100 Project Collaboration

Repository: device DHRT100 (historical build name RP2350_TRIG), hardware RP2350/RP2350B, architecture
HAOFV. `AGENTS.md` is the authoritative operating manual; **this skill is a pointer sheet, not a copy** —
if it ever disagrees with `AGENTS.md`, `docs/check/DOCS_REGISTRY.md` or `DOCS_EXECUTION_CONSTRAINTS.md`,
those win and this file gets fixed.

## 1. Read first

| File | Why |
|---|---|
| `AGENTS.md` | Entry point: system in 30 s, §2 repo hard rules, §5 commands, §8 milestone trigger. |
| `docs/README.md` | Docs index and domain routing (which domain owns which topic). |
| `docs/check/DOCS_REGISTRY.md` | Contract registry — the only source of truth for registered contracts. |
| `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` | Execution flow: acceptance, debug gates, roles, §7 milestone report body. |

## 2. Lanes and separation

Docs/contract changes commit separately from code (auditable history); split work into packages with
**disjoint file scopes** so parallel workers cannot collide.

- **DOCS lane** — `docs/**`, `*.md`. Gates: `.agents/skills/doc-self-regression/SKILL.md` (it owns the
  doc rules and the exact commands).
- **FIRMWARE lane** — `application/`, `boards/`, `components/`, `drivers/`, `middleware/`, `config/`,
  `bootloader/`, PIO, build files. Owner boundaries apply; the P3 hardware credential is required.
- **TOOL/TEST lane** — `tests/`, `tools/`, `.githooks/`, `.agents/**/*.py`. Host credential only; no bench.

Credential routing is **not** restated here: run `python tools/acceptance/credential_scope.py --explain`.

## 3. HAOFV non-negotiables (worker and reviewer both check these)

- Flash erase/program is core0-only; core1 parks and lockout-acks before flash writes (XIP dual-core safety).
- core1 realtime plane never touches FatFs / USB / SCPI / OTA / LCD / long logging / dynamic memory.
- Shared cross-core fields need one writer + seqlock / double buffer / `__atomic` / DMB.
- Function Block actions return immediately (`FB_RESULT_BUSY` + `next_state=self`); no blocking waits.
- Registry/clause status changes need C11 cross-review — never self-approved, records land in
  `docs/check/submissions/`.

## 4. Operating shape

- Run independent packages as parallel workstreams with disjoint file scopes; wait for all of them, then
  hand the combined diff + evidence to **one independent read-only cross-reviewer** (never the author, C11).
- Analysis and execution stay separated: the reviewer only reads; the main session writes and operates hardware.
- Cross-domain index files (`docs/README.md`, `docs/check/DOCS_REGISTRY.md`,
  `docs/check/submissions/README.md`) are written by the main controller only — two writers cause
  "file changed since it was read".
- If a task has no safe parallel split, run it single-threaded; never force parallelism on dependent work.

## 5. Quick reference

- Architecture boundary: `docs/arch/HAOFV_ARCHITECTURE.md`.
- Owner domain for a topic: `docs/README.md` domain table.
- Evaluation/oversight: `docs/evaluation/`. Governance: `docs/check/`.
- Credential table: `python tools/acceptance/credential_scope.py --explain`.
- Commands: `AGENTS.md` §5 (single source). Doc rules: the `doc-self-regression` skill.
- Branch/remote: read `git branch --show-current` and `git remote -v`; confirm with the user before push.
