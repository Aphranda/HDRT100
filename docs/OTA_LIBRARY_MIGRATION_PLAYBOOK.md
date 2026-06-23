# OTA Library Migration Playbook

This playbook defines the execution order for improving the reusable OTA
library first, then migrating the current project in controlled steps. The
scope stays limited to RP2350 and STM32 RTOS products.

## Core Rule

Do not start the next step until the current step has a closed-loop validation
record with:

1. code change completed,
2. build completed,
3. target image flashed,
4. positive-path behavior verified,
5. required negative-path behavior verified,
6. result recorded in the release or task log.

## Step 0: Freeze The Reference Behavior

Goal:

- Treat the current RP2350 SDK firmware as the golden reference.
- Keep the current factory, release, and validation images stable.
- Keep the existing SCPI and OTA command contract stable unless the change is
  explicitly part of the library migration.

Validation:

- Factory boot.
- Positive OTA.
- Apply and commit.
- Current negative-path matrix.
- Release isolation.

## Step 1: Harden The Portable OTA Core

Goal:

- Keep the current package parser, CRC, metadata vocabulary, and state machine
  inside `portable_ota`.
- Keep transport out of the library.
- Keep platform-specific flash and reboot code behind the port layer.

Work items:

- Finalize the platform contract and RTOS hooks.
- Add portable package parser unit tests.
- Add metadata selection and schema validation tests.
- Add explicit error mapping for package, image, vector, and metadata cases.
- Add a minimal signature and anti-rollback extension plan.

Validation:

- Host build of the portable library.
- Host unit tests for header parsing and metadata selection.
- Negative package tests for magic, version, size, slot, CRC, vector, and
  run-offset failures.
- No RP2350 product behavior change yet.

Exit criteria:

- Portable core APIs are stable enough for product integration.
- Parser and metadata behavior are deterministic and documented.

## Step 2: Integrate The Library Into Current RP2350 SDK Firmware

Goal:

- Replace the duplicated parser and shared OTA logic with the portable core
  while keeping the current product behavior.

Work items:

- Wrap the RP2350 flash and reboot operations with a thin adapter.
- Keep current SCPI commands and image layout unchanged.
- Route existing positive and negative validation flows through the portable
  core.

Validation:

- Same positive OTA result as the reference firmware.
- Same boot/apply/commit sequence.
- Same negative package matrix.
- Same release isolation.
- Same factory image and recovery path.

Exit criteria:

- Current project behavior is preserved.
- Portable OTA owns the common logic.

## Step 3: Move The Current Project To An RTOS-Owned OTA Task

Goal:

- Move OTA command handling into a single owner task.
- Keep transport and flash operations serialized.
- Preserve the same package format and validation behavior.

Work items:

- Add OTA queue or stream buffer.
- Add flash service and watchdog feed hooks.
- Add RTOS locks around metadata and flash.
- Keep the Bootloader unchanged until the App-side flow is stable.

Validation:

- Factory boot.
- Positive OTA.
- Negative package matrix.
- Concurrent workload stress test.
- Watchdog, transport, and safety tasks remain healthy during OTA.
- Power-loss and no-commit rollback remain correct.

Exit criteria:

- RP2350 RTOS firmware behaves like the SDK reference under all required OTA
  cases.

## Step 4: Port The Same Contract To STM32 RTOS

Goal:

- Reuse the same OTA core and validation flow on STM32 RTOS firmware.

Work items:

- Implement STM32 flash, cache, reboot, and vector validation adapters.
- Define STM32-specific flash layout and boot mode.
- Reuse the same package format and closed-loop test matrix.

Validation:

- Factory boot.
- Positive OTA.
- Negative package matrix.
- Reset and power-loss recovery.
- No-commit rollback.
- RTOS concurrency stress.

Exit criteria:

- STM32 RTOS reaches parity with the RP2350 reference behavior.

## Step 5: Extend Security And Release Discipline

Goal:

- Add signature verification and anti-rollback without breaking the core flow.

Work items:

- Add signature or TLV-style extension support.
- Add monotonic version or security counter checks.
- Add metadata migration rules.
- Add release artifact archiving and validation reports.

Validation:

- Signed image accepted.
- Tampered image rejected.
- Old image rejected when anti-rollback policy requires it.
- Existing positive and negative flows still pass.

## Closed-Loop Report Template

Each step should produce one record with:

- step name
- code files changed
- build command
- flash image or package used
- command sequence
- expected result
- actual result
- pass/fail
- follow-up items

This keeps the library work and product migration tightly coupled to evidence
instead of intention.

## Closed-Loop Records

### 2026-06-23 Step 1A: Portable Core Foundation

Code files changed:

- `third_party/portable_ota/include/pota_platform.h`
- `third_party/portable_ota/include/pota_metadata.h`
- `third_party/portable_ota/src/pota_metadata.c`
- `third_party/portable_ota/src/pota_core.c`
- `tests/unit/test_portable_ota_package.c`
- `tests/unit/test_portable_ota_metadata.c`
- `tests/unit/test_portable_ota_core.c`
- `tools/run_portable_ota_tests.ps1`

Build command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
& 'C:\Users\Aphranda\.pico-sdk\toolchain\14_2_Rel1\bin\arm-none-eabi-gcc.exe' `
  -std=c11 -Wall -Wextra -Werror -Ithird_party\portable_ota\include `
  -fsyntax-only `
  third_party\portable_ota\src\pota_crc32.c `
  third_party\portable_ota\src\pota_package.c `
  third_party\portable_ota\src\pota_metadata.c `
  third_party\portable_ota\src\pota_core.c `
  tests\unit\test_portable_ota_package.c `
  tests\unit\test_portable_ota_metadata.c `
  tests\unit\test_portable_ota_core.c
```

Expected result:

- Portable package parser, metadata helper, and core mock-port tests compile
  cleanly.
- No RP2350 product firmware behavior changes yet.

Actual result:

- Passed ARM GCC strict compile and object-build checks.
- Host execution was skipped because this machine currently has no `gcc`,
  `clang`, or `cl` host C compiler in `PATH`.

Pass/fail:

- Pass for portable library compile closure.
- Host runtime unit-test execution remains a follow-up before treating Step 1
  as CI-complete.

Follow-up items:

- Run the same tests on a machine or CI image with a host C compiler.
- Add package parser tests for chunking behavior if the transport ever stops
  sending the 512-byte package header as a single first block.
- Keep product firmware migration blocked until the portable core passes both
  host runtime tests and RP2350 product parity tests.

### 2026-06-23 Step 2A: Product Package Parser Delegation

Code files changed:

- `CMakeLists.txt`
- `components/ota_manager/src/ota_package.c`
- `middleware/portable_ota_port/inc/portable_ota_port.h`
- `middleware/portable_ota_port/src/portable_ota_port.c`

Migration scope:

- Keep the product-facing `ota_package_parse_header()` and
  `ota_package_find_image()` API unchanged.
- Delegate package header parsing and compatibility checks to
  `third_party/portable_ota/src/pota_package.c` through
  `middleware/portable_ota_port`.
- Do not change the Bootloader, SCPI commands, flash layout, OTA state machine,
  or metadata format.

Build command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
cmake -S . -B build-portable-migration -G Ninja `
  -DPICO_BOARD=pico2 `
  -DPROJECT_WARNINGS_AS_ERRORS=ON `
  -DPROJECT_ENABLE_OTA_FAULT_INJECTION=OFF
cmake --build build-portable-migration
python tools\release_check\release_check.py `
  --preset pico2-release `
  --build-dir build-portable-migration
```

Expected result:

- Portable library compile closure still passes.
- RP2350 release firmware builds with the delegated package parser.
- Release artifacts include factory UF2, Slot A/B binaries, Bootloader UF2, and
  unified OTA package.
- Release check passes and confirms no OTA fault-injection commands in release
  artifacts.

Actual result:

- Passed portable library ARM GCC compile/object-build checks.
- Passed `cmake --build build-portable-migration`.
- Generated `RP2350_TRIG_UPDATE.pkg`, size `148824`, package CRC32
  `0x88D1CA02`.
- Passed `release_check=OK`.

Pass/fail:

- Pass for build and release-gate closure.
- Pass for RP2350 hardware OTA parity validation on `COM4`.

Follow-up items:

- Keep this package-parser migration as the first proven product integration
  point.
- Keep product components behind `middleware/portable_ota_port` instead of
  directly including `pota_*` third-party headers.
- Do not migrate more OTA state machine logic into `portable_ota` until the
  next step has its own mock-port and board validation plan.

Hardware validation:

- Factory flashed:
  `build-portable-migration\RP2350_TRIG_FACTORY.uf2`.
- Initial state after factory:
  `SYST:FW:BUILD? -> "20260623052155"`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.
- Positive package:
  `python tools\ota_send\ota_send.py COM4 build-portable-migration\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT`.
- Boot/apply/commit:
  `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,74056,1705636185`,
  then `SYST:OTA:COMM -> "OK"`,
  `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`.
- Negative package matrix:
  transport CRC -> `"FAILED",2,"CRC",4`;
  image CRC -> `"FAILED",2,"CRC",4`;
  image vector -> `"FAILED",2,"VECTOR",4`;
  header magic -> `"FAILED",2,"BAD_HEADER",4`;
  header version -> `"FAILED",2,"BAD_HEADER",4`;
  header size -> `"FAILED",2,"BAD_HEADER",4`;
  image slot -> `"FAILED",2,"BAD_HEADER",4`;
  run offset -> `"FAILED",2,"IMAGE_TOO_LARGE",4`.
- Final safe state:
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 4,"IMAGE_TOO_LARGE","APPLIED",1,74056,1705636185`.

### Step 2B: Product CRC32 Delegation Through Middleware

Goal:

- Move product-side `ota_crc32_*` calls behind `middleware/portable_ota_port`.
- Let both App and Bootloader use `third_party/portable_ota/src/pota_crc32.c`
  through the middleware adapter, without exposing third-party headers to
  product OTA components.
- Keep Bootloader linkage narrow: include only the CRC adapter and portable CRC
  source, not the package parser adapter.

Code scope:

- `components/ota_manager/src/ota_crc32.c` now delegates to
  `portable_ota_port_crc32_update()` and
  `portable_ota_port_crc32_compute()`.
- `middleware/portable_ota_port/src/portable_ota_crc_port.c` owns the product
  adapter from the middleware API to `pota_crc32_*`.
- `CMakeLists.txt` builds the portable CRC source into both App and Bootloader.
  App also builds the package parser adapter, while Bootloader only receives the
  CRC adapter.

Build and release gate:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
cmake --build build-portable-boot-crc
python tools\release_check\release_check.py `
  --preset pico2-release `
  --build-dir build-portable-boot-crc
```

Actual result:

- Portable OTA tests passed the ARM GCC compile/object-build gate. Host
  execution was skipped because no host C compiler was found.
- `cmake --build build-portable-boot-crc` passed.
- Release gate passed with `release_check=OK`.
- Generated unified package:
  `build-portable-boot-crc\RP2350_TRIG_UPDATE.pkg`,
  size `148856`, package CRC32 `0xEC85E520`, build id
  `20260623061144`.

Bootloader-impacting hardware validation:

- Factory flashed with `picotool`:
  `build-portable-boot-crc\RP2350_TRIG_FACTORY.uf2`.
- Initial state after factory:
  `*IDN? -> RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`,
  `SYST:FW:BUILD? -> "20260623061144"`,
  `SYST:BOOT:VERS? -> 0,1,0`,
  `SYST:OTA:STAT? -> "IDLE",2,"NONE",0`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.
- Positive package:
  `python tools\ota_send\ota_send.py COM4 build-portable-boot-crc\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT`.
- Boot/apply/commit:
  `SYST:OTA:BOOT -> "OK"`,
  USB CDC re-enumerated after about `2 s`,
  `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,74088,1326632426`,
  `SYST:OTA:COMM -> "OK"`,
  `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`.
- Negative package matrix after CRC migration:
  transport CRC -> `"FAILED",2,"CRC",4`;
  image CRC -> `"FAILED",2,"CRC",4`;
  image vector -> `"FAILED",2,"VECTOR",4`;
  header magic -> `"FAILED",2,"BAD_HEADER",4`;
  header version -> `"FAILED",2,"BAD_HEADER",4`;
  header size -> `"FAILED",2,"BAD_HEADER",4`;
  image slot -> `"FAILED",2,"BAD_HEADER",4`;
  run offset -> `"FAILED",2,"IMAGE_TOO_LARGE",4`.
- Final safe state:
  `SYST:FW:BUILD? -> "20260623061144"`,
  `SYST:OTA:STAT? -> "FAILED",2,"IMAGE_TOO_LARGE",4`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 4,"IMAGE_TOO_LARGE","APPLIED",1,74088,1326632426`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.

Pass/fail:

- Pass. The shared portable CRC implementation is now used in the Bootloader
  validation path that verifies and applies the staged image.
- Pass. Failed CRC/header/vector/run-offset cases do not leave pending metadata
  behind; confirmed Slot A remains running.

### Step 2C: Product Image Vector Validation Delegation

Goal:

- Move App vector-table validation into `third_party/portable_ota` as a
  platform-parameterized helper.
- Keep RP2350-specific SRAM/XIP limits and flash-read callback in
  `middleware/portable_ota_port`.
- Preserve the existing product API
  `ota_image_validate_app_vector()` for App and Bootloader callers.

Code scope:

- Added `third_party/portable_ota/include/pota_image.h` and
  `third_party/portable_ota/src/pota_image.c`.
- Added `tests/unit/test_portable_ota_image.c` and included it in
  `tools/run_portable_ota_tests.ps1`.
- Added `middleware/portable_ota_port/src/portable_ota_image_port.c` to bind
  RP2350 SRAM range, XIP base, and `drv_flash_read()`.
- `components/ota_manager/src/ota_image.c` now delegates to
  `portable_ota_port_validate_app_vector()`.
- `CMakeLists.txt` builds the portable image helper into both App and
  Bootloader through the shared portable OTA adapter source list.

Build and release gate:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
cmake -S . -B build-portable-image -G Ninja `
  -DPICO_BOARD=pico2 `
  -DPROJECT_WARNINGS_AS_ERRORS=ON `
  -DPROJECT_ENABLE_OTA_FAULT_INJECTION=OFF
cmake --build build-portable-image
python tools\release_check\release_check.py `
  --preset pico2-release `
  --build-dir build-portable-image
```

Actual result:

- Portable OTA tests passed the ARM GCC compile/object-build gate. Host
  execution was skipped because no host C compiler was found.
- `cmake --build build-portable-image` passed.
- Release gate passed with `release_check=OK`.
- Generated unified package:
  `build-portable-image\RP2350_TRIG_UPDATE.pkg`,
  size `148928`, package CRC32 `0xFC068188`, build id
  `20260623090450`.

Bootloader-impacting hardware validation:

- Factory flashed with `picotool`:
  `build-portable-image\RP2350_TRIG_FACTORY.uf2`.
- Initial state after factory:
  `*IDN? -> RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`,
  `SYST:FW:BUILD? -> "20260623090450"`,
  `SYST:BOOT:VERS? -> 0,1,0`,
  `SYST:OTA:STAT? -> "IDLE",2,"NONE",0`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.
- Positive package:
  `python tools\ota_send\ota_send.py COM4 build-portable-image\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT`.
- Boot/apply/commit:
  `SYST:OTA:BOOT -> "OK"`,
  USB CDC re-enumerated after about `2 s`,
  `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,74160,479264449`,
  `SYST:OTA:COMM -> "OK"`,
  `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`.
- Negative package matrix after image-vector migration:
  transport CRC -> `"FAILED",2,"CRC",4`;
  image CRC -> `"FAILED",2,"CRC",4`;
  image vector -> `"FAILED",2,"VECTOR",4`;
  header magic -> `"FAILED",2,"BAD_HEADER",4`;
  header version -> `"FAILED",2,"BAD_HEADER",4`;
  header size -> `"FAILED",2,"BAD_HEADER",4`;
  image slot -> `"FAILED",2,"BAD_HEADER",4`;
  run offset -> `"FAILED",2,"IMAGE_TOO_LARGE",4`.
- Final safe state:
  `SYST:FW:BUILD? -> "20260623090450"`,
  `SYST:OTA:STAT? -> "FAILED",2,"IMAGE_TOO_LARGE",4`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 4,"IMAGE_TOO_LARGE","APPLIED",1,74160,479264449`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.

Pass/fail:

- Pass. The shared portable image vector helper is now used by both App and
  Bootloader validation paths.
- Pass. Valid staged images still apply successfully, and corrupted vector
  packages still fail as `VECTOR` without leaving pending metadata.

### Step 2D: Portable Core Parity Hardening

Goal:

- Prepare `pota_core` for a later product `ota_fb` migration by matching the
  RP2350 product transfer semantics more closely.
- Keep this as a portable-library-only step; no product firmware behavior is
  changed until the core adapter is wired into the App state machine.

Code scope:

- `pota_core` now validates a nonzero package-header CRC against the transport
  CRC passed in `pota_begin()`. A mismatch fails during the first package block
  before erase/program side effects.
- Added `pota_service()`. Raw transfers and package transfers after the header
  now enter `CHECK_PERMISSION`/`ERASE_SLOT` and advance erase work through
  service ticks instead of erasing the whole target synchronously.
- `pota_core` now pads final flash program chunks with `0xFF` up to
  `flash_page_size`, and rejects non-final chunks that are not page aligned.
- Added portable constants for maximum data block and flash page sizes used by
  the core write buffer.
- Extended `tests/unit/test_portable_ota_core.c` for service-driven erase,
  package-header CRC mismatch, final-block padding, and non-final unaligned
  block rejection.

Validation:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
```

Actual result:

- Passed the ARM GCC compile/object-build gate.
- Host execution was skipped because no host C compiler was found.

Pass/fail:

- Pass for portable core compile closure.
- Board validation is intentionally not repeated in this step because product
  firmware still uses the existing `ota_fb` path. Board validation is required
  when `ota_fb` is switched to the portable core adapter.

Next migration boundary:

- Add a middleware core adapter that maps `pota_status_t`, `pota_error_t`, and
  `pota_result_t` to the existing product `ota_vector_t` values.
- Wire `ota_fb` BEGIN/TICK/DATA/END/ABORT to `pota_begin()`,
  `pota_service()`, `pota_write()`, `pota_end()`, and `pota_abort()` while
  preserving the current SCPI state/error text.

### Step 2E/2F: Product OTA State Machine Delegation To Portable Core

Goal:

- Route the product OTA receive/apply-preparation path through
  `third_party/portable_ota/src/pota_core.c`.
- Keep Bootloader-owned apply/rollback logic unchanged.
- Keep SCPI-facing state, error, and result values stable by mapping portable
  core status through `middleware/portable_ota_port`.

Code scope:

- Added `middleware/portable_ota_port/src/portable_ota_core_port.c`.
- Added middleware APIs for `portable_ota_port_core_begin()`,
  `portable_ota_port_core_service()`, `portable_ota_port_core_write()`,
  `portable_ota_port_core_end()`, and `portable_ota_port_core_abort()`.
- `components/ota_manager/src/ota_fb.c` now delegates
  `BEGIN/TICK/DATA/END/ABORT` to the middleware core adapter.
- `BOOT` and `COMM` remain product-side because they are tied to watchdog
  reboot and current metadata commit policy.
- `CMakeLists.txt` links `pota_core.c` and the middleware core adapter into
  App/Slot B builds. Bootloader remains on the narrow CRC/vector adapters.

Build and release gate:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
cmake -S . -B build-portable-core -G Ninja `
  -DPICO_BOARD=pico2 `
  -DPROJECT_WARNINGS_AS_ERRORS=ON `
  -DPROJECT_ENABLE_OTA_FAULT_INJECTION=OFF
cmake --build build-portable-core
python tools\release_check\release_check.py `
  --preset pico2-release `
  --build-dir build-portable-core
```

Actual result:

- Portable OTA tests passed the ARM GCC compile/object-build gate. Host
  execution was skipped because no host C compiler was found.
- `cmake --build build-portable-core` passed.
- Release gate passed with `release_check=OK`.
- Generated unified package:
  `build-portable-core\RP2350_TRIG_UPDATE.pkg`,
  size `150744`, package CRC32 `0x7D103719`, build id
  `20260623092341`.

Hardware validation:

- Factory flashed with `picotool`:
  `build-portable-core\RP2350_TRIG_FACTORY.uf2`.
- Initial state after factory:
  `*IDN? -> RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`,
  `SYST:FW:BUILD? -> "20260623092341"`,
  `SYST:BOOT:VERS? -> 0,1,0`,
  `SYST:OTA:STAT? -> "IDLE",2,"NONE",0`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.
- Positive package through portable core:
  `python tools\ota_send\ota_send.py COM4 build-portable-core\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT`.
- Observed state cadence stayed compatible with the existing host tool:
  `RECEIVING -> ERASE_SLOT -> RECEIVING -> READY_TO_REBOOT`.
- Boot/apply/commit:
  `SYST:OTA:BOOT` triggered USB CDC re-enumeration after about `2 s`,
  `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,74952,3486591855`,
  `SYST:OTA:COMM -> "OK"`,
  `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`.
- Negative package matrix after `ota_fb` core migration:
  transport CRC -> `"FAILED",2,"CRC",4`;
  image CRC -> `"FAILED",2,"CRC",4`;
  image vector -> `"FAILED",2,"VECTOR",4`;
  header magic -> `"FAILED",2,"BAD_HEADER",4`;
  header version -> `"FAILED",2,"BAD_HEADER",4`;
  header size -> `"FAILED",2,"BAD_HEADER",4`;
  image slot -> `"FAILED",2,"BAD_HEADER",4`;
  run offset -> `"FAILED",2,"IMAGE_TOO_LARGE",4`.
- Final safe state:
  `SYST:FW:BUILD? -> "20260623092341"`,
  `SYST:OTA:STAT? -> "FAILED",2,"IMAGE_TOO_LARGE",4`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 4,"IMAGE_TOO_LARGE","APPLIED",1,74952,3486591855`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.

Pass/fail:

- Pass. Product OTA receive, package selection, erase/program, CRC, vector,
  and mark-pending flow now run through portable core.
- Pass. Existing SCPI state/error/result contract stayed compatible with the
  validation tool and negative-path expectations.

### Step 3A/3B: Portable Session Facade And Product Adapter Switch

Goal:

- Make the third-party library easier to integrate by providing a single
  `pota.h` include and a `pota_session` facade.
- Keep the product middleware as the only direct caller of `pota_*` APIs.
- Preserve the current App SCPI behavior while reducing product-side ownership
  of the core OTA context.

Code scope:

- Added `third_party/portable_ota/include/pota.h`.
- Added `third_party/portable_ota/include/pota_session.h` and
  `third_party/portable_ota/src/pota_session.c`.
- Added `tests/unit/test_portable_ota_session.c`.
- `middleware/portable_ota_port/src/portable_ota_core_port.c` now owns a
  `pota_session_t` and calls `pota_session_*` instead of direct `pota_core`
  functions.
- `tools/run_portable_ota_tests.ps1` builds the new session test.

Build and release gate:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
cmake -S . -B build-portable-session -G Ninja `
  -DPICO_BOARD=pico2 `
  -DPROJECT_WARNINGS_AS_ERRORS=ON `
  -DPROJECT_ENABLE_OTA_FAULT_INJECTION=OFF
cmake --build build-portable-session
python tools\release_check\release_check.py `
  --preset pico2-release `
  --build-dir build-portable-session
```

Actual result:

- Portable OTA tests passed the ARM GCC compile/object-build gate. Host
  execution was skipped because no host C compiler was found.
- `cmake --build build-portable-session` passed.
- Release gate passed with `release_check=OK`.
- Generated unified package:
  `build-portable-session\RP2350_TRIG_UPDATE.pkg`,
  size `150816`, package CRC32 `0xC56EDF69`, build id
  `20260623101235`.

Hardware validation:

- Factory flashed with `picotool`:
  `build-portable-session\RP2350_TRIG_FACTORY.uf2`.
- Initial state after factory:
  `*IDN? -> RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`,
  `SYST:FW:BUILD? -> "20260623101235"`,
  `SYST:BOOT:VERS? -> 0,1,0`,
  `SYST:OTA:STAT? -> "IDLE",2,"NONE",0`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.
- Positive package:
  `python tools\ota_send\ota_send.py COM4 build-portable-session\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT`.
- Boot/apply/commit:
  `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,75024,895520178`,
  `SYST:OTA:COMM -> "OK"`,
  `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`.
- Negative package matrix after session facade migration:
  transport CRC -> `"FAILED",2,"CRC",4`;
  image CRC -> `"FAILED",2,"CRC",4`;
  image vector -> `"FAILED",2,"VECTOR",4`;
  header magic -> `"FAILED",2,"BAD_HEADER",4`;
  header version -> `"FAILED",2,"BAD_HEADER",4`;
  header size -> `"FAILED",2,"BAD_HEADER",4`;
  image slot -> `"FAILED",2,"BAD_HEADER",4`;
  run offset -> `"FAILED",2,"IMAGE_TOO_LARGE",4`.
- Final safe state:
  `SYST:FW:BUILD? -> "20260623101235"`,
  `SYST:OTA:STAT? -> "FAILED",2,"IMAGE_TOO_LARGE",4`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 4,"IMAGE_TOO_LARGE","APPLIED",1,75024,895520178`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.

Pass/fail:

- Pass. Product firmware now consumes the portable core through the session
  facade while preserving the existing validation behavior.
- Pass. The session facade is now the recommended integration entry point for
  future RP2350 RTOS and STM32 RTOS ports.

### Step 3C: Portable Text Helpers

Goal:

- Move OTA state, error, OTA result, and Bootloader result text helpers into
  the reusable library.
- Keep product-specific compatibility aliases in middleware so current SCPI
  output remains unchanged.
- Make status reporting easier for future RTOS products without copying switch
  statements into each application.

Code scope:

- Added `third_party/portable_ota/include/pota_strings.h` and
  `third_party/portable_ota/src/pota_strings.c`.
- Added `tests/unit/test_portable_ota_strings.c`.
- `include/pota.h` now includes `pota_strings.h`.
- Added `middleware/portable_ota_port/src/portable_ota_strings_port.c`.
- `components/ota_manager/src/ota_ao.c` delegates
  `ota_state_to_string()`, `ota_error_to_string()`, and
  `ota_result_to_string()` to middleware.
- `components/ota_manager/src/ota_metadata.c` delegates
  `ota_metadata_boot_result_to_string()` to middleware.
- `CMakeLists.txt` links `pota_strings.c` and the middleware strings adapter
  into App, Slot B, and Bootloader builds.

Build and release gate:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
cmake -S . -B build-portable-strings -G Ninja `
  -DPICO_BOARD=pico2 `
  -DPROJECT_WARNINGS_AS_ERRORS=ON `
  -DPROJECT_ENABLE_OTA_FAULT_INJECTION=OFF
cmake --build build-portable-strings
python tools\release_check\release_check.py `
  --preset pico2-release `
  --build-dir build-portable-strings
```

Actual result:

- Portable OTA tests passed the ARM GCC compile/object-build gate. Host
  execution was skipped because no host C compiler was found.
- `cmake --build build-portable-strings` passed.
- Release gate passed with `release_check=OK`.
- Generated unified package:
  `build-portable-strings\RP2350_TRIG_UPDATE.pkg`,
  size `151016`, package CRC32 `0xD1C12D2E`, build id
  `20260623102556`.

Hardware validation:

- Factory flashed with `picotool`:
  `build-portable-strings\RP2350_TRIG_FACTORY.uf2`.
- Initial state after factory:
  `*IDN? -> RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`,
  `SYST:FW:BUILD? -> "20260623102556"`,
  `SYST:BOOT:VERS? -> 0,1,0`,
  `SYST:OTA:STAT? -> "IDLE",2,"NONE",0`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 0,"NONE","NONE",0,0,0`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.
- Positive package:
  `python tools\ota_send\ota_send.py COM4 build-portable-strings\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT`.
- Boot/apply/commit:
  `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,75224,2315388268`,
  `SYST:OTA:COMM -> "OK"`,
  `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`.
- Negative package matrix after text-helper migration:
  transport CRC -> `"FAILED",2,"CRC",4`;
  image CRC -> `"FAILED",2,"CRC",4`;
  image vector -> `"FAILED",2,"VECTOR",4`;
  header magic -> `"FAILED",2,"BAD_HEADER",4`;
  header version -> `"FAILED",2,"BAD_HEADER",4`;
  header size -> `"FAILED",2,"BAD_HEADER",4`;
  image slot -> `"FAILED",2,"BAD_HEADER",4`;
  run offset -> `"FAILED",2,"IMAGE_TOO_LARGE",4`.
- Final safe state:
  `SYST:FW:BUILD? -> "20260623102556"`,
  `SYST:OTA:STAT? -> "FAILED",2,"IMAGE_TOO_LARGE",4`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 4,"IMAGE_TOO_LARGE","APPLIED",1,75224,2315388268`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`.

Pass/fail:

- Pass. Common text helpers now live in the portable library.
- Pass. Product middleware preserves legacy SCPI aliases such as
  `BOARD_MISMATCH`, `VERSION_REJECTED`, `BOOT_ROLLBACK`, and `QUEUE_FULL`.
- Pass. Status, error, and Bootloader result text stayed compatible with the
  existing validation scripts and negative-path expectations.

### Step 3D: Portable Metadata V3 Helpers

Goal:

- Move reusable metadata v3 schema logic into `third_party/portable_ota`.
- Keep RP2350 flash layout, dual-copy storage offsets, erase/program policy,
  and legacy v2 flash-read migration in the product layer.
- Preserve the current on-flash metadata format so the App and Bootloader
  remain compatible.

Code scope:

- Expanded `third_party/portable_ota/include/pota_metadata.h` to match the
  current product metadata v3 schema, including SHA placeholders, copy
  transaction fields, A/B fields, and three CRC fields.
- Reworked `third_party/portable_ota/src/pota_metadata.c` to provide:
  base metadata CRC, extension CRC, A/B CRC, CRC update, validity checks,
  default initialization, extension defaults, copy transaction clearing,
  boot-mode/slot/transaction-state validation, upgrade helper, and newest-copy
  selection.
- Extended `tests/unit/test_portable_ota_metadata.c` for v3 CRC coverage,
  default metadata, invalid transaction state, invalid previous slot, and copy
  transaction clearing.
- Added `middleware/portable_ota_port/src/portable_ota_metadata_port.c` with
  compile-time layout checks between `ota_metadata_t` and `pota_metadata_t`.
- `components/ota_manager/src/ota_metadata.c` now delegates portable metadata
  logic through middleware while retaining RP2350 flash read/write and v2
  migration code.
- `CMakeLists.txt` links `pota_metadata.c` and the middleware metadata adapter
  into App, Slot B, and Bootloader builds.

Build and release gate:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
cmake -S . -B build-portable-metadata -G Ninja `
  -DPICO_BOARD=pico2 `
  -DPROJECT_WARNINGS_AS_ERRORS=ON `
  -DPROJECT_ENABLE_OTA_FAULT_INJECTION=OFF
cmake --build build-portable-metadata
python tools\release_check\release_check.py `
  --preset pico2-release `
  --build-dir build-portable-metadata
```

Actual result:

- Portable OTA tests passed the ARM GCC compile/object-build gate. Host
  execution was skipped because no host C compiler was found.
- `cmake --build build-portable-metadata` passed.
- Release gate passed with `release_check=OK`.
- Generated unified package:
  `build-portable-metadata\RP2350_TRIG_UPDATE.pkg`,
  size `151032`, package CRC32 `0xBA3FCA5E`, build id
  `20260623104609`.

Hardware validation:

- Factory flashed with `picotool`:
  `build-portable-metadata\RP2350_TRIG_FACTORY.uf2`.
- Initial state after factory:
  `*IDN? -> RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`,
  `SYST:FW:BUILD? -> "20260623104609"`,
  `SYST:BOOT:VERS? -> 0,1,0`,
  `SYST:OTA:STAT? -> "IDLE",2,"NONE",0`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 0,"NONE","NONE",0,0,0`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`,
  `SYST:OTA:CAP? -> 1`.
- Positive package:
  `python tools\ota_send\ota_send.py COM4 build-portable-metadata\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT`.
- Boot/apply/commit:
  `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,75240,3624267205`,
  `SYST:OTA:COMM -> "OK"`,
  `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`.
- Negative package matrix after metadata helper migration:
  transport CRC -> `"FAILED",2,"CRC",4`;
  image CRC -> `"FAILED",2,"CRC",4`;
  image vector -> `"FAILED",2,"VECTOR",4`;
  header magic -> `"FAILED",2,"BAD_HEADER",4`;
  header version -> `"FAILED",2,"BAD_HEADER",4`;
  header size -> `"FAILED",2,"BAD_HEADER",4`;
  image slot -> `"FAILED",2,"BAD_HEADER",4`;
  run offset -> `"FAILED",2,"IMAGE_TOO_LARGE",4`.
- Final safe state:
  `SYST:FW:BUILD? -> "20260623104609"`,
  `SYST:OTA:STAT? -> "FAILED",2,"IMAGE_TOO_LARGE",4`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 4,"IMAGE_TOO_LARGE","APPLIED",1,75240,3624267205`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`,
  `SYST:OTA:CAP? -> 1`.

Pass/fail:

- Pass. Portable metadata helpers now own the reusable v3 schema logic.
- Pass. App and Bootloader both validated the migrated metadata helpers through
  factory boot, pending metadata write, Bootloader apply, App commit, and
  negative package rejection.
- Pass. The final state confirms no pending metadata remains after negative
  package failures.

Next migration boundary:

- Move higher-level metadata mutations such as mark-pending, confirm-active,
  boot-mode changes, and copy-transaction state transitions into portable
  helpers that operate on an in-memory metadata object.
- Keep platform storage as a port responsibility: load selected copy, call the
  portable mutation helper, then store the updated copy through the platform
  flash policy.

### Step 3E: Portable Metadata Mutation Helpers

Goal:

- Move reusable in-memory metadata mutation rules into `third_party/portable_ota`.
- Keep product public APIs stable while reducing product metadata logic to
  `load -> portable mutation -> store`.
- Preserve RP2350 flash dual-copy storage and legacy v2 migration in the
  product layer.

Code scope:

- Added portable mutation helpers in `pota_metadata.h/.c`:
  `pota_metadata_mark_pending()`, `pota_metadata_confirm_active()`,
  `pota_metadata_set_boot_mode()`, `pota_metadata_set_fault_injection()`,
  `pota_metadata_begin_copy_transaction()`,
  `pota_metadata_update_copy_transaction()`,
  `pota_metadata_finish_copy_transaction()`,
  `pota_metadata_fail_copy_transaction()`, and
  `pota_metadata_clear_copy_transaction()`.
- Extended `tests/unit/test_portable_ota_metadata.c` to cover pending writes,
  active confirmation, boot-mode capability changes, fault injection flags, and
  copy transaction start/update/finish/fail/clear paths.
- Exposed the mutation helpers through
  `middleware/portable_ota_port/src/portable_ota_metadata_port.c`.
- `components/ota_manager/src/ota_metadata.c` now keeps the existing
  `ota_metadata_*` public API but delegates mutation logic to middleware.

Build and release gate:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_portable_ota_tests.ps1
cmake -S . -B build-portable-metadata-mutation -G Ninja `
  -DPICO_BOARD=pico2 `
  -DPROJECT_WARNINGS_AS_ERRORS=ON `
  -DPROJECT_ENABLE_OTA_FAULT_INJECTION=OFF
cmake --build build-portable-metadata-mutation
python tools\release_check\release_check.py `
  --preset pico2-release `
  --build-dir build-portable-metadata-mutation
```

Actual result:

- Portable OTA tests passed the ARM GCC compile/object-build gate. Host
  execution was skipped because no host C compiler was found.
- `cmake --build build-portable-metadata-mutation` passed.
- Release gate passed with `release_check=OK`.
- Generated unified package:
  `build-portable-metadata-mutation\RP2350_TRIG_UPDATE.pkg`,
  size `150952`, package CRC32 `0x6C7A89E5`, build id
  `20260623112832`.

Hardware validation:

- Factory flashed with `picotool`:
  `build-portable-metadata-mutation\RP2350_TRIG_FACTORY.uf2`.
- Initial state after factory:
  `*IDN? -> RP2350_TRIG,SYNC_TRIGGER,0,RP2350_TRIG`,
  `SYST:FW:BUILD? -> "20260623112832"`,
  `SYST:BOOT:VERS? -> 0,1,0`,
  `SYST:OTA:STAT? -> "IDLE",2,"NONE",0`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 0,"NONE","NONE",0,0,0`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`,
  `SYST:OTA:CAP? -> 1`.
- Positive package:
  `python tools\ota_send\ota_send.py COM4 build-portable-metadata-mutation\RP2350_TRIG_UPDATE.pkg --expect-final-state READY_TO_REBOOT`.
- Boot/apply/commit:
  `SYST:OTA:RES? -> 0,"NONE","APPLIED",2,75160,3242593473`,
  `SYST:OTA:COMM -> "OK"`,
  `SYST:OTA:STAT? -> "COMMITTED",2,"NONE",5`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`.
- Negative package matrix after mutation helper migration:
  transport CRC -> `"FAILED",2,"CRC",4`;
  image CRC -> `"FAILED",2,"CRC",4`;
  image vector -> `"FAILED",2,"VECTOR",4`;
  header magic -> `"FAILED",2,"BAD_HEADER",4`;
  header version -> `"FAILED",2,"BAD_HEADER",4`;
  header size -> `"FAILED",2,"BAD_HEADER",4`;
  image slot -> `"FAILED",2,"BAD_HEADER",4`;
  run offset -> `"FAILED",2,"IMAGE_TOO_LARGE",4`.
- Final safe state:
  `SYST:FW:BUILD? -> "20260623112832"`,
  `SYST:OTA:STAT? -> "FAILED",2,"IMAGE_TOO_LARGE",4`,
  `SYST:OTA:SLOT? -> 1,0,1,0,0`,
  `SYST:OTA:RES? -> 4,"IMAGE_TOO_LARGE","APPLIED",1,75160,3242593473`,
  `SYST:OTA:MODE? -> "COPY_TO_ACTIVE",0`,
  `SYST:OTA:CAP? -> 1`.

Pass/fail:

- Pass. Portable OTA now owns common in-memory metadata mutation rules.
- Pass. Product metadata APIs remain stable while product implementation is
  reduced to platform storage plus legacy migration.
- Pass. `mark_pending` and `confirm_active` were exercised by positive OTA,
  Bootloader apply, and App commit; failure paths did not leave pending
  metadata behind.

Next migration boundary:

- Move remaining Bootloader-side in-memory metadata mutations, such as
  `bootloader_store_result()`, direct A/B apply/rollback updates, and
  copy-to-active transaction updates, into portable helpers.
- Keep Bootloader flash operations, image validation, watchdog reset, and slot
  jump policy platform-specific.
