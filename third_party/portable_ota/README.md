# Portable OTA

Portable OTA is a platform-neutral C library skeleton extracted from the
RP2350_TRIG OTA validation flow. It is intended for RP2350 and STM32 MCU
products, including future RTOS-based firmware, and connected through a small
port layer.

The library deliberately does not include transport code. A product may feed it
from SCPI, UART, CAN, Ethernet, USB, SD card, an RTOS transport task, or a
factory test fixture.

All repository text files should be read as UTF-8. This matters for the Chinese
project documents under `docs/` and for validation reports copied between PCs.

## Library Boundary

Portable:

- package manifest parsing.
- CRC32.
- App vector-table validation helper.
- common OTA states, errors, results, and slot identifiers.
- common state/error/result text helpers.
- App-side begin/service/write/end/abort/commit API shape.
- one-shot session facade for simple product integration.
- redundant metadata v3 structure, CRC helpers, default initializer,
  transaction helpers, and newest-copy selector.

Platform-specific:

- flash erase/program/read.
- metadata copy storage layout and write policy.
- cache and interrupt coordination.
- RTOS locking, task ownership, and queueing policy.
- reboot and boot partition selection.
- watchdog feeding.
- transport protocol.
- product logging and diagnostics.

## Integration Flow

```text
transport receives file
  -> pota_session_init()
  -> pota_session_begin()
  -> pota_session_service() until erase is complete when state is CHECK_PERMISSION/ERASE_SLOT
  -> pota_session_write() for each block
  -> pota_session_service() again after a package header schedules target erase
  -> pota_session_end()
  -> platform reboot
  -> Bootloader applies pending image
  -> App self-test
  -> pota_session_commit()
```

Products that want lower-level ownership may call `pota_begin()`,
`pota_service()`, `pota_write()`, `pota_end()`, and `pota_abort()` directly.
For new RP2350 RTOS and STM32 RTOS ports, start from `include/pota.h` and the
session facade unless there is a concrete reason to own the core context
manually.

## Migration Rule

The current RP2350 SDK firmware is the behavioral reference. Improve this
library in small steps, then migrate the product firmware only after each step
has a closed-loop validation record. The detailed sequence lives in
`../../docs/OTA_LIBRARY_MIGRATION_PLAYBOOK.md`.

## Files

```text
include/pota_types.h
include/pota_compat.h
include/pota_platform.h
include/pota_package.h
include/pota_metadata.h
include/pota_image.h
include/pota_operation.h
include/pota_core.h
include/pota_session.h
include/pota_strings.h
include/pota.h
src/pota_crc32.c
src/pota_compat.c
src/pota_package.c
src/pota_image.c
src/pota_metadata.c
src/pota_core.c
src/pota_operation.c
src/pota_session.c
src/pota_strings.c
```

The current RP2350_TRIG product integration keeps all direct `pota_*` use in
`middleware/portable_ota_port`. Product OTA modules call the middleware adapter
so the same core library can later be reused by RP2350 RTOS and STM32 RTOS
ports without changing the package format or validation chain.
