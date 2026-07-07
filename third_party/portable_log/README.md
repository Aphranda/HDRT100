# portable_log

`portable_log` is a small platform-neutral logging core for RP2350_TRIG and
future MCU targets. It is intentionally kept independent from Pico SDK, RTOS,
SCPI, FatFs, and storage code.

The caller owns all platform details:

- `line_buffer`: fixed caller-provided formatting buffer.
- `time_ms`: optional timestamp callback.
- `emit`: required output callback.
- `lock` / `unlock`: optional paired callbacks for caller-provided
  serialization.
- `user`: caller context passed to callbacks.

Current P0 scope:

- Four levels: `DEBUG`, `INFO`, `WARN`, `ERROR`.
- Runtime global minimum level.
- Emitted, filtered, truncated, and emit-failed counters per level.
- Optional caller-provided lock/unlock callbacks; the core owns no RTOS object.
- Single-line text formatting suitable for USB CDC debug output.
- Table-driven level metadata, so level names and future level attributes stay
  in one platform-neutral rule table.

Design references:

- `rxi/log.c`: tiny C99 logging core, function-like logging API, level
  filtering, callbacks, and optional locking.
- Zephyr logging: frontend/backend separation, runtime filtering, timestamp
  callback, rate limiting, and deferred logging as later expansion points.
- Memfault embedded logging guidance: fixed log storage, concise parseable
  lines, and collection around fault events.

Project-specific adapters must stay outside this directory. The current RP2350
adapter is `middleware/portable_log_port`; diagnostics code should include that
middleware adapter instead of including `portable_log.h` directly.

The core stays synchronous by design. Queueing, backend fan-out, rate limiting,
domain filtering, and persistent storage belong in middleware adapters or higher
diagnostics layers. The current RP2350 adapter uses a fixed ring buffer and a
service flush function so the core `emit` callback only enqueues a completed log
line.

Run the focused gate from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_portable_log_tests.ps1
```
