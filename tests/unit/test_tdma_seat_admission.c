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

static void test_loop_plan(void)
{
    tdma_loop_plan_t plan;
    tdma_loop_plan_request_t r={.loop_period_ns=4000000u,.emission_period_ns=1500000u,.close_guard_ns=0u,.priority_pending=true,.first_class_pending=true,.load_pending=true,.start_offset_ns=0u};
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==2);
    assert(plan.slots[0].cls==TDMA_LOOP_SLOT_PRIORITY);
    assert(plan.slots[0].window.start_ns==0u && plan.slots[0].window.end_ns==1500000u);
    assert(plan.slots[1].cls==TDMA_LOOP_SLOT_FIRST_CLASS);
    assert(plan.slots[1].window.start_ns==1500000u && plan.slots[1].window.end_ns==3000000u);
    assert(plan.slots[2].cls==TDMA_LOOP_SLOT_NONE);
    r.loop_period_ns=4500000u;
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==3 && plan.slots[2].cls==TDMA_LOOP_SLOT_LOAD);
    assert(plan.slots[2].window.start_ns==3000000u && plan.slots[2].window.end_ns==4500000u);
    r.loop_period_ns=4000000u; r.close_guard_ns=1100000u;
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==1 && plan.slots[0].cls==TDMA_LOOP_SLOT_PRIORITY);
    r.close_guard_ns=0u; r.priority_pending=false;
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==2 && plan.slots[0].cls==TDMA_LOOP_SLOT_FIRST_CLASS);
    assert(plan.slots[1].cls==TDMA_LOOP_SLOT_LOAD && plan.slots[1].window.end_ns==3000000u);
    r.priority_pending=true; r.first_class_pending=false;
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==2 && plan.slots[1].cls==TDMA_LOOP_SLOT_LOAD);
    assert(tdma_loop_plan(NULL,&plan)==TDMA_SEAT_BAD_ARGUMENT);
    assert(tdma_loop_plan(&r,NULL)==TDMA_SEAT_BAD_ARGUMENT);
    tdma_loop_plan_request_t bad={.loop_period_ns=0u,.emission_period_ns=1500000u,.close_guard_ns=0u,.priority_pending=true,.first_class_pending=true,.load_pending=true,.start_offset_ns=0u};
    assert(tdma_loop_plan(&bad,&plan)==TDMA_SEAT_BAD_ARGUMENT);
    bad.loop_period_ns=1000000u;
    assert(tdma_loop_plan(&bad,&plan)==TDMA_SEAT_BAD_ARGUMENT);
}

/* Production intent: three emissions per 4 ms loop — PRIORITY, FIRST_CLASS and
 * a flexible LOAD seat — so the emission period must leave room for three
 * windows inside one loop period. */
static void test_loop_plan_three_per_loop(void)
{
    tdma_loop_plan_t plan;
    tdma_loop_plan_request_t r={.loop_period_ns=4000000u,.emission_period_ns=1300000u,.close_guard_ns=0u,.priority_pending=true,.first_class_pending=true,.load_pending=true,.start_offset_ns=0u};
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==3);
    assert(plan.slots[0].cls==TDMA_LOOP_SLOT_PRIORITY && plan.slots[0].window.start_ns==0u);
    assert(plan.slots[1].cls==TDMA_LOOP_SLOT_FIRST_CLASS && plan.slots[1].window.start_ns==1300000u);
    assert(plan.slots[2].cls==TDMA_LOOP_SLOT_LOAD && plan.slots[2].window.start_ns==2600000u);
    assert(plan.slots[2].window.end_ns==3900000u);
    r.close_guard_ns=30000u;
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK && plan.count==3);
    r.close_guard_ns=0u; r.emission_period_ns=1340000u;   /* 3x1.34ms > 4ms */
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK && plan.count==2);
    /* A seat that does not fit is reported, never silently omitted: with the
     * third emission dropped the mask names exactly LOAD, and with an offset
     * that leaves room for one window only the first class is reported. */
    assert(plan.dropped_mask==TDMA_LOOP_PLAN_DROPPED_LOAD);
    assert(!tdma_loop_plan_seat_dropped(&plan,TDMA_LOOP_SLOT_PRIORITY));
    assert(!tdma_loop_plan_seat_dropped(&plan,TDMA_LOOP_SLOT_FIRST_CLASS));
    assert(tdma_loop_plan_seat_dropped(&plan,TDMA_LOOP_SLOT_LOAD));
    tdma_loop_plan_request_t starved={.loop_period_ns=4000000u,.emission_period_ns=1500000u,
        .close_guard_ns=30000u,.priority_pending=true,.first_class_pending=true,
        .load_pending=false,.start_offset_ns=1000000u};
    assert(tdma_loop_plan(&starved,&plan)==TDMA_SEAT_OK);
    assert(plan.count==1 && plan.slots[0].cls==TDMA_LOOP_SLOT_PRIORITY);
    assert(plan.dropped_mask==TDMA_LOOP_PLAN_DROPPED_FIRST_CLASS);
    assert(tdma_loop_plan_seat_dropped(&plan,TDMA_LOOP_SLOT_FIRST_CLASS));
    assert(!tdma_loop_plan_seat_dropped(NULL,TDMA_LOOP_SLOT_FIRST_CLASS));
    assert(!tdma_loop_plan_seat_dropped(&plan,TDMA_LOOP_SLOT_NONE));
    /* An offset that leaves room for nothing reports every requested seat. */
    starved.start_offset_ns=2500000u;
    assert(tdma_loop_plan(&starved,&plan)==TDMA_SEAT_OK && plan.count==0);
    assert(plan.dropped_mask==(TDMA_LOOP_PLAN_DROPPED_PRIORITY |
                              TDMA_LOOP_PLAN_DROPPED_FIRST_CLASS));
    /* Anchored at the loop start nothing is dropped. */
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    r.emission_period_ns=1300000u;
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK && plan.dropped_mask==0u);

    /* Seat-window decision (the rule the origin publish gate uses): a seat the
     * plan cannot serve at all is STARVED -- whether it was dropped for space or
     * (like the deliberately disabled LOAD seat) never requested; the right seat
     * on the wrong boundary is a plain DEFER. */
    tdma_loop_plan_request_t two={.loop_period_ns=4000000u,.emission_period_ns=1300000u,
        .close_guard_ns=0u,.priority_pending=true,.first_class_pending=true,
        .load_pending=false,.start_offset_ns=0u};
    assert(tdma_loop_plan(&two,&plan)==TDMA_SEAT_OK && plan.count==2);
    assert(tdma_loop_plan_seat_present(&plan,TDMA_LOOP_SLOT_PRIORITY));
    assert(tdma_loop_plan_seat_present(&plan,TDMA_LOOP_SLOT_FIRST_CLASS));
    assert(!tdma_loop_plan_seat_present(&plan,TDMA_LOOP_SLOT_LOAD));
    assert(!tdma_loop_plan_seat_present(&plan,TDMA_LOOP_SLOT_NONE));
    assert(!tdma_loop_plan_seat_present(NULL,TDMA_LOOP_SLOT_PRIORITY));
    assert(tdma_loop_plan_seat_service(&plan,TDMA_LOOP_SLOT_NONE,0u,1000000u)==
           TDMA_LOOP_SEAT_SERVICE_OK);
    assert(tdma_loop_plan_seat_service(&plan,TDMA_LOOP_SLOT_LOAD,0u,1000000u)==
           TDMA_LOOP_SEAT_SERVICE_STARVED);   /* disabled seat never gets a window */
    assert(tdma_loop_plan_seat_service(&plan,TDMA_LOOP_SLOT_PRIORITY,0u,1000000u)==
           TDMA_LOOP_SEAT_SERVICE_OK);        /* first window, priority */
    assert(tdma_loop_plan_seat_service(&plan,TDMA_LOOP_SLOT_FIRST_CLASS,0u,1000000u)==
           TDMA_LOOP_SEAT_SERVICE_DEFER);
    assert(tdma_loop_plan_seat_service(&plan,TDMA_LOOP_SLOT_FIRST_CLASS,1u,1000000u)==
           TDMA_LOOP_SEAT_SERVICE_DEFER);     /* beyond the loop: no window */
    assert(tdma_loop_plan_seat_service(NULL,TDMA_LOOP_SLOT_FIRST_CLASS,0u,1000000u)==
           TDMA_LOOP_SEAT_SERVICE_STARVED);
    /* A first-class seat dropped for space is starved on every boundary. */
    tdma_loop_plan_request_t offset_request={.loop_period_ns=4000000u,
        .emission_period_ns=1500000u,.close_guard_ns=30000u,.priority_pending=true,
        .first_class_pending=true,.load_pending=false,.start_offset_ns=1000000u};
    tdma_loop_plan_t dropped_plan;
    assert(tdma_loop_plan(&offset_request,&dropped_plan)==TDMA_SEAT_OK);
    assert(dropped_plan.dropped_mask==TDMA_LOOP_PLAN_DROPPED_FIRST_CLASS);
    assert(tdma_loop_plan_seat_service(&dropped_plan,TDMA_LOOP_SLOT_FIRST_CLASS,0u,1000000u)==
           TDMA_LOOP_SEAT_SERVICE_STARVED);
}

/* The third (flexible LOAD) emission is supported but deliberately NOT enabled
 * today: the owner simply leaves the load seat unrequested, so the loop keeps
 * exactly two emissions even when a third window would fit. */
static void test_loop_plan_third_disabled(void)
{
    tdma_loop_plan_t plan;
    tdma_loop_plan_request_t r={.loop_period_ns=4000000u,.emission_period_ns=1300000u,.close_guard_ns=0u,.priority_pending=true,.first_class_pending=true,.load_pending=false,.start_offset_ns=0u};
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==2);
    assert(plan.slots[0].cls==TDMA_LOOP_SLOT_PRIORITY);
    assert(plan.slots[1].cls==TDMA_LOOP_SLOT_FIRST_CLASS);
    assert(plan.slots[2].cls==TDMA_LOOP_SLOT_NONE);
    tdma_loop_plan_request_t nominal={.loop_period_ns=4000000u,.emission_period_ns=1500000u,.close_guard_ns=0u,.priority_pending=true,.first_class_pending=true,.load_pending=false,.start_offset_ns=0u};
    assert(tdma_loop_plan(&nominal,&plan)==TDMA_SEAT_OK && plan.count==2);
}

static void test_loop_plan_defaults(void)
{
    tdma_loop_plan_t plan;
    tdma_loop_plan_request_t r;
    tdma_loop_plan_default_request(&r,true,true,true);
    assert(r.loop_period_ns==4000000u && r.emission_period_ns==1500000u);
    assert(r.close_guard_ns==30000u);
    assert(r.load_pending==(TDMA_LOOP_PLAN_LOAD_ENABLED!=0));
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==2 && plan.slots[1].cls==TDMA_LOOP_SLOT_FIRST_CLASS);
    tdma_loop_plan_default_request(NULL,true,true,true);
}

/* Composition: planned windows must be directly admissible by the seat state
 * machine (nonzero close guard, increasing epoch, monotonic time). */
static void test_loop_plan_drives_seat_windows(void)
{
    tdma_loop_plan_t plan;
    tdma_loop_plan_request_t r;
    tdma_loop_plan_default_request(&r,true,true,false);
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK && plan.count==2);
    tdma_seat_admission_t s;
    assert(tdma_seat_admission_init(&s));
    tdma_seat_identity_t id={0};
    id.config_generation=1; id.profile_generation=1; id.binding_generation=1;
    uint64_t now=0;
    for (uint32_t i=0;i<plan.count;++i) {
        id.epoch=1u+i;
        assert(tdma_seat_admission_begin(&s,&id,&plan.slots[i].window,now)==TDMA_SEAT_OK);
        assert(s.window.start_ns==plan.slots[i].window.start_ns);
        assert(s.window.end_ns==plan.slots[i].window.end_ns);
        assert(s.window.close_guard_ns==plan.slots[i].window.close_guard_ns);
        now=plan.slots[i].window.start_ns;
    }
}

/* Compressing the plan forward: the same two emissions may start later inside
 * the loop; a start offset that leaves no complete window yields no slot. */
static void test_loop_plan_compressed_forward(void)
{
    tdma_loop_plan_t plan;
    tdma_loop_plan_request_t r={.loop_period_ns=4000000u,.emission_period_ns=1500000u,.close_guard_ns=30000u,.priority_pending=true,.first_class_pending=true,.load_pending=false,.start_offset_ns=200000u};
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==2);
    assert(plan.slots[0].window.start_ns==200000u && plan.slots[0].window.end_ns==1700000u);
    assert(plan.slots[1].window.start_ns==1700000u && plan.slots[1].window.end_ns==3200000u);
    r.start_offset_ns=2600000u;
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK);
    assert(plan.count==0);
    r.start_offset_ns=0u;
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK && plan.count==2);
    tdma_loop_plan_request_t d;
    tdma_loop_plan_default_request(&d,true,true,true);
    assert(d.start_offset_ns==0u);
}

/* The origin asks which class may emit at a given loop time. */
static void test_loop_plan_slot_for_time(void)
{
    tdma_loop_plan_t plan;
    tdma_loop_plan_request_t r;
    tdma_loop_plan_default_request(&r,true,true,true);
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK && plan.count==2);
    assert(tdma_loop_plan_slot_for_time(&plan,0u)==TDMA_LOOP_SLOT_PRIORITY);
    /* The seat contract closes a window close_guard_ns before its end, so the
     * usable emission span is [start, end-guard): 30 us of 1.5 ms here. */
    assert(tdma_loop_plan_slot_for_time(&plan,1469999u)==TDMA_LOOP_SLOT_PRIORITY);
    assert(tdma_loop_plan_slot_for_time(&plan,1470000u)==TDMA_LOOP_SLOT_NONE);
    assert(tdma_loop_plan_slot_for_time(&plan,1499999u)==TDMA_LOOP_SLOT_NONE);
    assert(tdma_loop_plan_slot_for_time(&plan,1500000u)==TDMA_LOOP_SLOT_FIRST_CLASS);
    assert(tdma_loop_plan_slot_for_time(&plan,2969999u)==TDMA_LOOP_SLOT_FIRST_CLASS);
    assert(tdma_loop_plan_slot_for_time(&plan,2970000u)==TDMA_LOOP_SLOT_NONE);
    assert(tdma_loop_plan_slot_for_time(&plan,3000000u)==TDMA_LOOP_SLOT_NONE);
    assert(tdma_loop_plan_slot_for_time(&plan,9999999u)==TDMA_LOOP_SLOT_NONE);
    assert(tdma_loop_plan_slot_for_time(NULL,0u)==TDMA_LOOP_SLOT_NONE);
    tdma_loop_plan_t empty={.count=0u,.slots={{.cls=TDMA_LOOP_SLOT_NONE,.window={0u,0u,0u}}},.loop_period_ns=0u};
    assert(tdma_loop_plan_slot_for_time(&empty,10u)==TDMA_LOOP_SLOT_NONE);
}

/* Loop-level contract: scanning the whole loop yields exactly two emission
 * classes, in order PRIORITY then FIRST_CLASS (the flexible LOAD seat is off). */
static void test_loop_plan_two_emissions_per_loop(void)
{
    tdma_loop_plan_t plan;
    tdma_loop_plan_request_t r;
    tdma_loop_plan_default_request(&r,true,true,true);
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK && plan.count==2);
    tdma_loop_slot_class_t seq[4]={TDMA_LOOP_SLOT_NONE,TDMA_LOOP_SLOT_NONE,
        TDMA_LOOP_SLOT_NONE,TDMA_LOOP_SLOT_NONE};
    unsigned count=0u;
    for (uint64_t t=0u; t<r.loop_period_ns; t+=1000u) {
        const tdma_loop_slot_class_t cls=tdma_loop_plan_slot_for_time(&plan,t);
        if (cls!=TDMA_LOOP_SLOT_NONE && (count==0u || seq[count-1u]!=cls)) {
            assert(count<4u);
            seq[count++]=cls;
        }
    }
    assert(count==2u);
    assert(seq[0]==TDMA_LOOP_SLOT_PRIORITY && seq[1]==TDMA_LOOP_SLOT_FIRST_CLASS);
    for (uint64_t t=r.loop_period_ns-1000u; t<r.loop_period_ns; t+=1000u)
        assert(tdma_loop_plan_slot_for_time(&plan,t)==TDMA_LOOP_SLOT_NONE);
}

/* Slot-index gating: with a 1 ms slot period the loop's two emissions appear at
 * slots 0/1 (priority) and 2 (first class); the loop wraps after 4 slots. */
static void test_loop_plan_slot_for_index(void)
{
    tdma_loop_plan_t plan;
    tdma_loop_plan_request_t r;
    tdma_loop_plan_default_request(&r,true,true,true);
    assert(tdma_loop_plan(&r,&plan)==TDMA_SEAT_OK && plan.loop_period_ns==4000000u);
    assert(tdma_loop_plan_slot_for_index(&plan,0u,1000000u)==TDMA_LOOP_SLOT_PRIORITY);
    assert(tdma_loop_plan_slot_for_index(&plan,1u,1000000u)==TDMA_LOOP_SLOT_PRIORITY);
    assert(tdma_loop_plan_slot_for_index(&plan,2u,1000000u)==TDMA_LOOP_SLOT_FIRST_CLASS);
    assert(tdma_loop_plan_slot_for_index(&plan,3u,1000000u)==TDMA_LOOP_SLOT_NONE);
    assert(tdma_loop_plan_slot_for_index(&plan,4u,1000000u)==TDMA_LOOP_SLOT_PRIORITY);
    assert(tdma_loop_plan_slot_for_index(&plan,6u,1000000u)==TDMA_LOOP_SLOT_FIRST_CLASS);
    assert(tdma_loop_plan_slot_for_index(&plan,7u,1000000u)==TDMA_LOOP_SLOT_NONE);
    assert(tdma_loop_plan_slot_for_index(&plan,1u,0u)==TDMA_LOOP_SLOT_NONE);
    assert(tdma_loop_plan_slot_for_index(NULL,1u,1000000u)==TDMA_LOOP_SLOT_NONE);
}

int main(void)
{
    test_fact_order_and_one_shot();
    test_each_budget_dimension();
    test_stale_binding_and_revoke();
    test_time_and_epoch_boundaries();
    test_null_and_backend_failure();
    test_loop_plan();
    test_loop_plan_three_per_loop();
    test_loop_plan_third_disabled();
    test_loop_plan_defaults();
    test_loop_plan_drives_seat_windows();
    test_loop_plan_compressed_forward();
    test_loop_plan_slot_for_time();
    test_loop_plan_two_emissions_per_loop();
    test_loop_plan_slot_for_index();
    puts("tdma_seat_admission: 14 case groups passed");
    return 0;
}
