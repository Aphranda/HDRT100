"""SCPI board identity helpers.

USB CDC COM numbers are host-assigned and may change after re-enumeration.
Validation reports therefore use the identity returned by ``*IDN?``.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass


@dataclass(frozen=True)
class BoardIdentity:
    idn: str
    manufacturer: str
    model: str
    serial_number: str
    firmware: str

    @property
    def address(self) -> str:
        return self.serial_number


def parse_idn_response(response: str) -> BoardIdentity:
    fields = next(csv.reader([response.strip()]), [])
    fields = [field.strip().strip('"') for field in fields]
    if response.strip() == "<timeout>" or len(fields) < 3 or not fields[2]:
        raise ValueError(f"invalid *IDN? response: {response!r}")
    return BoardIdentity(
        idn=response.strip(),
        manufacturer=fields[0],
        model=fields[1],
        serial_number=fields[2],
        firmware=fields[3] if len(fields) > 3 else "",
    )


def normalize_build_response(response: str) -> str:
    return response.strip().strip('"')
