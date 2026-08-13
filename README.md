# RP2350_TRIG

Industrial firmware template for an RP2350 / Pico 2 based product. The project
uses a medium-size embedded layout so application code, board support, drivers,
OS adaptation, middleware, and third-party modules can grow independently.

## Text Encoding

All project source files, Markdown documents, and Python scripts should be read
and written as UTF-8. On Windows PowerShell, prefer explicit UTF-8 reads when
checking Chinese documents:

```powershell
Get-Content -Path docs\OTA_TODO.md -Encoding UTF8
Get-Content -Path README.md -Encoding UTF8
```

Avoid relying on the console default code page when editing or validating
documentation, because garbled text can hide real checklist changes.

## Architecture

The top-level software architecture is the Hybrid Active Object Function Block
Vector Architecture, abbreviated as HAOFV. The full design is documented in
`docs/HAOFV_ARCHITECTURE.md`.

```text
SCPI / UI / SD / Bootloader Result
        ↓
Active Object Layer
        ↓
IEC 61499-style Function Block Layer
        ↓
Time-Synchronized Vector Blackboard
        ↓
Hardware Service Layer

Hard Real-Time Side Path:
PIO / DMA / IRQ
```

The design rules are:

- Active Objects own domain execution, event queues, lifecycle, and public APIs.
- Lightweight IEC 61499-style Function Blocks own domain logic and ECC state
  transitions.
- Vector Blackboards store system facts, summaries, progress, resource state,
  and diagnostics snapshots.
- Resource Arbiter owns shared resources such as Flash, SPI, USB, PIO, DMA,
  LCD, and SD.
- PIO/DMA/IRQ own hard real-time capture and trigger output; RTOS tasks or the
  bare-metal main loop only manage configuration, state, and diagnostics.

Future FreeRTOS support must be added as an OSAL port and task runtime for
Active Objects. It must not replace the HAOFV boundaries or move hard real-time
edge generation into ordinary RTOS tasks. See `docs/RTOS_PORTING_PLAN.md`.

## Directory Layout

```text
RP2350_TRIG/
├─ application/                 # Product application and main loop
│  ├─ inc/
│  └─ src/
├─ boards/
│  └─ rp2350_trig/              # Board support package for this hardware
│     ├─ inc/
│     └─ src/
├─ components/                  # Reusable project components
│  └─ diagnostics/              # Logging, heartbeat, fault latch
│  └─ sync_io/                   # PIO-based sync trigger IO driver
│  └─ sync_config_ui/            # U8G2-based sync trigger config screen
├─ drivers/
│  ├─ mcu/                      # MCU peripheral drivers
│  │  ├─ spi/
│  │  ├─ i2c/
│  │  ├─ uart/
│  │  └─ watchdog/
│  └─ external/                 # External device drivers, add as needed
│     └─ lcd/                   # ST7789 SPI TFT driver
├─ platform/
│  └─ rp2350_pico_sdk/          # Pico SDK adaptation area
├─ osal/                        # OS abstraction layer
│  ├─ inc/
│  └─ port/
│     └─ baremetal/
├─ middleware/                  # Integrated middleware wrappers
│  └─ portable_ota_port/         # Product adapter for third_party/portable_ota
│  └─ u8g2_port/                # Project glue for U8G2
├─ third_party/                 # Unmodified upstream third-party source subsets
│  └─ u8g2/                     # U8G2 source subset used by the firmware
├─ config/                      # Product configuration and feature switches
├─ tests/                       # Unit, integration, and HIL tests
├─ tools/                       # Flashing, packaging, production scripts
├─ docs/                        # Architecture and release documents
├─ cmake/                       # Shared CMake helpers
└─ DOC/                         # Reference documents and SDK examples
```

## Layer Responsibilities

`application/` contains product behavior. It should not directly configure MCU
peripherals.

`boards/rp2350_trig/` contains board-specific pin mapping, power-on state, and
board bring-up. Replace or add board folders when hardware revisions diverge.

`drivers/mcu/` wraps RP2350 peripherals such as SPI, I2C, UART, GPIO, PIO, DMA,
and watchdog. Drivers may depend on Pico SDK, but not on application code.

`components/` contains reusable project services such as diagnostics, parameter
storage, event routing, and system monitoring.

`osal/` provides delay, time, task, queue, mutex, and event abstractions. The
current port is bare metal. A future FreeRTOS port should be added under
`osal/port/freertos/` without forcing application logic changes.

`middleware/` contains project integration code for protocol stacks, file
systems, USB classes, and bootloader clients.

`middleware/portable_ota_port/` is the product adapter for
`third_party/portable_ota`. Product components should depend on this adapter
instead of including `pota_*` headers directly.

`third_party/` is reserved for unmodified upstream libraries or source subsets.
Project adapters belong in `middleware/`, `components/`, or `platform/`.

## Dependency Direction

```text
application
  ↓
components ─────────┐
  ↓                 │
boards              │
  ↓                 │
drivers/mcu         │
  ↓                 │
platform / osal ←───┘
  ↓
Pico SDK / hardware
```

Keep dependencies one-way. Lower layers should not include application headers.

## Build

The supported build entry is still CMake. Python scripts are used by CMake and
by the OTA workflow; do not use them as a replacement for the firmware build.

Required host tools:

- CMake and Ninja.
- Pico SDK toolchain and `picotool`.
- Python 3 available in `PATH` or discoverable by CMake.

Preset build:

```powershell
cmake --preset pico2-release
cmake --build --preset pico2-release
```

CMake build directories are machine-local. If the source tree is moved between
computers or drive letters, remove or reconfigure `build/`, `build-validation/`,
and `build-debug/` before building, because `CMakeCache.txt` stores absolute
source, SDK, and toolchain paths.

For drive-letter switches such as `D:` and `E:`, the project helper can do this
check automatically. It compares `CMakeCache.txt` against the current workspace,
cleans only stale CMake metadata inside the selected build directory, then
configures and builds with the requested preset:

```powershell
python tools\cmake_build_auto\cmake_build_auto.py --preset pico2-release --build-dir build
python tools\cmake_build_auto\cmake_build_auto.py --preset pico2-release --build-dir build-sd-verify
```

Release gate check after building:

```powershell
python tools/release_check/release_check.py --preset pico2-release --build-dir build
```

Validation build with destructive OTA fault-injection commands:

```powershell
cmake --preset pico2-validation
cmake --build --preset pico2-validation
```

Manual build:

```powershell
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2 -DPROJECT_WARNINGS_AS_ERRORS=ON
cmake --build build
```

During the build, CMake automatically runs:

```text
tools/uf2_join/uf2_join.py
tools/build_info/gen_build_info.py
```

This script combines:

```text
build/RP2350_TRIG_BOOT.bin@0x10000000
build/RP2350_TRIG.bin@0x10040000
build/ota_metadata_clear.bin@0x10340000
```

into:

```text
build/RP2350_TRIG_FACTORY.uf2
```

Normally you should not call `uf2_join.py` manually. If you must reproduce the
factory image command by hand:

```powershell
python tools/uf2_join/uf2_join.py build/RP2350_TRIG_FACTORY.uf2 `
  build/RP2350_TRIG_BOOT.bin@0x10000000 `
  build/RP2350_TRIG.bin@0x10040000 `
  build/ota_metadata_clear.bin@0x10340000
```

The build generates a factory image and an OTA application image:

```text
build/RP2350_TRIG_FACTORY.uf2  # first-time UF2 flash: bootloader + Slot A app
build/RP2350_TRIG.bin          # OTA payload sent by SCPI USB CDC
build/RP2350_TRIG_B.bin        # Slot B linked image for direct A/B validation
build/RP2350_TRIG_UPDATE.pkg   # unified OTA package containing Slot A + Slot B apps
build/RP2350_TRIG_BOOT.uf2     # bootloader-only image for recovery/debug
build/sdcard/                  # SD-card filesystem staging tree, generated by tools/sd_fs_build
build/RP2350_TRIG_SDCARD.zip   # zipped SD-card filesystem contents, generated by tools/sd_fs_build
```

`tools/build_info/gen_build_info.py` generates the firmware build id source on
each build, so `SYST:FW:BUILD?` changes when a new OTA payload is produced.
The build also generates `RP2350_TRIG_UPDATE.pkg` from the Slot A and Slot B
linked App binaries.

After a release build, generate SD-card contents separately:

```powershell
python tools\sd_fs_build\sd_fs_build.py --build-dir build --output-dir build\sdcard --clean
```

Copy the contents of `build\sdcard\` to the root of a FAT32 SD card. The
firmware-readable SD index is `/manifest.idx`; `/manifest.json` is for PC tools
and inspection.

First-time programming should use `RP2350_TRIG_FACTORY.uf2`. Product OTA should
use the unified package so the host sends one file and the device selects the
correct internal image. CMake generates the package automatically; to reproduce
it by hand:

```powershell
python tools/ota_packager/ota_packager.py `
  --image-a build/RP2350_TRIG.bin `
  --image-b build/RP2350_TRIG_B.bin `
  --app-version 0.1.0 `
  --build-id-file build/generated/project_build_info.c `
  --min-bootloader-version 0.1.0 `
  -o build/RP2350_TRIG_UPDATE.pkg
python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG_UPDATE.pkg
```

The sender auto-detects the unified package header and uses
`SYST:OTA:PBEGIN <size>,<crc32>`. New release/factory builds default blank
metadata to `DIRECT_AB`, so the device selects the image matching the inactive
target slot and boots that slot directly after `SYST:OTA:BOOT`. The legacy
`COPY_TO_ACTIVE` path remains supported for devices whose metadata is already in
that mode; in that mode the device selects the Slot A linked image and stages it
in Slot B before the Bootloader copies it to Slot A. The package header records
product id, hardware id, App version, build id, payload SHA-256, per-image CRC32,
and `min_bootloader_version`; the device rejects product, hardware, and minimum
Bootloader mismatches before erasing the target slot.

Raw `.bin` OTA remains supported for compatibility and bench work:

```powershell
python tools/ota_bin_info/ota_bin_info.py build/RP2350_TRIG.bin
python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin
```

SD-card maintenance media can be staged from the release artifacts:

```powershell
python tools/sd_fs_build/sd_fs_build.py --build-dir build --output-dir build/sdcard
```

The generated SD root uses `/update/RP2350_TRIG_UPDATE.pkg` as the default
offline OTA payload. Raw `.bin` images are copied to `/update/compat/` only for
compatibility.

For direct A/B validation, query the target slot and let the sender choose the
matching linked image:

```powershell
python tools/ota_send/ota_send.py COM4 --auto-target `
  --image-a build-validation/RP2350_TRIG.bin `
  --image-b build-validation/RP2350_TRIG_B.bin
```

`SYST:OTA:MODE <0|1>` is available only in validation builds. Release builds
keep `SYST:OTA:MODE?`, `SYST:OTA:TARG?`, and `SYST:OTA:CAP?` as read-only
queries.

For negative-path validation, the sender can intentionally corrupt the announced
CRC and assert the expected final state:

```powershell
python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin `
  --corrupt-crc --expect-final-state FAILED
python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin `
  --corrupt-vector --expect-final-state FAILED
python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG.bin `
  --abort-after-blocks 8 --expect-final-state ABORTED
```

Unified package negative-path validation is also supported:

```powershell
python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG_UPDATE.pkg `
  --corrupt-crc --expect-final-state FAILED --expect-error CRC
python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG_UPDATE.pkg `
  --package-negative image-vector --expect-final-state FAILED --expect-error VECTOR
python tools/ota_send/ota_send.py COM4 build/RP2350_TRIG_UPDATE.pkg `
  --package-negative header-magic --expect-final-state FAILED --expect-error BAD_HEADER
```

OTA fault-injection SCPI commands are available only when the CMake cache option
`PROJECT_ENABLE_OTA_FAULT_INJECTION=ON` is enabled. They are intended for
development validation, not production firmware. Use the `pico2-validation`
preset for bench validation. The `pico2-release` preset disables these commands.

```text
SYST:OTA:INJ:COPY?      # query fault flags
SYST:OTA:INJ:COPY       # force next Bootloader copy to fail
SYST:OTA:INJ:CLEAR      # clear fault flags
SYST:OTA:INJ:MCOR 0     # erase metadata copy 0
SYST:OTA:INJ:MCOR 1     # erase metadata copy 1
SYST:OTA:INJ:MREP       # repair metadata dual copies
```

`SYST:OTA:INJ:COPY` requires a factory image that contains the matching
Bootloader. If only the App was updated by OTA, the App-side SCPI command may be
present while the old Bootloader still ignores the injection flag.

After `SYST:OTA:STAT?` reports `READY_TO_REBOOT`, send `SYST:OTA:BOOT` or use
the OTA sender's boot flow once enabled.

After the board re-enumerates, query the OTA audit state:

```text
SYST:FW:VERS?
SYST:FW:BUILD?
SYST:BOOT:VERS?
SYST:BOOT:CAP?
SYST:OTA:STAT?
SYST:OTA:SLOT?   # active,pending,confirmed,boot_attempts,rollback_count
SYST:OTA:RES?    # app_result,app_error,boot_result,boot_source_slot,boot_size,boot_crc32
SYST:OTA:COMM    # confirm the running image after application self-test
```

Closed-loop validation tools:

| Script | When to use | Purpose |
|---|---|---|
| `tools/ota_send/ota_send.py` | Board OTA validation | Main closed-loop OTA sender over USB CDC SCPI. Sends raw `.bin` or unified `.pkg`, asserts final state with `--expect-final-state`, asserts error text with `--expect-error`, and supports package mutations with `--package-negative`. |
| `tools/scpi_query/scpi_query.py` | Bench SCPI checks | Sends one or more SCPI commands over USB CDC, filters shared-stream diagnostic logs, closes the serial port on exit, and can save a UTF-8 transcript with `--out`. |
| `tools/release_check/release_check.py` | Release gate | Verifies release preset safety switches, required artifacts, and absence of OTA fault-injection command strings in release artifacts. |
| `tools/tests/run_portable_ota_tests.ps1` | Portable OTA library gate | Builds or runs `third_party/portable_ota` unit tests. If no host C compiler is available, it falls back to ARM GCC compile/object-build checks and reports that host execution was skipped. |
| `tools/ota_packager/ota_packager.py` | Release OTA packaging | Builds one unified package from Slot A and Slot B linked `.bin` files. CMake normally invokes this automatically. |
| `tools/ota_bin_info/ota_bin_info.py` | Raw `.bin` bench work | Prints `.bin` size, CRC32, and the matching `SYST:OTA:BEGIN` command. |
| `tools/uf2_join/uf2_join.py` | Factory image generation | Generates the first-time factory UF2 from Bootloader + Slot A App binaries. CMake normally invokes this automatically. |
| `tools/build_info/gen_build_info.py` | Build metadata | Generates the firmware build id used by `SYST:FW:BUILD?`. CMake normally invokes this automatically. |

Recommended OTA board validation loop:

```powershell
python tools\release_check\release_check.py --preset pico2-release --build-dir build-portable-migration
python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg `
  --expect-final-state READY_TO_REBOOT
python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg `
  --corrupt-crc --expect-final-state FAILED --expect-error CRC
python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg `
  --package-negative image-crc --expect-final-state FAILED --expect-error CRC
python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg `
  --package-negative image-vector --expect-final-state FAILED --expect-error VECTOR
python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg `
  --package-negative header-magic --expect-final-state FAILED --expect-error BAD_HEADER
python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg `
  --package-negative header-version --expect-final-state FAILED --expect-error BAD_HEADER
python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg `
  --package-negative header-size --expect-final-state FAILED --expect-error BAD_HEADER
python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg `
  --package-negative slot --expect-final-state FAILED --expect-error BAD_HEADER
python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg `
  --package-negative run-offset --expect-final-state FAILED --expect-error IMAGE_TOO_LARGE
```

Run OTA board tests serially. Only one process may own the USB CDC COM port at a
time.

## Key Configuration Files

- `config/project_config.h`: firmware version, loop periods, watchdog timeout,
  and product-level constants.
- `boards/rp2350_trig/inc/board_config.h`: board pin map and peripheral
  instances.
- `CMakePresets.json`: release and debug build presets.
- `docs/README.md`: documentation index and current domain hierarchy.
- `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`: documentation naming, metadata,
  hierarchy, cross-reference, and migration rules.
- `docs/HAOFV_ARCHITECTURE.md`: top-level HAOFV product
  architecture based on Active Objects, lightweight IEC 61499-style function
  blocks, time-synchronized vectors, table-driven state machines, event
  dispatch, and resource arbitration.
- `docs/release/RELEASE_CHECKLIST.md`: release gate template.
- `docs/SYNC_IO_RESOURCE_PLAN.md`: sync trigger PIO, state-machine, and GPIO
  allocation.
- `docs/TRIGGER_SYNC_TODO.md`: remaining work for the production trigger
  subsystem.
- `docs/SCPI_COMMANDS.md`: basic SCPI command list for trigger configuration.
- `docs/SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md`: implementable A0-A3 RP2350
  distributed DPLL, virtual DC clock, AUX calibration ring, and local scheduled
  trigger plan.
- `docs/OTA_SYSTEM_DESIGN.md`: A/B OTA upgrade design for the W25Q32 4 MB QSPI Flash.
- `docs/OTA_COPY_TRANSACTION_DESIGN.md`: copy-to-active OTA transaction
  design for power-loss recovery within the current flash partition layout.
- `docs/OTA_TODO.md`: OTA productization backlog for release gating,
  power-loss recovery, manifest compatibility, validation reports, and
  automation.
- `docs/OTA_PORTABLE_ARCHITECTURE.md`: portable OTA design and validation
  guide for reusing the proven package, metadata, Bootloader, and negative-path
  validation flow on RP2350 and STM32 RTOS products.
- `docs/OTA_OPEN_SOURCE_COMPARISON.md`: comparison with MCUboot, ESP-IDF OTA,
  Mender MCU, and STM32 X-CUBE-SBSFU for the RP2350/STM32 RTOS scope.
- `docs/OTA_LIBRARY_MIGRATION_PLAYBOOK.md`: staged plan for hardening
  `portable_ota` first, then migrating the current project with closed-loop
  validation at each step.
- `docs/RTOS_PORTING_PLAN.md`: HAOFV-aligned FreeRTOS migration plan and
  OTA RTOS adapter requirements.
- `docs/RTOS_DISTRIBUTED_TRIGGER_PARTITION.md`: product RTOS + dual-core
  partition for the four-board distributed trigger system, including the
  simulated reflective-memory DistributedVectorTable.
- `docs/TASK_PROGRESS.md`: task progress log for goals, completed work,
  verification results, remaining work, and next steps.

## Expansion Rules

- Add new MCU peripherals under `drivers/mcu/<peripheral>/`.
- Add external chips under `drivers/external/<device>/`.
- Add reusable services under `components/<component>/`.
- Add third-party source subsets under `third_party/<name>/`, keep local
  adapters outside the third-party tree, and do not commit nested `.git`
  metadata from upstream checkouts.
- Add RTOS support as a new OSAL port, not by spreading RTOS APIs through
  application code.
- Keep `DOC/` as reference material. Product firmware should not depend on files
  under `DOC/`.

## LCD And Graphics

The board LCD uses the SPI TFT signals listed in `IO约束.md`:

- `GPIO8`: LCD DC
- `GPIO9`: LCD CS
- `GPIO10`: SPI SCK
- `GPIO11`: SPI MOSI
- `GPIO25`: LCD backlight

The LCD is write-only on this board and does not use SPI MISO. `GPIO12` is
available as the shared SPI MISO signal for the TF/SD card interface.

The native RGB565 LCD driver is located at `drivers/external/lcd/` and currently
targets the board's 240x135 ST7789-compatible SPI panel.

The U8G2 source subset used by this firmware is placed under
`third_party/u8g2/`. Project-specific glue is placed under
`middleware/u8g2_port/`.

The current UI path uses U8G2 as a monochrome composition layer and flushes the
result through the native RGB565 ST7789 driver. The sync trigger configuration
page lives in `components/sync_config_ui/` and uses U8G2's built-in button
drawing API for the action controls.

## Sync Trigger IO

The synchronous trigger IO driver lives in `components/sync_io/`. It currently
initializes the reserved PIO resources at boot and exposes interfaces for
4-bit input capture, deterministic pulse output, synchronous clock output, and
four `pio2` auxiliary IO channels.
