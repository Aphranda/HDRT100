# U8G2 Integration

Source: <https://github.com/olikraus/u8g2>

This directory contains the upstream U8G2 source tree. Keep it unmodified where
possible.

Project-specific glue belongs under `middleware/u8g2_port/`.

The RP2350 firmware currently links the native RGB565 ST7789 driver under
`drivers/external/lcd/`. U8G2 is available as a third-party dependency for
future font and graphics integration, but it is not linked into the main
firmware by default to avoid pulling every upstream display driver into the
image.
