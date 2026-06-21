# RP2350 Sync Trigger PIO Resource Plan

This document reserves RP2350 PIO resources for the synchronous trigger system.
The intent is to keep deterministic input capture and deterministic output
generation isolated from UI, logging, storage, and other non-real-time services.

## Hardware Budget

RP2350 provides three PIO blocks. Each PIO block has four state machines and its
own instruction memory.

| Resource | Total | Reserved by sync trigger plan |
|---|---:|---:|
| PIO blocks | 3 | 3 |
| State machines | 12 | 12 reserved for sync trigger IO |
| PIO instruction memory | 3 x 32 instructions | managed per PIO block |

Status LED, LCD, UART, I2C, watchdog, and diagnostics must not consume sync
trigger PIO state machines. The board status LED is driven as a normal GPIO.

## PIO Block Assignment

| PIO block | Role | Reason |
|---|---|---|
| `pio0` | Fast input capture and trigger qualification | Keeps sampling paths together and close to DMA/IRQ ownership. |
| `pio1` | Deterministic output waveform and trigger generation | Isolates output timing from capture stalls or DMA pressure. |
| `pio2` | Auxiliary routing, protocol assist, and future expansion | Leaves space for product variants without disturbing capture/output timing. |

## State Machine Assignment

| PIO | State machine | Name | Function |
|---|---:|---|---|
| `pio0` | `sm0` | `CAPTURE` | Parallel or single-ended input sampling into RX FIFO/DMA. |
| `pio0` | `sm1` | `TIMESTAMP` | Coarse/fine edge timing, event tick, or capture strobe generator. |
| `pio0` | `sm2` | `QUALIFIER` | Trigger filtering, debounce, gate qualification, edge select. |
| `pio0` | `sm3` | `ARM` | Hardware arm/disarm handshake and capture window control. |
| `pio1` | `sm0` | `OUTPUT` | Main trigger output pulse or programmable pulse train. |
| `pio1` | `sm1` | `CLOCK` | Synchronous clock output or divided reference clock. |
| `pio1` | `sm2` | `PULSE` | Secondary programmable pulse or burst output. |
| `pio1` | `sm3` | `MARKER` | Scope marker, frame marker, or debug timing output. |
| `pio2` | `sm0` | `AUX0` | Auxiliary timing IO, routing, or product-specific protocol output. |
| `pio2` | `sm1` | `AUX1` | Auxiliary timing IO, routing, or product-specific protocol output. |
| `pio2` | `sm2` | `AUX2` | Auxiliary timing IO, routing, or product-specific protocol output. |
| `pio2` | `sm3` | `AUX3` | Auxiliary timing IO, routing, or product-specific protocol output. |

## GPIO Assignment

The cleanest free GPIO groups on the external connectors are `GPIO16..GPIO23`
on the J2/J1 trigger side and `GPIO26..GPIO29` on the J1 auxiliary side. These
pins are reserved as the synchronous trigger high-speed IO zone.

| GPIO | Direction | Signal | PIO owner | Notes |
|---:|---|---|---|---|
| 16 | Input | `TRIG_IN` | `pio0/sm0`, `pio0/sm2` | Primary external trigger input. |
| 17 | Input | `ARM_IN` | `pio0/sm3` | External arm or capture enable. |
| 18 | Input | `EXT_CLK_IN` | `pio0/sm1` | Optional external sampling/reference clock. |
| 19 | Input | `GATE_IN` | `pio0/sm2` | External gate or inhibit input. |
| 20 | Output | `TRIG_OUT` | `pio1/sm0` | Main deterministic trigger output. |
| 21 | Output | `PULSE_OUT` | `pio1/sm2` | Secondary programmable pulse or burst output. |
| 22 | Output | `SYNC_CLK_OUT` | `pio1/sm1` | Reference or divided synchronous clock. |
| 23 | Output | `MARKER_OUT` | `pio1/sm3` | Scope/debug marker output. |
| 26 | Bidirectional | `AUX0_IO` | `pio2/sm0` | Auxiliary timing or protocol pin; also ADC-capable. |
| 27 | Bidirectional | `AUX1_IO` | `pio2/sm1` | Auxiliary timing or protocol pin; also ADC-capable. |
| 28 | Bidirectional | `AUX2_IO` | `pio2/sm2` | Auxiliary timing or protocol pin; also ADC-capable. |
| 29 | Bidirectional | `AUX3_IO` | `pio2/sm3` | Auxiliary timing or protocol pin; also ADC-capable. |

`GPIO24` is left as a spare external GPIO for future board-level functions or
debug use.

## Practical Performance Targets

These targets assume default `clk_sys` around 150 MHz and production firmware
using DMA for sustained capture/output streams.

| Function | Theoretical limit | Product target |
|---|---:|---:|
| Single-bit input sampling | up to 150 MS/s | 10-50 MS/s sustained, higher after signal integrity validation |
| 4-bit parallel input sampling | up to 150 MS/s per sample | 10-50 MS/s sustained |
| Simple square-wave output | about 75 MHz | 1-50 MHz depending on jitter and load requirements |
| Pulse output resolution | one PIO cycle | 6.7 ns at 150 MHz |
| DMA-backed output stream | up to one word per PIO pull cadence | validate per waveform format |

The theoretical limits are PIO execution limits, not guaranteed board-level
electrical limits. Final bandwidth must be verified with the target IO voltage,
trace length, load, probe capacitance, and firmware DMA configuration.

## Implementation Rules

- Do not allocate `pio0`, `pio1`, or `pio2` state machines outside the sync
  trigger subsystem without updating this document.
- Keep LED heartbeat and UI timing on GPIO/software timers, not PIO.
- Keep LCD on SPI; do not reuse `GPIO8..GPIO11` or `GPIO25` for sync timing.
- Keep `GPIO12..GPIO15` reserved for the TF/SD card interface.
- Keep high-speed sync IO on contiguous pins where possible so PIO `in pins,n`
  and `out pins,n` instructions remain efficient.
- Put PIO programs under `drivers/mcu/pio/` or the owning sync component, then
  generate headers from CMake using `pico_generate_pio_header`.
- Use DMA for any capture or output mode expected to run beyond a short burst.
