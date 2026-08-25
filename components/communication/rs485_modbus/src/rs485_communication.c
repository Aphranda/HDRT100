#include "rs485_communication.h"

#include <string.h>

#include "drv_rs485.h"
#include "rs485_modbus.h"
#include "scpi_port.h"

static char s_scpi_line[256];
static size_t s_scpi_line_length;

bool rs485_communication_init(void)
{
    s_scpi_line_length = 0u;
    return rs485_modbus_device_init();
}

bool rs485_communication_set_baud_hz(uint32_t baud_hz)
{
    /* Do not retime a live transaction.  The Modbus owner has to return to
     * IDLE first so its frame-gap and response deadline are derived from one
     * coherent baud value.  The driver separately guards an active TX lease. */
    if (!rs485_modbus_service_ready() ||
        rs485_modbus_master_state() == RS485_MODBUS_MASTER_WAITING) {
        return false;
    }
    return drv_rs485_set_baud_hz(baud_hz);
}

uint32_t rs485_communication_baud_hz(void)
{
    return drv_rs485_baud_hz();
}

static void rs485_scpi_service(void)
{
    if (!scpi_uart_mode_is_scpi()) {
        s_scpi_line_length = 0u;
        return;
    }
    uint8_t input[64];
    const uint32_t count = drv_rs485_read(input, sizeof(input));
    for (uint32_t index = 0u; index < count; ++index) {
        const char ch = (char)input[index];
        if (ch == '\n' || ch == '\r') {
            if (s_scpi_line_length == 0u) {
                continue;
            }
            char response[256];
            char command[sizeof(s_scpi_line) + 1u];
            size_t response_length = 0u;
            memcpy(command, s_scpi_line, s_scpi_line_length);
            command[s_scpi_line_length] = '\n';
            if (scpi_port_execute(command, s_scpi_line_length + 1u,
                                  response, sizeof(response),
                                  &response_length) && response_length > 0u) {
                (void)drv_rs485_write((const uint8_t *)response,
                                      (uint32_t)response_length);
            }
            s_scpi_line_length = 0u;
            continue;
        }
        if (s_scpi_line_length + 1u >= sizeof(s_scpi_line)) {
            s_scpi_line_length = 0u;
            continue;
        }
        s_scpi_line[s_scpi_line_length++] = ch;
    }
}

void rs485_communication_service(void)
{
    drv_rs485_service();
    rs485_modbus_service_poll();
    rs485_scpi_service();
    drv_rs485_service();
}
