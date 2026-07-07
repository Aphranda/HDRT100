# Third Party

Place unmodified third-party source code or source subsets here.

The normal policy for this repository is vendoring: track only the upstream
files required by the firmware build, plus the upstream license and a short
source note. Do not commit nested `.git/`, `.git` submodule files,
`.gitmodules`, `.github/`, examples, generated packages, or upstream tool
trees unless the firmware build actually depends on them.

If a dependency was copied from a Git checkout, record its source repository
and commit before removing the nested Git metadata. Leaving `.git/` in place
causes the outer repository to treat the directory as an embedded repository
instead of ordinary vendored source files.

Current upstream checkouts identified during the vendor cleanup:

| Path | Source | Current ref |
| --- | --- | --- |
| `third_party/freertos/FreeRTOS-Kernel/` | `https://github.com/FreeRTOS/FreeRTOS-Kernel.git` | `main` at `49cec3e9b27e517ac5ea5db5482c59f937e6aea4` |
| `third_party/freertos/FreeRTOS-Kernel/portable/ThirdParty/Community-Supported-Ports/` | `https://github.com/FreeRTOS/FreeRTOS-Kernel-Community-Supported-Ports` | `main` at `bae4c7aa19009825ba48071a8fe25dcb8be84880`; track only `GCC/RP2350_ARM_NTZ` for this firmware |
| `third_party/scpi-parser/` | `https://github.com/j123b567/scpi-parser.git` | `master` at `886615902b2777b71860445305bfe5a3faef4f3e` |
| `third_party/u8g2/` | `https://github.com/olikraus/u8g2` | source subset; exact upstream commit not available from local metadata |

Project-specific adapters should live in `middleware/`, `components/`, or
`platform/` instead of modifying upstream code directly.

`portable_ota/` is a project-derived, platform-neutral library candidate. Keep
RP2350-specific adapters outside that directory so it remains reusable by other
targets in the RP2350 and STM32 RTOS product scope.

`portable_log/` is a project-derived, platform-neutral logging core. It is kept
free of Pico SDK, RTOS, SCPI, FatFs, and storage dependencies; platform code
must provide timestamp and emit callbacks from an adapter layer such as
`components/diagnostics`.
