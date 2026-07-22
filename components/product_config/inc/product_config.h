#ifndef PRODUCT_CONFIG_H
#define PRODUCT_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PRODUCT_CONFIG_USB_MODE_CDC = 0,
    PRODUCT_CONFIG_USB_MODE_USBTMC = 1,
} product_config_usb_mode_t;

bool product_config_init(void);
bool product_config_get_usb_mode(product_config_usb_mode_t *mode);
bool product_config_set_usb_mode(product_config_usb_mode_t mode);
const char *product_config_usb_mode_to_string(product_config_usb_mode_t mode);
bool product_config_usb_mode_from_text(const char *text, uint32_t length, product_config_usb_mode_t *mode);

#endif
