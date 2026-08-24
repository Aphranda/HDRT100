#ifndef RS485_COMMUNICATION_H
#define RS485_COMMUNICATION_H

#include <stdbool.h>
#include <stdint.h>

bool rs485_communication_init(void);
void rs485_communication_service(void);

/* Serial configuration belongs to the communication owner.  SCPI and other
 * control-plane adapters must use these functions instead of reaching into
 * the UART driver directly. */
bool rs485_communication_set_baud_hz(uint32_t baud_hz);
uint32_t rs485_communication_baud_hz(void);

#endif
