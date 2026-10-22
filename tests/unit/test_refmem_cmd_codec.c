#include "refmem_cmd_codec.h"

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
    r.command_seq = 0x10203040u;
    r.source_node = 2u;
    r.source_instance = 1u;
    r.target_mask = 0x0Fu;
    r.required_mask = 0x05u;
    r.command_type = REFMEM_COMMAND_TYPE_START;
    r.command_class = REFMEM_COMMAND_CLASS_RUN;
    r.payload_kind = REFMEM_COMMAND_PAYLOAD_STAGING_REF;
    r.payload_ref = 0x55667788u;
    r.payload_size = 0x1234u;
    r.payload_crc32 = 0xAABBCCDDu;
    r.issue_epoch = 0x01020304u;
    r.run_id = 0x05060708u;
    r.timeout_us = 250000u;
    return r;
}

static int test_roundtrip(void)
{
    refmem_command_request_t in = make_request();
    refmem_command_request_t out;
    uint8_t wire[REFMEM_CMD_WIRE_SIZE];
    int rc = 0;

    (void)memset(&out, 0, sizeof(out));
    rc |= expect_u32("encode status",
                     (uint32_t)refmem_cmd_codec_encode(&in, wire),
                     (uint32_t)REFMEM_CMD_CODEC_OK);
    rc |= expect_u32("decode status",
                     (uint32_t)refmem_cmd_codec_decode(wire, &out),
                     (uint32_t)REFMEM_CMD_CODEC_OK);
    rc |= expect_u32("seq", out.command_seq, in.command_seq);
    rc |= expect_u32("source_node", out.source_node, in.source_node);
    rc |= expect_u32("source_instance", out.source_instance,
                     in.source_instance);
    rc |= expect_u32("target_mask", out.target_mask, in.target_mask);
    rc |= expect_u32("required_mask", out.required_mask, in.required_mask);
    rc |= expect_u32("command_type", out.command_type, in.command_type);
    rc |= expect_u32("command_class", out.command_class, in.command_class);
    rc |= expect_u32("payload_kind", out.payload_kind, in.payload_kind);
    rc |= expect_u32("payload_ref", out.payload_ref, in.payload_ref);
    rc |= expect_u32("payload_size", out.payload_size, in.payload_size);
    rc |= expect_u32("payload_crc32", out.payload_crc32, in.payload_crc32);
    rc |= expect_u32("issue_epoch", out.issue_epoch, in.issue_epoch);
    rc |= expect_u32("run_id", out.run_id, in.run_id);
    rc |= expect_u32("timeout_us", out.timeout_us, in.timeout_us);
    return rc;
}

static int test_bad_frames(void)
{
    refmem_command_request_t in = make_request();
    refmem_command_request_t out;
    uint8_t wire[REFMEM_CMD_WIRE_SIZE];
    int rc = 0;

    (void)memset(&out, 0, sizeof(out));
    rc |= expect_u32("base encode",
                     (uint32_t)refmem_cmd_codec_encode(&in, wire),
                     (uint32_t)REFMEM_CMD_CODEC_OK);

    wire[0] ^= 0xFFu;
    rc |= expect_u32("bad magic",
                     (uint32_t)refmem_cmd_codec_decode(wire, &out),
                     (uint32_t)REFMEM_CMD_CODEC_BAD_MAGIC);
    wire[0] ^= 0xFFu;

    wire[4] = 0u;
    wire[5] = 0u;
    rc |= expect_u32("bad version",
                     (uint32_t)refmem_cmd_codec_decode(wire, &out),
                     (uint32_t)REFMEM_CMD_CODEC_BAD_VERSION);
    wire[4] = (uint8_t)(REFMEM_CMD_VERSION & 0xFFu);
    wire[5] = (uint8_t)((REFMEM_CMD_VERSION >> 8u) & 0xFFu);

    wire[6] = 1u;
    rc |= expect_u32("bad flags",
                     (uint32_t)refmem_cmd_codec_decode(wire, &out),
                     (uint32_t)REFMEM_CMD_CODEC_BAD_RESERVED);
    wire[6] = 0u;

    wire[50] = 1u;
    rc |= expect_u32("bad reserved",
                     (uint32_t)refmem_cmd_codec_decode(wire, &out),
                     (uint32_t)REFMEM_CMD_CODEC_BAD_RESERVED);
    wire[50] = 0u;

    wire[REFMEM_CMD_CRC_OFFSET] ^= 0xFFu;
    rc |= expect_u32("bad crc",
                     (uint32_t)refmem_cmd_codec_decode(wire, &out),
                     (uint32_t)REFMEM_CMD_CODEC_BAD_CRC);
    return rc;
}

static int test_ranges(void)
{
    refmem_command_request_t in = make_request();
    uint8_t wire[REFMEM_CMD_WIRE_SIZE];
    int rc = 0;

    in.source_node = 0x10000u;
    rc |= expect_u32("source range",
                     (uint32_t)refmem_cmd_codec_encode(&in, wire),
                     (uint32_t)REFMEM_CMD_CODEC_RANGE);
    in = make_request();
    in.payload_size = 0x10000u;
    rc |= expect_u32("payload range",
                     (uint32_t)refmem_cmd_codec_encode(&in, wire),
                     (uint32_t)REFMEM_CMD_CODEC_RANGE);
    in = make_request();
    in.command_class = 0x100u;
    rc |= expect_u32("class range",
                     (uint32_t)refmem_cmd_codec_encode(&in, wire),
                     (uint32_t)REFMEM_CMD_CODEC_RANGE);
    in = make_request();
    in.payload_kind = 0x100u;
    rc |= expect_u32("kind range",
                     (uint32_t)refmem_cmd_codec_encode(&in, wire),
                     (uint32_t)REFMEM_CMD_CODEC_RANGE);
    return rc;
}

static int test_null_and_size(void)
{
    refmem_command_request_t in = make_request();
    uint8_t wire[REFMEM_CMD_WIRE_SIZE];
    int rc = 0;

    rc |= expect_u32("null request",
                     (uint32_t)refmem_cmd_codec_encode(NULL, wire),
                     (uint32_t)REFMEM_CMD_CODEC_BAD_ARGUMENT);
    rc |= expect_u32("null wire",
                     (uint32_t)refmem_cmd_codec_encode(&in, NULL),
                     (uint32_t)REFMEM_CMD_CODEC_BAD_ARGUMENT);
    rc |= expect_u32("null decode wire",
                     (uint32_t)refmem_cmd_codec_decode(NULL, &in),
                     (uint32_t)REFMEM_CMD_CODEC_BAD_ARGUMENT);
    rc |= expect_u32("wire size", REFMEM_CMD_WIRE_SIZE, 56u);
    return rc;
}

int main(void)
{
    int rc = 0;
    rc |= test_roundtrip();
    rc |= test_bad_frames();
    rc |= test_ranges();
    rc |= test_null_and_size();
    if (rc == 0) {
        (void)printf("refmem_cmd_codec tests passed\n");
    }
    return rc;
}
