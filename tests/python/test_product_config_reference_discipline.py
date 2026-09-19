"""Exercise real product-config journal migration and discipline persistence."""
from pathlib import Path
import os
import shutil
import subprocess

import pytest

from test_product_config_reference_profile import HARNESS as REFERENCE_HARNESS

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def discipline_host(tmp_path_factory):
    directory = tmp_path_factory.mktemp("product-reference-discipline")
    previous = (ROOT / "tests/unit/test_output_timing_persistence.c").read_text(encoding="utf-8")
    source = directory / "discipline.c"
    source.write_text(
        previous.replace("int main(void)", "int existing_timing_main(void)")
        + REFERENCE_HARNESS.replace("int main(int argc,char **argv)",
                                    "int existing_reference_main(int argc,char **argv)")
        + HARNESS, encoding="utf-8")
    compiler = shutil.which("gcc") or shutil.which("clang")
    assert compiler, "host C compiler required"
    executable = directory / ("discipline.exe" if os.name == "nt" else "discipline")
    includes = [ROOT, ROOT / "components/product_config/inc", ROOT / "components/flash_transaction/inc",
                ROOT / "components/ota_manager/inc", ROOT / "drivers/mcu/flash/inc", ROOT / "config"]
    result = subprocess.run(
        [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         *["-I" + str(path) for path in includes], str(source),
         str(ROOT / "components/product_config/src/product_config.c"), "-o", str(executable)],
        capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("mode", ["legacy", "values", "preserve", "crc", "powercut"])
def test_discipline_journal(discipline_host, mode):
    result = subprocess.run([str(discipline_host), mode], capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
typedef struct {
    reference_record_t reference;
    uint32_t slew, divisor, max_ppb;
} discipline_record_t;
_Static_assert(offsetof(discipline_record_t,slew)==108u,"v6 boundary");
_Static_assert(sizeof(discipline_record_t)==120u,"independent v7 layout");
static const product_config_vdc_reference_discipline_profile_t discipline_a={50u,8u,9000u};
static const product_config_vdc_reference_discipline_profile_t discipline_b={75u,2u,30000u};
static void expect_discipline(const product_config_vdc_reference_discipline_profile_t *expected){
    product_config_vdc_reference_discipline_profile_t value;
    assert(product_config_get_vdc_reference_discipline_profile(&value));
    assert(value.slew_ppb_per_s==expected->slew_ppb_per_s &&
        value.filter_divisor==expected->filter_divisor && value.max_ppb==expected->max_ppb);
}
static void expect_default_discipline(void){
    const product_config_vdc_reference_discipline_profile_t expected={100u,4u,10000u};
    expect_discipline(&expected);
}
static void discipline_migrations(void){
    for(unsigned version=1;version<=6;++version)for(unsigned flags=0;flags<4;++flags){
        test_reset_flash();
        reference_record_t record={.timing=legacy(version<=5?version:5,flags),
            .port=ref_a.input_port,.edge=ref_a.edge,.hz=ref_a.nominal_hz,
            .window_ms=ref_a.window_ms,.timeout_ms=ref_a.timeout_ms};
        const unsigned length=version<3?64u:version==3?72u:version==4?76u:version==5?88u:108u;
        record.timing.prefix.version=version;record.timing.prefix.crc32=0u;
        record.timing.prefix.crc32=ota_crc32_compute((const uint8_t *)&record,length);
        memcpy(s_flash,&record,length);memset(s_flash+length,0xA5,DRV_FLASH_PAGE_SIZE-length);
        uint8_t before[DRV_FLASH_PAGE_SIZE];memcpy(before,s_flash,sizeof(before));
        assert(product_config_init());expect_default_discipline();
        assert(s_program_count==0u&&s_erase_count==0u&&!memcmp(before,s_flash,sizeof(before)));
        assert(product_config_get_board_no()==3u);
        if(version==6)expect_ref(&ref_a);else expect_default_ref();
        if(version>=5)expect_timing(32000u,40000u,9000u);else expect_defaults();
        product_config_dpll_servo_profile_t servo,servo_after;
        product_config_dpll_control_profile_t control,control_after;
        product_config_dpll_baseline_profile_t baseline,baseline_after;
        int32_t delay,delay_after;
        assert(product_config_get_dpll_servo_profile(&servo));
        assert(product_config_get_dpll_control_profile(&control));
        assert(product_config_get_dpll_baseline_profile(&baseline));
        assert(product_config_get_dpll_output_compensation_ns(&delay));
        assert(product_config_set_vdc_reference_discipline_profile(&discipline_a));
        assert(s_program_count==1u&&s_erase_count==0u);
        assert(product_config_init());expect_discipline(&discipline_a);
        assert(product_config_get_dpll_servo_profile(&servo_after)&&!memcmp(&servo,&servo_after,sizeof(servo)));
        assert(product_config_get_dpll_control_profile(&control_after)&&!memcmp(&control,&control_after,sizeof(control)));
        assert(product_config_get_dpll_baseline_profile(&baseline_after)&&!memcmp(&baseline,&baseline_after,sizeof(baseline)));
        assert(product_config_get_dpll_output_compensation_ns(&delay_after)&&delay_after==delay);
        if(version==6)expect_ref(&ref_a);else expect_default_ref();
    }
}
static void discipline_values(void){
    test_reset_flash();assert(product_config_init());expect_default_discipline();
    assert(!product_config_get_vdc_reference_discipline_profile(NULL));
    assert(!product_config_set_vdc_reference_discipline_profile(NULL));
    assert(s_program_count==0u&&s_erase_count==0u);
    const product_config_vdc_reference_discipline_profile_t values[]={
        {0,0,0},{UINT32_MAX,UINT32_MAX,UINT32_MAX},{1,0,UINT32_MAX},{50,4,10000}};
    for(unsigned n=0;n<sizeof(values)/sizeof(values[0]);++n){
        assert(product_config_set_vdc_reference_discipline_profile(&values[n]));
        assert(product_config_init());expect_discipline(&values[n]);
    }
}
static void discipline_preserve(void){
    test_reset_flash();assert(product_config_init());
    assert(product_config_set_vdc_reference_discipline_profile(&discipline_a));
    assert(product_config_set_vdc_reference_profile(&ref_a));
    const product_config_dpll_servo_profile_t servo={-9,3,1000,345,789};
    const product_config_dpll_control_profile_t control={1,3,17};
    const product_config_dpll_baseline_profile_t baseline={1,500};
    const product_config_vdc_output_timing_profile_t timing_profile={24000,32000,16000};
    assert(product_config_set_dpll_servo_profile(&servo));
    assert(product_config_set_dpll_control_profile(&control));
    assert(product_config_set_dpll_baseline_profile(&baseline));
    assert(product_config_set_dpll_output_compensation_ns(-68));
    assert(product_config_set_vdc_output_timing_profile(&timing_profile));
    assert(product_config_set_board_no(4));assert(product_config_set_usb_mode(PRODUCT_CONFIG_USB_MODE_USBTMC));
    assert(product_config_init());expect_discipline(&discipline_a);
    s_fail_program=true;assert(!product_config_set_vdc_reference_discipline_profile(&discipline_b));
    expect_discipline(&discipline_a);s_fail_program=false;s_corrupt_program=true;
    assert(!product_config_set_vdc_reference_discipline_profile(&discipline_b));expect_discipline(&discipline_a);
    s_corrupt_program=false;assert(product_config_init());expect_discipline(&discipline_a);
    assert(product_config_set_vdc_reference_discipline_profile(&discipline_b));
    assert(product_config_init());expect_discipline(&discipline_b);expect_ref(&ref_a);
    expect_timing(24000,32000,16000);assert(product_config_get_board_no()==4u);
    product_config_dpll_servo_profile_t s;product_config_dpll_control_profile_t c;
    product_config_dpll_baseline_profile_t b;int32_t d;
    assert(product_config_get_dpll_servo_profile(&s)&&!memcmp(&servo,&s,sizeof(s)));
    assert(product_config_get_dpll_control_profile(&c)&&!memcmp(&control,&c,sizeof(c)));
    assert(product_config_get_dpll_baseline_profile(&b)&&!memcmp(&baseline,&b,sizeof(b)));
    assert(product_config_get_dpll_output_compensation_ns(&d)&&d==-68);
}
static void discipline_crc(void){
    for(unsigned byte=108;byte<120;++byte){
        test_reset_flash();assert(product_config_init());
        assert(product_config_set_vdc_reference_discipline_profile(&discipline_a));
        assert(product_config_set_vdc_reference_discipline_profile(&discipline_b));
        s_flash[DRV_FLASH_PAGE_SIZE+byte]^=1u;
        assert(product_config_init());expect_discipline(&discipline_a);
        assert(s_program_count==2u&&s_erase_count==0u);
    }
}
static void discipline_powercut(void){
    test_reset_flash();assert(product_config_init());
    assert(product_config_set_vdc_reference_discipline_profile(&discipline_a));
    assert(product_config_set_vdc_reference_discipline_profile(&discipline_b));
    uint8_t first[DRV_FLASH_PAGE_SIZE],second[DRV_FLASH_PAGE_SIZE];
    memcpy(first,s_flash,sizeof(first));memcpy(second,s_flash+sizeof(first),sizeof(second));
    for(unsigned cut=0;cut<120;++cut){
        test_reset_flash();memcpy(s_flash,first,sizeof(first));
        memcpy(s_flash+DRV_FLASH_PAGE_SIZE,second,cut);
        assert(product_config_init());expect_discipline(&discipline_a);
        assert(s_program_count==0u&&s_erase_count==0u);
    }
    const unsigned slots=FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE/DRV_FLASH_PAGE_SIZE;
    test_reset_flash();assert(product_config_init());
    for(unsigned n=0;n<slots;++n)assert(product_config_set_vdc_reference_discipline_profile(&discipline_a));
    s_fail_program=true;assert(!product_config_set_vdc_reference_discipline_profile(&discipline_b));
    expect_discipline(&discipline_a);assert(s_erase_count==1u);s_fail_program=false;
    assert(product_config_init());expect_discipline(&discipline_a);
    assert(product_config_set_vdc_reference_discipline_profile(&discipline_b));
    assert(product_config_init());expect_discipline(&discipline_b);
    for(unsigned n=FLASH_DEPLOYMENT_MAP_PRODUCT_CONFIG_STORE_SIZE;n<OTA_PRODUCT_CONFIG_SIZE;++n)assert(s_flash[n]==0xA5);
}
int main(int argc,char **argv){
    assert(argc==2);
    if(!strcmp(argv[1],"legacy"))discipline_migrations();
    else if(!strcmp(argv[1],"values"))discipline_values();
    else if(!strcmp(argv[1],"preserve"))discipline_preserve();
    else if(!strcmp(argv[1],"crc"))discipline_crc();
    else if(!strcmp(argv[1],"powercut"))discipline_powercut();
    else assert(false);
    return 0;
}
'''
