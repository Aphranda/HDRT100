# Release Checklist

Use this checklist for each firmware release candidate.

## Inputs

- Hardware revision:
- Firmware version:
- Pico SDK version:
- Toolchain version:
- Commit or archive ID:
- Build preset:

## Build Gate

- `cmake --preset pico2-release`
- `cmake --build --preset pico2-release`
- `python tools/release_check/release_check.py --preset pico2-release --build-dir build`
- No compiler warnings from project code.
- `build/RP2350_TRIG_FACTORY.uf2` generated.
- `build/RP2350_TRIG.bin` generated.
- `build/RP2350_TRIG_B.bin` generated.
- `build/RP2350_TRIG_UPDATE.pkg` generated.
- Release build does not contain `SYST:OTA:INJ:*`.
- Periodic health log default is disabled.
- Map file reviewed for unexpected memory growth.

## Hardware Gate

- Boot log includes project name, firmware version, and clock frequencies.
- Watchdog reset path tested.
- Power cycle boot tested.
- USB logging tested.
- UART logging setting matches the product requirement.
- SPI, I2C, UART, PIO pin map verified against the schematic.
- All unused external outputs are configured to a defined safe state.

## Production Gate

- Programming method documented.
- Recovery method documented.
- Firmware artifact archived with checksum.
- Unified OTA package manifest reviewed: product id, hardware id, App version,
  build id, payload SHA-256, and minimum Bootloader version.
- Test report archived.
- Board serial number or traceability ID recorded.
- Known limitations recorded.
