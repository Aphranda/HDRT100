# Portable OTA Porting Notes

Each product integrates Portable OTA by filling `pota_platform_t`.

## RP2350

Recommended mapping:

```text
flash_read      -> XIP memcpy or project flash read wrapper
flash_erase     -> Pico SDK flash_range_erase through a safe wrapper
flash_program   -> Pico SDK flash_range_program through a safe wrapper
mark_pending    -> existing redundant metadata writer
confirm_active  -> existing active slot confirm API
validate_vector -> App vector check using slot offset and run offset
reboot          -> watchdog reboot
```

Important details:

- Flash erase/program code must obey Pico SDK XIP safety rules.
- USB CDC and watchdog servicing must be considered during long transfers.
- `COPY_TO_ACTIVE` remains useful for fixed-address Slot A images.
- `DIRECT_AB` requires a Slot B linked image or position-independent App.

## RP2350 RTOS

Recommended mapping keeps the same flash and metadata layout as the current
RP2350 reference, but serializes OTA through one owner task:

```text
ota task        -> owns pota_context_t and state transitions
transport task  -> sends BEGIN/DATA/END/BOOT/COMM messages to OTA task
flash service   -> performs XIP-safe erase/program/read operations
watchdog        -> fed during long erase/program/copy operations
```

Important details:

- Do not let multiple RTOS tasks call `pota_write()` on the same context.
- Use a mutex or mailbox around metadata updates and Bootloader handoff state.
- Keep flash erase/program in a scheduler-aware service so long operations do
  not starve USB, watchdog, or safety tasks.
- If code executes from external flash, preserve the same XIP critical-section
  rules as the SDK reference.
- Keep the current RP2350 SDK validation results as the golden behavior for the
  RTOS migration.

## STM32 RTOS

Recommended mapping:

```text
flash_read      -> memcpy from flash address or HAL flash read abstraction
flash_erase     -> HAL_FLASHEx_Erase by sector/page
flash_program   -> HAL_FLASH_Program using platform alignment
mark_pending    -> redundant metadata sectors
confirm_active  -> metadata confirmed slot update
validate_vector -> stack pointer in RAM range, reset handler in image range
reboot          -> NVIC_SystemReset
```

Important details:

- Unlock and lock flash around erase/program.
- Suspend or coordinate interrupts if executing from the same flash bank.
- On Cortex-M7/H7, handle I-cache/D-cache invalidation.
- For dual-bank devices, decide whether to use hardware bank swap or explicit
  Bootloader slot selection.
- Keep option byte changes out of the generic OTA core.
- In an RTOS, run OTA from a single owner task and feed it through queues or
  stream buffers.
- Feed the independent watchdog during erase/program operations.
- Keep HAL flash calls out of interrupt context.
- For single-bank parts, prefer copy-to-active or external staging unless the
  product can safely stop execution from the bank being programmed.

## Common Port Checklist

- Define product id and hardware id.
- Define Bootloader semantic version and capability bits.
- Define slot offsets, sizes, and run offsets.
- Define whether `flash_program()` accepts arbitrary byte lengths or requires
  adapter-side padding to the MCU page/program granularity.
- Prove metadata can survive one corrupted copy.
- Prove old confirmed image remains bootable after failed update.
- Prove rollback when a new image never commits.
- Keep destructive validation commands out of release firmware.
- Prove the RTOS integration has exactly one OTA context owner.
- Prove watchdog, USB/transport, and safety tasks survive long OTA operations.
