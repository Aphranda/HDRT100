#include "refmem_action_registry.h"

#include <stdio.h>
#include <string.h>

static void dummy_handler(void *context, const refmem_action_request_t *request)
{
    (void)context;
    (void)request;
}

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

static refmem_action_entry_t make_entry(uint32_t action_id,
                                        uint32_t command_type,
                                        bool required)
{
    refmem_action_entry_t e;
    (void)memset(&e, 0, sizeof(e));
    e.action_id = action_id;
    e.command_type = command_type;
    e.owner_id = 3u;
    e.required = required;
    e.version = 1u;
    e.crc = 0x1234u;
    e.handler = dummy_handler;
    return e;
}

static refmem_action_request_t make_request(uint32_t seq)
{
    refmem_action_request_t r;
    (void)memset(&r, 0, sizeof(r));
    r.command_seq = seq;
    r.target_mask = 0x03u;
    r.required_mask = 0x01u;
    r.payload_ref = 0xA0u;
    r.payload_size = 4u;
    r.payload_crc32 = 0xB0u;
    return r;
}

static int test_registry(void)
{
    refmem_action_registry_t reg;
    refmem_action_entry_t e;
    uint32_t crc0;
    int rc = 0;

    refmem_action_registry_init(&reg);
    crc0 = refmem_action_registry_crc(&reg);
    e = make_entry(1u, 0u, true);
    rc |= expect_u32("register action",
                     (uint32_t)refmem_action_registry_register(&reg, &e),
                     (uint32_t)REFMEM_ACTION_OK);
    rc |= expect_u32("crc changed",
                     refmem_action_registry_crc(&reg) == crc0 ? 1u : 0u, 0u);
    rc |= expect_u32("duplicate action",
                     (uint32_t)refmem_action_registry_register(&reg, &e),
                     (uint32_t)REFMEM_ACTION_DUPLICATE);
    e = make_entry(2u, 7u, false);
    e.owner_id = 0u;
    rc |= expect_u32("bad owner",
                     (uint32_t)refmem_action_registry_register(&reg, &e),
                     (uint32_t)REFMEM_ACTION_BAD_ARGUMENT);
    e = make_entry(2u, 7u, false);
    rc |= expect_u32("register command",
                     (uint32_t)refmem_action_registry_register(&reg, &e),
                     (uint32_t)REFMEM_ACTION_OK);
    rc |= expect_u32("required handlers",
                     refmem_action_registry_required_handlers_ok(&reg) ? 1u : 0u,
                     1u);
    e = make_entry(3u, 0u, true);
    e.handler = NULL;
    rc |= expect_u32("required missing",
                     (uint32_t)refmem_action_registry_register(&reg, &e),
                     (uint32_t)REFMEM_ACTION_REQUIRED_HANDLER_MISSING);
    return rc;
}

static int test_dispatcher(void)
{
    refmem_action_registry_t reg;
    refmem_action_dispatcher_t d;
    refmem_action_entry_t e;
    refmem_action_request_t req;
    refmem_action_request_t out;
    uint32_t i;
    int rc = 0;

    refmem_action_registry_init(&reg);
    refmem_action_dispatcher_init(&d);
    e = make_entry(1u, 7u, true);
    (void)refmem_action_registry_register(&reg, &e);

    req = make_request(11u);
    rc |= expect_u32("post event",
                     (uint32_t)refmem_action_dispatcher_post(
                         &d, &reg, REFMEM_ACTION_SOURCE_EVENT, 1u, &req),
                     (uint32_t)REFMEM_ACTION_OK);
    rc |= expect_u32("depth", refmem_action_dispatcher_depth(&d), 1u);
    rc |= expect_u32("pop",
                     (uint32_t)refmem_action_dispatcher_pop(&d, &out),
                     (uint32_t)REFMEM_ACTION_OK);
    rc |= expect_u32("pop seq", out.command_seq, 11u);
    rc |= expect_u32("empty",
                     (uint32_t)refmem_action_dispatcher_pop(&d, &out),
                     (uint32_t)REFMEM_ACTION_NOT_FOUND);

    rc |= expect_u32("post command",
                     (uint32_t)refmem_action_dispatcher_post(
                         &d, &reg, REFMEM_ACTION_SOURCE_COMMAND, 7u, &req),
                     (uint32_t)REFMEM_ACTION_OK);
    rc |= expect_u32("pop command",
                     (uint32_t)refmem_action_dispatcher_pop(&d, &out),
                     (uint32_t)REFMEM_ACTION_OK);
    rc |= expect_u32("unhandled",
                     (uint32_t)refmem_action_dispatcher_post(
                         &d, &reg, REFMEM_ACTION_SOURCE_EVENT, 99u, &req),
                     (uint32_t)REFMEM_ACTION_UNHANDLED);

    for (i = 0u; i < REFMEM_ACTION_QUEUE_DEPTH; ++i) {
        rc |= expect_u32("fill",
                         (uint32_t)refmem_action_dispatcher_post(
                             &d, &reg, REFMEM_ACTION_SOURCE_EVENT, 1u, &req),
                         (uint32_t)REFMEM_ACTION_OK);
    }
    rc |= expect_u32("full",
                     (uint32_t)refmem_action_dispatcher_post(
                         &d, &reg, REFMEM_ACTION_SOURCE_EVENT, 1u, &req),
                     (uint32_t)REFMEM_ACTION_QUEUE_FULL);
    rc |= expect_u32("dropped", d.dropped, 1u);
    return rc;
}

int main(void)
{
    int rc = 0;
    rc |= test_registry();
    rc |= test_dispatcher();
    if (rc == 0) {
        (void)printf("refmem_action_registry tests passed\n");
    }
    return rc;
}
