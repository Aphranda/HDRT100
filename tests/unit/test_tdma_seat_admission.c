#include "tdma_seat_admission.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static tdma_seat_identity_t identity(void)
{
    return (tdma_seat_identity_t){.epoch=1, .cycle=7, .config_generation=2,
        .profile_generation=3, .binding_generation=4, .priority_sequence=0};
}

static tdma_seat_budget_t budget(void)
{
    tdma_seat_budget_t b = {.identity=identity(), .defined_mask=TDMA_SEAT_BUDGET_ALL};
    for (unsigned i=0; i<TDMA_SEAT_RESOURCE_COUNT; ++i) b.remaining[i]=8;
    return b;
}

static tdma_seat_request_t request(void)
{
    tdma_seat_request_t r = {.load_id=9, .duration_ns=20};
    for (unsigned i=0; i<TDMA_SEAT_RESOURCE_COUNT; ++i) r.cost[i]=2;
    return r;
}

static void begin(tdma_seat_admission_t *s)
{
    tdma_seat_identity_t id=identity();
    tdma_seat_window_t w={.start_ns=100, .end_ns=300, .close_guard_ns=40};
    assert(tdma_seat_admission_init(s));
    assert(s->state==TDMA_SEAT_CLOSED);
    assert(tdma_seat_admission_begin(s,&id,&w,50)==TDMA_SEAT_OK);
}

static void priority(tdma_seat_admission_t *s)
{
    begin(s);
    tdma_seat_identity_t id=identity();
    assert(tdma_seat_admission_priority_selected(s,&id,17,60)==TDMA_SEAT_OK);
    assert(tdma_seat_admission_priority_committed(s,&id,17,70)==TDMA_SEAT_OK);
}

static void granted(tdma_seat_admission_t *s)
{
    priority(s);
    tdma_seat_budget_t b=budget();
    tdma_seat_request_t r=request();
    assert(tdma_seat_admission_grant(s,&b,&r,100)==TDMA_SEAT_OK);
}

static void test_fact_order_and_one_shot(void)
{
    tdma_seat_admission_t s; begin(&s);
    tdma_seat_identity_t id=identity();
    tdma_seat_budget_t b=budget();
    tdma_seat_request_t r=request();
    assert(tdma_seat_admission_priority_committed(&s,&id,17,50)==TDMA_SEAT_STALE_IDENTITY);
    assert(tdma_seat_admission_grant(&s,&b,&r,50)==TDMA_SEAT_WRONG_STATE);
    assert(tdma_seat_admission_priority_selected(&s,&id,17,60)==TDMA_SEAT_OK);
    assert(!s.priority_committed && s.state==TDMA_SEAT_PRIORITY_SELECTED);
    assert(tdma_seat_admission_grant(&s,&b,&r,60)==TDMA_SEAT_WRONG_STATE);
    assert(tdma_seat_admission_priority_committed(&s,&id,18,60)==TDMA_SEAT_STALE_IDENTITY);
    assert(tdma_seat_admission_priority_committed(&s,&id,17,70)==TDMA_SEAT_OK);
    assert(tdma_seat_admission_grant(&s,&b,&r,99)==TDMA_SEAT_WINDOW_WAIT);
    assert(tdma_seat_admission_grant(&s,&b,&r,100)==TDMA_SEAT_OK);
    assert(!s.load_committed);
    assert(tdma_seat_admission_commit_load(&s,&id,9,100)==TDMA_SEAT_WRONG_STATE);
    assert(tdma_seat_admission_grant(&s,&b,&r,110)==TDMA_SEAT_DUPLICATE);
    assert(tdma_seat_admission_priority_selected(&s,&id,17,110)==TDMA_SEAT_DUPLICATE);
    assert(s.state==TDMA_SEAT_FIRST_CLASS_WINDOW_GRANTED);
    assert(tdma_seat_admission_priority_committed(&s,&id,17,110)==TDMA_SEAT_DUPLICATE);
    r.duration_ns++;
    assert(tdma_seat_admission_grant(&s,&b,&r,110)==TDMA_SEAT_WRONG_STATE);
    assert(s.request.duration_ns==20);
    assert(tdma_seat_admission_select_load(&s,&b,9,120)==TDMA_SEAT_OK);
    assert(!s.load_committed && s.state==TDMA_SEAT_LOAD_SELECTED);
    for (unsigned i=0; i<TDMA_SEAT_RESOURCE_COUNT; ++i) assert(s.budget.remaining[i]==6);
    assert(tdma_seat_admission_select_load(&s,&b,9,120)==TDMA_SEAT_DUPLICATE);
    assert(tdma_seat_admission_commit_load(&s,&id,9,120)==TDMA_SEAT_OK);
    assert(s.load_committed && s.state==TDMA_SEAT_LOAD_TX_COMMITTED);
    assert(tdma_seat_admission_commit_load(&s,&id,9,130)==TDMA_SEAT_DUPLICATE);
    assert(s.load_commit_time_ns==120);
    for (unsigned i=0; i<TDMA_SEAT_RESOURCE_COUNT; ++i) assert(s.budget.remaining[i]==6);
    assert(tdma_seat_admission_grant(&s,&b,&r,130)==TDMA_SEAT_WRONG_STATE);
}

static void test_each_budget_dimension(void)
{
    for (unsigned i=0; i<TDMA_SEAT_RESOURCE_COUNT; ++i) {
        tdma_seat_admission_t s; priority(&s);
        tdma_seat_budget_t b=budget();
        tdma_seat_request_t r=request();
        b.defined_mask &= ~(1u<<i);
        assert(tdma_seat_admission_grant(&s,&b,&r,100)==TDMA_SEAT_BUDGET_UNDEFINED);
        assert(s.state==TDMA_SEAT_PRIORITY_TX_COMMITTED);
        b.defined_mask=TDMA_SEAT_BUDGET_ALL; b.remaining[i]=1;
        assert(tdma_seat_admission_grant(&s,&b,&r,100)==TDMA_SEAT_BUDGET_EXHAUSTED);
        b.remaining[i]=2;
        assert(tdma_seat_admission_grant(&s,&b,&r,100)==TDMA_SEAT_OK);
        b.remaining[i]=1;
        assert(tdma_seat_admission_select_load(&s,&b,9,100)==TDMA_SEAT_BUDGET_EXHAUSTED);
        assert(s.state==TDMA_SEAT_REVOKED && !s.load_committed);
        assert(s.revoke_reason==TDMA_SEAT_REVOKE_BUDGET);
        granted(&s); b=budget(); b.defined_mask &= ~(1u<<i);
        assert(tdma_seat_admission_grant(&s,&b,&r,110)==TDMA_SEAT_BUDGET_UNDEFINED);
        assert(s.state==TDMA_SEAT_REVOKED);
    }
}

static void test_stale_binding_and_revoke(void)
{
    for (unsigned field=0; field<6; ++field) {
        tdma_seat_admission_t s; granted(&s);
        tdma_seat_budget_t b=budget();
        switch(field) {
        case 0: b.identity.epoch++; break;
        case 1: b.identity.cycle++; break;
        case 2: b.identity.config_generation++; break;
        case 3: b.identity.profile_generation++; break;
        case 4: b.identity.binding_generation++; break;
        case 5: b.identity.priority_sequence++; break;
        }
        assert(tdma_seat_admission_select_load(&s,&b,9,100)==TDMA_SEAT_STALE_IDENTITY);
        assert(s.state==TDMA_SEAT_FIRST_CLASS_WINDOW_GRANTED);
        assert(tdma_seat_admission_priority_committed(&s,&b.identity,17,100)==TDMA_SEAT_STALE_IDENTITY);
    }
    for (unsigned why=TDMA_SEAT_REVOKE_STOP; why<=TDMA_SEAT_REVOKE_BUDGET; ++why) {
        tdma_seat_admission_t s; granted(&s);
        tdma_seat_budget_t b=budget();
        assert(tdma_seat_admission_revoke(&s,(tdma_seat_revoke_reason_t)why));
        assert(tdma_seat_admission_select_load(&s,&b,9,110)==TDMA_SEAT_WRONG_STATE);
        assert(!s.load_committed);
        assert(tdma_seat_admission_begin(&s,&b.identity,&s.window,110)==TDMA_SEAT_STALE_IDENTITY);
    }
    tdma_seat_admission_t s; granted(&s);
    tdma_seat_identity_t id=identity();
    assert(tdma_seat_admission_priority_selected(&s,&id,18,100)==TDMA_SEAT_STALE_IDENTITY);
    assert(s.state==TDMA_SEAT_REVOKED && s.revoke_reason==TDMA_SEAT_REVOKE_NEW_PRIORITY);
}

static void test_time_and_epoch_boundaries(void)
{
    tdma_seat_admission_t s; granted(&s);
    tdma_seat_budget_t b=budget();
    assert(tdma_seat_admission_select_load(&s,&b,9,241)==TDMA_SEAT_WINDOW_EXPIRED);
    assert(s.state==TDMA_SEAT_REVOKED);
    granted(&s);
    assert(tdma_seat_admission_select_load(&s,&b,9,240)==TDMA_SEAT_OK);
    assert(tdma_seat_admission_commit_load(&s,&b.identity,9,240)==TDMA_SEAT_OK);
    assert(tdma_seat_admission_expire(&s,260)==TDMA_SEAT_WINDOW_EXPIRED);
    assert(s.load_committed && s.state==TDMA_SEAT_REVOKED);
    granted(&s);
    assert(tdma_seat_admission_select_load(&s,&b,9,99)==TDMA_SEAT_TIME_INVALID);
    assert(s.state==TDMA_SEAT_REVOKED);
    tdma_seat_window_t w={.start_ns=UINT64_MAX-100, .end_ns=UINT64_MAX, .close_guard_ns=10};
    b.identity.epoch=UINT64_MAX;
    assert(tdma_seat_admission_begin(&s,&b.identity,&w,UINT64_MAX-100)==TDMA_SEAT_OK);
    assert(tdma_seat_admission_priority_selected(&s,&b.identity,17,UINT64_MAX-99)==TDMA_SEAT_OK);
    assert(tdma_seat_admission_priority_committed(&s,&b.identity,17,UINT64_MAX-99)==TDMA_SEAT_OK);
    tdma_seat_request_t r=request(); r.duration_ns=UINT32_MAX;
    assert(tdma_seat_admission_grant(&s,&b,&r,UINT64_MAX-98)==TDMA_SEAT_WINDOW_EXPIRED);
    assert(tdma_seat_admission_expire(&s,UINT64_MAX)==TDMA_SEAT_WINDOW_EXPIRED);
    b.identity.epoch=1;
    assert(tdma_seat_admission_begin(&s,&b.identity,&w,UINT64_MAX)==TDMA_SEAT_STALE_IDENTITY);
    granted(&s); b=budget(); b.identity.epoch=3; w.end_ns=w.start_ns;
    assert(tdma_seat_admission_begin(&s,&b.identity,&w,110)==TDMA_SEAT_BAD_ARGUMENT);
    assert(s.state==TDMA_SEAT_REVOKED && s.epoch_high_water==3);
    b.identity.epoch=2;
    assert(tdma_seat_admission_begin(&s,&b.identity,&s.window,110)==TDMA_SEAT_STALE_IDENTITY);
    b.identity.epoch=4; w=(tdma_seat_window_t){300,500,40};
    assert(tdma_seat_admission_begin(&s,&b.identity,&w,300)==TDMA_SEAT_OK);
    assert(!s.selected_token && !s.priority_committed && !s.load_committed);
    assert(!s.request.load_id && !s.budget.defined_mask);
    b.identity.epoch=5;
    assert(tdma_seat_admission_begin(&s,&b.identity,&s.window,310)==TDMA_SEAT_OK);
    assert(s.window.start_ns==300 && s.window.end_ns==500 && s.window.close_guard_ns==40);
    s.identity.epoch=6;
    assert(tdma_seat_admission_begin(&s,&s.identity,&s.window,320)==TDMA_SEAT_OK);
    assert(s.identity.epoch==6 && s.identity.binding_generation==4 && s.window.end_ns==500);
    for (unsigned time=259; time<=260; ++time) {
        granted(&s); b=budget();
        assert(tdma_seat_admission_select_load(&s,&b,9,240)==TDMA_SEAT_OK);
        assert(tdma_seat_admission_commit_load(&s,&b.identity,9,time)==TDMA_SEAT_WINDOW_EXPIRED);
        assert(s.state==TDMA_SEAT_REVOKED && s.load_committed && s.load_commit_time_ns==time);
    }
}

static void test_null_and_backend_failure(void)
{
    assert(!tdma_seat_admission_init(NULL));
    assert(tdma_seat_admission_expire(NULL,0)==TDMA_SEAT_BAD_ARGUMENT);
    assert(tdma_seat_admission_begin(NULL,NULL,NULL,0)==TDMA_SEAT_BAD_ARGUMENT);
    assert(tdma_seat_admission_grant(NULL,NULL,NULL,0)==TDMA_SEAT_BAD_ARGUMENT);
    tdma_seat_admission_t s; granted(&s);
    tdma_seat_budget_t b=budget();
    assert(tdma_seat_admission_select_load(&s,&b,9,120)==TDMA_SEAT_OK);
    assert(tdma_seat_admission_revoke(&s,TDMA_SEAT_REVOKE_FAULT));
    assert(tdma_seat_admission_commit_load(&s,&b.identity,9,120)==TDMA_SEAT_WRONG_STATE);
    assert(!s.load_committed && s.budget.remaining[TDMA_SEAT_TX_BYTES]==6);
}

int main(void)
{
    test_fact_order_and_one_shot();
    test_each_budget_dimension();
    test_stale_binding_and_revoke();
    test_time_and_epoch_boundaries();
    test_null_and_backend_failure();
    puts("tdma_seat_admission: 5 case groups passed");
    return 0;
}
