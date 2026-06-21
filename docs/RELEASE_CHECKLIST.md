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
- No compiler warnings from project code.
- `build/RP2350_TRIG.uf2` generated.
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
- Test report archived.
- Board serial number or traceability ID recorded.
- Known limitations recorded.

