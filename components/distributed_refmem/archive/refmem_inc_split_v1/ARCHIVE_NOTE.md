# RefMem .inc fragments (ARCHIVED)

Status: ARCHIVED / NOT BUILT

These four fragments were textually `#include`d into the middle of
`components/distributed_refmem/src/distributed_refmem.c`, so they could reach
the parent's file-static state directly.

Each now owns a real translation unit:

| archived fragment | replacement |
|---|---|
| `distributed_action_probe.inc` | `src/distributed_action_probe.c` + `inc/distributed_action_probe.h` |
| `distributed_refmem_position_ack.inc` | `src/distributed_refmem_position_ack.c` + `inc/distributed_refmem_position_ack.h` |
| `distributed_refmem_vdc_feedback.inc` | `src/distributed_refmem_vdc_feedback.c` + `inc/distributed_refmem_vdc_feedback.h` |
| `distributed_refmem_priority_health.inc` | `src/distributed_refmem_priority_health.c` + `inc/distributed_refmem_priority_health.h` |

The fragments used to read parent `static` state directly, so the split adds
read-only accessors in `src/distributed_refmem_internal.h`:
`distributed_refmem_vdc_flight_rx_binding()`,
`distributed_refmem_vdc_flight_rx_admission_ready()` and
`distributed_refmem_is_initialized()`. Storage stays private so the Core0
RefMem service remains the only writer.

Two helpers lost `static` for cross-unit callers
(`distributed_refmem_vdc_flight_rx_read_binding`,
`distributed_refmem_vdc_flight_rx_same_binding`), and the Core0 VDC vector
region accessor `distributed_refmem_vdc_vector_region()` is now module
internal instead of file static.

The little-endian wire accessors moved to `src/refmem_le.h` as `static inline`
so the Core1 hot path keeps its inlined code generation, and the RAM placement
macros moved to `src/distributed_refmem_time_critical.h`.

`distributed_action_probe` already had its interface header
(`inc/distributed_action_probe.h`); the other three gained one. Each unit `.c`
includes both its own header and `distributed_refmem.h`, so the compiler checks
the two declarations against each other.

Do not delete this archive. It is the audit copy of the pre-split sources.
