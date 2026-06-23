# Tools

Place build, flashing, release packaging, production test, and diagnostics tools
in this directory.

- `ota_packager/ota_packager.py`: build a unified OTA package from Slot A and
  Slot B linked App binaries.
- `ota_send/ota_send.py`: send raw `.bin` or unified package images over SCPI
  USB CDC; supports package negative-path mutations through
  `--package-negative`.
