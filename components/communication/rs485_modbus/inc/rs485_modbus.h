#ifndef RS485_MODBUS_H
#define RS485_MODBUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Modbus RTU transport is deliberately independent of Flash/OTA owners. */
#define RS485_MODBUS_MAX_REGISTERS 32u
#define RS485_MODBUS_MAX_FRAME 256u

typedef uint16_t (*rs485_modbus_read_register_fn)(void *context,
                                                   uint16_t address,
                                                   bool *valid);
typedef bool (*rs485_modbus_write_register_fn)(void *context,
                                                uint16_t address,
                                                uint16_t value);

typedef struct {
    uint8_t unit_id;
    rs485_modbus_read_register_fn read_register;
    rs485_modbus_write_register_fn write_register;
    void *context;
} rs485_modbus_context_t;

typedef enum {
    RS485_MODBUS_ROLE_SLAVE = 0u,
    RS485_MODBUS_ROLE_MASTER = 1u,
} rs485_modbus_role_t;

typedef enum {
    RS485_MODBUS_MASTER_IDLE = 0u,
    RS485_MODBUS_MASTER_WAITING = 1u,
    RS485_MODBUS_MASTER_SUCCESS = 2u,
    RS485_MODBUS_MASTER_TIMEOUT = 3u,
    RS485_MODBUS_MASTER_PROTOCOL_ERROR = 4u,
    RS485_MODBUS_MASTER_REJECTED = 5u,
} rs485_modbus_master_state_t;

typedef struct {
    uint32_t rx_frame_count;
    uint32_t tx_frame_count;
    uint32_t crc_error_count;
    uint32_t protocol_error_count;
    uint32_t timeout_count;
    uint16_t last_frame_length;
    uint16_t last_expected_length;
    uint32_t last_frame_prefix;
    bool last_frame_crc_ok;
} rs485_modbus_diagnostics_t;

typedef struct {
    uint8_t unit_id;
    rs485_modbus_read_register_fn read_register;
    rs485_modbus_write_register_fn write_register;
    void *context;
    uint32_t frame_gap_us;
    uint32_t response_timeout_us;
    uint8_t retries;
} rs485_modbus_service_config_t;

uint16_t rs485_modbus_crc16(const uint8_t *data, size_t length);

size_t rs485_modbus_build_read_request(uint8_t unit_id, uint16_t address,
                                       uint16_t quantity, uint8_t *frame,
                                       size_t capacity);
size_t rs485_modbus_build_write_request(uint8_t unit_id, uint16_t address,
                                        uint16_t value, uint8_t *frame,
                                        size_t capacity);

/* Handle one complete RTU request. Zero means no response (broadcast/invalid). */
size_t rs485_modbus_handle_request(const rs485_modbus_context_t *context,
                                   const uint8_t *frame, size_t frame_length,
                                   uint8_t *response, size_t response_capacity);

bool rs485_modbus_parse_read_response(const uint8_t *frame, size_t frame_length,
                                      uint8_t unit_id, uint16_t quantity,
                                      uint16_t *values, size_t value_capacity);
bool rs485_modbus_parse_write_response(const uint8_t *frame, size_t frame_length,
                                       uint8_t unit_id, uint16_t address,
                                       uint16_t value);

bool rs485_modbus_service_init(const rs485_modbus_service_config_t *config);
/* DHRT100 diagnostic register endpoint; owns the Modbus register map. */
bool rs485_modbus_device_init(void);
void rs485_modbus_service_poll(void);
bool rs485_modbus_service_set_enabled(bool enabled);
bool rs485_modbus_service_set_role(rs485_modbus_role_t role);
rs485_modbus_role_t rs485_modbus_service_role(void);
bool rs485_modbus_service_ready(void);
bool rs485_modbus_master_read_holding(uint8_t unit_id, uint16_t address,
                                      uint16_t quantity);
bool rs485_modbus_master_write_single(uint8_t unit_id, uint16_t address,
                                      uint16_t value);
rs485_modbus_master_state_t rs485_modbus_master_state(void);
uint8_t rs485_modbus_master_retries_used(void);
uint32_t rs485_modbus_master_error_count(void);
bool rs485_modbus_master_take_result(uint16_t *values, size_t value_capacity,
                                     size_t *value_count);
void rs485_modbus_service_get_diagnostics(rs485_modbus_diagnostics_t *snapshot);

#endif
