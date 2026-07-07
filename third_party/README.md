# Third Party

Place unmodified third-party source code here.

Project-specific adapters should live in `middleware/`, `components/`, or
`platform/` instead of modifying upstream code directly.

`portable_ota/` is a project-derived, platform-neutral library candidate. Keep
RP2350-specific adapters outside that directory so it remains reusable by other
targets in the RP2350 and STM32 RTOS product scope.

`portable_log/` is a project-derived, platform-neutral logging core. It is kept
free of Pico SDK, RTOS, SCPI, FatFs, and storage dependencies; platform code
must provide timestamp and emit callbacks from an adapter layer such as
`components/diagnostics`.
