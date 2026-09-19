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

typedef struct {
    uint32_t max_replacements;
    uint32_t window_ns;
} product_config_dpll_baseline_profile_t;

typedef struct {
    uint32_t plan_ahead_us;
    uint32_t commit_ahead_us;
    uint32_t refill_low_us;
} product_config_vdc_output_timing_profile_t;

#define PRODUCT_CONFIG_VDC_OUTPUT_TIMING_MIN_US 1000u
#define PRODUCT_CONFIG_VDC_OUTPUT_TIMING_MAX_US 1000000u
#define PRODUCT_CONFIG_VDC_OUTPUT_PLAN_DEFAULT_US 12000u
#define PRODUCT_CONFIG_VDC_OUTPUT_COMMIT_DEFAULT_US 16000u
#define PRODUCT_CONFIG_VDC_OUTPUT_REFILL_DEFAULT_US 6000u
bool product_config_get_vdc_output_timing_profile(product_config_vdc_output_timing_profile_t *profile);
bool product_config_set_vdc_output_timing_profile(const product_config_vdc_output_timing_profile_t *profile);

/* Signal parameters only: arming an external reference is session-local. */
typedef struct {
    uint32_t input_port;
    uint32_t edge; /* 0: rising, 1: falling */
    uint32_t nominal_hz;
    uint32_t window_ms;
    uint32_t timeout_ms;
} product_config_vdc_reference_profile_t;

#define PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_INPUT_PORT 4u
#define PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_EDGE 0u
#define PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_NOMINAL_HZ 10000000u
#define PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_WINDOW_MS 1000u
#define PRODUCT_CONFIG_VDC_REFERENCE_DEFAULT_TIMEOUT_MS 2500u
#define PRODUCT_CONFIG_VDC_REFERENCE_MIN_INPUT_PORT 1u
#define PRODUCT_CONFIG_VDC_REFERENCE_MAX_INPUT_PORT 4u
#define PRODUCT_CONFIG_VDC_REFERENCE_MIN_NOMINAL_HZ 1000u
#define PRODUCT_CONFIG_VDC_REFERENCE_MAX_NOMINAL_HZ 20000000u
#define PRODUCT_CONFIG_VDC_REFERENCE_MIN_WINDOW_MS 100u
#define PRODUCT_CONFIG_VDC_REFERENCE_MAX_WINDOW_MS 5000u
#define PRODUCT_CONFIG_VDC_REFERENCE_MAX_TIMEOUT_MS 10000u
bool product_config_get_vdc_reference_profile(product_config_vdc_reference_profile_t *profile);
bool product_config_set_vdc_reference_profile(const product_config_vdc_reference_profile_t *profile);

/* Debug tuning values are stored verbatim; the realtime owner defines their
 * saturated arithmetic, including zero. Enabling discipline is session-local. */
typedef struct {
    uint32_t slew_ppb_per_s;
    uint32_t filter_divisor;
    uint32_t max_ppb;
} product_config_vdc_reference_discipline_profile_t;

#define PRODUCT_CONFIG_VDC_REFERENCE_DISCIPLINE_DEFAULT_SLEW_PPB_PER_S 100u
#define PRODUCT_CONFIG_VDC_REFERENCE_DISCIPLINE_DEFAULT_FILTER_DIVISOR 4u
#define PRODUCT_CONFIG_VDC_REFERENCE_DISCIPLINE_DEFAULT_MAX_PPB 10000u
bool product_config_get_vdc_reference_discipline_profile(product_config_vdc_reference_discipline_profile_t *profile);
bool product_config_set_vdc_reference_discipline_profile(const product_config_vdc_reference_discipline_profile_t *profile);

/* Output-only compensation, independent of MATCH transport delay. Positive
 * delays the physical edge; boot migration defaults to zero in RAM only. */
#define PRODUCT_CONFIG_DPLL_OUTPUT_COMPENSATION_DEFAULT_NS INT32_C(0)
bool product_config_get_dpll_output_compensation_ns(int32_t *value);
bool product_config_set_dpll_output_compensation_ns(int32_t value);

#define PRODUCT_CONFIG_DPLL_BASELINE_MAX_REPLACEMENTS 2u
#define PRODUCT_CONFIG_DPLL_BASELINE_MAX_WINDOW_NS 250000000u
#define PRODUCT_CONFIG_DPLL_BASELINE_DEFAULT_REPLACEMENTS 2u
#define PRODUCT_CONFIG_DPLL_BASELINE_DEFAULT_WINDOW_NS 250000000u

/* Conservative startup values. product_config_init() seeds missing profiles
 * in RAM only; an explicit store writes the Flash journal after bring-up. */
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
bool product_config_get_dpll_baseline_profile(product_config_dpll_baseline_profile_t *profile);
bool product_config_set_dpll_baseline_profile(const product_config_dpll_baseline_profile_t *profile);
const char *product_config_usb_mode_to_string(product_config_usb_mode_t mode);
bool product_config_usb_mode_from_text(const char *text, uint32_t length, product_config_usb_mode_t *mode);

#endif
