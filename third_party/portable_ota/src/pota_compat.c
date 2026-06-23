#include "pota_compat.h"

static const pota_compat_map_entry_t s_default_error_map[] = {
    {POTA_ERR_NONE, POTA_ERR_NONE},
    {POTA_ERR_BUSY, POTA_ERR_BUSY},
    {POTA_ERR_INVALID_STATE, POTA_ERR_INVALID_STATE},
    {POTA_ERR_IMAGE_TOO_LARGE, POTA_ERR_IMAGE_TOO_LARGE},
    {POTA_ERR_BAD_HEADER, POTA_ERR_BAD_HEADER},
    {POTA_ERR_FLASH_ERASE, POTA_ERR_FLASH_ERASE},
    {POTA_ERR_FLASH_PROGRAM, POTA_ERR_FLASH_PROGRAM},
    {POTA_ERR_READBACK, POTA_ERR_READBACK},
    {POTA_ERR_CRC, POTA_ERR_CRC},
    {POTA_ERR_VECTOR, POTA_ERR_VECTOR},
    {POTA_ERR_METADATA, POTA_ERR_METADATA},
    {POTA_ERR_ABORTED, POTA_ERR_ABORTED},
    {POTA_ERR_BAD_ARGUMENT, POTA_ERR_BAD_ARGUMENT},
};

static const pota_compat_map_entry_t s_default_result_map[] = {
    {POTA_RESULT_NONE, POTA_RESULT_NONE},
    {POTA_RESULT_ACCEPTED, POTA_RESULT_ACCEPTED},
    {POTA_RESULT_IMAGE_STAGED, POTA_RESULT_IMAGE_STAGED},
    {POTA_RESULT_COMMITTED, POTA_RESULT_COMMITTED},
    {POTA_RESULT_FAILED, POTA_RESULT_FAILED},
    {POTA_RESULT_ABORTED, POTA_RESULT_ABORTED},
};

uint32_t pota_compat_map_u32(uint32_t value,
                             const pota_compat_map_entry_t *entries,
                             size_t entry_count,
                             uint32_t fallback)
{
    if (entries == NULL) {
        return fallback;
    }

    for (size_t i = 0u; i < entry_count; i++) {
        if (entries[i].source == value) {
            return entries[i].destination;
        }
    }

    return fallback;
}

uint32_t pota_compat_error_to_product(pota_error_t error,
                                      const pota_compat_map_entry_t *aliases,
                                      size_t alias_count,
                                      uint32_t fallback)
{
    const uint32_t alias =
        pota_compat_map_u32((uint32_t)error, aliases, alias_count, UINT32_MAX);
    if (alias != UINT32_MAX) {
        return alias;
    }

    return pota_compat_map_u32((uint32_t)error,
                               s_default_error_map,
                               sizeof(s_default_error_map) / sizeof(s_default_error_map[0]),
                               fallback);
}

uint32_t pota_compat_result_to_product(pota_result_t result,
                                       const pota_compat_map_entry_t *aliases,
                                       size_t alias_count,
                                       uint32_t fallback)
{
    const uint32_t alias =
        pota_compat_map_u32((uint32_t)result, aliases, alias_count, UINT32_MAX);
    if (alias != UINT32_MAX) {
        return alias;
    }

    return pota_compat_map_u32((uint32_t)result,
                               s_default_result_map,
                               sizeof(s_default_result_map) / sizeof(s_default_result_map[0]),
                               fallback);
}

static uint32_t pota_compat_reverse_map_u32(uint32_t value,
                                            const pota_compat_map_entry_t *entries,
                                            size_t entry_count,
                                            uint32_t fallback)
{
    if (entries == NULL) {
        return fallback;
    }

    for (size_t i = 0u; i < entry_count; i++) {
        if (entries[i].destination == value) {
            return entries[i].source;
        }
    }

    return fallback;
}

pota_error_t pota_compat_product_to_error(uint32_t value,
                                          const pota_compat_map_entry_t *aliases,
                                          size_t alias_count,
                                          pota_error_t fallback)
{
    const uint32_t alias =
        pota_compat_reverse_map_u32(value, aliases, alias_count, UINT32_MAX);
    if (alias != UINT32_MAX) {
        return (pota_error_t)alias;
    }

    return (pota_error_t)pota_compat_reverse_map_u32(
        value,
        s_default_error_map,
        sizeof(s_default_error_map) / sizeof(s_default_error_map[0]),
        (uint32_t)fallback);
}

pota_result_t pota_compat_product_to_result(uint32_t value,
                                            const pota_compat_map_entry_t *aliases,
                                            size_t alias_count,
                                            pota_result_t fallback)
{
    const uint32_t alias =
        pota_compat_reverse_map_u32(value, aliases, alias_count, UINT32_MAX);
    if (alias != UINT32_MAX) {
        return (pota_result_t)alias;
    }

    return (pota_result_t)pota_compat_reverse_map_u32(
        value,
        s_default_result_map,
        sizeof(s_default_result_map) / sizeof(s_default_result_map[0]),
        (uint32_t)fallback);
}

const char *pota_compat_text_u32(uint32_t value,
                                 const pota_compat_text_entry_t *entries,
                                 size_t entry_count,
                                 const char *fallback)
{
    if (entries != NULL) {
        for (size_t i = 0u; i < entry_count; i++) {
            if (entries[i].value == value) {
                return entries[i].text;
            }
        }
    }

    return fallback;
}
