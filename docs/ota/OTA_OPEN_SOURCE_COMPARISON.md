# OTA Open Source Comparison For RP2350 And STM32 RTOS

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_OPEN_SOURCE_COMPARISON.md`
Related: `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/ota/OTA_PORTABLE_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-08-21

This note compares the current RP2350_TRIG OTA architecture with commonly used
open-source or vendor-visible OTA stacks. The target scope is intentionally
limited to RP2350 and STM32 RTOS products.

## Decision Summary

Keep the current `portable_ota` package/session algorithms, but place them under
the HAOFV Flash v2 ownership model in `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`.

The current design is smaller and better matched to this project than adopting a
full OTA framework directly:

- It is transport-agnostic and works with SCPI, UART, USB, CAN, Ethernet, SD
  file, and factory tools.
- It already supports the validated RP2350 package flow, direct A/B flow,
  redundant metadata, rollback, and negative-path tests.
- It can be adapted to RP2350 RTOS and STM32 RTOS through a narrow flash,
  metadata, cache, watchdog, and task-serialization port layer.
- It avoids pulling in cloud, Linux, Zephyr-only, or SoC-specific assumptions.

The Flash v2 redesign is broader than the OTA core: it adds a single FlashMap,
one App write owner, Boot Control, NVS/blob/FCB stores, Recovery, signature and
anti-rollback. The old copy-to-active mode and whole-package on-board cache are
not long-term v2 requirements.

## Comparison Matrix

| Stack | Strong At | Cost Or Mismatch | Reuse Decision |
|---|---|---|---|
| Current `portable_ota` | Small MCU OTA core, product/hardware package checks, dual image package, transport independence, validation closure. | Needs signature verification, anti-rollback, more portable tests, and RTOS adapter examples. | Keep as core. |
| MCUboot | Secure boot, signed image format, TLV metadata, slot swap/overwrite/direct-XIP strategies, test/revert rollback. | Integration expects MCUboot flash map, image format, trailer semantics, signing tool flow, and bootloader ownership. | Borrow concepts; do not replace current boot path yet. |
| ESP-IDF OTA | Clean App/Bootloader split, redundant OTA data sectors, rollback confirmation API, anti-rollback references. | ESP32 partition and bootloader model is not portable to RP2350/STM32. | Use as design reference only. |
| Zephyr flash map/NVS/FCB | Single partition vocabulary, append-only KV, sector rotation, torn-write recovery and circular event records. | Zephyr device model and APIs are not used by this project. | Borrow storage semantics behind HAOFV adapters. |
| littlefs | Power-loss resilient copy-on-write filesystem for small embedded volumes. | Critical BCB/KV must not depend on a general filesystem; it is unnecessary for fixed extents. | Evaluate only for System Pack/blob needs after a fixed blob store. |
| OpenHarmony DSoftBus | Capability, node identity, session, lane/QoS and unified bytes/stream/file semantics. | Dynamic discovery/routing/IPC are too nondeterministic and heavy for the static TDMA ring. | Borrow concepts for TDMA OTA session, not implementation. |
| Mender MCU | Device identity, server integration, inventory, update modules, Zephyr-oriented reference integration. | Much larger product scope; assumes a Mender service workflow and currently emphasizes Zephyr/MCUboot integration. | Consider only when fleet management is required. |
| STM32 X-CUBE-SBSFU | STM32 secure boot/update reference, authenticity/integrity checks, anti-rollback, STM32 protection examples. | STM32-specific, security-heavy reference package rather than a small cross-platform OTA core. | Use as STM32 security reference. |

## MCUboot

MCUboot is the closest architectural reference for MCU-class products. Its
documentation separates reusable boot logic (`bootutil`) from per-platform boot
applications, uses image headers plus TLV records for metadata and signatures,
and defines primary/secondary image slots. It supports swap/overwrite update
strategies and direct-XIP style equal slots, with a test/revert mechanism for
rollback.

Advantages over the current project:

- Mature image authentication model with hash/signature TLVs.
- Well-known rollback/test confirmation behavior.
- Broad Zephyr and embedded ecosystem adoption.
- Good model for anti-rollback via security counters.

Tradeoffs for this project:

- Adopting it directly would require changing the current package format,
  Bootloader metadata model, image production tools, and validation scripts.
- Its boot trailer and flash-map assumptions would need careful mapping to the
  current RP2350 external flash layout.
- It does not solve this project's transport/product package problem by itself.

Best use: borrow the signed-image/TLV/security-counter ideas and consider a
future compatibility layer, but keep the current RP2350 boot path as the golden
reference until parity is proven.

## ESP-IDF OTA

ESP-IDF OTA is not portable to RP2350 or STM32, but it is a strong reference for
the safety pattern. Its documented safe update path writes a new application to
the inactive OTA slot, updates redundant OTA data sectors, and requires the new
application to mark itself valid or be rolled back.

Advantages over the current project:

- Very clear App confirmation and rollback semantics.
- Redundant OTA data sectors with counter-based selection are similar to our
  redundant metadata copies.
- Mature error taxonomy around image validation and rollback state.

Tradeoffs for this project:

- ESP32 partition tables, bootloader, and app descriptor formats do not map
  cleanly to RP2350 or STM32.
- It is a platform service, not a portable library to embed elsewhere.

Best use: keep its confirm-or-rollback semantics as a reference for SCPI/API
wording and metadata state naming.

## Mender MCU

Mender MCU targets a higher product layer than `portable_ota`. It provides a
client, identity, inventory, server interaction, and update-module model. The
current reference flow is Zephyr-oriented and its `zephyr-image` update module
uses MCUboot-compatible board support for rollback.

Advantages over the current project:

- Fleet-management concepts are already defined.
- Update modules create a clean boundary between delivery and install logic.
- Useful when product requirements include server enrollment, polling,
  inventory, and managed deployments.

Tradeoffs for this project:

- It is too heavy if the immediate need is a small embedded OTA library callable
  by local tools or product transports.
- It brings Zephyr/server workflow assumptions that are not part of the current
  RP2350_TRIG release process.

Best use: revisit when fleet orchestration becomes a product requirement; do
not use it as the low-level OTA core.

## STM32 X-CUBE-SBSFU

ST's Secure Boot and Secure Firmware Update package is not a generic OTA
library, but it is valuable for STM32 RTOS products. It demonstrates secure boot
root-of-trust behavior, authenticity/integrity checks before execution,
anti-rollback, partial update support, and STM32 protection features.

Advantages over the current project:

- Strong STM32-specific security reference.
- Shows how to combine firmware update with device protection settings.
- Covers single-image and dual-image installation examples.

Tradeoffs for this project:

- It is tied to STM32Cube, STM32 protection mechanisms, and ST example layouts.
- Using it directly would fragment RP2350 and STM32 behavior.

Best use: use it as the STM32 security and protection checklist while keeping
the package format, metadata state machine, and validation flow common.

## Recommended Architecture

Use this layered model:

```text
Product transport adapter
  SCPI / UART / USB / SD / TDMA reliable bulk / factory tool

portable_ota core
  package parser, CRC/hash, state machine, compatibility checks,
  status/error vocabulary and transport-neutral stream session

HAOFV App owner
  OtaAO/OtaFB -> FlashTransactionAO -> FlashMap + stores

Bootloader
  minimal BootFlashService + BootControlStore + ImageVerifier,
  direct A/B test/confirm/revert + Recovery
```

Security roadmap:

1. Add package/image signature verification, preferably with a TLV-style
   extensible manifest section inspired by MCUboot.
2. Add anti-rollback using a monotonic firmware security counter.
3. Add key rotation policy and product/hardware key separation.
4. Define metadata schema migration rules before the next incompatible metadata
   change.
5. Add portable unit tests for package parsing, metadata selection, and failure
   state transitions.
6. Stream only the receiver's inactive-slot image object; keep the complete
   package at the PC, SD card, or TDMA distribution source.

## References

- MCUboot design: https://docs.mcuboot.com/design.html
- MCUboot repository: https://github.com/mcu-tools/mcuboot
- ESP-IDF OTA guide: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html
- Zephyr flash map: https://docs.zephyrproject.org/latest/services/storage/flash_map/flash_map.html
- Zephyr NVS: https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html
- Zephyr FCB: https://docs.zephyrproject.org/latest/services/storage/fcb/fcb.html
- littlefs repository: https://github.com/littlefs-project/littlefs
- OpenHarmony DSoftBus: https://github.com/openharmony/communication_dsoftbus
- Mender MCU repository: https://github.com/mendersoftware/mender-mcu
- ST X-CUBE-SBSFU: https://www.st.com/en/embedded-software/x-cube-sbsfu.html
