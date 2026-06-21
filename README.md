# RP2350_TRIG

Industrial firmware template for an RP2350 / Pico 2 based product. The project
uses a medium-size embedded layout so application code, board support, drivers,
OS adaptation, middleware, and third-party modules can grow independently.

## Directory Layout

```text
RP2350_TRIG/
├─ application/                 # Product application and main loop
│  ├─ include/
│  └─ src/
├─ boards/
│  └─ rp2350_trig/              # Board support package for this hardware
│     ├─ include/
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
│  ├─ include/
│  └─ port/
│     └─ baremetal/
├─ middleware/                  # Integrated middleware wrappers
│  └─ u8g2_port/                # Project glue for U8G2
├─ third_party/                 # Unmodified upstream third-party source
│  └─ u8g2/                     # Upstream U8G2 source tree
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

`third_party/` is reserved for unmodified upstream libraries. Project adapters
belong in `middleware/`, `components/`, or `platform/`.

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

Preset build:

```powershell
cmake --preset pico2-release
cmake --build --preset pico2-release
```

Manual build:

```powershell
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2 -DPROJECT_WARNINGS_AS_ERRORS=ON
cmake --build build
```

The firmware artifact is generated as:

```text
build/RP2350_TRIG.uf2
```

## Key Configuration Files

- `config/project_config.h`: firmware version, loop periods, watchdog timeout,
  and product-level constants.
- `boards/rp2350_trig/include/board_config.h`: board pin map and peripheral
  instances.
- `CMakePresets.json`: release and debug build presets.
- `docs/RELEASE_CHECKLIST.md`: release gate template.
- `docs/PIO_RESOURCE_PLAN.md`: sync trigger PIO, state-machine, and GPIO
  allocation.
- `docs/SYNC_TRIGGER_TODO.md`: remaining work for the production trigger
  subsystem.

## Expansion Rules

- Add new MCU peripherals under `drivers/mcu/<peripheral>/`.
- Add external chips under `drivers/external/<device>/`.
- Add reusable services under `components/<component>/`.
- Add third-party source under `third_party/<name>/` and keep local adapters
  outside the third-party tree.
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
- `GPIO12`: SPI MISO
- `GPIO25`: LCD backlight

The native RGB565 LCD driver is located at `drivers/external/lcd/` and currently
targets the board's 240x135 ST7789-compatible SPI panel.

U8G2 upstream source is placed under `third_party/u8g2/`. Project-specific glue
is placed under `middleware/u8g2_port/`.

The current UI path uses U8G2 as a monochrome composition layer and flushes the
result through the native RGB565 ST7789 driver. The sync trigger configuration
page lives in `components/sync_config_ui/` and uses U8G2's built-in button
drawing API for the action controls.

## Sync Trigger IO

The synchronous trigger IO driver lives in `components/sync_io/`. It currently
initializes the reserved PIO resources at boot and exposes interfaces for
4-bit input capture, deterministic pulse output, synchronous clock output, and
four `pio2` auxiliary IO channels.
