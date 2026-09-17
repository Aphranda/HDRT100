#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "product_config.h"
#include "vdc_output_delay.h"
static unsigned core,session,metadata_calls,maintenance_calls,flash_calls;
static bool stopped=true,guard=false,readable=true,flash_ok=true;
static int32_t saved=-20;
static int dummy_service;
static void *s_vdc_tdma_service=&dummy_service;
static unsigned get_core_num(void){return core;}
static unsigned vdc_dpll_manager_feedback_session(void){return session;}
static bool tdma_service_update_stopped_metadata(void *s,bool(*callback)(void *),void *context)
{assert(s==&dummy_service);++metadata_calls;if(!stopped||guard)return false;
 guard=true;bool result=callback(context);guard=false;return result;}
static bool tdma_service_run_stopped_maintenance(void *s,bool(*callback)(void *),void *context)
{assert(s==&dummy_service);++maintenance_calls;if(!stopped||guard)return false;
 guard=true;bool result=callback(context);guard=false;return result;}
bool product_config_get_dpll_output_compensation_ns(int32_t *v)
{if(!readable)return false;*v=saved;return true;}
bool product_config_set_dpll_output_compensation_ns(int32_t v)
{assert(guard&&stopped&&core==0);++flash_calls;if(!flash_ok)return false;saved=v;return true;}
#include "components/vdc_dpll_manager/src/vdc_output_delay_config.inc"

typedef int scpi_t;
typedef int scpi_result_t;
#define TRUE 1
#define FALSE 0
#define SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA 1
typedef struct { const char *ptr; size_t len; int type; } scpi_parameter_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static bool parse_ok=true;
static int64_t parameter;
static const char *parameter_override;
static int32_t result_value;
static unsigned results,errors;
static int SCPI_Parameter(scpi_t *c,scpi_parameter_t *out,int required)
{static char token[32];(void)c;if(!required||!parse_ok)return 0;
 snprintf(token,sizeof(token),"%lld",(long long)parameter);
 out->ptr=parameter_override?parameter_override:token;out->len=strlen(out->ptr);
 out->type=SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA;return TRUE;}
static bool SCPI_ParamErrorOccurred(scpi_t *c){(void)c;return false;}
static void SCPI_ResultInt32(scpi_t *c,int32_t v){(void)c;++results;result_value=v;}
static void SCPI_ResultText(scpi_t *c,const char *v){(void)c;assert(!strcmp(v,"OK"));++results;}
static void scpi_port_push_exec_error(scpi_t *c,const char *v){(void)c;assert(v);++errors;}
#include "scpi_callbacks.inc"

static int32_t requested(void){int32_t v;assert(vdc_dpll_manager_get_output_delay_ns(&v));return v;}
static void reset_response(void){results=errors=0;}
int main(void)
{
    assert(!vdc_dpll_manager_get_output_delay_ns(NULL));
    assert(output_delay_init_from_product_config()&&requested()==-20&&flash_calls==0);
    readable=false;assert(!output_delay_init_from_product_config()&&requested()==-20);
    readable=true;session=1;assert(!output_delay_init_from_product_config());session=0;
    core=1;assert(!output_delay_init_from_product_config());core=0;
    scpi_t c=0;
    const int32_t values[]={INT32_MIN,-1000000,-1,0,1,1000000,INT32_MAX};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i){
        parameter=values[i];reset_response();
        assert(scpi_cmd_vdc_output_delay(&c)==SCPI_RES_OK&&results==1&&errors==0&&result_value==values[i]);
        assert(requested()==values[i]&&saved==-20&&flash_calls==0);
        reset_response();assert(scpi_cmd_vdc_output_delay_q(&c)==SCPI_RES_OK&&result_value==values[i]);
    }
    unsigned calls=metadata_calls;parse_ok=false;reset_response();
    assert(scpi_cmd_vdc_output_delay(&c)==SCPI_RES_ERR&&errors==1&&results==0&&metadata_calls==calls);
    parse_ok=true;
    const int64_t invalid[]={ (int64_t)INT32_MIN-1, (int64_t)INT32_MAX+1,
                             INT64_MIN, INT64_MAX };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){
        parameter=invalid[i];reset_response();
        assert(scpi_cmd_vdc_output_delay(&c)==SCPI_RES_ERR&&errors==1&&results==0);
        assert(metadata_calls==calls&&requested()==INT32_MAX&&saved==-20&&flash_calls==0);
    }
    parameter=100;
    const char *malformed[]={"", "+", "-", "1.5", "1e12", "#HFFFFFFFFFFFFFFFF", "125ns", "123abc"};
    for(unsigned i=0;i<sizeof(malformed)/sizeof(malformed[0]);++i){
        parameter_override=malformed[i];reset_response();
        assert(scpi_cmd_vdc_output_delay(&c)==SCPI_RES_ERR&&errors==1&&results==0);
        assert(metadata_calls==calls&&requested()==INT32_MAX&&saved==-20&&flash_calls==0);
    }
    parameter_override=NULL;
    for(unsigned blocked=0;blocked<4;++blocked){
        core=blocked==0?1u:0u;stopped=blocked!=1;guard=blocked==2;
        s_vdc_tdma_service=blocked==3?NULL:&dummy_service;
        reset_response();assert(scpi_cmd_vdc_output_delay(&c)==SCPI_RES_ERR&&results==0);
        reset_response();assert(scpi_cmd_vdc_output_delay_default(&c)==SCPI_RES_ERR&&results==0);
        reset_response();assert(scpi_cmd_vdc_output_delay_recall(&c)==SCPI_RES_ERR&&results==0);
        reset_response();assert(scpi_cmd_vdc_output_delay_store(&c)==SCPI_RES_ERR&&results==0);
        assert(requested()==INT32_MAX&&saved==-20&&flash_calls==0);
    }
    core=0;stopped=true;guard=false;s_vdc_tdma_service=&dummy_service;
    reset_response();assert(scpi_cmd_vdc_output_delay_default(&c)==SCPI_RES_OK&&requested()==0&&saved==-20);
    reset_response();assert(scpi_cmd_vdc_output_delay_recall(&c)==SCPI_RES_OK&&requested()==-20);
    assert(vdc_dpll_manager_set_output_delay_ns(100));
    readable=false;reset_response();assert(scpi_cmd_vdc_output_delay_recall(&c)==SCPI_RES_ERR&&requested()==100);readable=true;
    flash_ok=false;reset_response();assert(scpi_cmd_vdc_output_delay_store(&c)==SCPI_RES_ERR&&results==0&&saved==-20);
    flash_ok=true;reset_response();assert(scpi_cmd_vdc_output_delay_store(&c)==SCPI_RES_OK&&results==1&&saved==100);
    assert(vdc_dpll_manager_default_output_delay()&&requested()==0);
    assert(output_delay_init_from_product_config()&&requested()==100);
    /* Future owner takes a value copy once. Changing requested never mutates it. */
    const int32_t latched=requested();assert(vdc_dpll_manager_set_output_delay_ns(-100));
    assert(latched==100&&requested()==-100);
    assert(flash_calls==2u&&maintenance_calls>=4u);
    puts("output delay actual service and SCPI callback tests passed");return 0;
}
