# TDMA .inc fragments (ARCHIVED)

Status: ARCHIVED / NOT BUILT

These fragments were textually `#include`d into the TDMA owner, phys and
ring-adapter translation units. Each is now its own translation unit and
the originals are kept here as the audit copy.

| archived fragment | replacement |
|---|---|
| `tdma_runtime_origin.inc` | `src/tdma_runtime_origin.c` + `src/tdma_runtime_origin.h` |

The owner keeps its device state file-private; the origin unit reaches it
through `tdma_owner_{service,phys,ring}_state()` accessors that the fragment
used to touch directly. Six helpers the owner calls are now module-visible,
and the `TDMA_ORIGIN_OWNER_RAM` placement macro moved to
`src/tdma_origin_owner_ram.h`.

Do not delete this archive. It is the audit copy of the pre-split sources.
