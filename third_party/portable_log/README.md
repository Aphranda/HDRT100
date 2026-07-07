# portable_log

`portable_log` is a small platform-neutral logging core for RP2350_TRIG and
future MCU targets. It is intentionally kept independent from Pico SDK, RTOS,
SCPI, FatFs, and storage code.

The caller owns all platform details:

- `line_buffer`: fixed caller-provided formatting buffer.
- `time_ms`: optional timestamp callback.
- `emit`: required output callback.
- `user`: caller context passed to callbacks.

Current P0 scope:

- Four levels: `DEBUG`, `INFO`, `WARN`, `ERROR`.
- Runtime global minimum level.
- Emitted and dropped counters per level.
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

Run the focused gate from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_portable_log_tests.ps1
```
