# Archived: direct REFMEM_CMD TDMA payload (v1)

Status: **ARCHIVED / NOT BUILT**

This directory preserves the first REFMEM_CMD-on-TDMA implementation that was
removed from the active build because it violated the HAOFV ownership rules:

- the TDMA adapter called RefMem decode/post/ACK directly;
- the transport owned a command-semantics queue (second command slot);
- seat selection was hard-coded instead of table driven;
- the reference steady path never reached the `tx_beacon()` hook.

Nothing here is compiled or linked. It is retained only as review history.

The compliant direction is:

```text
table-declared load
  -> origin scheduler selects load
  -> transport moves bytes only
  -> RefMem AO decodes/posts/takes/ACKs in the single command slot
```

Do not delete this archive; supersede it with a new archived version if the
design changes.

## Removed adapter hook (preserved for review)

```c
uint8_t refmem_control_payload[REFMEM_CMD_WIRE_SIZE];
bool refmem_control_frame = false;
if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE) {
    const uint32_t priority = tdma_seat_rotation_priority(
        adapter->seat_rotation_priority_share);
    const uint32_t first = tdma_seat_rotation_first_class(
        adapter->seat_rotation_first_class_share);
    const uint32_t second = adapter->seat_rotation_second_class_share;
    const uint32_t cycle = priority + first + second;
    if (second > 0u && cycle > 0u) {
        const uint32_t phase = s_refmem_control_cycle++ % cycle;
        if (phase >= priority + first &&
            refmem_cmd_tdma_take(refmem_control_payload)) {
            refmem_control_frame = true;
            wire_payload = refmem_control_payload;
            wire_payload_size = sizeof(refmem_control_payload);
            emit_process_image = true;
            dpll_observation_ready = false;
        }
    }
}
```

## Removed RX hook (preserved for review)

```c
if (decoded && view.payload_class == TDMA_PAYLOAD_CLASS_CONFIG_CONTROL &&
    view.payload != NULL && view.payload_size >= REFMEM_CMD_WIRE_SIZE) {
    refmem_cmd_tdma_rx(view.payload, view.payload_size);
}
```

## Removed SCPI submit hook (preserved for review)

The archived command was `SYSTem:REFMEM:CMD:TX "<56-byte hex>"`; it decoded the
wire frame and called `refmem_cmd_tdma_submit()`. It was removed from the build
because it exposed a second command-semantics queue.

