#include <assert.h>
#include <stdint.h>
#include "sync_io_reference_math.h"
int main(void)
{
    sync_io_reference_config_t c={4,0,10000000,1000,2500};
    uint32_t n,ticks;int32_t ppb;
    assert(sync_io_reference_validate(&c,&n) && n==10000000);
    assert(!sync_io_reference_validate(NULL,&n));
    assert(!sync_io_reference_validate(&c,NULL));
    assert(sync_io_reference_evaluate(&c,250000000,42,250000043,&ticks,&ppb));
    assert(ticks==250000000 && ppb==0);
    assert(sync_io_reference_evaluate(&c,250000000,UINT32_MAX-10,249999990,&ticks,&ppb));
    assert(ticks==250000000 && ppb==0);
    assert(sync_io_reference_evaluate(&c,250000000,0,249999989,&ticks,&ppb));
    assert(ppb==48);
    assert(sync_io_reference_evaluate(&c,250000000,0,250000014,&ticks,&ppb));
    assert(ppb==-51);
    assert(!sync_io_reference_evaluate(&c,250000000,0,1,&ticks,&ppb));
    assert(!sync_io_reference_evaluate(&c,250000000,0,2,&ticks,&ppb));
    assert(!sync_io_reference_evaluate(&c,250000000,0,625000001,&ticks,&ppb));
    assert(!sync_io_reference_evaluate(&c,0,0,250000001,&ticks,&ppb));
    assert(!sync_io_reference_evaluate(&c,250000001,0,250000001,&ticks,&ppb));
    c.input_port=0;assert(!sync_io_reference_validate(&c,&n));c.input_port=5;
    assert(!sync_io_reference_validate(&c,&n));c.input_port=1;
    c.edge=2;assert(!sync_io_reference_validate(&c,&n));c.edge=1;
    c.nominal_hz=999;assert(!sync_io_reference_validate(&c,&n));
    c.nominal_hz=20000001;assert(!sync_io_reference_validate(&c,&n));
    c.nominal_hz=1001;c.window_ms=100;assert(!sync_io_reference_validate(&c,&n));
    c.nominal_hz=1000;assert(sync_io_reference_validate(&c,&n)&&n==100);
    c.window_ms=99;assert(!sync_io_reference_validate(&c,&n));
    c.window_ms=5001;assert(!sync_io_reference_validate(&c,&n));
    c.window_ms=5000;c.timeout_ms=10000;c.nominal_hz=20000000;
    assert(sync_io_reference_validate(&c,&n)&&n==100000000);
    assert(sync_io_reference_evaluate(&c,250000000,0,1250000001,&ticks,&ppb)&&ppb==0);
    c.timeout_ms=10001;assert(!sync_io_reference_validate(&c,&n));
    c.timeout_ms=5000;assert(!sync_io_reference_validate(&c,&n));
    return 0;
}
