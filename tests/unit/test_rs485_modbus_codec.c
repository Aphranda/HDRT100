#include "rs485_modbus.h"

#include <stdio.h>
#include <string.h>

static uint16_t read_register(void *context, uint16_t address, bool *valid)
{
    (void)context;
    *valid = address < 4u;
    return (uint16_t)(0x1000u + address);
}

static bool write_register(void *context, uint16_t address, uint16_t value)
{
    (void)context;
    return address == 0x0010u && value <= 0xffu;
}

int main(void)
{
    uint8_t frame[RS485_MODBUS_MAX_FRAME];
    uint8_t response[RS485_MODBUS_MAX_FRAME];
    uint16_t values[2];
    if (rs485_modbus_crc16((const uint8_t *)"123456789", 9u) != 0x4b37u) {
        return 1;
    }
    if (rs485_modbus_build_read_request(1u, 0u, 2u, frame, sizeof(frame)) != 8u) {
        return 2;
    }
    const rs485_modbus_context_t context = {
        .unit_id = 1u,
        .read_register = read_register,
        .write_register = write_register,
        .context = NULL,
    };
    const size_t response_length = rs485_modbus_handle_request(
        &context, frame, 8u, response, sizeof(response));
    if (response_length != 9u ||
        !rs485_modbus_parse_read_response(response, response_length, 1u, 2u,
                                          values, 2u) ||
        values[0] != 0x1000u || values[1] != 0x1001u) {
        return 3;
    }
    response[response_length - 1u] ^= 0x01u;
    if (rs485_modbus_parse_read_response(response, response_length, 1u, 2u,
                                         values, 2u)) {
        return 4;
    }
    if (rs485_modbus_build_write_request(1u, 0x0010u, 0x0055u,
                                         frame, sizeof(frame)) != 8u ||
        rs485_modbus_handle_request(&context, frame, 8u, response,
                                    sizeof(response)) != 8u ||
        !rs485_modbus_parse_write_response(response, 8u, 1u, 0x0010u,
                                           0x0055u)) {
        return 5;
    }
    (void)printf("rs485_modbus_codec=OK\n");
    return 0;
}
