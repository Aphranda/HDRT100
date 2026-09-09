#ifndef PRODUCT_CONFIG_H
#define PRODUCT_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int32_t kp_q16;
    int32_t ki_q16;
    uint32_t update_period_us;
    uint32_t step_threshold_ns;
    uint32_t sanity_freq_limit_ppb;
} product_config_dpll_servo_profile_t;

typedef struct {
    uint32_t mode;
    uint32_t follow_master_slot_id;
    uint32_t generation;
} product_config_dpll_control_profile_t;

/* Conservative startup values.  product_config_init() seeds these into the
 * Flash journal when no valid persisted DPLL profile exists. */
#define PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16              16384
#define PRODUCT_CONFIG_DPLL_DEFAULT_KI_Q16                256
#define PRODUCT_CONFIG_DPLL_DEFAULT_UPDATE_PERIOD_US    1000u
#define PRODUCT_CONFIG_DPLL_DEFAULT_STEP_THRESHOLD_NS  10000u
#define PRODUCT_CONFIG_DPLL_DEFAULT_SANITY_FREQ_LIMIT_PPB 10000u

typedef enum {
    PRODUCT_CONFIG_USB_MODE_CDC = 0,
    PRODUCT_CONFIG_USB_MODE_USBTMC = 1,
} product_config_usb_mode_t;

bool product_config_init(void);
bool product_config_get_usb_mode(product_config_usb_mode_t *mode);
bool product_config_set_usb_mode(product_config_usb_mode_t mode);
uint8_t product_config_get_board_no(void);
bool product_config_set_board_no(uint32_t board_no);
bool product_config_get_dpll_servo_profile(product_config_dpll_servo_profile_t *profile);
bool product_config_set_dpll_servo_profile(const product_config_dpll_servo_profile_t *profile);
bool product_config_get_dpll_control_profile(product_config_dpll_control_profile_t *profile);
bool product_config_set_dpll_control_profile(const product_config_dpll_control_profile_t *profile);
const char *product_config_usb_mode_to_string(product_config_usb_mode_t mode);
bool product_config_usb_mode_from_text(const char *text, uint32_t length, product_config_usb_mode_t *mode);

#endif
