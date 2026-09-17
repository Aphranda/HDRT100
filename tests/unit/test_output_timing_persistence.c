#define main existing_product_config_main
#include "tests/unit/test_product_config.c"
#undef main

typedef struct {
    test_product_config_record_t prefix;
    uint32_t replacements, window;
    int32_t delay;
    uint32_t plan, commit, low;
} timing_record_t;
_Static_assert(offsetof(timing_record_t, replacements)==64u, "legacy boundary");
_Static_assert(offsetof(timing_record_t, delay)==72u, "baseline boundary");
_Static_assert(offsetof(timing_record_t, plan)==76u, "delay boundary");
_Static_assert(sizeof(timing_record_t)==88u, "independent timing layout");

static product_config_vdc_output_timing_profile_t timing(void)
{
    product_config_vdc_output_timing_profile_t p;
    assert(product_config_get_vdc_output_timing_profile(&p));
    return p;
}
static void expect_timing(uint32_t plan,uint32_t commit,uint32_t low)
{
    const product_config_vdc_output_timing_profile_t p=timing();
    assert(p.plan_ahead_us==plan && p.commit_ahead_us==commit && p.refill_low_us==low);
}
static void expect_defaults(void)
{
    expect_timing(PRODUCT_CONFIG_VDC_OUTPUT_PLAN_DEFAULT_US,
        PRODUCT_CONFIG_VDC_OUTPUT_COMMIT_DEFAULT_US,PRODUCT_CONFIG_VDC_OUTPUT_REFILL_DEFAULT_US);
}
static timing_record_t legacy(unsigned version,unsigned flags)
{
    timing_record_t r={
        .prefix={.magic=TEST_PRODUCT_CONFIG_MAGIC,.version=version,.sequence=23u,
            .usb_mode=PRODUCT_CONFIG_USB_MODE_USBTMC,.board_no=3u,
            .reserved={0x44504C4Cu,(uint32_t)-321,765u,987u,4321u,9999u,
                       0x44524F4Cu,1u,2u,19u}},
        .replacements=1u,.window=1234567u,.delay=-654321,
        .plan=32000u,.commit=40000u,.low=9000u};
    if(!(flags&1u))r.prefix.reserved[0]=0u;
    if(!(flags&2u))r.prefix.reserved[6]=0u;
    r.prefix.crc32=ota_crc32_compute((const uint8_t *)&r,
        version<3u?64u:version==3u?72u:version==4u?76u:88u);
    return r;
}
static void test_migrations(void)
{
    for(unsigned version=1u;version<=5u;++version)for(unsigned flags=0u;flags<4u;++flags){
        test_reset_flash();timing_record_t r=legacy(version,flags);
        memcpy(s_flash,&r,sizeof(r));
        assert(product_config_init());
        if(version<5u)expect_defaults();else expect_timing(32000u,40000u,9000u);
        assert(s_program_count==0u && s_erase_count==0u);
        int32_t delay;
        assert(product_config_get_dpll_output_compensation_ns(&delay));
        assert(delay==(version>=4u?-654321:0));
        assert(product_config_get_board_no()==3u);
        product_config_usb_mode_t usb;
        assert(product_config_get_usb_mode(&usb)&&usb==PRODUCT_CONFIG_USB_MODE_USBTMC);
        product_config_dpll_servo_profile_t servo;
        product_config_dpll_control_profile_t control;
        product_config_dpll_baseline_profile_t baseline;
        assert(product_config_get_dpll_servo_profile(&servo));
        assert(servo.kp_q16==(version>=2u&&(flags&1u)?-321:PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16));
        assert(product_config_get_dpll_control_profile(&control));
        assert(control.mode==(version>=2u&&(flags&2u)?1u:0u));
        assert(product_config_get_dpll_baseline_profile(&baseline));
        assert(baseline.max_replacements==(version>=3u?1u:2u));
        assert(baseline.window_ns==(version>=3u?1234567u:250000000u));
        const product_config_vdc_output_timing_profile_t p={40000u,50000u,10000u};
        assert(product_config_set_vdc_output_timing_profile(&p));
        assert(product_config_init());expect_timing(40000u,50000u,10000u);
        int32_t saved_delay;
        assert(product_config_get_dpll_output_compensation_ns(&saved_delay)&&saved_delay==delay);
        product_config_dpll_servo_profile_t s2;product_config_dpll_control_profile_t c2;
        product_config_dpll_baseline_profile_t b2;
        assert(product_config_get_dpll_servo_profile(&s2)&&!memcmp(&servo,&s2,sizeof(servo)));
        assert(product_config_get_dpll_control_profile(&c2)&&!memcmp(&control,&c2,sizeof(control)));
        assert(product_config_get_dpll_baseline_profile(&b2)&&!memcmp(&baseline,&b2,sizeof(baseline)));
    }
}
static void test_ranges_and_semantic_repair(void)
{
    const product_config_vdc_output_timing_profile_t bad[]={
        {12000u,16000u,999u},{1000u,16000u,1000u},{12000u,11999u,1000u},
        {12000u,1000001u,1000u},{0u,0u,0u},{UINT32_MAX,UINT32_MAX,1000u}};
    test_reset_flash();assert(product_config_init());expect_defaults();
    assert(!product_config_get_vdc_output_timing_profile(NULL));
    assert(!product_config_set_vdc_output_timing_profile(NULL));
    for(unsigned i=0u;i<sizeof(bad)/sizeof(bad[0]);++i){
        assert(!product_config_set_vdc_output_timing_profile(&bad[i]));expect_defaults();
        assert(s_program_count==0u&&s_erase_count==0u);
    }
    const product_config_vdc_output_timing_profile_t bounds[]={
        {1001u,1001u,1000u},{1000000u,1000000u,999999u}};
    for(unsigned i=0u;i<2u;++i){
        assert(product_config_set_vdc_output_timing_profile(&bounds[i]));
        assert(product_config_init());expect_timing(bounds[i].plan_ahead_us,bounds[i].commit_ahead_us,bounds[i].refill_low_us);
    }
    test_reset_flash();timing_record_t r=legacy(5u,3u);
    r.low=r.plan;r.prefix.crc32=0u;r.prefix.crc32=ota_crc32_compute((const uint8_t *)&r,sizeof(r));
    memcpy(s_flash,&r,sizeof(r));assert(product_config_init());expect_defaults();
    int32_t delay;assert(product_config_get_dpll_output_compensation_ns(&delay)&&delay==-654321);
    assert(product_config_get_board_no()==3u&&s_program_count==0u&&s_erase_count==0u);
}
static void test_cross_stores_failures_crc_rotation(void)
{
    test_reset_flash();assert(product_config_init());
    const product_config_vdc_output_timing_profile_t p={22000u,30000u,11000u};
    assert(product_config_set_vdc_output_timing_profile(&p));
    const product_config_dpll_servo_profile_t servo={-12,345,678u,901u,234u};
    const product_config_dpll_control_profile_t control={1u,3u,47u};
    const product_config_dpll_baseline_profile_t baseline={0u,999u};
    assert(product_config_set_dpll_servo_profile(&servo));
    assert(product_config_set_dpll_control_profile(&control));
    assert(product_config_set_dpll_baseline_profile(&baseline));
    assert(product_config_set_dpll_output_compensation_ns(-987654));
    assert(product_config_set_usb_mode(PRODUCT_CONFIG_USB_MODE_USBTMC));
    assert(product_config_set_board_no(4u));
    assert(product_config_init());expect_timing(22000u,30000u,11000u);
    const product_config_vdc_output_timing_profile_t q={24000u,32000u,12000u};
    s_fail_program=true;assert(!product_config_set_vdc_output_timing_profile(&q));
    expect_timing(22000u,30000u,11000u);s_fail_program=false;s_corrupt_program=true;
    assert(!product_config_set_vdc_output_timing_profile(&q));expect_timing(22000u,30000u,11000u);
    s_corrupt_program=false;assert(product_config_init());expect_timing(22000u,30000u,11000u);
    assert(product_config_set_vdc_output_timing_profile(&q));
    product_config_dpll_servo_profile_t s2;product_config_dpll_control_profile_t c2;
    product_config_dpll_baseline_profile_t b2;int32_t delay;
    assert(product_config_get_dpll_servo_profile(&s2)&&!memcmp(&servo,&s2,sizeof(servo)));
    assert(product_config_get_dpll_control_profile(&c2)&&!memcmp(&control,&c2,sizeof(control)));
    assert(product_config_get_dpll_baseline_profile(&b2)&&!memcmp(&baseline,&b2,sizeof(baseline)));
    assert(product_config_get_dpll_output_compensation_ns(&delay)&&delay==-987654);
    assert(product_config_get_board_no()==4u);
    test_reset_flash();assert(product_config_init());
    assert(product_config_set_vdc_output_timing_profile(&p));
    assert(product_config_set_vdc_output_timing_profile(&q));
    s_flash[DRV_FLASH_PAGE_SIZE+76u]^=1u;
    assert(product_config_init());expect_timing(22000u,30000u,11000u);
    const unsigned slots=FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE/DRV_FLASH_PAGE_SIZE;
    for(unsigned i=0u;i<slots+3u;++i){
        const product_config_vdc_output_timing_profile_t item={22000u+i,30000u+i,11000u};
        assert(product_config_set_vdc_output_timing_profile(&item));
    }
    assert(s_erase_count>0u&&product_config_init());
    expect_timing(22000u+slots+2u,30000u+slots+2u,11000u);
    for(unsigned i=FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE;i<OTA_PRODUCT_CONFIG_SIZE;++i)
        assert(s_flash[i]==0xA5u);
}
int main(void)
{
    assert(existing_product_config_main()==0);
    test_migrations();test_ranges_and_semantic_repair();test_cross_stores_failures_crc_rotation();
    puts("timing product v1-v5 migration, bounds, v4 delay, cross-stores, failures, CRC and rotation passed");
    return 0;
}
