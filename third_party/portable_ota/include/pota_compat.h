#ifndef POTA_COMPAT_H
#define POTA_COMPAT_H

#include "pota_strings.h"

typedef struct {
    uint32_t source;
    uint32_t destination;
} pota_compat_map_entry_t;

typedef struct {
    uint32_t value;
    const char *text;
} pota_compat_text_entry_t;

uint32_t pota_compat_map_u32(uint32_t value,
                             const pota_compat_map_entry_t *entries,
                             size_t entry_count,
                             uint32_t fallback);
uint32_t pota_compat_error_to_product(pota_error_t error,
                                      const pota_compat_map_entry_t *aliases,
                                      size_t alias_count,
                                      uint32_t fallback);
uint32_t pota_compat_result_to_product(pota_result_t result,
                                       const pota_compat_map_entry_t *aliases,
                                       size_t alias_count,
                                       uint32_t fallback);
pota_error_t pota_compat_product_to_error(uint32_t value,
                                          const pota_compat_map_entry_t *aliases,
                                          size_t alias_count,
                                          pota_error_t fallback);
pota_result_t pota_compat_product_to_result(uint32_t value,
                                            const pota_compat_map_entry_t *aliases,
                                            size_t alias_count,
                                            pota_result_t fallback);
const char *pota_compat_text_u32(uint32_t value,
                                 const pota_compat_text_entry_t *entries,
                                 size_t entry_count,
                                 const char *fallback);

#endif
