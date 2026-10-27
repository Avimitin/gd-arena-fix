#pragma once
#include "../src/system_timing.h"
static uint64_t __fastcall system_probe_stub() {return 0;}
static void native_system_timing_test() {
    using F=gd::target::CounterForm;
    const auto& p=gd::target::get();const auto hooks=gd::system_inline_hooks();
    size_t object_size=0;
    for(const auto& s:p.system_counters)if(s.base!=gd::target::CounterBase::Global)object_size=std::max(object_size,size_t(s.value)+16);
    std::vector<uint8_t> object(object_size);
    // Exercise every instruction form with its actual mapped data location.
    for(size_t index=0;index<p.system_counters.size();++index) {
        const auto& s=p.system_counters[index];const bool global=s.base==gd::target::CounterBase::Global;
        if(global) {timing_commit(s.value,4);if(s.identity)timing_commit(s.identity,4);}
        if(s.form==F::IncR8LoadRdx||s.form==F::IncEcxStoreR8)timing_commit(s.auxiliary,4);
        const bool down=s.form==F::DecEax||s.form==F::DecEaxEcx;
        auto& value=global?timing_memory<uint32_t>(s.value):anim_field<uint32_t>(object.data(),s.value);
        std::unique_ptr<TimingRedirect> probe;
        if(s.form==F::IncMemoryCall)probe=std::make_unique<TimingRedirect>(s.auxiliary,reinterpret_cast<void*>(system_probe_stub));
        for(unsigned hz:{95U,120U,144U,240U,360U}) {
            value=down?120:0;if(s.identity) {
                if(global)timing_memory<uint32_t>(s.identity)=17;
                else anim_field<uint32_t>(object.data(),s.identity)=17;
            }
            struct {gd::InlineContext c{};std::array<uint64_t,32> stack{};} fixture;
            auto& c=fixture.c;
            auto invoke=[&] {
                c.rax=c.rcx=c.rdx=c.r8=value;c.rsi=2;c.flags=0x203;
                c.rdi=c.rbx=c.r13=c.r14=reinterpret_cast<uintptr_t>(object.data());
                c.ip=reinterpret_cast<uintptr_t>(gd::at(s.site.address))+s.site.signature.length;
                const auto hook=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==&s.site;});
                hook->handler(c);
                if(s.form==F::IncR8LoadRdx)check(c.rdx==uint64_t(int64_t(int32_t(timing_memory<uint32_t>(s.auxiliary)))),"displaced signed state load preserved");
                if(s.form==F::IncEcxStoreR8)check(timing_memory<uint32_t>(s.auxiliary)==uint32_t(c.r8),"displaced sibling counter store preserved");
                if(s.form==F::IncEaxCmpEsi2)check(c.flags&64,"displaced service parameter comparison preserved");
                else if(s.form!=F::IncEcxCmp60&&s.form!=F::IncMemoryCmp)check(c.flags&1,"counter step preserves native carry flag");
            };
            gd::begin_timing_frame(0);gd::begin_timing_frame(0);invoke();invoke();
            check(value==(down?120U:1U),"counter initialization is immediate and duplicate-safe");
            for(unsigned frame=0;frame<hz;++frame) {gd::begin_timing_frame(1.0/hz);invoke();invoke();}
            check(value==(down?60U:61U),"system wait/countdown is cadence independent");
            gd::begin_timing_frame(10);invoke();check(value==(down?0U:661U),"system wait/countdown retains stalled time");
        }
    }
    auto invoke=[&](const gd::target::Site& site,gd::InlineContext& c) {
        const auto hook=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==&site;});
        c.ip=reinterpret_cast<uintptr_t>(gd::at(site.address))+site.signature.length;hook->handler(c);
    };
    timing_commit(p.lifecycle_wait_age,4);timing_commit(p.lifecycle_wait_state,4);timing_commit(p.lifecycle_init_age,4);
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        auto& age=timing_memory<uint32_t>(p.lifecycle_wait_age);age=0;timing_memory<uint32_t>(p.lifecycle_wait_state)=1;
        gd::InlineContext c{};gd::begin_timing_frame(0);gd::begin_timing_frame(0);invoke(p.lifecycle_wait_begin,c);
        unsigned n=0;
        while(age<10&&n<hz) {
            gd::begin_timing_frame(1.0/hz);invoke(p.lifecycle_wait_begin,c);
            for(const auto& site:p.lifecycle_wait_steps)invoke(site,c);
            invoke(p.lifecycle_wait_begin,c);++n;
        }
        check(std::abs(double(n)/hz-5.0/60)<=1.0/hz+1e-9,"native double-increment prologue retains its five-tick duration");
        auto& init=timing_memory<uint32_t>(p.lifecycle_init_age);init=0;
        gd::begin_timing_frame(0);invoke(p.lifecycle_init_step,c);
        gd::begin_timing_frame(70);invoke(p.lifecycle_init_step,c);c.rax=init;invoke(p.lifecycle_init_event,c);
        check(!(c.flags&64),"initialization cue does not fire early");
        gd::begin_timing_frame(5);invoke(p.lifecycle_init_step,c);c.rax=init;invoke(p.lifecycle_init_event,c);
        check(c.flags&64,"initialization cue survives a stall past its 72-second deadline");
        invoke(p.lifecycle_init_event,c);check(!(c.flags&64),"initialization cue is a single global event");
    }
    // Run the actual button-gesture state machine: render frequency must not
    // shorten its release, double-press or held-button windows.
    timing_commit(p.service_gesture_state,16);
    const auto gesture=reinterpret_cast<uint64_t(__fastcall*)(int,int)>(gd::at(p.service_gesture));
    auto& state=timing_memory<uint32_t>(p.service_gesture_state);auto& age=timing_memory<uint32_t>(p.service_gesture_age);
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        state=1;age=0;timing_memory<uint16_t>(p.service_gesture_edges)=0;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);gesture(0,0);
        for(unsigned f=0;f<hz*3-1;++f) {gd::begin_timing_frame(1.0/hz);gesture(0,0);gesture(0,0);}
        check(state==1,"native gesture release window does not finish early");
        gd::begin_timing_frame(1.0/hz);gesture(0,0);check(state==2,"native gesture release window expires after three seconds");
        gesture(0,1);check(state==3&&age==0,"native gesture input edge remains immediate");
        gd::begin_timing_frame(0);gesture(0,0);
        gd::begin_timing_frame(0.5);gesture(0,0);check(age==31,"gesture elapsed phase reaches half second");
        gesture(0,1);check(state==4,"second press is handled within the current render");
        gd::begin_timing_frame(0.5);gesture(0,0);check(state==1,"gesture states 3/4 share their original continuous deadline");
    }
    gd::end_scene_frame();puts("PASS: system counter instruction forms and native service gestures at 95/120/144/240/360 Hz");
}

static bool wait_held[2],wait_done;
static unsigned wait_polls[2],wait_updates,wait_closes,wait_completion_polls;
static uint64_t __fastcall wait_input(unsigned player) {++wait_polls[player];return wait_held[player];}
static uint64_t __fastcall wait_update(void*) {++wait_updates;return 0;}
static uint64_t __fastcall wait_close(void*) {++wait_closes;return 0;}
static uint64_t __fastcall wait_completion(void*) {++wait_completion_polls;return wait_done;}

static void native_remaining_wait_test() {
    namespace L=gd::target::layout;
    const auto& p=gd::target::get();
    for(auto address:{p.handshake_state,p.handshake_age,p.request_scene_active,p.request_scene_state,
            p.request_scene_age,p.request_scene_total,p.request_scene_status,p.scene_abort,p.network_nodes,p.network_connection})
        timing_commit(address,sizeof(uint32_t));
    timing_memory<uint32_t>(p.scene_abort)=0;
    timing_memory<uint32_t>(p.network_nodes)=2;timing_memory<uint32_t>(p.network_connection)=0;
    timing_memory<uint8_t>(p.request_scene_status)=0;
    const auto handshake=reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.handshake_update));
    const auto request=reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.request_scene_update));
    const auto hold=reinterpret_cast<uint64_t(__fastcall*)(void*)>(gd::at(p.hold_scene_update));
    TimingRedirect input(p.hold_scene_input,reinterpret_cast<void*>(wait_input));
    std::array<uintptr_t,L::hold_scene_vtable_size> vtable{};
    vtable[L::hold_scene_update_slot]=reinterpret_cast<uintptr_t>(wait_update);
    vtable[L::hold_scene_close_slot]=reinterpret_cast<uintptr_t>(wait_close);
    vtable[L::hold_scene_done_slot]=reinterpret_cast<uintptr_t>(wait_completion);
    auto& controller=timing_memory<uintptr_t>(p.hold_scene_controller);
    const auto saved_controller=controller;controller=reinterpret_cast<uintptr_t>(vtable.data());
    std::array<uint8_t,L::hold_scene_size> object{};
    auto& state=anim_field<uint32_t>(object.data(),L::hold_scene_state);
    auto& held_age=anim_field<uint32_t>(object.data(),L::hold_scene_hold);
    auto& close_age=anim_field<uint32_t>(object.data(),L::hold_scene_close);
    auto& active=object[L::hold_scene_active];
    auto& handshake_state=timing_memory<uint32_t>(p.handshake_state);
    auto& request_state=timing_memory<uint32_t>(p.request_scene_state);
    auto& request_age=timing_memory<uint32_t>(p.request_scene_age);
    auto& total_age=timing_memory<uint32_t>(p.request_scene_total);
    auto reset_handshake=[&] {handshake_state=3;timing_memory<uint32_t>(p.handshake_age)=0;};
    auto reset_request=[&](uint32_t s) {
        request_state=s;request_age=total_age=0;timing_memory<uint8_t>(p.request_scene_active)=1;
    };
    auto reset_hold=[&](uint32_t s) {
        object.fill(0);state=s;active=1;wait_held[0]=false;wait_held[1]=true;wait_done=false;
        wait_polls[0]=wait_polls[1]=wait_updates=wait_closes=wait_completion_polls=0;
    };
    // Reference the actual native branches with elapsed correction inactive.
    // A callback at time zero consumes the first tick in both native and modded runs.
    auto stock_calls=[&](auto invoke,auto done) {
        gd::end_scene_frame();unsigned calls=0;
        do {invoke();check(++calls<2000,"native wait reference terminates");} while(!done());
        return calls;
    };
    reset_handshake();const auto handshake_calls=stock_calls(handshake,[&]{return handshake_state==4;});
    reset_request(2);const auto request_calls=stock_calls(request,[&]{return request_state==12;});
    reset_request(17);bool lifetime_done=false;
    const auto lifetime_calls=stock_calls([&]{lifetime_done=!request();},[&]{return lifetime_done;});
    reset_hold(2);const auto hold_calls=stock_calls([&]{hold(object.data());},[&]{return state==3;});
    reset_hold(3);const auto close_calls=stock_calls([&]{hold(object.data());},[&]{return !active;});
    check(handshake_calls==31&&request_calls==1201&&lifetime_calls==60&&hold_calls==180&&close_calls==121,
        "native reference confirms all five audited wait thresholds");

    std::vector<std::vector<double>> cadences;
    for(unsigned hz:{60U,75U,90U,95U,120U,144U,165U,240U,360U})cadences.push_back({1.0/hz});
    cadences.push_back({1.0/240,1.0/75,1.0/144,1.0/60,1.0/360});
    for(const auto& cadence:cadences) {
        auto elapsed_calls=[&](auto invoke,auto done,unsigned reference) {
            gd::begin_timing_frame(0);gd::begin_timing_frame(0);invoke();invoke();
            double elapsed=0,last_dt=0;unsigned n=0;
            while(!done()) {
                last_dt=cadence[n%cadence.size()];gd::begin_timing_frame(last_dt);elapsed+=last_dt;
                invoke();check(++n<10000,"elapsed native wait terminates");
                if(!done())invoke(); // Aliases cannot consume an extra tick.
            }
            const double deadline=double(reference-1)/60;
            check(elapsed+1e-9>=deadline&&elapsed<=deadline+last_dt+1e-9,"native wait matches stock duration at this cadence");
        };
        reset_handshake();elapsed_calls(handshake,[&]{return handshake_state==4;},handshake_calls);
        reset_request(2);elapsed_calls(request,[&]{return request_state==12;},request_calls);
        reset_request(17);lifetime_done=false;
        elapsed_calls([&]{lifetime_done=!request();},[&]{return lifetime_done;},lifetime_calls);
        reset_hold(2);elapsed_calls([&]{hold(object.data());},[&]{return state==3;},hold_calls);
        check(wait_closes==1&&wait_polls[0]==wait_updates&&wait_polls[1]==wait_updates,
            "both players are polled every call and closing starts once");
        reset_hold(3);elapsed_calls([&]{hold(object.data());},[&]{return !active;},close_calls);
        check(wait_completion_polls==wait_updates,"completion is polled at full update frequency");
    }
    reset_handshake();gd::begin_timing_frame(0);handshake();
    gd::begin_timing_frame(2);handshake();check(handshake_state==4,"handshake deadline crosses a stall");
    reset_request(2);gd::begin_timing_frame(0);request();
    gd::begin_timing_frame(25);request();check(request_state==12,"request timeout crosses a stall");
    reset_request(17);gd::begin_timing_frame(0);request();
    gd::begin_timing_frame(2);check(!request(),"minimum scene age crosses a stall");
    reset_hold(2);gd::begin_timing_frame(0);hold(object.data());
    gd::begin_timing_frame(4);hold(object.data());check(state==3&&wait_closes==1,"held input deadline crosses a stall once");
    gd::begin_timing_frame(0);hold(object.data());
    gd::begin_timing_frame(3);hold(object.data());check(!active,"close fallback crosses a stall");

    reset_hold(2);gd::begin_timing_frame(0);hold(object.data());
    gd::begin_timing_frame(1);hold(object.data());check(held_age==61,"hold accumulates elapsed time");
    wait_held[1]=false;hold(object.data());check(held_age==0&&state==2,"release resets hold within the same frame");
    wait_held[0]=true;hold(object.data());check(held_age==1,"other player's new hold starts immediately");
    gd::begin_timing_frame(1);hold(object.data());check(held_age==61&&state==2,"repress has a fresh hold duration");
    // Completion need not wait for an elapsed tick or the fallback deadline.
    state=3;gd::begin_timing_frame(0);hold(object.data());wait_done=true;hold(object.data());
    check(!active&&close_age==1,"native completion remains immediate within the current frame");

    reset_request(17);gd::begin_timing_frame(0);request();
    gd::begin_timing_frame(1.0/120);request();
    request_state=18;gd::begin_timing_frame(1.0/120);request();
    check(total_age==2&&request_age==2,"continuous ages retain fractional time across state changes");
    request_age=0;request();check(request_age==1&&total_age==2,"native request reset leaves total lifetime intact");
    gd::begin_timing_frame(1.0/60);request();check(request_age==2&&total_age==3,"request reset starts a separate elapsed interval");
    timing_memory<uint8_t>(p.request_scene_active)=0;
    gd::begin_timing_frame(5);request();gd::begin_timing_frame(5);request();
    timing_memory<uint8_t>(p.request_scene_active)=1;gd::begin_timing_frame(5);request();
    check(request_age==3&&total_age==4,"inactive scene accrues no elapsed backlog");
    reset_request(17);gd::begin_timing_frame(0);request();
    gd::begin_timing_frame(1.0e12);check(!request()&&total_age==INT32_MAX,"long scene stalls saturate without signed wrap");
    gd::end_scene_frame();controller=saved_controller;
    puts("PASS: five native scene waits at 60/75/90/95/120/144/165/240/360 Hz and variable cadence; resets, input, stalls and inactivity");
}
