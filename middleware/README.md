# Middleware

Place project-integrated middleware here, such as file systems, protocol stacks,
bootloader clients, or USB class integrations.

Keep wrappers and configuration in this directory. Put unmodified upstream source
archives or source trees under `third_party/`.

`portable_ota_port/` adapts `third_party/portable_ota` to the current product
types and configuration. Product components should include the middleware
adapter instead of including `pota_*` headers directly.
