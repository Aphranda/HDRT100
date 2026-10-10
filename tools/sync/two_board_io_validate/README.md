# Two-Board IO Validate

`two_board_io_validate.py` checks the active SYNC_IO wiring between two
minimum-system boards. It uses SCPI serial lifecycle management, drives one
active output bit at a time, reads the opposite board input mask, and reports
missing, swapped, or shorted lines.

Example:

```powershell
python tools\two_board_io_validate\two_board_io_validate.py --port-a COM6 --port-b COM7
```

Default debug wiring maps are directional and follow the measured COM3/COM4
minimum-system setup:

```text
B0 -> B1: OUT0->IN1, OUT1->IN2, OUT2->IN0, OUT3->IN3
B1 -> B0: OUT0->IN2, OUT1->IN1, OUT2->IN0, OUT3->IN3
```

Override them when the wiring changes:

```powershell
python tools\two_board_io_validate\two_board_io_validate.py --port-a COM6 --port-b COM7 --expect-a-to-b 0,1,2,3 --expect-b-to-a 0,1,2,3
```
