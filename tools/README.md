# Tools

Place build, flashing, release packaging, production test, and diagnostics
tools in this directory.

The current layout is grouped by function:

- `tools/bench/`: bench GUI and ad-hoc trigger helpers
- `tools/checks/`: repository guards and namespace checks
- `tools/tests/`: host/ARM fallback test runners
- feature-specific subdirectories such as `tools/ota_send/` and
  `tools/sd_board_validate/`

## Test Layering

- Standard unit tests live under `tests/python/` and run with `python -m pytest`.
  They cover pure host-side logic from tools, package builders, decoders, and
  repository checks.
- Hardware-in-the-loop pytest wrappers live under `tests/hil/`. They are skipped
  by default and only open a serial port when `--run-hil --hil-port COMx` is
  provided. HIL tests must use the shared pytest serial lifecycle fixture rather
  than opening `serial.Serial` directly.
- `tools/*_validate/` scripts remain board validation and production smoke
  runners. They may use serial, VISA, picotool, or generated build artifacts and
  should not be imported by default unit tests unless guarded by a HIL fixture.

Legacy root-level wrappers remain in `tools/` for backward compatibility while
the docs and scripts move over to the new paths.

Read Markdown and Python files as UTF-8 on Windows:

```powershell
Get-Content -Path tools\README.md -Encoding UTF8
```

## Closed-Loop Validation Tools

- `ota_send/ota_send.py`: main board OTA validation sender. It sends raw `.bin`
  or unified `.pkg` images over SCPI USB CDC, auto-detects package mode, asserts
  final state with `--expect-final-state`, asserts OTA error text with
  `--expect-error`, and supports package negative-path mutations through
  `--package-negative`.
- `ota_boot_commit/ota_boot_commit.py`: OTA post-transfer helper. It sends
  `SYSTem:OTA:BOOT`, tolerates the expected USB CDC reset/re-enumeration,
  reopens the port, verifies build/slot/error state, and sends
  `SYSTem:OTA:COMMit`.
- `ota_multi_update/ota_multi_update.py`: multi-board OTA orchestrator. It
  enumerates USB CDC ports, probes `*IDN?` / `SYSTem:FW:BUILD?`, filters online
  DHRT100/RP2350_TRIG boards, then runs `ota_send.py` and `ota_boot_commit.py`
  in parallel with one worker per discovered board by default. Unified package
  build id is read from the package header and used for commit verification.
  Product-ring updates support 1–8 unique addresses; use
  `--expected-board-count 4` plus repeated `--serial-number` values to make a
  four-board, all-at-once update fail closed before Flash writes if any board
  is missing or duplicated.
- `scpi_legacy_validate/scpi_legacy_validate.py`: removed-command validator.
  It sends each legacy command one by one, then queries `SYSTem:ERRor?` and
  requires `-113,"Undefined header"` for every entry. Use this instead of
  hand-written one-off serial scripts when removing SCPI aliases.
- `release_check/release_check.py`: release gate. It verifies release preset
  safety switches, required artifacts, and absence of OTA fault-injection
  command strings in release artifacts.
- `factory_package/factory_package.py`: builds a deterministic, externally
  signed factory-recovery package from the v2 baseline report and verifies its
  map identity, full-erase policy, region hashes, CRC, and role-bound P-256
  signature. It never reads or generates private keys.
- `factory_restore/factory_restore.py`: read-only by default; verifies the
  signed package against UF2 target addresses and only calls the existing
  full-erase picotool workflow when `--execute` is explicitly supplied.
- `artifact_checksum/artifact_checksum.py`: generates and verifies a
  deterministic SHA-256/size manifest for a factory or recovery artifact; it
  never writes firmware.
- `ota_keys/generate_ota_key.py`: creates a local-only P-256 debug signing key
  and registers only its public key/profile entry; release builds must select a
  separate release profile and never package the private key.
- `docs_check/docs_check.py`: documentation gate. It checks Markdown metadata,
  `docs/README.md` index coverage, conflict markers, broken `docs/*.md`
  references, and filename conformance. Existing legacy names warn by default;
  use `--strict-names` for new-file enforcement.
- `cmake_build_auto/cmake_build_auto.py`: CMake configure/build wrapper for
  drive-letter moves. It checks `CMakeCache.txt`; if the cached source/build
  path points at another workspace location such as `D:` versus `E:`, it removes
  only stale CMake metadata in that build directory and reconfigures from the
  selected preset before building.
- `ota_board_validate/ota_board_validate.py`: one-command board validation
  runner. It runs `release_check`, optionally flashes the factory UF2, queries
  baseline SCPI state, sends a positive OTA package, triggers `BOOT/COMM`, runs
  the negative package matrix, and writes `summary.json`, `summary.txt`, serial
  query files, and per-step logs under a validation output directory.
- `multicore_board_validate/multicore_board_validate.py`: RTOS + AMP smoke
  runner. It checks `*IDN?`, `SYST:FW:BUILD?`, `SYST:CORE?`, `SYST:LOOP:STAT?`,
  `SYST:SYNC:VDC:STAT?`, `SYST:SYNC:VDC:DPLL:STAT?`, `SYSTem:CONFigure:STAT?`,
  `SYSTem:REFMEM:CLAIM?`, static `SYSTem:CONFigure:*?` config queries,
  Trigger arm/disarm, `SYST:ERR?`, `SYST:LOG:STAT?`, and `SYST:TRAC:LAST?`,
  then writes a board validation summary under `build-rtos-multicore-smoke/`.
- `refmem_network_validate/refmem_network_validate.py`: two minimal-system board
  RefMem/VDC baseline runner. It opens two serial ports with explicit lifecycle
  management, checks identity/build/core/VDC/DPLL/config/SlotClaim snapshots,
  verifies default SlotClaim evidence is empty, and can compare build id and
  SlotClaimMap CRC before later RJ45 `CLAIM_*` tests are enabled.
- `tdma_ring_monitor/flight_bitmap_validate.py`: read-only 2..8 board cyclic
  process-image validator. Boards are discovered by the unique address returned
  from `*IDN?`, never by COM number. It correlates core1 bitmap scan/hit/drop,
  cross-core FIFO counters, and core0 RefMem mailbox accept/reject counters over
  a fixed window, then writes `summary.json` under `build-validation/`.
- `calibration_ring_validate/calibration_ring_topology.py`: Calibration-owned
  directed link-adjacency and closed-ring-order measurement. It uses TDMA only
  as the isolated probe transport and can commit/read back NO labels after the
  topology is accepted.
- `calibration_ring_validate/calibration_clk_train.py`: Calibration-owned
  first-stage CLK RTT bracket acquisition across each ring anchor.
- `calibration_ring_validate/calibration_clk_codebook_eval.py`: deterministic
  Calibration CLK marker evaluator. It generates maximal-length LFSR sequences,
  applies NRZ/Manchester/differential-Manchester waveform encoding, and compares
  adjacent/raw-sample lag discrimination inside the coarse RTT search window.
- `calibration_ring_validate/calibration_path_delay_probe.py`: read-only active
  Calibration path-delay and underlying TDMA transport status collector.
- `vdc_observer_validate/vdc_observer_validate.py`: VDC raw capture observer
  maintenance runner. It opens one or more CDC ports, verifies the safe disable
  path, enables the observer with a minimal explicit config, asserts the 40-field
  `SYSTem:SYNC:VDC:OBServer?` evidence layout, checks schedule/dictionary CRCs,
  disables again, and records a transcript.
- `vdc_latch_validate/vdc_latch_validate.py`: Sync IO core1 capture latch smoke
  runner. It starts capture through the realtime SCPI maintenance path, reads
  `REALtime:IO:SAMPle:LATCh?`, verifies the hardware-tick diagnostic timestamp
  source/resolution/flags, forces one observer edge, verifies that the VDC gate
  rejects it as diagnostic-only, stops capture, and records a transcript.
- `vdc_lock_readiness_validate/vdc_lock_readiness_validate.py`: VDC/DPLL minimum
  instance readiness runner. It configures the observer through
  `SYSTem:SYNC:VDC:OBServer:TDMA`, starts Sync IO capture, queries
  `SYSTem:SYNC:VDC:LOCK:READiness?`, and verifies the current diagnostic path
  remains blocked at `TIMESTAMP_NOT_ELIGIBLE` instead of reporting a false
  lock.
- `dpll_vdc_monitor/dpll_vdc_monitor.py`: read-only multi-board VDC/DPLL
  monitor. It polls NO1..NO8 (including the NO5 observer), validates the
  core1-owned RefMem vectors and TDMA simultaneous-feedback/timestamp gates,
  and writes CSV/JSON/SVG reports under `out/`. Existing SD raw-waveform
  analysis JSON can be attached with `--waveform-analysis`; the monitor never
  arms, transmits, changes calibration, or writes the SD card.
- `dpll_observation_capture/dpll_observation_capture.py`: maintenance-side
  DPLL residual capture runner. It arms the fixed SRAM recorder, waits for the
  requested interval without polling, stops all boards, queues StorageAO SD
  writes, downloads the immutable binary captures, and invokes the offline
  decoder/residual SVG analyzer. It never adds work to the TDMA short frame or
  the Core1/PIO real-time path.
- `analyzer_no5_correlator/analyzer_no5_correlator.py`: offline-only evidence
  join for a decoded local SYNC_IO analyzer segment and a decoded NO5 SMA
  waveform. It aligns absolute hardware-time records, retains analyzer
  capture and NO5 sample sequence domains separately, and marks the two
  evidence sources as non-substitutable.
- `analyzer_trace_batch_index/analyzer_trace_batch_index.py`: offline batch
  index for persisted analyzer `.bin` segments. It verifies per-file CRCs,
  groups sessions, and emits explicit record-sequence/header drop intervals;
  it does not infer external waveform health.
- `vdc_tdma_selftest_validate/vdc_tdma_selftest_validate.py`: VDC TDMA
  self-test runner. It starts `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest` on each
  board, then checks `SYSTem:SYNC:VDC:TDMA:STATus?` for a completed
  `VDC_SYNC_SAMPLE` short-frame intent. Current evidence must remain
  `SOFTWARE_US / 1000 ns / DIAGNOSTIC_ONLY`; this validates scheduler/payload
  mounting, not DPLL lock.
- `refmem_sync_hil_validate/refmem_sync_hil_validate.py`: two-board RefMem Sync
  HELLO/EPOCH/DELTA/ACK_NACK/FENCE/QUALITY runner. It initializes each board's
  `SYSTem:REFMEM:SYNC` maintenance context, checks build/SlotClaim/adapter
  snapshots, moves HELLO, EPOCH, u32 DELTA, ACK_NACK, FENCE, and QUALITY hex
  frames through SCPI or USBTMC, then checks `MIRRor?`, `ACK:STATus?`,
  `FENCe:STATus?`, `QUALity:STATus?`, `PEER?`, and `QUALity?`. Negative paths
  cover duplicate seq, target mismatch, payload CRC mismatch, FENCE min-seq
  timeout, and remote quality error propagation before the real PIO SPI physical
  adapter is enabled. Use `--package-crc`, `--line-remap-a-to-b`,
  `--line-remap-b-to-a`, and `--preflight-io` to record package/remap metadata
  and run `two_board_io_validate.py` before opening the sync exchange ports.

### `two_board_io_validate/two_board_io_validate.py`

Checks the active SYNC_IO wiring between two minimum-system boards. The tool
opens both serial ports with explicit lifecycle management, queries
`REALtime:IO:PROFile?`, drives one `REALtime:IO:OUTPut:MASK` bit at a time, and
reads the opposite board with `REALtime:IO:INPut:LEVel?`.

```powershell
python tools\two_board_io_validate\two_board_io_validate.py --port-a COM6 --port-b COM7
```

### `debug_model_overlay_validate/debug_model_overlay_validate.py`

Checks the GPIO4..7 minimum-system model overlay between X/Y boards. The tool
opens both serial ports with explicit lifecycle management, verifies
`REALtime:IO:MODel:PROFile?` reports UART disabled, releases both boards, then
drives X GPIO4, X GPIO5, Y GPIO6, and X GPIO7 one at a time while reading the
opposite board with `REALtime:IO:MODel:INPut:LEVel?`.

```powershell
python tools\debug_model_overlay_validate\debug_model_overlay_validate.py --port-x COM3 --port-y COM4
```
- `tests/run_portable_ota_tests.ps1`: portable OTA library gate. It builds or runs
  `third_party/portable_ota` unit tests. If no host C compiler exists, it falls
  back to ARM GCC compile/object-build checks and reports that host execution
  was skipped.
- `tests/run_host_unit_tests.ps1`: host assertion gate for the product C unit
  tests. It resolves `gcc` from `PATH` by default, then runs the BiSS, portable
  LOG/OTA, and RefMem unit test scripts as executable host assertions. Use this
  gate when the target machine has MinGW GCC installed;
  the individual scripts may otherwise fall back to ARM compile-only checks.
- `tests/run_drv_flash_lockout_tests.ps1`: S0 Flash/Core1 lockout gate. It
  executes the pure C lockout state machine on host, including request/ACK,
  PARKED/release, supported-but-offline rejection, and no-ACK fault injection.
- `flash_lockout_hil_validate/flash_lockout_hil_validate.py`: S0 board HIL gate.
  It performs a real OTA package write, boots and commits the image, then checks
  `SYSTem:PROTection:STATus?` evidence fields so request/ACK/release sequence
  counters grow, timeout counters stay unchanged, and `last_result` is ACKED.
- `tests/run_portable_log_tests.ps1`: portable LOG core gate. It builds or runs
  `third_party/portable_log` unit tests. If no host C compiler exists, it falls
  back to ARM GCC compile/object-build checks and reports that host execution
  was skipped.
- `tests/run_refmem_slot_claim_tests.ps1`: RefMem SlotClaim core gate. It builds
  or runs the pure C SlotClaim map/gate tests, including nominal assignment,
  duplicate claim, UUID mismatch, and candidate overflow. If no host C compiler
  exists, it falls back to ARM GCC compile/object-build checks.
- `tests/run_refmem_table_registry_tests.ps1`: RefMem TableRegistry image gate.
  It builds or runs pure C tests for active/staging/rollbackable descriptors,
  activation gate rejection, successful activation, and invalid staging
  rejection. If no host C compiler exists, it falls back to ARM GCC
  compile/object-build checks.
- `ota_packager/ota_packager.py`: builds a unified OTA package from Slot A and
  Slot B linked App binaries. CMake normally invokes this automatically.
- `sd_fs_build/sd_fs_build.py`: builds the SD-card filesystem staging tree under
  `build/sdcard/`, copies the unified OTA package to `/update/`, keeps raw `.bin`
  compatibility payloads under `/update/compat/`, writes `manifest.json` and
  firmware-readable `manifest.idx`, and creates `build/RP2350_TRIG_SDCARD.zip`.
- `refmem_pack_build/refmem_pack_build.py`: builds a minimal RefMem table image
  package under `/refmem/` for System Pack staging format validation. It writes
  `app_model.rmtp`, `app_model.idx`, and `app_model.json`; board-side parsing is
  intentionally staged behind the RefMem TLV parser work.
- `refmem_pack_write/refmem_pack_write.py`: uploads `/refmem/app_model.rmtp`
  through generic `SYSTem:STORage:FILE:WRITe:*` SCPI commands, reads back the
  first bytes with `SYSTem:STORage:FILE:READ?`, and can run
  `SYSTem:REFMEM:LOAD:SD` for the RefMem staging gate.
- `refmem_table_registry_validate/refmem_table_registry_validate.py`: validates
  RMTP package/table CRCs against `SYSTem:REFMEM:TABle?`; pass `--activate` to
  verify `SYSTem:REFMEM:LOAD:ACTivate` and active image descriptor switching.
- `storage_scpi_validate/storage_scpi_validate.py`: board-side validation for
  generic StorageAO file and directory CRUD. It covers directory create,
  catalog, rename and delete, plus file write transaction, info, readback,
  rename and delete.
- `sd_board_validate/sd_board_validate.py`: board-side SD validation over SCPI.
  It does not flash firmware; it checks `SYST:SD:*`, `SYST:STOR:*`, root and
  key directory catalogs, path-denial behavior, boot/arm/fault snapshot
  behavior, StorageAO `MANIFEST_SCAN`, `FILE_INFO`, `FILE_READ`,
  `CATALOG_PAGE`, `SNAPSHOT_WRITE`, and `FAULT_EVIDENCE` job completion, fault
  trace `.bin/.idx`, fault report behavior, and reads back the latest fault
  trace via `MMEM:READ?` for decoder checks. The decoded trace must include
  management-plane trigger configuration events, Trigger resource snapshots,
  `sync_io.seq_runtime`, SEQ PIO state, DMA restart, DMA overflow baseline,
  and AUX/READY/REDY baseline/timeout latch events; flashing remains a
  separate picotool/script step. The tool writes
  `summary.*`, `queries.txt`, and `trace_readback\` under
  `build/sd_validation_*`. Use `--validate-trigger-release` for SYNC_IO
  release-path validation; it exercises armed SEQ_STEP `*RST` and `TRIG:FAULT`,
  then requires decoded `trigger.resource_release` records for `PIO1`/`DMA`.
  Use `--validate-resource-owner` for SYNC_IO owner-boundary validation; it
  arms/disarms SEQ_STEP, ENC_COUNT, and BISS_TAP, then asserts `SYST:RES?`
  contains the expected mode resources while armed and releases them after
  disarm.
- `sd_trace_decode/sd_trace_decode.py`: offline decoder for SD trace `.bin`
  files. It verifies header, event CRC, and optional `.idx` metadata, then
  emits JSON or CSV with decoded domain/event/severity names and details such
  as trigger state transitions, source/edge/gate/safe configuration changes,
  Trigger resource snapshots, SyncIO runtime flags, rollover progress, DMA
  overflow baseline/latched status, AUX0..AUX3 snapshots, READY/REDY masks,
  timeout latch status, and trace file CRC checks.
- `biss_board_validate/biss_board_validate.py`: board-side BiSS-C TAP smoke
  validation over SCPI. It configures the TAP profile, confirms query state,
  arms BiSS mode, optionally injects software frames for crossing checks, and
  disarms. Use `--enable-scan --expect-scan-steps N --capture-trace` to validate
  timeout sample-scan progress, fault trace readback, and decoded
  `trigger.biss_timeout` / `trigger.biss_scan_step` events.
- `product_scpi_validate/product_scpi_validate.py`: product SCPI framework
  validation over USB CDC or USBTMC/VISA. It generates its command list and
  fixed expected responses from `scpi_product_commands.h/.c`, runs every
  product command, and writes a transcript plus summary under
  `build/product_scpi_validation_*`.
- `sd_raw_clear/sd_raw_clear.py`: destructive SD recovery helper. It sends
  `SYST:SD:RAW:CLEAR <sectors>,"ERASE"` over SCPI after `--yes`, clearing the
  first 1..64 sectors so a host can recreate the partition/FAT metadata.
- `sd_mkfs/sd_mkfs.py`: destructive SD format helper. It sends
  `SYST:SD:MKFS "ERASE"` over SCPI after `--yes`, asking Pico to create a
  FAT/FAT32 filesystem on the inserted SD card, then reads raw sector 0 for
  write verification. It never formats or deletes a host PC drive.
- A FAT32 card can also be initialized by firmware without formatting:
  `SYST:SD:INIT` creates the minimum System Pack directories, default JSON
  files, `/manifest.idx`, and `/manifest.json` if `/manifest.idx` is missing.
  `SYST:SD:MAN?` runs the same non-destructive bootstrap automatically before
  rescanning a mounted card with no manifest.
- `ota_bin_info/ota_bin_info.py`: prints raw `.bin` size, CRC32, and
  `SYST:OTA:BEGIN` parameters for bench work.
- `uf2_join/uf2_join.py`: generates the first-time factory UF2 from Bootloader,
  Slot A App, and metadata-clear binaries. CMake normally invokes this
  automatically.
- `build_info/gen_build_info.py`: generates firmware build id source used by
  `SYST:FW:BUILD?`. CMake normally invokes this automatically.
- `bench/rp2350_tk_toolbox.py`: Tkinter bench GUI for common tool operations. It wraps
  release build/check, OTA send/board validation, trigger SCPI controls,
  frequency measurement, timing tests, and ad-hoc SCPI commands.

Launch the GUI from the repository root:

```powershell
python tools\bench\rp2350_tk_toolbox.py
```

Check documentation hygiene:

```powershell
python tools\docs_check\docs_check.py
python tools\docs_check\docs_check.py --strict-names
```

Build SD-card contents after a release build:

```powershell
python tools\sd_fs_build\sd_fs_build.py --build-dir build --output-dir build\sdcard --clean
```

Build with automatic CMake cache repair when switching the workspace between
drive letters:

```powershell
python tools\cmake_build_auto\cmake_build_auto.py --preset pico2-release --build-dir build
python tools\cmake_build_auto\cmake_build_auto.py --preset pico2-release --build-dir build-sd-verify
```

Copy the contents of `build\sdcard\` to the root of a FAT32 SD card. The
firmware reads `/manifest.idx`; `/manifest.json` is for PC tools and inspection.
The default offline OTA file is `/update/RP2350_TRIG_UPDATE.pkg`; raw `.bin`
files are kept only for compatibility under `/update/compat/`.

For a new FAT32 card used only for board-side SD validation, copying the PC
staging tree is no longer required. Insert the card and run `SYST:SD:INIT` or
`SYST:SD:MAN?`; the firmware creates the minimum System Pack structure without
formatting. The generated `/update/RP2350_TRIG_UPDATE.pkg` is only a placeholder
for manifest/catalog validation and must be replaced by a real release package
before offline OTA validation.

Validate SD-card behavior after firmware is already flashed and the prepared SD
card is inserted:

```powershell
python tools\sd_board_validate\sd_board_validate.py COM4
Get-Content -Encoding UTF8 build\sd_validation_*\summary.txt
```

Keep flashing separate from SD validation. A flashing script or picotool command
should only load the UF2; `sd_board_validate.py` should only verify the running
firmware through SCPI.

When fault trace validation runs, the tool writes the latest board-generated
trace files and decoder output under `trace_readback\`:

```powershell
Get-ChildItem build\sd_validation_*\trace_readback
Get-Content -Encoding UTF8 build\sd_validation_*\trace_readback\decoded_fault_trace.json
```

Decode a copied SD trace after validation or field capture:

```powershell
python tools\sd_trace_decode\sd_trace_decode.py traces\fault\fault_000001.bin `
  --idx traces\fault\fault_000001.idx `
  --output build\fault_000001_trace.json
python tools\sd_trace_decode\sd_trace_decode.py traces\fault\fault_000001.bin --csv
```

## OTA Board Validation Loop

Run COM-port tests serially; only one process may own the USB CDC port.

Preferred full validation:

```powershell
python tools\ota_board_validate\ota_board_validate.py COM4 build-portable-port-merge
Get-Content -Encoding UTF8 build-portable-port-merge\ota_validation_*\summary.txt
```

Useful options:

```powershell
python tools\ota_board_validate\ota_board_validate.py COM4 build-portable-port-merge --skip-flash
python tools\ota_board_validate\ota_board_validate.py COM4 build-portable-port-merge --skip-negative
python tools\ota_board_validate\ota_board_validate.py COM4 build-portable-port-merge --out-dir build-portable-port-merge\validation_manual
```

The legacy manual sequence is kept below for debugging individual steps.

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
