# Tools

Place build, flashing, release packaging, production test, and diagnostics tools
in this directory.

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
- `release_check/release_check.py`: release gate. It verifies release preset
  safety switches, required artifacts, and absence of OTA fault-injection
  command strings in release artifacts.
- `ota_board_validate/ota_board_validate.py`: one-command board validation
  runner. It runs `release_check`, optionally flashes the factory UF2, queries
  baseline SCPI state, sends a positive OTA package, triggers `BOOT/COMM`, runs
  the negative package matrix, and writes `summary.json`, `summary.txt`, serial
  query files, and per-step logs under a validation output directory.
- `run_portable_ota_tests.ps1`: portable OTA library gate. It builds or runs
  `third_party/portable_ota` unit tests. If no host C compiler exists, it falls
  back to ARM GCC compile/object-build checks and reports that host execution
  was skipped.
- `ota_packager/ota_packager.py`: builds a unified OTA package from Slot A and
  Slot B linked App binaries. CMake normally invokes this automatically.
- `sd_fs_build/sd_fs_build.py`: builds the SD-card filesystem staging tree under
  `build/sdcard/`, copies the unified OTA package to `/update/`, keeps raw `.bin`
  compatibility payloads under `/update/compat/`, writes `manifest.json` and
  firmware-readable `manifest.idx`, and creates `build/RP2350_TRIG_SDCARD.zip`.
- `sd_board_validate/sd_board_validate.py`: board-side SD validation over SCPI.
  It does not flash firmware; it checks `SYST:SD:*`, `SYST:STOR:*`, root and
  key directory catalogs, path-denial behavior, boot/arm/fault snapshot
  behavior, StorageAO `MANIFEST_SCAN`, `FILE_INFO`, `SNAPSHOT_WRITE`, and
  `FAULT_EVIDENCE` job completion, fault trace `.bin/.idx`, fault report
  behavior, and reads back the latest fault trace via `MMEM:READ?` for decoder
  checks. The decoded trace must include
  management-plane trigger configuration events and `sync_io.seq_runtime`;
  flashing remains a separate picotool/script step. The tool writes
  `summary.*`, `queries.txt`, and `trace_readback\` under
  `build/sd_validation_*`.
- `sd_trace_decode/sd_trace_decode.py`: offline decoder for SD trace `.bin`
  files. It verifies header, event CRC, and optional `.idx` metadata, then
  emits JSON or CSV with decoded domain/event/severity names and details such
  as trigger state transitions, source/edge/gate/safe configuration changes,
  SyncIO runtime flags, rollover progress, and trace file CRC checks.
- `ota_bin_info/ota_bin_info.py`: prints raw `.bin` size, CRC32, and
  `SYST:OTA:BEGIN` parameters for bench work.
- `uf2_join/uf2_join.py`: generates the first-time factory UF2 from Bootloader,
  Slot A App, and metadata-clear binaries. CMake normally invokes this
  automatically.
- `build_info/gen_build_info.py`: generates firmware build id source used by
  `SYST:FW:BUILD?`. CMake normally invokes this automatically.
- `rp2350_tk_toolbox.py`: Tkinter bench GUI for common tool operations. It wraps
  release build/check, OTA send/board validation, trigger SCPI controls,
  frequency measurement, timing tests, and ad-hoc SCPI commands.

Launch the GUI from the repository root:

```powershell
python tools\rp2350_tk_toolbox.py
```

Build SD-card contents after a release build:

```powershell
python tools\sd_fs_build\sd_fs_build.py --build-dir build --output-dir build\sdcard --clean
```

Copy the contents of `build\sdcard\` to the root of a FAT32 SD card. The
firmware reads `/manifest.idx`; `/manifest.json` is for PC tools and inspection.
The default offline OTA file is `/update/RP2350_TRIG_UPDATE.pkg`; raw `.bin`
files are kept only for compatibility under `/update/compat/`.

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
