# Calibration origin timing fragment (ARCHIVED)

Status: ARCHIVED / NOT BUILT

`calibration_origin_timing.inc` was textually `#include`d into
`components/calibration_manager/src/calibration_manager.c`. It is now its own
translation unit:

| archived fragment | replacement |
|---|---|
| `calibration_origin_timing.inc` | `src/calibration_origin_timing.c` + `inc/calibration_origin_timing.h` |

The interface header already existed (it declares
`calibration_manager_origin_*` and the timing/attempt types), so the split only
created the `.c`: the body moved verbatim, including its own
`PICO_ON_DEVICE` RAM-placement block for the origin epoch.

Do not delete this archive. It is the audit copy of the pre-split source.
