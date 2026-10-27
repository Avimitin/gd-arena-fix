#pragma once
#include "../src/effect_timing.h"
static unsigned ending_inputs,ending_labels;
static uint64_t __fastcall ending_input() {++ending_inputs;return 0;}
static uint64_t __fastcall ending_label(void*,const char*) {++ending_labels;return 7;}
static void overlay_transition_test() {
    const auto& p=gd::target::get();const auto hooks=gd::effect_inline_hooks();timing_commit(p.overlay_age,16);
    auto& age=timing_memory<uint32_t>(p.overlay_age);auto& active=timing_memory<uint32_t>(p.result_request_active);
    auto& kind=timing_memory<uint32_t>(p.result_request_code);
    const auto opened=reinterpret_cast<unsigned char(__fastcall*)()>(gd::at(p.overlay_open_query));
    const auto closed=reinterpret_cast<unsigned char(__fastcall*)()>(gd::at(p.overlay_close_query));
    auto invoke=[&](unsigned index) {
        const auto& s=p.overlay_age_steps[index];gd::InlineContext c{};
        c.ip=reinterpret_cast<uintptr_t>(gd::at(s.address))+s.signature.length;
        std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==&s;})->handler(c);
    };
    for(unsigned hz:{95U,120U,144U,240U,360U})for(unsigned type:{0U,1U,4U,10U,13U}) {
        age=0;active=1;kind=type;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);invoke(0);invoke(0);
        check(age==1&&!opened(),"overlay consumes its zero-entry sentinel once and starts incomplete");
        for(unsigned f=0;f<hz;++f) {gd::begin_timing_frame(1.0/hz);invoke(0);invoke(0);}
        check(age==(type==0||type==10?45U:61U)&&opened(),"native overlay completion reads elapsed age and exact clamped endpoint");
        active=2;const auto start=age;gd::begin_timing_frame(0);invoke(1);
        gd::begin_timing_frame(double(start)/60);invoke(1);invoke(1);
        check(age==0&&(type==13||closed()),"overlay closing reaches exact zero after its elapsed duration");
        active=1;age=0;gd::begin_timing_frame(0);invoke(0);gd::begin_timing_frame(100);invoke(0);
        check(age==(type==0||type==10?45U:6001U)&&opened(),"long overlay stalls retain time without skipping clamped equality waits");
    }
    active=age=kind=0;gd::end_scene_frame();
    puts("PASS: shared overlay opening/closing clocks, zero-entry sentinel, clamped terminal predicates and stalls");
}
static void native_gf_overlay_test() {
    const auto& p=gd::target::get();timing_commit(p.gf_overlay_records,gd::target::layout::gf_overlay_storage);
    timing_commit(p.gf_overlay_scratch,1);
    const auto update=reinterpret_cast<void(__fastcall*)()>(gd::at(p.gf_overlay_update));
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        memset(gd::at(p.gf_overlay_records),0,gd::target::layout::gf_overlay_storage);
        for(unsigned player=0;player<2;++player) for(auto control:p.gf_overlay_controls) timing_memory<uint8_t>(control+player)=5;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);update();
        for(unsigned i=0;i<hz;++i) {gd::begin_timing_frame(1.0/hz);update();update();}
        for(unsigned player=0;player<2;++player) {
            const auto offset=player*gd::target::layout::gf_overlay_stride;
            check(timing_memory<uint32_t>(p.gf_overlay_hold_value+offset)==120,"GF shutter hold lasts three seconds");
            check(std::abs(timing_memory<float>(p.gf_overlay_hold_alpha+offset)-1)<1e-6f,"GF held overlay fades to full opacity");
            for(unsigned col=0;col<2;++col) check(timing_memory<uint32_t>(p.gf_overlay_counter_values[col]+offset)==120,
                "GF status hold countdown uses elapsed ticks for both players");
        }
        gd::begin_timing_frame(2.5);update();
        for(unsigned player=0;player<2;++player) {
            const auto offset=player*gd::target::layout::gf_overlay_stride;
            check(timing_memory<uint32_t>(p.gf_overlay_hold_value+offset)==0 &&
                timing_memory<float>(p.gf_overlay_hold_alpha+offset)==0,"stalled GF fade integrates time before and after hold expiry");
        }
    }
    const auto hooks=gd::effect_inline_hooks();
    auto invoke=[&](const gd::target::Site& site,gd::InlineContext& c) {
        const auto hook=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==&site;});
        c.ip=reinterpret_cast<uintptr_t>(gd::at(site.address))+site.signature.length;hook->handler(c);
    };
    // Synthetic registers supply the same operands as the native fade sites.
    alignas(32) std::array<uint8_t,512+sizeof(gd::InlineContext)> storage{};
    auto& c=*reinterpret_cast<gd::InlineContext*>(storage.data()+512);
    const float step=timing_memory<float>(p.gf_overlay_step);memcpy(c.ymm(6),&step,sizeof(step));
    for(unsigned player=0;player<2;++player) for(unsigned channel=0;channel<2;++channel) {
        c.r10=player*gd::target::layout::gf_overlay_stride;
        auto& alpha=timing_memory<float>(p.gf_overlay_alphas[channel]+c.r10);alpha=0;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);invoke(p.gf_overlay_fades[channel*2],c);
        gd::begin_timing_frame(1.0/240);invoke(p.gf_overlay_fades[channel*2],c);
        check(std::abs(alpha-step*0.25f)<1e-6f,"GF overlay fade advances a fractional authored step");
        gd::begin_timing_frame(10);invoke(p.gf_overlay_fades[channel*2],c);check(alpha==1,"GF overlay rise clamps after a stall");
        gd::begin_timing_frame(10);invoke(p.gf_overlay_fades[channel*2+1],c);check(alpha==0,"GF overlay fall clamps after a stall");
    }
    for(unsigned player=0;player<2;++player) {
        const auto offset=player*gd::target::layout::gf_overlay_stride;
        auto& remaining=timing_memory<uint32_t>(p.gf_overlay_counter_values[2]+offset);remaining=362;c.r10=offset;
        gd::begin_timing_frame(0);invoke(p.gf_overlay_counters[2],c);
        gd::begin_timing_frame(6);invoke(p.gf_overlay_counters[2],c);
        check(c.rax==2 && c.rcx==362,"GF long effect frame derives from elapsed countdown");
        auto& hit=timing_memory<int32_t>(p.gf_overlay_hit_value+offset);hit=1;
        gd::begin_timing_frame(0);invoke(p.gf_overlay_hit,c);check(hit==2,"GF overlay hit starts immediately");
        gd::begin_timing_frame(2);invoke(p.gf_overlay_hit,c);check(hit==0,"GF overlay hit expires after a stall");
    }
    gd::end_scene_frame();
    puts("PASS: native GF overlay holds, smooth alpha, expiry-crossing stalls, long effects and both players");
}
extern "C" void gd_number_probe(void*,uint32_t,void*,void*);
static void native_gf_number_test() {
    const auto& p=gd::target::get();const auto hooks=gd::effect_inline_hooks();
    const auto hook=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==&p.gf_number_step;});
    check(hook!=hooks.end(),"GF number easing hook registered");
    // This block contains only internal branches and register-relative data.
    // Run the original instructions as the arithmetic reference, including
    // the native slope calculation and its special multiple-of-ten case.
    const size_t length=p.gf_number_next-p.gf_number_native_start;
    auto code=std::unique_ptr<void,gd::FreeBridge>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    check(code&&length+1<4096,"allocate native number arithmetic fixture");
    memcpy(code.get(),gd::at(p.gf_number_native_start),length);
    memcpy(static_cast<uint8_t*>(code.get())+p.gf_number_step.address-p.gf_number_native_start,
        p.gf_number_step.signature.storage.data(),p.gf_number_step.signature.length);
    static_cast<uint8_t*>(code.get())[length]=0xc3;
    FlushInstructionCache(GetCurrentProcess(),code.get(),length+1);
    struct Number {uint32_t target,value;int32_t step;uint32_t age;};
    static_assert(offsetof(Number,step)-offsetof(Number,value)==gd::target::layout::gf_number_step);
    static_assert(offsetof(Number,age)-offsetof(Number,value)==gd::target::layout::gf_number_age);
    uint32_t output[2]{};
    const auto native=[&](Number& n,uint32_t target){gd_number_probe(&n.value,target,code.get(),output);};
    const auto invoke=[&](Number& n,uint32_t target) {
        struct {gd::InlineContext c{};uint64_t stack[24]{};} fixture;
        auto& c=fixture.c;c.rbx=reinterpret_cast<uintptr_t>(&n.value);c.rdi=target;
        c.ip=reinterpret_cast<uintptr_t>(gd::at(p.gf_number_step.address))+p.gf_number_step.signature.length;
        hook->handler(c);check(c.rdi==n.value,"number renderer receives the adapted value");
        check(c.ip==reinterpret_cast<uintptr_t>(gd::at(p.gf_number_next)),"number adapter rejoins native digit handling");
        return uint32_t(c.r14);
    };
    for(unsigned hz:{60U,95U,120U,144U,240U,360U})for(const auto pair:{std::pair{0U,700U},
        std::pair{900U,0U},std::pair{0U,3U},std::pair{1000U,1700U},std::pair{35000U,15000U}}) {
        Number value{UINT32_MAX,pair.first,0,0};native(value,pair.second);
        value.value=pair.first;value.age=0;Number reference=value;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);native(reference,pair.second);invoke(value,pair.second);
        check(value.value==reference.value&&value.age==reference.age&&value.step==reference.step,
            "new number transition retains the original immediate first step");
        unsigned applied=0;
        for(unsigned i=1;i<=hz;++i) {
            const unsigned due=unsigned(uint64_t(i)*60/hz)-applied;
            for(unsigned tick=0;tick<due;++tick)native(reference,pair.second);
            applied+=due;gd::begin_timing_frame(1.0/hz);invoke(value,pair.second);
            check(value.value==reference.value&&value.age==reference.age&&value.step==reference.step,
                "number easing matches native arithmetic by elapsed time at every cadence");
            const Number before=value;const auto previous=invoke(value,pair.second);
            check(value.value==before.value&&value.age==before.age&&previous==value.value,
                "duplicate number consumers neither advance nor replay a digit change");
        }
        check(value.value==pair.second,"number easing reaches its native target");
        value={UINT32_MAX,pair.first,0,0};native(value,pair.second);value.value=pair.first;value.age=0;
        gd::begin_timing_frame(0);invoke(value,pair.second);gd::begin_timing_frame(100);invoke(value,pair.second);
        check(value.value==pair.second&&value.age==7&&value.step==0,"number easing coalesces a stall into its final value");
    }
    gd::end_scene_frame();puts("PASS: GF number easing compared with original instructions at 60/95/120/144/240/360 Hz, duplicates and stalls");
}
static void native_effect_test() {
    native_gf_number_test();
    const auto& p=gd::target::get();const auto hooks=gd::effect_inline_hooks();
    timing_commit(p.shutter_objects,static_cast<size_t>(p.shutter_fades-p.shutter_objects)+64);
    timing_commit(p.gf_effect_age,4);timing_commit(p.end_gauge,4);
    timing_commit(p.combo_effect_age,4);timing_commit(p.hud_age,4);
    timing_commit(p.hud_countdowns,gd::target::layout::hud_counter_stride+4);
    for(const auto address:p.effect_ages) timing_commit(address,4);
    auto invoke=[&](const gd::target::Site& site,gd::InlineContext& c) {
        const auto hook=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==&site;});
        check(hook!=hooks.end(),"effect inline handler found");
        c.ip=reinterpret_cast<uintptr_t>(gd::at(site.address))+site.signature.length;hook->handler(c);
    };
    std::vector<uint8_t> object(gd::target::layout::shutter_size);
    const auto old_shutter=timing_memory<void*>(p.shutter_object),old_mode=timing_memory<void*>(p.game_mode_pointer);
    timing_memory<void*>(p.shutter_object)=timing_memory<void*>(p.game_mode_pointer)=object.data();
    const auto shutter=reinterpret_cast<void(__fastcall*)()>(gd::at(p.shutter_update));
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        memset(gd::at(p.shutter_objects),0,static_cast<size_t>(p.shutter_fades-p.shutter_objects)+64);
        timing_memory<uint32_t>(p.effect_ages[2])=0;
        object[gd::target::layout::shutter_setting]=5;object[gd::target::layout::shutter_setting+2]=7;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);shutter();
        for(unsigned i=0;i<hz;++i) {gd::begin_timing_frame(1.0/hz);shutter();shutter();}
        check(timing_memory<uint32_t>(p.effect_ages[2])==60,"native shutter notification lasts one second");
        for(unsigned player=0;player<2;++player) {
            check(timing_memory<uint32_t>(p.shutter_fades+player*gd::target::layout::shutter_fade_stride)==0,
                "native two-player shutter fades complete");
            check(timing_memory<float>(p.shutter_colors+player*16+gd::target::layout::shutter_alpha)==1,
                "native shutter alpha reaches full opacity");
        }
    }
    object[gd::target::layout::shutter_setting]=0;
    gd::begin_timing_frame(0);shutter();gd::begin_timing_frame(1.0/240);shutter();
    check(std::abs(timing_memory<float>(p.shutter_colors+gd::target::layout::shutter_alpha)-
        (gd::config.animation_timing==2?0.975f:1.0f))<1e-6f,"shutter fade uses fractional elapsed alpha");
    gd::begin_timing_frame(30);shutter();
    check(timing_memory<uint32_t>(p.shutter_fades)==0,"shutter fade finishes after a full render stall");
    TimingRedirect input0(p.end_inputs[0],reinterpret_cast<void*>(ending_input));
    TimingRedirect input1(p.end_inputs[1],reinterpret_cast<void*>(ending_input));
    TimingRedirect label(p.animation_label_lookup,reinterpret_cast<void*>(ending_label));
    std::array<uint8_t,gd::target::layout::animation_size> ending_object{};
    timing_memory<void*>(p.end_object)=ending_object.data();
    anim_field<uint16_t>(ending_object.data(),gd::target::layout::kind)=1;
    const auto end=reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.end_update));
    timing_memory<uint8_t>(p.end_enabled)=1;
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        timing_memory<uint32_t>(p.end_age)=0;*reinterpret_cast<uint32_t*>(object.data())=1;
        ending_inputs=ending_labels=0;gd::begin_timing_frame(0);end();
        for(unsigned i=0;i<hz*2;++i) {gd::begin_timing_frame(1.0/hz);end();end();}
        check(timing_memory<uint32_t>(p.end_age)==120 && ending_labels==1,
            "native ending cue at 90 is elapsed and one-shot");
        check(ending_inputs==(hz*4+1)*2,"native ending polls input on every invocation");
        check(anim_field<uint16_t>(ending_object.data(),gd::target::layout::frame)==7,"native ending label is applied");
    }
    timing_memory<uint32_t>(p.end_age)=0;*reinterpret_cast<uint32_t*>(object.data())=0;
    ending_labels=0;gd::begin_timing_frame(0);end();gd::begin_timing_frame(0);end();
    check(ending_labels==1,"immediate ending cue at zero cannot repeat");
    timing_memory<uint32_t>(p.end_age)=0;*reinterpret_cast<uint32_t*>(object.data())=1;
    ending_labels=0;gd::begin_timing_frame(0);end();gd::begin_timing_frame(10);end();
    check(ending_labels==1,"ending cue survives an overshot deadline");
    gd::InlineContext c{};
    timing_memory<int32_t>(p.gf_effect_age)=1;gd::begin_timing_frame(0);invoke(p.gf_effect_step,c);
    check(timing_memory<int32_t>(p.gf_effect_age)==2,"new GF effect gets its immediate native first sample");
    gd::begin_timing_frame(0.1);invoke(p.gf_effect_step,c);
    check(timing_memory<int32_t>(p.gf_effect_age)==8,"GF effect retains its last authored sample");
    invoke(p.gf_effect_step,c);check(timing_memory<int32_t>(p.gf_effect_age)==8,"held GF effect sample cannot expire early");
    gd::begin_timing_frame(1.0/60);invoke(p.gf_effect_step,c);check(!timing_memory<int32_t>(p.gf_effect_age),"GF effect expires on elapsed time");
    timing_memory<uint32_t>(p.combo_effect_age)=6;gd::begin_timing_frame(0);invoke(p.combo_effect_step,c);
    gd::begin_timing_frame(0.1);invoke(p.combo_effect_step,c);check(c.rax==0,"combo effect retains its six-tick duration");
    timing_memory<uint32_t>(p.hud_age)=4090;gd::begin_timing_frame(0);invoke(p.hud_phase_step,c);
    gd::begin_timing_frame(1);invoke(p.hud_phase_step,c);check(c.rax==54,"HUD phase wraps after elapsed ticks");
    for(unsigned player=0;player<2;++player) {
        c.r10=player*gd::target::layout::hud_counter_stride;
        auto& value=timing_memory<uint32_t>(p.hud_countdowns+c.r10);value=39;
        gd::begin_timing_frame(0);invoke(p.hud_countdown_step,c);
        gd::begin_timing_frame(0.65);invoke(p.hud_countdown_step,c);check(value==0,"per-player HUD countdown expires after 39 ticks");
    }
    timing_memory<void*>(p.end_object)=nullptr;
    timing_memory<void*>(p.shutter_object)=old_shutter;timing_memory<void*>(p.game_mode_pointer)=old_mode;
    gd::end_scene_frame();
    puts("PASS: native two-player shutter/ending effects, fractional opacity, input cadence, GF/combo lifetimes and HUD clocks");
}
