# Middleware

Place project-integrated middleware here, such as file systems, protocol stacks,
bootloader clients, or USB class integrations.

Keep wrappers and configuration in this directory. Put unmodified upstream source
archives or source trees under `third_party/`.

`portable_ota_port/` adapts `third_party/portable_ota` to the current product
types and configuration. Product components should include the middleware
adapter instead of including `pota_*` headers directly.

`portable_log_port/` adapts `third_party/portable_log` to the current RP2350
stdio platform. Product components should include the middleware adapter or the
diagnostics facade instead of including `portable_log.h` directly. Keep level
mapping, backend routing, and future domain policy table-driven in this adapter.
The current adapter queues completed log lines in a fixed ring buffer and flushes
them from the diagnostics service loop.
