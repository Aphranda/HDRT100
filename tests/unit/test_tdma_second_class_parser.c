#include "tdma_second_class_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static bool refmem_test_parser(const uint8_t *payload,
                               size_t payload_size,
                               void *context)
{
    (void)context;
    return payload_size == 4u && memcmp(payload, "RCMD", 4u) == 0;
}

int main(void)
{
    tdma_second_class_parser_reset();
    assert(tdma_second_class_parser_register(
        TDMA_SECOND_CLASS_TYPE_REFMEM_CMD, refmem_test_parser, NULL));
    assert(tdma_second_class_parser_register(
        TDMA_SECOND_CLASS_TYPE_T2_STATIC, tdma_t2_static_parser, NULL));
    assert(tdma_second_class_parser_register(
        TDMA_SECOND_CLASS_TYPE_T2_DYNAMIC, tdma_t2_dynamic_parser, NULL));
    assert(tdma_second_class_parser_registered_count() == 3u);
    assert(!tdma_second_class_parser_register(
        TDMA_SECOND_CLASS_TYPE_REFMEM_CMD, refmem_test_parser, NULL));

    assert(tdma_second_class_parser_dispatch(
        TDMA_SECOND_CLASS_TYPE_REFMEM_CMD, (const uint8_t *)"RCMD", 4u));
    assert(!tdma_second_class_parser_dispatch(
        TDMA_SECOND_CLASS_TYPE_REFMEM_CMD, (const uint8_t *)"BAD!", 4u));
    tdma_second_class_parser_count_t count;
    assert(tdma_second_class_parser_get_count(
        TDMA_SECOND_CLASS_TYPE_REFMEM_CMD, &count));
    assert(count.accepted == 1u && count.rejected == 1u);

    uint8_t static_frame[TDMA_T2_STATIC_SIZE] = {0};
    put_u16(static_frame, TDMA_T2_STATIC_MAGIC);
    static_frame[2] = 1u;
    put_u32(static_frame + 4u, 0x0Fu);
    put_u32(static_frame + 8u, 7u);
    put_u32(static_frame + 12u, 11u);
    assert(tdma_second_class_parser_dispatch(
        TDMA_SECOND_CLASS_TYPE_T2_STATIC, static_frame, sizeof(static_frame)));
    static_frame[0] = 0u;
    assert(!tdma_second_class_parser_dispatch(
        TDMA_SECOND_CLASS_TYPE_T2_STATIC, static_frame, sizeof(static_frame)));
    assert(tdma_second_class_parser_get_count(
        TDMA_SECOND_CLASS_TYPE_T2_STATIC, &count));
    assert(count.accepted == 1u && count.rejected == 1u);
    assert(tdma_t2_static_applied_count() == 1u);
    uint32_t last_target = 0u, last_config = 0u, last_value = 0u;
    assert(tdma_t2_static_get_last(&last_target, &last_config, &last_value));
    assert(last_target == 0x0Fu && last_config == 7u && last_value == 11u);

    uint8_t dynamic_frame[TDMA_T2_DYNAMIC_SIZE] = {0};
    put_u16(dynamic_frame, TDMA_T2_DYNAMIC_MAGIC);
    dynamic_frame[2] = 1u;
    put_u32(dynamic_frame + 4u, 0x0Fu);
    put_u32(dynamic_frame + 8u, 42u);
    put_u32(dynamic_frame + 12u, 7u);
    put_u32(dynamic_frame + 16u, 9u);
    assert(tdma_second_class_parser_dispatch(
        TDMA_SECOND_CLASS_TYPE_T2_DYNAMIC, dynamic_frame,
        sizeof(dynamic_frame)));
    dynamic_frame[20] = 1u;
    assert(!tdma_second_class_parser_dispatch(
        TDMA_SECOND_CLASS_TYPE_T2_DYNAMIC, dynamic_frame,
        sizeof(dynamic_frame)));
    assert(tdma_second_class_parser_get_count(
        TDMA_SECOND_CLASS_TYPE_T2_DYNAMIC, &count));
    assert(count.accepted == 1u && count.rejected == 1u);

    assert(!tdma_second_class_parser_dispatch(0x7Fu, static_frame,
                                              sizeof(static_frame)));
    assert(tdma_second_class_parser_unknown_count() == 1u);

    printf("tdma_second_class_parser: refmem/static/dynamic/unknown OK\n");
    return 0;
}
