# Tests

- `python/`: pytest unit tests for host-side Python tools and generated package helpers.
- `hil/`: pytest hardware-in-the-loop tests. These are skipped by default and
  only open the board serial port after hardware is explicitly selected.
- `unit/`: host-side unit tests for pure C modules.
- `integration/`: firmware integration tests.
- `hil/`: hardware-in-the-loop and production regression tests.

Run Python unit tests:

```powershell
python -m pytest
```

Run pure C unit-test gates:

```powershell
powershell -ExecutionPolicy Bypass -File tools\tests\run_refmem_slot_claim_tests.ps1
powershell -ExecutionPolicy Bypass -File tools\tests\run_biss_protocol_tests.ps1
```

Run hardware-in-the-loop pytest after confirming the board and serial port:

```powershell
python -m pytest -m hil --run-hil --hil-port COM4
```

The default pytest run never opens a serial port. HIL tests must use the shared
`hil_serial` fixture so the serial lifecycle is centralized: open, settle,
clear input/output buffers, run the test, flush, and close. Keep long production
flows in `tools/*_validate/` until their hardware assumptions are explicit
enough to wrap as `tests/hil/` cases.
