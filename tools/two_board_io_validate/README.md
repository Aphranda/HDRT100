# Two-Board IO Validate

`two_board_io_validate.py` checks the active SYNC_IO wiring between two
minimum-system boards. It uses SCPI serial lifecycle management, drives one
active output bit at a time, reads the opposite board input mask, and reports
missing, swapped, or shorted lines.

Example:

```powershell
python tools\two_board_io_validate\two_board_io_validate.py --port-a COM6 --port-b COM7
```
