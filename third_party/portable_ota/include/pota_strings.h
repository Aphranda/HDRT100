#ifndef POTA_STRINGS_H
#define POTA_STRINGS_H

#include "pota_metadata.h"
#include "pota_types.h"

const char *pota_state_to_string(pota_state_t state);
const char *pota_error_to_string(pota_error_t error);
const char *pota_result_to_string(pota_result_t result);
const char *pota_boot_result_to_string(pota_boot_result_t result);

#endif
