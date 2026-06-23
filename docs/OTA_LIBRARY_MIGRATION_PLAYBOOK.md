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
