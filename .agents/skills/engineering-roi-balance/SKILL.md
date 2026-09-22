---
name: engineering-roi-balance
description: Evaluate the expected time and decision value of the next action or process, then continue, skip, defer, or reclaim work when marginal return declines.
---

# Engineering ROI And Marginal Reclamation

Use this skill only to decide whether the next action or process is worth its time and how to reclaim work when marginal return declines. It is not a root-cause-analysis method and does not replace project rules.

## 0. Authority

Follow applicable instructions, safety requirements, and the repository's authority order. This skill may optimize optional investment; it may not waive a mandatory gate. Hooks enforce obligations: a hook whose path filter does not fire does not cancel an obligation. A mandatory obligation is not an ROI removal candidate unless its owning authority changes.

In HDRT100, consult `AGENTS.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md` (especially EXE-CHANGE-01, EXE-ACCEPT-01, EXE-SAFE-01/02), domain canonical documents, and `docs/check/DOCS_REGISTRY.md`. Applicable obligations include staged-source P3 evidence, documentation checks, C11 cross-review, and C14/C15. These examples are not exhaustive. Preserve the acceptance scope fixed before execution; ROI cannot narrow it or turn incomplete verification into a pass.

## 1. Core Model

The unit of evaluation is the next decision. Candidates must include execution, more evidence, skipping, deferral, and reclaiming prior complexity.

Use two primary measures:

- **Expected time**: hands-on attention, elapsed waiting, and exclusive resource time, expressed as best / likely / worst.
- **Expected value**: the decision changed, loss avoided, or downstream work unblocked. If every plausible result leads to the same next action, value is zero.

```text
decision efficiency = expected value / likely total time
marginal efficiency = next increment's added value / next increment's added time
```

Use one 0-5 value scale:

- `0`: cannot change the decision;
- `1`: repeated confirmation or cosmetic consistency;
- `2`: removes narrow friction;
- `3`: changes the next action or closes the current gap;
- `4`: protects official evidence or unblocks several tasks;
- `5`: prevents a high-cost wrong action or resolves a critical blocker.

Keep time in minutes or hours. Normally do not schedule standalone work below value `3`; otherwise many tiny low-value tasks can dominate the queue merely because they are short.

## 2. Execute Or Skip A Process

Assign exactly one state:

| State | Action |
|---|---|
| `MANDATORY` | Required by an authoritative rule; takes precedence over every other state. Execute; optimize only timing and prerequisite reuse permitted by that rule. |
| `EXECUTE` | Expected value justifies the time. Stop at the earliest decision checkpoint. |
| `SKIP_SUBSUMED` | Stronger current-run evidence already proves the same claim. |
| `SKIP_NOT_INVALIDATED` | Relevant source fingerprint, build, device identity, configuration, and evidence remain valid, and the project permits reuse. |
| `DEFER_OR_RECOVER` | Marginal efficiency is too low; defer, revert, archive, or merge. |

Evidence reuse requires both project permission and unchanged relevant prerequisites. Otherwise `SKIP_NOT_INVALIDATED` is invalid.

`EXECUTE` may use `scope=FULL` or `scope=REDUCED`. REDUCED must record the original scope, retained scope, unverified scope, and the condition that restores FULL. It applies to optional work, never to reducing an applicable mandatory acceptance scope.

For an expensive process, identify the earliest point sufficient to decide within the agreed scope. Do not use a final release gate as the inner debugging loop.

## 3. Diminishing Return

For the same unchanged decision, assume diminishing marginal return: early work removes major uncertainty; later work usually costs more to cover rarer cases.

Re-evaluate when:

- the previous increment did not change the decision;
- stronger evidence already covers the claim;
- failures repeatedly occur outside the target question;
- added tests, gates, schemas, or coordination cost exceed remaining value;
- another candidate has clearly higher value per unit time.

Keeping a capability and executing it every time are separate decisions. A low-frequency diagnostic capability may be retained without paying its cost on every healthy run. If new evidence changes the problem scope, create a new decision instead of extending the old one indefinitely.

A new decision must state its own expected time and stop condition, with a bounded number of attempts for retries. Renaming a decision does not reset consumed retry budgets or erase previous failures.

## 4. Reclamation

When marginal efficiency reaches zero or becomes negative, choose an applicable reclamation action. Perform only actions already authorized and consistent with ownership boundaries; preserve user-owned changes. If the proposed action lacks authorization, stop further investment and report the proposal instead of changing state.

- reuse still-valid evidence;
- reduce a full workflow to the smallest decision-changing experiment;
- revert an unproven change;
- merge overlapping checks and retire the weaker one;
- archive failed evidence or temporary implementation;
- release resources and restore the state required by the owning workflow, with its required readback evidence;
- redirect effort to a higher-efficiency decision.

Move files to a pending-deletion area only when the repository or user has explicitly defined one. Otherwise preserve them in place and report them. Reclamation is part of completion.

## 5. Quality Weighting

- For core product code, include lifetime defect cost, readability, and maintainability in expected value; excellent implementation can justify more time.
- For tools and workflow glue, target a simple safe result that captures most practical value. The user's roughly 85% target is an effort-allocation heuristic, not a measured product fact, a correctness threshold, or a gate. Once the result is safe, discoverable, recoverable, reproducible, and adequate for the current decision, stop refining unless a real failure or new consumer raises its value.

## 6. Output Contract

For a high-cost next step, report the candidate, expected time range, value score, decision changed, earliest stop point, and reclamation action.

Any skip or downgrade must report:

- state (`SKIP_SUBSUMED`, `SKIP_NOT_INVALIDATED`, `DEFER_OR_RECOVER`, or `EXECUTE scope=REDUCED`);
- the affected check or process and evidence supporting the decision;
- what remains unverified;
- the condition that triggers re-execution.

A material skip or downgrade must also report likely time, value score, and the decision it changes. Material means it changes a decision, skips or reduces an established verification scope, or occupies an exclusive resource. Trivial actions need no separate scorecard.

After execution, report actual time, actual decision change, reusable evidence, and completed reclamation. If evidence disproves the expected benefit, say so and apply the authorized reclamation described above; preserve evidence and follow the repository's archive and deletion rules.
