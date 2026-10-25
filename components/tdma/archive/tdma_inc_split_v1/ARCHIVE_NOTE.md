# TDMA .inc fragments (ARCHIVED)

Status: ARCHIVED / NOT BUILT

These fragments were textually `#include`d into the TDMA owner, phys and
ring-adapter translation units. Each is now its own translation unit and
the originals are kept here as the audit copy.

| archived fragment | replacement |
|---|---|
| `tdma_runtime_origin.inc` | `src/tdma_runtime_origin.c` + `src/tdma_runtime_origin.h` |
| `tdma_pio_spi_phys_marker.inc` | orphan: superseded by `src/tdma_pio_spi_phys_marker_restored.c` + `src/tdma_pio_spi_phys_data_train_restored.c` |

## 2026-10-25: orphan marker fragment archived

`tdma_pio_spi_phys_marker.inc` (19.5 KB) was not `#include`d by any
translation unit: `src/tdma_pio_spi_phys.c` includes only geometry, event,
priority, rx_scan, origin and flight_io. Its marker snapshot/capture entry
points are implemented in `src/tdma_pio_spi_phys_marker_restored.c`, and the
data-train helpers in `src/tdma_pio_spi_phys_data_train_restored.c`, so the
fragment was a stale pre-restore draft rather than a live module. It is kept
here only as the audit copy; nothing was deleted.

The owner keeps its device state file-private; the origin unit reaches it
through `tdma_owner_{service,phys,ring}_state()` accessors that the fragment
used to touch directly. Six helpers the owner calls are now module-visible,
and the `TDMA_ORIGIN_OWNER_RAM` placement macro moved to
`src/tdma_origin_owner_ram.h`.

Do not delete this archive. It is the audit copy of the pre-split sources.
