# OTA RTOS Porting Plan

This plan limits the reusable OTA scope to RP2350 and STM32 RTOS products. The
current RP2350 SDK firmware is the golden reference for behavior and validation.
The step-by-step library hardening and product migration sequence is tracked in
`docs/OTA_LIBRARY_MIGRATION_PLAYBOOK.md`; this document focuses on the RTOS
adapter shape and validation gates.

## Target Architecture

```text
transport task
  parses product protocol and receives OTA bytes

OTA owner task
  owns pota_context_t
  runs begin/write/end/boot/commit state transitions
  reports status to product protocol

flash service
  serializes erase/program/read
  handles XIP or bank execution constraints
  feeds watchdog during long operations

Bootloader
  validates metadata and image
  applies copy-to-active or direct A/B update
  records result and rolls back if unconfirmed
```

Only the OTA owner task may call the OTA core for one device context. Other
tasks must send messages to the OTA owner.

## RP2350 RTOS Migration

Keep the current flash map, package format, metadata format, and validation
commands stable while moving the App runtime to an RTOS.

Recommended order:

1. Keep Bootloader unchanged.
2. Move current SCPI OTA command handling behind an OTA owner task.
3. Add an RTOS queue or stream buffer for OTA data blocks.
4. Wrap RP2350 flash erase/program in a scheduler-aware flash service.
5. Feed watchdog and service USB/transport around long erase/program/copy
   operations.
6. Re-run the existing positive OTA and negative package validation matrix.
7. Add a stress test with concurrent non-OTA tasks active during update.

RP2350-specific risks:

- XIP flash programming can stall code/data fetches if critical sections are
  wrong.
- USB CDC or transport tasks can starve during long flash operations.
- Watchdog timing can change after scheduler tick and task priorities are
  introduced.
- Metadata writes must remain single-owner and power-loss tolerant.

## STM32 RTOS Port

Use the same OTA core and package format, but implement a separate STM32 RTOS
platform adapter.

Recommended order:

1. Define flash map: Bootloader, Slot A, Slot B or staging, metadata copies,
   product config, scratch if required.
2. Choose boot mode: `COPY_TO_ACTIVE` for fixed-link or single-bank constraints,
   `DIRECT_AB` for true dual-slot boot.
3. Implement vector validation: initial stack pointer in RAM, reset handler in
   selected image range, optional image header checks.
4. Implement metadata copies in protected internal flash or a reserved external
   flash region.
5. Implement flash service using HAL/LL erase and program APIs.
6. Add cache and interrupt coordination for Cortex-M7/H7 or same-bank updates.
7. Reuse the RP2350 validation matrix before adding product-specific tests.

STM32-specific risks:

- Same-bank erase/program while executing from flash can break RTOS timing.
- Flash program alignment differs across STM32 families.
- I-cache/D-cache maintenance is mandatory on some Cortex-M7/H7 layouts.
- Option bytes and bank swap should stay in the platform adapter, not the OTA
  core.

## Required RTOS Adapter Contract

The platform adapter should provide:

```text
flash_read(offset, buffer, size)
flash_erase(offset, size)
flash_program(offset, data, size)
mark_pending(slot, image_size, image_crc32)
confirm_active()
validate_vector(slot_offset, image_size, run_offset)
ota_lock()
ota_unlock()
ota_yield_or_delay()
feed_watchdog()
invalidate_cache()
reboot()
time_ms()
log(level, message)
```

`ota_lock()` protects adapter-global resources such as metadata and flash
services. It does not make it safe for multiple tasks to mutate the same OTA
context concurrently; that remains the OTA owner task's responsibility.

## Validation Gate

Before a RP2350 RTOS or STM32 RTOS release is considered equivalent to the
current RP2350 SDK reference, it must pass:

- Factory boot.
- Positive unified package OTA.
- Bootloader apply result equals `APPLIED`.
- App commit result equals `COMMITTED`.
- Release isolation: no destructive validation commands in release firmware.
- Transport CRC failure.
- Image CRC failure.
- App vector failure.
- Package magic/version/size failure.
- Slot mismatch failure.
- Run-offset mismatch failure.
- Reset during receive keeps old confirmed image.
- Reset during Bootloader apply recovers or rolls back.
- No-commit rollback.
- Concurrent RTOS workload during OTA does not starve watchdog, transport, or
  safety tasks.
