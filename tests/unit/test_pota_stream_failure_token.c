#include "pota_stream_failure.h"

#include <stdio.h>

static int expect_token(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        printf("%s: expected %u, got %u\n", name, expected, actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;
    failed += expect_token("write core token",
                           pota_stream_write_failure_token(
                               POTA_STREAM_RESULT_CORE, 17u),
                           6017u);
    failed += expect_token("write offset token",
                           pota_stream_write_failure_token(
                               POTA_STREAM_RESULT_OFFSET, 0u),
                           5007u);
    failed += expect_token("service non-core token",
                           pota_stream_service_failure_token(
                               POTA_STREAM_RESULT_OFFSET, 99u, 0u),
                           1007u);
    failed += expect_token("service flash-stage token",
                           pota_stream_service_failure_token(
                               POTA_STREAM_RESULT_CORE, 17u, 0u),
                           2017u);
    failed += expect_token("service core-error token",
                           pota_stream_service_failure_token(
                               POTA_STREAM_RESULT_CORE, 0u, 22u),
                           3022u);
    return failed == 0 ? 0 : 1;
}
