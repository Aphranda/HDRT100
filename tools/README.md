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
- `run_portable_ota_tests.ps1`: portable OTA library gate. It builds or runs
  `third_party/portable_ota` unit tests. If no host C compiler exists, it falls
  back to ARM GCC compile/object-build checks and reports that host execution
  was skipped.
- `ota_packager/ota_packager.py`: builds a unified OTA package from Slot A and
  Slot B linked App binaries. CMake normally invokes this automatically.
- `ota_bin_info/ota_bin_info.py`: prints raw `.bin` size, CRC32, and
  `SYST:OTA:BEGIN` parameters for bench work.
- `uf2_join/uf2_join.py`: generates the first-time factory UF2 from Bootloader,
  Slot A App, and metadata-clear binaries. CMake normally invokes this
  automatically.
- `build_info/gen_build_info.py`: generates firmware build id source used by
  `SYST:FW:BUILD?`. CMake normally invokes this automatically.

## OTA Board Validation Loop

Run COM-port tests serially; only one process may own the USB CDC port.

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
