# Portable OTA Architecture And Validation Guide

Status: Active
Domain: OTA
Canonical: `docs/PORTABLE_OTA_ARCHITECTURE.md`
Related: `docs/OTA方案.md`, `docs/OTA_LIBRARY_MIGRATION_PLAYBOOK.md`, `docs/OTA_OPEN_SOURCE_COMPARISON.md`
Last updated: 2026-07-07

This document extracts the current RP2350_TRIG OTA design into a reusable
architecture for MCU-class RTOS products. The immediate product scope is
RP2350 and STM32. The current RP2350 SDK implementation remains the golden
reference; future RP2350 and STM32 products should integrate the same OTA core
through RTOS-aware platform adapters.

The goal is to keep the proven safety chain, package format, and validation
flow while isolating platform-specific flash, reset, cache, scheduler, and
boot-mode details behind a small port layer.

## Product Scope

In scope:

- Current RP2350 SDK firmware as the reference implementation.
- Future RP2350 RTOS firmware, for example FreeRTOS or another Cortex-M RTOS.
- STM32 RTOS firmware, including single-bank, dual-bank, and external-flash
  layouts.
- Transport-agnostic App update flow fed by SCPI, UART, USB, CAN, Ethernet, or
  file-like factory tools.

Out of scope for this library:

- Linux userspace OTA managers.
- Zynq/MPSoC boot-chain management.
- Filesystem/package-manager style updates.
- Bootloader environment management such as U-Boot variables.

## Why This Design Is Valuable

The current OTA flow has reached a useful product boundary:

- A fixed Bootloader owns final boot selection and recovery.
- The running App only receives, verifies, and marks an update pending.
- Updates are written to an inactive or staging slot, never directly over the
  only known-good image during transfer.
- Metadata uses redundant copies and monotonic sequence selection.
- New firmware must be confirmed after boot; otherwise rollback remains
  possible.
- A unified package can carry both Slot A and Slot B linked images.
- Negative-path validation covers corrupt transport CRC, image CRC, vector
  table, package header, slot, and run offset mismatches.

This combination is portable because it separates three concerns:

```text
Transport and App update manager
  Receives bytes, parses package, writes inactive/staging image, marks pending.

Bootloader safety chain
  Loads metadata, validates slot, switches/copies, records result, rolls back.

Platform port
  Flash erase/program/read, reset, cache/interrupt handling, scheduler locks,
  watchdog feeding, logging, time.
```

## Reference Flash Model

The RP2350 reference implementation uses a 4 MB external QSPI flash:

| Region | Purpose |
|---|---|
| Bootloader | Fixed recovery program. Normal OTA does not overwrite it. |
| Slot A | App image A or active fixed run image. |
| Slot B | App image B or staging image. |
| OTA Metadata | Redundant boot/update records. |
| Product Config | Product parameters and calibration. |
| Scratch/Reserved | Future logs, staging helpers, or scratch data. |

This maps cleanly to the target MCU RTOS platforms:

| Platform | Typical Mapping |
|---|---|
| RP2350 SDK reference | Bootloader plus Slot A/Slot B in external QSPI flash, redundant metadata in reserved flash. |
| RP2350 RTOS | Same flash layout as the SDK reference; OTA runs as an RTOS task and flash operations are serialized through a platform service. |
| STM32 RTOS internal dual-bank flash | Bootloader in protected sectors, bank 1/2 as A/B slots, metadata in reserved sectors. |
| STM32 RTOS single-bank flash + external QSPI | Internal Bootloader, external staging or A/B slots, metadata in internal or QSPI sectors. |

## Package Format

The portable package is a single file containing a fixed 512 byte manifest
followed by aligned payload images.

```text
0x000  magic                 u32  "TPKG" little endian
0x004  version               u32
0x008  header_size           u32  512
0x00C  package_size          u32
0x010  package_crc32         u32  optional/reserved; transport CRC is authoritative
0x014  image_count           u32
0x020  product_id            char[32]
0x040  hardware_id           char[32]
0x060  app_version_major     u32
0x064  app_version_minor     u32
0x068  app_version_patch     u32
0x06C  min_bootloader_ver    u32  packed major.minor.patch
0x070  build_id              char[32]
0x090  payload_sha256        u8[32]
0x0C0  image[0]              32 bytes
0x0E0  image[1]              32 bytes
...
```

Each image entry:

```text
slot       u32
offset     u32  offset from package start
size       u32
crc32      u32
run_offset u32  expected runtime flash/partition offset
flags      u32
reserved   8 bytes
```

The package is designed so the receiver can reject incompatible updates after
the first 512 byte block, before erasing any target slot:

- Wrong magic/version/header size.
- Wrong product id or hardware id.
- Required Bootloader version is newer than the device capability.
- Missing target slot image.
- Image run offset does not match the selected boot mode and target slot.
- Image does not fit the target slot.

## Boot Modes

Two boot modes are supported by the architecture.

### Copy To Active

```text
App runs from Slot A
OTA writes Slot A-linked image into Slot B staging
Bootloader validates Slot B
Bootloader copies Slot B to Slot A
Bootloader validates Slot A
Bootloader clears pending and boots Slot A
```

This mode works even when an App can only be linked to one fixed address.
It needs copy transaction recovery because Slot A is erased and rewritten.

### Direct A/B

```text
App runs from active slot
OTA writes the inactive slot
Bootloader validates pending slot
Bootloader switches active slot
App self-test confirms the new slot
Bootloader rolls back if confirmation never happens
```

This is the preferred long-term mode when the platform can boot from both
application partitions and each image is linked or position-independent for its
target address.

## Metadata Model

Portable metadata should be stored in two or more copies. Each copy contains:

- magic and schema version.
- monotonic sequence number.
- active slot.
- pending slot.
- confirmed slot.
- boot attempts.
- rollback count.
- per-slot image size and CRC32.
- last Bootloader result.
- copy transaction state if copy-to-active is supported.
- boot mode and capability bits.
- CRC over the protected fields.

Load rule:

```text
Read all copies.
Keep copies with valid magic/version/CRC.
Select the valid copy with the newest sequence.
If no valid copy exists, create safe defaults.
```

Write rule:

```text
Construct a full new metadata image in RAM.
Increment sequence.
Compute CRC.
Erase/program one inactive metadata copy.
Read back and verify.
Never erase the last valid copy first.
```

## State Machine

The App-side OTA manager should use an event-driven state machine. Under an
RTOS this state machine should have a single owner task. Transport tasks,
protocol parsers, and factory tools should feed that owner through a queue,
stream buffer, command mailbox, or product adapter instead of calling the OTA
context concurrently.

```text
IDLE
  BEGIN
CHECK_PERMISSION
  TICK/resource granted
ERASE_SLOT
  TICK until erased
RECEIVING
  DATA blocks
VERIFYING
  END triggers CRC/vector checks
MARK_PENDING
  metadata pending write
READY_TO_REBOOT
  BOOT request
```

Failure states:

```text
FAILED
ABORTED
```

Boot result states:

```text
APPLIED
NO_PENDING
MAX_ATTEMPTS
STAGE_VALIDATE_FAILED
COPY_FAILED
ACTIVE_VALIDATE_FAILED
```

## App Responsibilities

The running App owns:

- Transport protocol, such as SCPI over USB CDC, UART, CAN, Ethernet, or SD file.
- RTOS task ownership, queueing, and timeout policy around OTA commands.
- Package header parsing.
- Product/hardware/version compatibility checks.
- Target slot selection.
- Flash erase/program/readback of the inactive or staging slot.
- Transport CRC and selected image CRC verification.
- App vector/header validation.
- Metadata pending write.
- Commit after self-test.

The App must not:

- Jump directly to an unconfirmed update.
- Clear pending after Bootloader failure.
- Erase the Bootloader during normal OTA.
- Treat command ACK as update completion.
- Let multiple RTOS tasks write the same OTA context without serialization.

## Bootloader Responsibilities

The Bootloader owns:

- Metadata copy selection.
- Pending image validation.
- Active slot selection.
- Copy transaction recovery in copy-to-active mode.
- Direct slot switch in direct A/B mode.
- Boot attempt counting.
- Rollback to confirmed slot.
- Boot result recording.
- Final jump into the selected App.

The Bootloader should stay small. It should not include UI, file systems, SCPI,
network stacks, or complex logs unless the product explicitly requires them.

## Port Layer Contract

A portable OTA library should depend on a port interface instead of platform
headers. The minimum contract is:

```text
flash_read(offset, buffer, size)
flash_erase(offset, size)
flash_program(offset, data, size)
flash_is_erased(offset, size)
reboot()
get_time_ms()
log(level, message)
```

Optional platform hooks:

```text
enter_flash_critical_section()
leave_flash_critical_section()
feed_watchdog()
ota_lock()
ota_unlock()
ota_yield_or_delay()
invalidate_cache()
set_boot_partition()
```

Platform-specific examples:

| Platform | Key Port Concerns |
|---|---|
| RP2350 SDK | XIP flash erase/program safety, USB CDC servicing, watchdog reboot. |
| RP2350 RTOS | Same XIP constraints plus OTA task serialization, scheduler-aware flash service, watchdog feeding during long erase/program operations. |
| STM32 RTOS | Flash unlock/lock, sector alignment, interrupt/cache coordination, RTOS critical sections, watchdog feeding, option bytes if bank swap is used. |

## Validation Matrix

Minimum release validation:

| Category | Test |
|---|---|
| Factory | Bootloader + Slot A factory image boots. |
| Positive OTA | Unified package reaches READY_TO_REBOOT. |
| Apply | Bootloader applies pending image and records APPLIED. |
| Commit | App commits and confirmed slot updates. |
| Release isolation | Fault injection commands are absent in release builds. |
| Transport CRC | Corrupt announced CRC fails with CRC. |
| Image CRC | Corrupt selected image CRC fails with CRC. |
| Vector | Corrupt reset vector fails with VECTOR. |
| Header magic | Corrupt magic fails with BAD_HEADER. |
| Header version | Corrupt version fails with BAD_HEADER. |
| Header size | Corrupt package size fails with BAD_HEADER. |
| Slot | Corrupt image slot fails with BAD_HEADER. |
| Run offset | Corrupt run offset fails before erase or before mark pending. |
| Power loss | Reset during receive keeps old confirmed image. |
| Power loss | Reset during Bootloader apply recovers or rolls back. |
| No commit | Repeated boot without commit rolls back. |

## Recommended Library Split

```text
portable_ota/
  include/
    pota_core.h          # state machine and public API
    pota_package.h       # manifest parser
    pota_metadata.h      # redundant metadata helpers
    pota_platform.h      # required port callbacks
    pota_types.h         # slot, state, error, result types
  src/
    pota_core.c
    pota_package.c
    pota_metadata.c
    pota_crc32.c
  ports/
    rp2350/
    rp2350_rtos/
    stm32_rtos/
```

The library should not own a transport. SCPI, UART, CAN, Ethernet, USB MSC, SD
file, and factory tools should all feed the same OTA core through:

```text
pota_begin()
pota_write()
pota_end()
pota_abort()
pota_commit()
```

## Integration Strategy For This Project

The existing RP2350_TRIG OTA code already implements the reference behavior.
The next optimization step is to gradually align it with `portable_ota`:

1. Keep current Bootloader and App behavior as the golden reference.
2. Move package parsing first because it is already platform-neutral.
3. Move CRC32 and metadata copy selection next.
4. Keep flash erase/program and watchdog reboot in RP2350-specific adapter code.
5. Add an RTOS adapter contract before moving the RP2350 product firmware onto
   an RTOS.
6. After parity tests pass, let `components/ota_manager` call the portable core.

This avoids destabilizing the proven product path while still producing a
third-party style OTA library that RP2350 RTOS and STM32 RTOS products can
reuse.
