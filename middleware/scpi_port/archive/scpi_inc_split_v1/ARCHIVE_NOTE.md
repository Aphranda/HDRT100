# SCPI command .inc fragments (ARCHIVED)

Status: ARCHIVED / NOT BUILT

These fragments used to be textually `#include`d into
`middleware/scpi_port/src/scpi_system_snapshot_commands.c`, which made one
translation unit out of thirteen independent command families and hid the
command interfaces from the build (an `.inc` fragment has no header of its own,
so nothing checked its signatures against a consumer).

Each fragment now owns a real translation unit and interface:

| archived fragment | replacement |
|---|---|
| `scpi_action_probe.inc` | `src/scpi_action_probe.c` + `inc/scpi_action_probe.h` |
| `scpi_trigger_grid_commands.inc` | `src/scpi_trigger_grid_commands.c` + `inc/scpi_trigger_grid_commands.h` |
| `scpi_trigger_cursor_commands.inc` | `src/scpi_trigger_cursor_commands.c` + `inc/scpi_trigger_cursor_commands.h` |
| `scpi_trigger_deployment_commands.inc` | `src/scpi_trigger_deployment_commands.c` + `inc/scpi_trigger_deployment_commands.h` |
| `scpi_trigger_template_commands.inc` | `src/scpi_trigger_template_commands.c` + `inc/scpi_trigger_template_commands.h` |
| `scpi_trigger_t2_runtime_commands.inc` | `src/scpi_trigger_t2_runtime_commands.c` + `inc/scpi_trigger_t2_runtime_commands.h` |
| `scpi_trigger_seat_commands.inc` | `src/scpi_trigger_seat_commands.c` + `inc/scpi_trigger_seat_commands.h` |
| `scpi_vdc_output_slices.inc` | `src/scpi_vdc_output_slices.c` + `inc/scpi_vdc_output_slices.h` |
| `scpi_trigger_counter_commands.inc` | `src/scpi_trigger_counter_commands.c` + `inc/scpi_trigger_counter_commands.h` |
| `scpi_trigger_gateway_commands.inc` | `src/scpi_trigger_gateway_commands.c` + `inc/scpi_trigger_gateway_commands.h` |
| `scpi_trigger_switch_commands.inc` | `src/scpi_trigger_switch_commands.c` + `inc/scpi_trigger_switch_commands.h` |
| `scpi_trigger_action_commands.inc` | `src/scpi_trigger_action_commands.c` + `inc/scpi_trigger_action_commands.h` |
| `scpi_trigger_action_execute.inc` | `src/scpi_trigger_action_execute.c` + `inc/scpi_trigger_action_execute.h` |

Behaviour is unchanged: function bodies, SCPI page field order and command
results were moved verbatim; the only code-level change is that five helpers
shared between sibling command modules
(`scpi_trigger_grid_uint`, `scpi_trigger_grid_end`, `scpi_trigger_grid_result`,
`scpi_trigger_deployment_u32`, `scpi_trigger_t2_fail`) are no longer `static`,
because their callers now live in other translation units. They stay module
internal and are declared in the owning command header.

Do not delete this archive. It is the audit copy of the pre-split sources.
