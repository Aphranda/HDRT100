# REFMEM_CMD transport-load v1 (ARCHIVED)

Status: ARCHIVED / NOT BUILT

This version carried REFMEM_CMD as a standalone `CONFIG_CONTROL` frame through
`tdma_transport_load` and `tx_beacon`. It built class 6 and launched successfully,
but the frame never appeared in either node's physical RX class counters; the
ring's intended fragmentation path is `tdma_maintenance_stream`, not a separate
transport load.

Superseded by the maintenance-stream integration in
`components/distributed_refmem/src/refmem_cmd_load.c`.

Do not delete this archive. If the design changes again, create a new archived
version and keep this one for audit.
