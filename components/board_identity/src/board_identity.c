#include "board_identity.h"

#include <string.h>

#include "pico/unique_id.h"

static char s_serial[BOARD_IDENTITY_SERIAL_MAX];
static volatile uint8_t s_logical_no;
static bool s_initialized;

bool board_identity_init(void)
{
    pico_get_unique_board_id_string(s_serial, sizeof(s_serial));
    if (s_serial[0] == '\0') {
        s_initialized = false;
        return false;
    }
    s_logical_no = 0u;
    s_initialized = true;
    return true;
}

const char *board_identity_serial(void)
{
    return s_initialized ? s_serial : "";
}

uint8_t board_identity_get_no(void)
{
    return s_logical_no;
}

bool board_identity_set_no(uint32_t logical_no)
{
    if (logical_no > BOARD_IDENTITY_MAX_NODES) {
        return false;
    }
    s_logical_no = (uint8_t)logical_no;
    return true;
}
