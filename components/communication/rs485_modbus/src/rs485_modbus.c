#include "rs485_modbus.h"

#include <string.h>

#include "drv_rs485.h"
#include "pico/time.h"

#define MODBUS_FUNC_READ_HOLDING 0x03u
#define MODBUS_FUNC_WRITE_SINGLE 0x06u
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01u
#define MODBUS_EXCEPTION_ILLEGAL_ADDRESS 0x02u
#define MODBUS_EXCEPTION_ILLEGAL_VALUE 0x03u
#define MODBUS_EXCEPTION_DEVICE_FAILURE 0x04u
#define MODBUS_BROADCAST_ID 0u
#define MODBUS_MIN_FRAME 4u
#define MODBUS_REQUEST_SIZE 8u

typedef struct {
    rs485_modbus_service_config_t config;
    bool ready;
    bool enabled;
    rs485_modbus_role_t role;
    uint8_t rx_frame[RS485_MODBUS_MAX_FRAME];
    size_t rx_length;
    uint64_t last_byte_us;
    uint8_t request[MODBUS_REQUEST_SIZE];
    size_t request_length;
    uint8_t response_unit;
    uint8_t response_function;
    uint16_t response_address;
    uint16_t response_quantity;
    uint16_t response_value;
    uint8_t retries_used;
    uint64_t deadline_us;
    rs485_modbus_master_state_t master_state;
    uint32_t master_errors;
    rs485_modbus_diagnostics_t diagnostics;
    uint16_t result[RS485_MODBUS_MAX_REGISTERS];
    size_t result_count;
    bool result_pending;
} rs485_modbus_service_state_t;

static rs485_modbus_service_state_t s_service;
static uint16_t s_device_test_pattern = 0x55u;
static uint16_t s_device_test_count;

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8u) | data[1]);
}

static void write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8u);
    data[1] = (uint8_t)value;
}

uint16_t rs485_modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffu;
    if (data == NULL) {
        return crc;
    }
    for (size_t index = 0u; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u ?
                (uint16_t)((crc >> 1u) ^ 0xa001u) :
                (uint16_t)(crc >> 1u);
        }
    }
    return crc;
}

static void append_crc(uint8_t *frame, size_t payload_length)
{
    const uint16_t crc = rs485_modbus_crc16(frame, payload_length);
    frame[payload_length] = (uint8_t)crc;
    frame[payload_length + 1u] = (uint8_t)(crc >> 8u);
}

static bool valid_unit(uint8_t unit_id)
{
    return unit_id <= 247u;
}

static bool valid_crc(const uint8_t *frame, size_t length)
{
    if (frame == NULL || length < MODBUS_MIN_FRAME) {
        return false;
    }
    const uint16_t received = (uint16_t)frame[length - 2u] |
                              (uint16_t)((uint16_t)frame[length - 1u] << 8u);
    return received == rs485_modbus_crc16(frame, length - 2u);
}

size_t rs485_modbus_build_read_request(uint8_t unit_id, uint16_t address,
                                       uint16_t quantity, uint8_t *frame,
                                       size_t capacity)
{
    if (!valid_unit(unit_id) || quantity == 0u ||
        quantity > RS485_MODBUS_MAX_REGISTERS || frame == NULL ||
        capacity < MODBUS_REQUEST_SIZE) {
        return 0u;
    }
    frame[0] = unit_id;
    frame[1] = MODBUS_FUNC_READ_HOLDING;
    write_u16(&frame[2], address);
    write_u16(&frame[4], quantity);
    append_crc(frame, 6u);
    return MODBUS_REQUEST_SIZE;
}

size_t rs485_modbus_build_write_request(uint8_t unit_id, uint16_t address,
                                        uint16_t value, uint8_t *frame,
                                        size_t capacity)
{
    if (!valid_unit(unit_id) || frame == NULL || capacity < MODBUS_REQUEST_SIZE) {
        return 0u;
    }
    frame[0] = unit_id;
    frame[1] = MODBUS_FUNC_WRITE_SINGLE;
    write_u16(&frame[2], address);
    write_u16(&frame[4], value);
    append_crc(frame, 6u);
    return MODBUS_REQUEST_SIZE;
}

static size_t exception_response(uint8_t unit_id, uint8_t function,
                                 uint8_t code, uint8_t *response,
                                 size_t capacity)
{
    if (response == NULL || capacity < 5u) {
        return 0u;
    }
    response[0] = unit_id;
    response[1] = (uint8_t)(function | 0x80u);
    response[2] = code;
    append_crc(response, 3u);
    return 5u;
}

size_t rs485_modbus_handle_request(const rs485_modbus_context_t *context,
                                   const uint8_t *frame, size_t frame_length,
                                   uint8_t *response, size_t response_capacity)
{
    if (context == NULL || frame == NULL || response == NULL ||
        !valid_unit(context->unit_id) || context->unit_id == MODBUS_BROADCAST_ID ||
        context->read_register == NULL || context->write_register == NULL ||
        frame_length < MODBUS_MIN_FRAME || frame_length > RS485_MODBUS_MAX_FRAME ||
        !valid_crc(frame, frame_length)) {
        return 0u;
    }
    const uint8_t unit_id = frame[0];
    const uint8_t function = frame[1];
    if (unit_id != context->unit_id && unit_id != MODBUS_BROADCAST_ID) {
        return 0u;
    }
    if (function == MODBUS_FUNC_READ_HOLDING) {
        if (frame_length != MODBUS_REQUEST_SIZE) {
            return unit_id == MODBUS_BROADCAST_ID ? 0u :
                exception_response(context->unit_id, function,
                                    MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                    response, response_capacity);
        }
        const uint16_t address = read_u16(&frame[2]);
        const uint16_t quantity = read_u16(&frame[4]);
        if (unit_id == MODBUS_BROADCAST_ID || quantity == 0u ||
            quantity > RS485_MODBUS_MAX_REGISTERS ||
            (uint32_t)address + quantity > 0x10000u ||
            response_capacity < 5u + (size_t)quantity * 2u) {
            return unit_id == MODBUS_BROADCAST_ID ? 0u :
                exception_response(context->unit_id, function,
                                    MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                    response, response_capacity);
        }
        response[0] = context->unit_id;
        response[1] = function;
        response[2] = (uint8_t)(quantity * 2u);
        for (uint16_t index = 0u; index < quantity; ++index) {
            bool valid = false;
            const uint16_t value = context->read_register(
                context->context, (uint16_t)(address + index), &valid);
            if (!valid) {
                return exception_response(context->unit_id, function,
                                          MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                          response, response_capacity);
            }
            write_u16(&response[3u + index * 2u], value);
        }
        const size_t payload_length = 3u + (size_t)quantity * 2u;
        append_crc(response, payload_length);
        return payload_length + 2u;
    }
    if (function == MODBUS_FUNC_WRITE_SINGLE) {
        if (frame_length != MODBUS_REQUEST_SIZE) {
            return unit_id == MODBUS_BROADCAST_ID ? 0u :
                exception_response(context->unit_id, function,
                                    MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                    response, response_capacity);
        }
        const uint16_t address = read_u16(&frame[2]);
        const uint16_t value = read_u16(&frame[4]);
        if (!context->write_register(context->context, address, value)) {
            return unit_id == MODBUS_BROADCAST_ID ? 0u :
                exception_response(context->unit_id, function,
                                    MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                    response, response_capacity);
        }
        if (unit_id == MODBUS_BROADCAST_ID) {
            return 0u;
        }
        if (response_capacity < MODBUS_REQUEST_SIZE) {
            return 0u;
        }
        memcpy(response, frame, 6u);
        append_crc(response, 6u);
        return MODBUS_REQUEST_SIZE;
    }
    return unit_id == MODBUS_BROADCAST_ID ? 0u :
        exception_response(context->unit_id, function,
                           MODBUS_EXCEPTION_ILLEGAL_FUNCTION,
                           response, response_capacity);
}

bool rs485_modbus_parse_read_response(const uint8_t *frame, size_t frame_length,
                                      uint8_t unit_id, uint16_t quantity,
                                      uint16_t *values, size_t value_capacity)
{
    if (frame == NULL || values == NULL || !valid_unit(unit_id) ||
        unit_id == MODBUS_BROADCAST_ID || quantity == 0u ||
        quantity > RS485_MODBUS_MAX_REGISTERS || value_capacity < quantity ||
        frame_length != 5u + (size_t)quantity * 2u || !valid_crc(frame, frame_length) ||
        frame[0] != unit_id || frame[1] != MODBUS_FUNC_READ_HOLDING ||
        frame[2] != quantity * 2u) {
        return false;
    }
    for (uint16_t index = 0u; index < quantity; ++index) {
        values[index] = read_u16(&frame[3u + index * 2u]);
    }
    return true;
}

bool rs485_modbus_parse_write_response(const uint8_t *frame, size_t frame_length,
                                       uint8_t unit_id, uint16_t address,
                                       uint16_t value)
{
    return frame != NULL && valid_unit(unit_id) && unit_id != MODBUS_BROADCAST_ID &&
           frame_length == MODBUS_REQUEST_SIZE && valid_crc(frame, frame_length) &&
           frame[0] == unit_id && frame[1] == MODBUS_FUNC_WRITE_SINGLE &&
           read_u16(&frame[2]) == address && read_u16(&frame[4]) == value;
}

static void service_reset_frame(void)
{
    s_service.rx_length = 0u;
    s_service.last_byte_us = 0u;
}

static uint16_t service_expected_response_length(void)
{
    if (s_service.response_function == MODBUS_FUNC_READ_HOLDING) {
        return (uint16_t)(5u + (size_t)s_service.response_quantity * 2u);
    }
    return s_service.response_function == MODBUS_FUNC_WRITE_SINGLE ? 8u : 0u;
}

static bool service_send_request(void)
{
    if (!drv_rs485_ready() || s_service.request_length == 0u ||
        !drv_rs485_write(s_service.request, (uint32_t)s_service.request_length)) {
        ++s_service.master_errors;
        return false;
    }
    ++s_service.diagnostics.tx_frame_count;
    const uint32_t baud = drv_rs485_baud_hz();
    const size_t response_length = s_service.response_function ==
        MODBUS_FUNC_READ_HOLDING ?
        5u + (size_t)s_service.response_quantity * 2u : 8u;
    const uint64_t wire_bits =
        ((uint64_t)s_service.request_length + response_length) * 10u;
    const uint64_t wire_us = baud != 0u ?
        (wire_bits * 1000000u + baud - 1u) / baud : 0u;
    const uint64_t protocol_timeout = wire_us +
        (uint64_t)drv_rs485_modbus_frame_gap_us() * 2u + 5000u;
    const uint64_t timeout_us = protocol_timeout > s_service.config.response_timeout_us ?
        protocol_timeout : s_service.config.response_timeout_us;
    s_service.deadline_us = time_us_64() + timeout_us;
    s_service.master_state = RS485_MODBUS_MASTER_WAITING;
    return true;
}

static void service_retry_or_fail(rs485_modbus_master_state_t terminal)
{
    if (s_service.retries_used < s_service.config.retries) {
        ++s_service.retries_used;
        (void)service_send_request();
        return;
    }
    ++s_service.master_errors;
    s_service.master_state = terminal;
}

static void service_process_master_frame(void)
{
    s_service.diagnostics.last_frame_length = (uint16_t)s_service.rx_length;
    s_service.diagnostics.last_expected_length = service_expected_response_length();
    s_service.diagnostics.last_frame_prefix = 0u;
    for (size_t index = 0u; index < s_service.rx_length && index < 4u; ++index) {
        s_service.diagnostics.last_frame_prefix =
            (s_service.diagnostics.last_frame_prefix << 8u) |
            s_service.rx_frame[index];
    }
    s_service.diagnostics.last_frame_crc_ok =
        valid_crc(s_service.rx_frame, s_service.rx_length);
    ++s_service.diagnostics.rx_frame_count;
    if (!s_service.diagnostics.last_frame_crc_ok) {
        ++s_service.diagnostics.crc_error_count;
    }
    bool ok = false;
    if (s_service.response_function == MODBUS_FUNC_READ_HOLDING) {
        ok = rs485_modbus_parse_read_response(
            s_service.rx_frame, s_service.rx_length, s_service.response_unit,
            s_service.response_quantity, s_service.result,
            RS485_MODBUS_MAX_REGISTERS);
        if (ok) {
            s_service.result_count = s_service.response_quantity;
        }
    } else if (s_service.response_function == MODBUS_FUNC_WRITE_SINGLE) {
        ok = rs485_modbus_parse_write_response(
            s_service.rx_frame, s_service.rx_length, s_service.response_unit,
            s_service.response_address, s_service.response_value);
        s_service.result_count = 0u;
    }
    service_reset_frame();
    if (ok) {
        s_service.master_state = RS485_MODBUS_MASTER_SUCCESS;
        s_service.result_pending = true;
    } else {
        ++s_service.diagnostics.protocol_error_count;
        service_retry_or_fail(RS485_MODBUS_MASTER_PROTOCOL_ERROR);
    }
}

static void service_process_slave_frame(void)
{
    const rs485_modbus_context_t context = {
        .unit_id = s_service.config.unit_id,
        .read_register = s_service.config.read_register,
        .write_register = s_service.config.write_register,
        .context = s_service.config.context,
    };
    uint8_t response[RS485_MODBUS_MAX_FRAME];
    const size_t response_length = rs485_modbus_handle_request(
        &context, s_service.rx_frame, s_service.rx_length,
        response, sizeof(response));
    service_reset_frame();
    if (response_length > 0u) {
        if (drv_rs485_write(response, (uint32_t)response_length)) {
            ++s_service.diagnostics.tx_frame_count;
        }
    }
}

bool rs485_modbus_service_init(const rs485_modbus_service_config_t *config)
{
    if (config == NULL || !valid_unit(config->unit_id) ||
        config->unit_id == MODBUS_BROADCAST_ID || config->frame_gap_us == 0u ||
        config->response_timeout_us == 0u || !drv_rs485_ready() ||
        config->read_register == NULL || config->write_register == NULL) {
        return false;
    }
    memset(&s_service, 0, sizeof(s_service));
    s_service.config = *config;
    s_service.role = RS485_MODBUS_ROLE_SLAVE;
    s_service.master_state = RS485_MODBUS_MASTER_IDLE;
    s_service.ready = true;
    return true;
}

static uint16_t device_read_register(void *context, uint16_t address, bool *valid)
{
    (void)context;
    if (valid == NULL) {
        return 0u;
    }
    *valid = true;
    switch (address) {
    case 0x0000u: return s_service.enabled ? 1u : 0u;
    case 0x0001u: return drv_rs485_ready() ? 1u : 0u;
    case 0x0002u: return drv_rs485_dma_enabled() ? 1u : 0u;
    case 0x0003u: return (uint16_t)drv_rs485_rx_count_snapshot();
    case 0x0004u: return (uint16_t)(drv_rs485_rx_count_snapshot() >> 16u);
    case 0x0005u: return (uint16_t)drv_rs485_tx_count();
    case 0x0006u: return (uint16_t)(drv_rs485_tx_count() >> 16u);
    case 0x0007u: return (uint16_t)drv_rs485_error_count();
    case 0x0008u: return (uint16_t)drv_rs485_dma_overrun_count();
    case 0x0010u: return s_device_test_pattern;
    case 0x0011u: return s_device_test_count;
    default:
        *valid = false;
        return 0u;
    }
}

static bool device_write_register(void *context, uint16_t address, uint16_t value)
{
    (void)context;
    if (address == 0x0010u && value <= 0xffu) {
        s_device_test_pattern = value;
        return true;
    }
    if (address == 0x0011u && value <= 256u) {
        s_device_test_count = value;
        return true;
    }
    return false;
}

bool rs485_modbus_device_init(void)
{
    rs485_modbus_service_config_t config = {
        .unit_id = 1u,
        .read_register = device_read_register,
        .write_register = device_write_register,
        .context = NULL,
        .frame_gap_us = 0u,
        .response_timeout_us = 20000u,
        .retries = 2u,
    };
    config.frame_gap_us = drv_rs485_modbus_frame_gap_us();
    return rs485_modbus_service_init(&config);
}

void rs485_modbus_service_poll(void)
{
    if (!s_service.ready || !s_service.enabled) {
        return;
    }
    uint8_t input[64];
    const uint32_t count = drv_rs485_read(input, sizeof(input));
    if (count > 0u) {
        if (s_service.rx_length + count > sizeof(s_service.rx_frame)) {
            service_reset_frame();
        }
        if (count <= sizeof(s_service.rx_frame) - s_service.rx_length) {
            memcpy(&s_service.rx_frame[s_service.rx_length], input, count);
            s_service.rx_length += count;
            s_service.last_byte_us = time_us_64();
        }
    }

    const uint64_t now = time_us_64();
    const uint32_t frame_gap_us = drv_rs485_modbus_frame_gap_us();
    if (s_service.rx_length > 0u && s_service.last_byte_us != 0u &&
        now - s_service.last_byte_us >=
            (frame_gap_us != 0u ? frame_gap_us : s_service.config.frame_gap_us)) {
        if (s_service.role == RS485_MODBUS_ROLE_MASTER &&
            s_service.master_state == RS485_MODBUS_MASTER_WAITING) {
            service_process_master_frame();
        } else if (s_service.role == RS485_MODBUS_ROLE_SLAVE) {
            service_process_slave_frame();
        } else {
            service_reset_frame();
        }
    }

    if (s_service.role == RS485_MODBUS_ROLE_MASTER &&
        s_service.master_state == RS485_MODBUS_MASTER_WAITING &&
        now >= s_service.deadline_us) {
        service_reset_frame();
        ++s_service.diagnostics.timeout_count;
        service_retry_or_fail(RS485_MODBUS_MASTER_TIMEOUT);
    }
    if (s_service.role == RS485_MODBUS_ROLE_MASTER &&
        s_service.master_state == RS485_MODBUS_MASTER_IDLE &&
        s_service.request_length != 0u && s_service.rx_length == 0u) {
        (void)service_send_request();
    }
}

bool rs485_modbus_service_set_enabled(bool enabled)
{
    if (!s_service.ready) {
        return false;
    }
    if (!enabled && (s_service.rx_length != 0u ||
                     s_service.master_state == RS485_MODBUS_MASTER_WAITING)) {
        return false;
    }
    if (!enabled) {
        service_reset_frame();
    }
    s_service.enabled = enabled;
    return true;
}

bool rs485_modbus_service_set_role(rs485_modbus_role_t role)
{
    if (!s_service.ready || (role != RS485_MODBUS_ROLE_SLAVE &&
                             role != RS485_MODBUS_ROLE_MASTER) ||
        s_service.rx_length != 0u ||
        s_service.master_state == RS485_MODBUS_MASTER_WAITING) {
        return false;
    }
    s_service.role = role;
    s_service.master_state = RS485_MODBUS_MASTER_IDLE;
    if (role == RS485_MODBUS_ROLE_SLAVE) {
        s_service.request_length = 0u;
    }
    s_service.result_pending = false;
    return true;
}

rs485_modbus_role_t rs485_modbus_service_role(void)
{
    return s_service.role;
}

bool rs485_modbus_service_ready(void)
{
    return s_service.ready;
}

bool rs485_modbus_master_read_holding(uint8_t unit_id, uint16_t address,
                                      uint16_t quantity)
{
    if (!s_service.ready || !s_service.enabled ||
        s_service.role != RS485_MODBUS_ROLE_MASTER ||
        s_service.master_state == RS485_MODBUS_MASTER_WAITING ||
        s_service.result_pending ||
        rs485_modbus_build_read_request(unit_id, address, quantity,
                                        s_service.request,
                                        sizeof(s_service.request)) == 0u) {
        return false;
    }
    s_service.request_length = MODBUS_REQUEST_SIZE;
    s_service.response_unit = unit_id;
    s_service.response_function = MODBUS_FUNC_READ_HOLDING;
    s_service.response_address = address;
    s_service.response_quantity = quantity;
    s_service.retries_used = 0u;
    s_service.result_count = 0u;
    s_service.master_state = RS485_MODBUS_MASTER_IDLE;
    return true;
}

bool rs485_modbus_master_write_single(uint8_t unit_id, uint16_t address,
                                      uint16_t value)
{
    if (!s_service.ready || !s_service.enabled ||
        s_service.role != RS485_MODBUS_ROLE_MASTER ||
        s_service.master_state == RS485_MODBUS_MASTER_WAITING ||
        s_service.result_pending ||
        rs485_modbus_build_write_request(unit_id, address, value,
                                         s_service.request,
                                         sizeof(s_service.request)) == 0u) {
        return false;
    }
    s_service.request_length = MODBUS_REQUEST_SIZE;
    s_service.response_unit = unit_id;
    s_service.response_function = MODBUS_FUNC_WRITE_SINGLE;
    s_service.response_address = address;
    s_service.response_value = value;
    s_service.retries_used = 0u;
    s_service.result_count = 0u;
    s_service.master_state = RS485_MODBUS_MASTER_IDLE;
    return true;
}

rs485_modbus_master_state_t rs485_modbus_master_state(void)
{
    return s_service.master_state;
}

uint8_t rs485_modbus_master_retries_used(void)
{
    return s_service.retries_used;
}

uint32_t rs485_modbus_master_error_count(void)
{
    return s_service.master_errors;
}

bool rs485_modbus_master_take_result(uint16_t *values, size_t value_capacity,
                                     size_t *value_count)
{
    if (!s_service.result_pending || values == NULL || value_count == NULL ||
        value_capacity < s_service.result_count) {
        return false;
    }
    memcpy(values, s_service.result,
           s_service.result_count * sizeof(s_service.result[0]));
    *value_count = s_service.result_count;
    s_service.result_pending = false;
    s_service.master_state = RS485_MODBUS_MASTER_IDLE;
    return true;
}

void rs485_modbus_service_get_diagnostics(rs485_modbus_diagnostics_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    *snapshot = s_service.diagnostics;
}
