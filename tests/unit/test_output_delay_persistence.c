#define main existing_product_config_main
#include "tests/unit/test_product_config.c"
#undef main

typedef struct {
    test_product_config_record_t prefix;
    uint32_t replacements, window;
    int32_t compensation;
} delay_record_t;
_Static_assert(sizeof(delay_record_t) == 76u, "independent v4 layout");
_Static_assert(offsetof(delay_record_t, compensation) == 72u, "v3 ends at 72");

static int32_t read_delay(void)
{
    int32_t v = 911;
    assert(product_config_get_dpll_output_compensation_ns(&v));
    return v;
}

static void test_migration(unsigned version, unsigned flags)
{
    test_reset_flash();
    delay_record_t old = {0};
    old.prefix = (test_product_config_record_t){
        .magic=TEST_PRODUCT_CONFIG_MAGIC, .version=version, .sequence=71u,
        .usb_mode=PRODUCT_CONFIG_USB_MODE_USBTMC, .board_no=4u,
        .reserved={0x44504C4Cu, (uint32_t)-123, 456u, 789u, 4321u, 9876u,
                   0x44524F4Cu, 1u, 2u, 19u}};
    if (!(flags & 1u)) old.prefix.reserved[0] = 0u;
    if (!(flags & 2u)) old.prefix.reserved[6] = 0u;
    old.replacements=1u;old.window=100000000u;old.compensation=INT32_MIN;
    const size_t bytes = version < 3u ? 64u : 72u;
    old.prefix.crc32=ota_crc32_compute((const uint8_t *)&old, bytes);
    memcpy(s_flash, &old, sizeof(old));
    assert(product_config_init());
    assert(read_delay()==0);
    assert(product_config_get_board_no()==4u);
    product_config_usb_mode_t usb;
    assert(product_config_get_usb_mode(&usb)&&usb==PRODUCT_CONFIG_USB_MODE_USBTMC);
    product_config_dpll_servo_profile_t servo;
    product_config_dpll_control_profile_t role;
    product_config_dpll_baseline_profile_t baseline;
    assert(product_config_get_dpll_servo_profile(&servo));
    assert(product_config_get_dpll_control_profile(&role));
    assert(servo.kp_q16==(version>=2u&&(flags&1u)?-123:PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16));
    assert(servo.ki_q16==(version>=2u&&(flags&1u)?456:PRODUCT_CONFIG_DPLL_DEFAULT_KI_Q16));
    assert(role.mode==(version>=2u&&(flags&2u)?1u:0u));
    assert(role.follow_master_slot_id==(version>=2u&&(flags&2u)?2u:0u));
    assert(product_config_get_dpll_baseline_profile(&baseline));
    assert(baseline.max_replacements==(version>=3u?1u:2u));
    assert(baseline.window_ns==(version>=3u?100000000u:250000000u));
    assert(s_program_count==0u&&s_erase_count==0u);
    assert(product_config_set_dpll_output_compensation_ns(-321));
    assert(product_config_init()&&read_delay()==-321);
    assert(product_config_get_board_no()==4u);
    product_config_dpll_servo_profile_t s2;
    product_config_dpll_control_profile_t r2;
    product_config_dpll_baseline_profile_t b2;
    assert(product_config_get_dpll_servo_profile(&s2)&&!memcmp(&servo,&s2,sizeof(servo)));
    assert(product_config_get_dpll_control_profile(&r2)&&!memcmp(&role,&r2,sizeof(role)));
    assert(product_config_get_dpll_baseline_profile(&b2)&&!memcmp(&baseline,&b2,sizeof(baseline)));
}

static void test_delay_roundtrip_and_failures(void)
{
    test_reset_flash();assert(product_config_init());assert(read_delay()==0);
    assert(!product_config_get_dpll_output_compensation_ns(NULL));
    const int32_t values[]={INT32_MIN,-1000000,-1,0,1,1000000,INT32_MAX};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i){
        assert(product_config_set_dpll_output_compensation_ns(values[i]));
        assert(product_config_init()&&read_delay()==values[i]);
    }
    const product_config_dpll_servo_profile_t servo={-17,911,777u,12345u,23456u};
    const product_config_dpll_control_profile_t role={1u,3u,29u};
    const product_config_dpll_baseline_profile_t baseline={0u,1u};
    assert(product_config_set_dpll_servo_profile(&servo));
    assert(product_config_set_dpll_control_profile(&role));
    assert(product_config_set_dpll_baseline_profile(&baseline));
    assert(product_config_set_board_no(3u));
    assert(product_config_set_usb_mode(PRODUCT_CONFIG_USB_MODE_USBTMC));
    assert(product_config_init()&&read_delay()==INT32_MAX);
    s_fail_program=true;
    assert(!product_config_set_dpll_output_compensation_ns(-88));assert(read_delay()==INT32_MAX);
    s_fail_program=false;s_corrupt_program=true;
    assert(!product_config_set_dpll_output_compensation_ns(-99));assert(read_delay()==INT32_MAX);
    s_corrupt_program=false;assert(product_config_init()&&read_delay()==INT32_MAX);
    assert(product_config_set_dpll_output_compensation_ns(-777));
    assert(product_config_init()&&read_delay()==-777);
    product_config_dpll_servo_profile_t s2;product_config_dpll_control_profile_t r2;
    product_config_dpll_baseline_profile_t b2;
    assert(product_config_get_dpll_servo_profile(&s2)&&!memcmp(&servo,&s2,sizeof(servo)));
    assert(product_config_get_dpll_control_profile(&r2)&&!memcmp(&role,&r2,sizeof(role)));
    assert(product_config_get_dpll_baseline_profile(&b2)&&!memcmp(&baseline,&b2,sizeof(baseline)));
    assert(product_config_get_board_no()==3u);
}

static void test_delay_crc_and_rotation(void)
{
    test_reset_flash();assert(product_config_init());
    assert(product_config_set_dpll_output_compensation_ns(101));
    assert(product_config_set_dpll_output_compensation_ns(-202));
    s_flash[DRV_FLASH_PAGE_SIZE+72u]^=1u;
    assert(product_config_init()&&read_delay()==101);
    const unsigned slots=FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE/DRV_FLASH_PAGE_SIZE;
    for(unsigned i=0;i<slots+3u;++i)
        assert(product_config_set_dpll_output_compensation_ns(-(int32_t)i));
    assert(s_erase_count>0u);
    assert(product_config_init()&&read_delay()==-(int32_t)(slots+2u));
    for(unsigned i=FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE;i<OTA_PRODUCT_CONFIG_SIZE;++i)
        assert(s_flash[i]==0xA5u);
}

int main(void)
{
    assert(existing_product_config_main()==0);
    for(unsigned v=1;v<=3;++v)for(unsigned f=0;f<4;++f)test_migration(v,f);
    test_delay_roundtrip_and_failures();test_delay_crc_and_rotation();
    puts("output delay persistence tests passed: 12 legacy variants, signed extremes, cross-stores, CRC, failure, rotation");
    return 0;
}
