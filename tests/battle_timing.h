#pragma once
static std::vector<unsigned> battle_sounds;
static unsigned battle_draw_calls;
static uint64_t __fastcall battle_sound(unsigned bank,unsigned id) {battle_sounds.push_back(bank*1000+id);return 0;}
static uint64_t __fastcall battle_draw() {++battle_draw_calls;return 0;}
static uint64_t __fastcall battle_label(void*,const char*) {return 0;}
static void native_battle_test() {
    const auto& p=gd::target::get();
    timing_commit(p.battle_age,gd::target::layout::battle_size);
    timing_commit(p.result_guitar_flag,1);timing_memory<uint8_t>(p.result_guitar_flag)=1;
    TimingRedirect sound(p.keysound,reinterpret_cast<void*>(battle_sound));
    TimingRedirect label(p.animation_label,reinterpret_cast<void*>(battle_label));
    TimingRedirect draw0(p.battle_renderers[0],reinterpret_cast<void*>(battle_draw));
    TimingRedirect draw1(p.battle_renderers[1],reinterpret_cast<void*>(battle_draw));
    TimingRedirect draw2(p.battle_renderers[2],reinterpret_cast<void*>(battle_draw));
    const auto update=reinterpret_cast<void(__fastcall*)()>(gd::at(p.battle_update));
    auto reset=[&](unsigned state,unsigned age=0) {
        memset(gd::at(p.battle_age),0,gd::target::layout::battle_size);
        timing_memory<uint32_t>(p.battle_state)=state;timing_memory<uint32_t>(p.battle_age)=age;
        battle_sounds.clear();battle_draw_calls=0;
        for(size_t player=0;player<2;++player) for(size_t category=0;category<2;++category) {
            const auto base=p.battle_scores+player*gd::target::layout::battle_player_stride+category*gd::target::layout::battle_score_category_stride;
            timing_memory<int32_t>(base+gd::target::layout::battle_score_initial)=3100;
            timing_memory<int32_t>(base+gd::target::layout::battle_score_goal)=3600;
            for(auto offset:{gd::target::layout::battle_score_property0,gd::target::layout::battle_score_property1,gd::target::layout::battle_score_property2}) {
                timing_memory<int32_t>(base+offset)=int32_t(offset);
                timing_memory<int32_t>(base+offset+gd::target::layout::battle_score_property_display)=-1;
            }
        }
        gd::begin_timing_frame(0);gd::begin_timing_frame(0); // End the previous scene's clock epoch.
    };
    auto timed=[&](unsigned hz,double duration) {
        gd::begin_timing_frame(0);update();
        double remaining=duration;
        while(remaining>1e-10) {
            const double dt=std::min(remaining,1.0/hz);remaining-=dt;
            gd::begin_timing_frame(dt);update();
        }
    };
    // Differential comparison against the original arithmetic left reachable
    // outside a render context. This covers property reveal side effects too.
    for(unsigned state:{3U,4U}) for(unsigned ticks:{15U,39U,40U,59U,60U}) {
        reset(state);gd::end_scene_frame();for(unsigned i=0;i<=ticks;++i) update();
        std::array<uint8_t,2*gd::target::layout::battle_player_stride> expected;
        memcpy(expected.data(),gd::at(p.battle_scores),expected.size());
        for(unsigned hz:{95U,120U,144U,240U,360U}) {
            reset(state);timed(hz,ticks/60.0);
            check(memcmp(expected.data(),gd::at(p.battle_scores),expected.size())==0,
                "battle score phases and property reveals match native authored timing");
        }
    }
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        reset(12);timing_memory<uint32_t>(p.battle_counter0)=180;
        timing_memory<uint32_t>(p.battle_counter0+4)=180;timing_memory<uint32_t>(p.battle_counter0+8)=180;
        timed(hz,4);
        check(timing_memory<uint32_t>(p.battle_age)==240,"battle age tracks elapsed time");
        check(std::count(battle_sounds.begin(),battle_sounds.end(),3091U)==3 &&
            std::count(battle_sounds.begin(),battle_sounds.end(),3101U)==3,"battle countdown cues fire once at 20 and zero");
        check(battle_draw_calls==(hz*4+1)*3,"battle rendering still runs on every display update");
        reset(6);timing_memory<int32_t>(p.battle_roulette_goal)=4000;
        timing_memory<int32_t>(p.battle_roulette_start)=1000;timing_memory<int32_t>(p.battle_roulette_value)=1000;
        timed(hz,1);
        check(timing_memory<int32_t>(p.battle_roulette_value)==2500,"roulette integrates all elapsed two-tick intervals");
        reset(10);for(unsigned col=0;col<4;++col) timing_memory<int32_t>(p.battle_awards_pending+col*4)=10;
        timed(hz,2);
        for(unsigned col=0;col<4;++col)
            check(timing_memory<int32_t>(p.battle_awards_pending+col*4)==7 && timing_memory<int32_t>(p.battle_awards_values+col*4)==3,
                "award transfers remain invariant across refresh rates");
        reset(7);timed(hz,1);
        check(std::count(battle_sounds.begin(),battle_sounds.end(),3109U)==2 &&
            std::count(battle_sounds.begin(),battle_sounds.end(),3101U)==1,"battle exact-age sounds do not repeat on held ages");
    }
    reset(10);for(unsigned col=0;col<4;++col) timing_memory<int32_t>(p.battle_awards_pending+col*4)=10;
    gd::begin_timing_frame(0);update();gd::begin_timing_frame(2);update();
    for(unsigned col=0;col<4;++col)
        check(timing_memory<int32_t>(p.battle_awards_pending+col*4)==7 && timing_memory<int32_t>(p.battle_awards_values+col*4)==3,
            "stalled award transfer preserves every earned award");
    check(battle_sounds==std::vector<unsigned>{3101},"stalled award sounds coalesce");
    reset(12);timing_memory<uint32_t>(p.battle_counter0)=180;
    gd::begin_timing_frame(0);update();gd::begin_timing_frame(4);update();
    check(battle_sounds==std::vector<unsigned>{3101},"stalled countdown suppresses obsolete warning sound");
    reset(6);timing_memory<int32_t>(p.battle_roulette_goal)=4000;
    timing_memory<int32_t>(p.battle_roulette_start)=1000;timing_memory<int32_t>(p.battle_roulette_value)=1000;
    gd::begin_timing_frame(0);update();gd::begin_timing_frame(2);update();
    check(timing_memory<int32_t>(p.battle_roulette_value)==4000,"roulette catches up across its intro and sound boundaries in one render");
    reset(7);gd::begin_timing_frame(0);update();gd::begin_timing_frame(2);update();gd::begin_timing_frame(0);update();
    check(std::count(battle_sounds.begin(),battle_sounds.end(),3109U)==1,"coalesced old battle clicks cannot replay next frame");
    const auto hooks=gd::result_inline_hooks();
    const auto exit=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==&p.battle_exit_step;});
    auto exit_step=[&](double dt) {
        gd::begin_timing_frame(dt);gd::InlineContext c{};
        c.ip=reinterpret_cast<uintptr_t>(gd::at(p.battle_exit_step.address))+p.battle_exit_step.signature.length;
        exit->handler(c);return c;
    };
    timing_memory<uint32_t>(p.battle_outer_state)=3;timing_memory<uint32_t>(p.battle_exit_age)=60;
    exit_step(0);const auto crossed=exit_step(2);
    check(crossed.rax==0 && (crossed.flags&0x40)!=0,"battle exit crosses 30 and zero after a stall");
    check((exit_step(0).flags&0x40)==0,"battle exit transition cannot repeat");
    gd::end_scene_frame();
    puts("PASS: native battle result phases, all-rate drawing, property reveals, roulette, award conservation, countdowns and stalled cues");
}
