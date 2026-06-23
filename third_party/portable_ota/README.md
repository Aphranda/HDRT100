# Portable OTA

Portable OTA is a platform-neutral C library skeleton extracted from the
RP2350_TRIG OTA validation flow. It is intended for RP2350 and STM32 MCU
products, including future RTOS-based firmware, and connected through a small
port layer.

The library deliberately does not include transport code. A product may feed it
from SCPI, UART, CAN, Ethernet, USB, SD card, an RTOS transport task, or a
factory test fixture.

## Library Boundary

Portable:

- package manifest parsing.
- CRC32.
- App vector-table validation helper.
- common OTA states, errors, results, and slot identifiers.
- App-side begin/service/write/end/abort/commit API shape.
- redundant metadata structure and helper declarations.

Platform-specific:

- flash erase/program/read.
- cache and interrupt coordination.
- RTOS locking, task ownership, and queueing policy.
- reboot and boot partition selection.
- watchdog feeding.
- transport protocol.
- product logging and diagnostics.

## Integration Flow

```text
transport receives file
  -> pota_begin()
  -> pota_service() until erase is complete when state is CHECK_PERMISSION/ERASE_SLOT
  -> pota_write() for each block
  -> pota_service() again after a package header schedules target erase
  -> pota_end()
  -> platform reboot
  -> Bootloader applies pending image
  -> App self-test
  -> pota_commit()
```

## Migration Rule

The current RP2350 SDK firmware is the behavioral reference. Improve this
library in small steps, then migrate the product firmware only after each step
has a closed-loop validation record. The detailed sequence lives in
`../../docs/OTA_LIBRARY_MIGRATION_PLAYBOOK.md`.

## Files

```text
include/pota_types.h
include/pota_platform.h
include/pota_package.h
include/pota_metadata.h
include/pota_image.h
include/pota_core.h
src/pota_crc32.c
src/pota_package.c
src/pota_image.c
src/pota_metadata.c
src/pota_core.c
```

The current RP2350_TRIG product integration keeps all direct `pota_*` use in
`middleware/portable_ota_port`. Product OTA modules call the middleware adapter
so the same core library can later be reused by RP2350 RTOS and STM32 RTOS
ports without changing the package format or validation chain.
