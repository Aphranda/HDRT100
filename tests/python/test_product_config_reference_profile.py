"""Run the real product-config owner against versioned journal/fault fixtures."""
from pathlib import Path
import os
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def reference_host(tmp_path_factory):
    directory = tmp_path_factory.mktemp("product-reference")
    # Reuse the existing flash stub and independently defined v1-v5 records.
    previous = (ROOT / "tests/unit/test_output_timing_persistence.c").read_text(encoding="utf-8")
    source = directory / "reference.c"
    source.write_text(previous.replace("int main(void)", "int existing_timing_main(void)") + HARNESS,
                      encoding="utf-8")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "host C compiler required"
    executable = directory / ("reference.exe" if os.name == "nt" else "reference")
    includes = [ROOT, ROOT / "components/product_config/inc", ROOT / "components/flash_transaction/inc",
                ROOT / "components/ota_manager/inc", ROOT / "drivers/mcu/flash/inc", ROOT / "config"]
    result = subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                             *["-I" + str(p) for p in includes], str(source),
                             str(ROOT / "components/product_config/src/product_config.c"), "-o", str(executable)],
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("mode", ["legacy", "bounds", "preserve", "crc", "powercut", "previous"])
def test_reference_journal(reference_host, mode):
    result = subprocess.run([str(reference_host), mode], capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
typedef struct {
    timing_record_t timing;
    uint32_t port, edge, hz, window_ms, timeout_ms;
} reference_record_t;
_Static_assert(offsetof(reference_record_t, port)==88u,"v5 boundary");
_Static_assert(sizeof(reference_record_t)==108u,"independent v6 layout");
static const product_config_vdc_reference_profile_t ref_a={2u,1u,1234000u,500u,1700u};
static const product_config_vdc_reference_profile_t ref_b={3u,0u,20000000u,5000u,10000u};
static void expect_ref(const product_config_vdc_reference_profile_t *p){
    product_config_vdc_reference_profile_t got;
    assert(product_config_get_vdc_reference_profile(&got));
    assert(got.input_port==p->input_port && got.edge==p->edge && got.nominal_hz==p->nominal_hz &&
        got.window_ms==p->window_ms && got.timeout_ms==p->timeout_ms);
}
static void expect_default_ref(void){
    const product_config_vdc_reference_profile_t p={4u,0u,10000000u,1000u,2500u};
    expect_ref(&p);
}
static void reference_migrations(void){
    for(unsigned version=1;version<=5;++version)for(unsigned flags=0;flags<4;++flags){
        test_reset_flash();timing_record_t r=legacy(version,flags);
        const unsigned length=version<3?64u:version==3?72u:version==4?76u:88u;
        memcpy(s_flash,&r,length);
        /* Appended bytes are not part of an older record's CRC or semantics. */
        memset(s_flash+length,0xA5,DRV_FLASH_PAGE_SIZE-length);
        assert(product_config_init());expect_default_ref();
        assert(s_program_count==0 && s_erase_count==0);
        assert(product_config_get_board_no()==3u);
        product_config_usb_mode_t usb;assert(product_config_get_usb_mode(&usb)&&usb==PRODUCT_CONFIG_USB_MODE_USBTMC);
        product_config_dpll_servo_profile_t servo;product_config_dpll_control_profile_t control;
        product_config_dpll_baseline_profile_t baseline;int32_t delay;
        assert(product_config_get_dpll_servo_profile(&servo));
        assert(servo.kp_q16==(version>=2&&(flags&1)?-321:PRODUCT_CONFIG_DPLL_DEFAULT_KP_Q16));
        assert(product_config_get_dpll_control_profile(&control));
        assert(control.mode==(version>=2&&(flags&2)?1u:0u));
        assert(product_config_get_dpll_baseline_profile(&baseline));
        assert(baseline.window_ns==(version>=3?1234567u:250000000u));
        assert(product_config_get_dpll_output_compensation_ns(&delay)&&delay==(version>=4?-654321:0));
        const product_config_vdc_output_timing_profile_t before=timing();
        if(version==5)expect_timing(32000u,40000u,9000u);else expect_defaults();
        assert(product_config_set_vdc_reference_profile(&ref_a));
        assert(product_config_init());expect_ref(&ref_a);
        expect_timing(before.plan_ahead_us,before.commit_ahead_us,before.refill_low_us);
        product_config_dpll_servo_profile_t s;product_config_dpll_control_profile_t c;
        product_config_dpll_baseline_profile_t b;int32_t d;
        assert(product_config_get_dpll_servo_profile(&s)&&!memcmp(&s,&servo,sizeof(s)));
        assert(product_config_get_dpll_control_profile(&c)&&!memcmp(&c,&control,sizeof(c)));
        assert(product_config_get_dpll_baseline_profile(&b)&&!memcmp(&b,&baseline,sizeof(b)));
        assert(product_config_get_dpll_output_compensation_ns(&d)&&d==delay);
    }
}
static void reference_bounds(void){
    test_reset_flash();assert(product_config_init());expect_default_ref();
    assert(!product_config_get_vdc_reference_profile(NULL));
    assert(!product_config_set_vdc_reference_profile(NULL));
    const product_config_vdc_reference_profile_t invalid[]={
        {0,0,1000,100,101},{5,0,1000,100,101},{1,2,1000,100,101},
        {1,0,999,100,101},{1,0,20000001,100,101},{1,0,1000,99,101},
        {1,0,1000,5001,9999},{1,0,1000,100,100},{1,0,1000,100,99},
        {1,0,1000,100,10001},{1,0,1001,100,101},{1,0,UINT32_MAX,UINT32_MAX,UINT32_MAX}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){
        assert(!product_config_set_vdc_reference_profile(&invalid[i]));expect_default_ref();
        assert(s_program_count==0&&s_erase_count==0);
    }
    const product_config_vdc_reference_profile_t valid[]={
        {1,0,1000,100,101},{4,1,20000000,5000,10000},{2,1,1010,100,102},
        {3,0,1234567,1000,2500}};
    for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);++i){
        assert(product_config_set_vdc_reference_profile(&valid[i]));
        assert(product_config_init());expect_ref(&valid[i]);
    }
}
static void reference_preserve(void){
    test_reset_flash();assert(product_config_init());
    assert(product_config_set_vdc_reference_profile(&ref_a));
    const product_config_dpll_servo_profile_t s={-9,3,1000,345,789};
    const product_config_dpll_control_profile_t c={1,3,17};
    const product_config_dpll_baseline_profile_t b={1,500};
    const product_config_vdc_output_timing_profile_t t={24000,32000,16000};
    assert(product_config_set_dpll_servo_profile(&s));assert(product_config_set_dpll_control_profile(&c));
    assert(product_config_set_dpll_baseline_profile(&b));assert(product_config_set_dpll_output_compensation_ns(-68));
    assert(product_config_set_vdc_output_timing_profile(&t));assert(product_config_set_board_no(4));
    assert(product_config_set_usb_mode(PRODUCT_CONFIG_USB_MODE_USBTMC));
    assert(product_config_init());expect_ref(&ref_a);
    s_fail_program=true;assert(!product_config_set_vdc_reference_profile(&ref_b));expect_ref(&ref_a);
    s_fail_program=false;s_corrupt_program=true;
    assert(!product_config_set_vdc_reference_profile(&ref_b));expect_ref(&ref_a);
    s_corrupt_program=false;assert(product_config_init());expect_ref(&ref_a);
    assert(product_config_set_vdc_reference_profile(&ref_b));assert(product_config_init());expect_ref(&ref_b);
    product_config_dpll_servo_profile_t sg;product_config_dpll_control_profile_t cg;
    product_config_dpll_baseline_profile_t bg;int32_t delay;
    assert(product_config_get_dpll_servo_profile(&sg)&&!memcmp(&sg,&s,sizeof(s)));
    assert(product_config_get_dpll_control_profile(&cg)&&!memcmp(&cg,&c,sizeof(c)));
    assert(product_config_get_dpll_baseline_profile(&bg)&&!memcmp(&bg,&b,sizeof(b)));
    assert(product_config_get_dpll_output_compensation_ns(&delay)&&delay==-68);
    expect_timing(24000,32000,16000);assert(product_config_get_board_no()==4);
}
static void reference_crc(void){
    for(unsigned byte=88;byte<108;++byte){
        test_reset_flash();assert(product_config_init());
        assert(product_config_set_vdc_reference_profile(&ref_a));
        assert(product_config_set_vdc_reference_profile(&ref_b));
        s_flash[DRV_FLASH_PAGE_SIZE+byte]^=1u;
        assert(product_config_init());expect_ref(&ref_a);
    }
    test_reset_flash();assert(product_config_init());assert(product_config_set_vdc_reference_profile(&ref_a));
    reference_record_t r;memcpy(&r,s_flash,sizeof(r));
    assert(r.timing.prefix.version==6u);r.edge=2u;r.timing.prefix.crc32=0u;
    r.timing.prefix.crc32=ota_crc32_compute((const uint8_t *)&r,sizeof(r));
    memcpy(s_flash,&r,sizeof(r));assert(product_config_init());expect_default_ref();
    assert(s_program_count==1u&&s_erase_count==0u);
}
static void reference_powercut(void){
    test_reset_flash();assert(product_config_init());
    assert(product_config_set_vdc_reference_profile(&ref_a));
    assert(product_config_set_vdc_reference_profile(&ref_b));
    uint8_t first[DRV_FLASH_PAGE_SIZE],second[DRV_FLASH_PAGE_SIZE];
    memcpy(first,s_flash,sizeof(first));memcpy(second,s_flash+sizeof(first),sizeof(second));
    for(unsigned cut=0;cut<108;++cut){
        test_reset_flash();memcpy(s_flash,first,sizeof(first));
        memcpy(s_flash+DRV_FLASH_PAGE_SIZE,second,cut);
        assert(product_config_init());expect_ref(&ref_a);
        assert(s_program_count==0u&&s_erase_count==0u);
    }
    const unsigned slots=FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE/DRV_FLASH_PAGE_SIZE;
    test_reset_flash();assert(product_config_init());
    for(unsigned n=0;n<slots;++n)assert(product_config_set_vdc_reference_profile(&ref_a));
    s_fail_program=true;assert(!product_config_set_vdc_reference_profile(&ref_b));expect_ref(&ref_a);
    assert(s_erase_count==1u);s_fail_program=false;assert(product_config_init());expect_ref(&ref_a);
    assert(product_config_set_vdc_reference_profile(&ref_b));assert(product_config_init());expect_ref(&ref_b);
    for(unsigned n=FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE;n<OTA_PRODUCT_CONFIG_SIZE;++n)assert(s_flash[n]==0xA5);
}
int main(int argc,char **argv){
    assert(argc==2);
    if(!strcmp(argv[1],"legacy"))reference_migrations();
    else if(!strcmp(argv[1],"bounds"))reference_bounds();
    else if(!strcmp(argv[1],"preserve"))reference_preserve();
    else if(!strcmp(argv[1],"crc"))reference_crc();
    else if(!strcmp(argv[1],"powercut"))reference_powercut();
    else if(!strcmp(argv[1],"previous"))return existing_timing_main();
    else assert(false);
    return 0;
}
'''
