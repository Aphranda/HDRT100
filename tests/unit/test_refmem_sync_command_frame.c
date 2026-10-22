#include "refmem_command.h"

#include <stdio.h>
#include <string.h>

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static refmem_command_request_t make_request(void)
{
    refmem_command_request_t r;
    (void)memset(&r, 0, sizeof(r));
    r.command_seq = 0x11223344u;
    r.source_node = 0u;
    r.source_instance = 1u;
    r.target_mask = 0x08u;
    r.required_mask = 0x08u;
    r.command_type = REFMEM_COMMAND_TYPE_MAINTENANCE;
    r.command_class = REFMEM_COMMAND_CLASS_MAINTENANCE;
    r.payload_kind = REFMEM_COMMAND_PAYLOAD_INLINE_SMALL;
    r.payload_ref = 0x55u;
    r.payload_size = 8u;
    r.payload_crc32 = 0x66u;
    r.issue_epoch = 7u;
    r.run_id = 9u;
    r.timeout_us = 50000u;
    return r;
}

int main(void)
{
    refmem_command_request_t in = make_request();
    refmem_command_request_t out;
    refmem_sync_command_payload_t payload;
    int rc = 0;

    (void)memset(&payload, 0, sizeof(payload));
    rc |= expect_u32("to payload",
                     refmem_command_request_to_sync_payload(&in, &payload) ? 1u : 0u,
                     1u);
    rc |= expect_u32("payload seq", payload.command_seq, in.command_seq);
    rc |= expect_u32("payload target", payload.target_mask, in.target_mask);
    rc |= expect_u32("payload req", payload.required_mask, in.required_mask);

    (void)memset(&out, 0, sizeof(out));
    rc |= expect_u32("from payload",
                     refmem_command_request_from_sync_payload(
                         &payload,
                         in.source_node,
                         in.issue_epoch,
                         in.run_id,
                         &out)
                         ? 1u
                         : 0u,
                     1u);
    rc |= expect_u32("out seq", out.command_seq, in.command_seq);
    rc |= expect_u32("out target", out.target_mask, in.target_mask);
    rc |= expect_u32("out epoch", out.issue_epoch, in.issue_epoch);
    rc |= expect_u32("out run", out.run_id, in.run_id);
    rc |= expect_u32("out timeout", out.timeout_us, in.timeout_us);

    in.required_mask = 0x04u;
    rc |= expect_u32("bad mask",
                     refmem_command_request_to_sync_payload(&in, &payload) ? 1u : 0u,
                     0u);
    in = make_request();
    in.command_seq = 0u;
    rc |= expect_u32("bad seq",
                     refmem_command_request_to_sync_payload(&in, &payload) ? 1u : 0u,
                     0u);
    in = make_request();
    in.command_type = REFMEM_COMMAND_TYPE_NONE;
    rc |= expect_u32("bad type",
                     refmem_command_request_to_sync_payload(&in, &payload) ? 1u : 0u,
                     0u);

    if (rc == 0) {
        (void)printf("refmem_sync_command_payload tests passed\n");
    }
    return rc;
}
