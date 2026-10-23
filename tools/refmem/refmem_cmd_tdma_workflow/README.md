# REFMEM_CMD TDMA second-class workflow
Stepwise four-board HIL workflow for `REFTMEM_CMD` carried by the existing
`tdma_maintenance_stream` fragmentation path.  It resets the boards, queues one
command before the ring starts, starts the ring without T2/VDC, reads the
read-only counters from every board, and writes JSON + Markdown reports.

## Run

```powershell
python tools/refmem/refmem_cmd_tdma_workflow/refmem_cmd_tdma_workflow.py \
  --target-mask 0x0F \
  --out-dir out/refmem_cmd_tdma/alltarget
```

The reference/source is COM5; targets are the slots selected by
`--target-mask` (0x0F = all four boards).

Payload types:

```text
0x52  REFMEM_CMD    56 B, requires target rx/decoded/posted/acked
0x53  T2_STATIC     16 B, requires target t2_static_ok
0x54  T2_DYNAMIC    24 B, requires target t2_dynamic_ok
```

Examples:

```powershell
# REFMEM_CMD
python tools/refmem/refmem_cmd_tdma_workflow/refmem_cmd_tdma_workflow.py \
  --payload-type 0x52 --target-mask 0x0F --out-dir out/refmem_cmd_tdma/refmem

# T2 static envelope parser
python tools/refmem/refmem_cmd_tdma_workflow/refmem_cmd_tdma_workflow.py \
  --payload-type 0x53 --target-mask 0x0F --out-dir out/refmem_cmd_tdma/t2_static

# T2 dynamic envelope parser
python tools/refmem/refmem_cmd_tdma_workflow/refmem_cmd_tdma_workflow.py \
  --payload-type 0x54 --target-mask 0x0F --out-dir out/refmem_cmd_tdma/t2_dynamic
```

## Steps

1. `reset_boards` — `picotool reboot -f --ser <board_id>` for all four boards.
2. `discover_boards` — verify `*IDN?`, read `SYSTem:FW:BUILD?`, require one build.
3. `set_second_class_seat` — `SYSTem:TDMA:SEAT:ROTation 1,2,7`.
4. `queue_refmem_cmd` — submit one 56 B frame **before** the ring starts so SCPI
   is not needed while the ring owns core0; verify `submitted >= 1`.
5. `start_ring_without_t2` — run `trn03_closed_loop.py` with
   `--no-record --no-t2 --clock-evidence disabled`; no recorder, no T2 seed.
6. `read_statuses` — `SYSTem:REFMEM:CMD:STATus?` from all four boards.
7. `verify_all_target_ack` — source `tx_ok`, `inject_attempts`; each target
   `rx_events`, `decoded`, `posted`, `acked` must all be at least 1.

## Report

```text
<out>/report.json      machine-readable schema HAOFV_REFTMEM_CMD_TDMA_WORKFLOW_V1
<out>/REPORT.md        human-readable step/status table
<out>/ring.log         trn03 output
```

The report is written on failure too, with `error` per failed step.

## Exit codes

- `0` — all steps and verification passed.
- `1` — failed step or verification; inspect `REPORT.md`.

## Scope

This workflow proves the REFMEM control plane before T2/VDC.  It does not seed
T2 flight images, does not touch the T2 FIFO, and does not add a bus or a second
command slot.  Dynamic T2 configuration must be a separate later workflow after
DPLL/VDC.

## Parser suite

Run all three registered second-class parsers and aggregate the result:

```powershell
python tools/refmem/refmem_cmd_tdma_workflow/run_refmem_parser_suite.py \
  --target-mask 0x0F --attempts 3 \
  --out-dir out/refmem_cmd_tdma/parser_suite
```

Outputs:

```text
<out>/suite_report.json
<out>/SUITE_REPORT.md
<out>/<parser>/attemptN/report.json
```

The suite retries a parser run when a single target misses a frame.  A passing
suite means each parser accepted the typed payload on all selected targets.

## Invalid-payload suite

```powershell
python tools/refmem/refmem_cmd_tdma_workflow/run_refmem_parser_reject_suite.py \
  --target-mask 0x0F --attempts 3 \
  --out-dir out/refmem_cmd_tdma/reject_suite
```

Cases:

```text
0x52  REFMEM_CMD    bad CRC       -> refmem_parser_reject
0x53  T2_STATIC     bad magic     -> t2_static_reject
0x54  T2_DYNAMIC    bad reserved  -> t2_dynamic_reject
```

## T2_STATIC execution

The T2_STATIC parser validates the envelope and then calls the registered
`tdma_t2_static_apply_fn`.  The default executor records the applied descriptor:

```text
t2_static_applied
t2_static_last_config
t2_static_last_value
```

The HIL workflow verifies `t2_static_ok` and `t2_static_applied` on every
target.  A real T2 state machine can replace the executor with
`tdma_t2_static_set_apply()` without changing the parser or transport.

## T2_DYNAMIC execution

The T2_DYNAMIC parser uses the same envelope-first contract: it validates
magic/version/target/run and the zero reserved word, then calls the registered
`tdma_t2_dynamic_apply_fn`.  The default executor records the validated
descriptor so the accepted payload is observable before a real T2 runtime
consumes it:

```text
t2_dynamic_applied
t2_dynamic_last_target
t2_dynamic_last_run
t2_dynamic_last_config
t2_dynamic_last_value
```

The HIL workflow verifies `t2_dynamic_ok` and `t2_dynamic_applied`, and checks
that the recorded target/run/config/value equal the sent default envelope.  A
real T2 state machine can replace the executor with
`tdma_t2_dynamic_set_apply()` without changing the parser or transport.


