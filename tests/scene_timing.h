#pragma once
#include "../src/scene_timing.h"
#include "../src/scene_timeline.h"
#include "result_timing.h"
#include "battle_timing.h"
#include "effect_timing.h"
#include "song_timing.h"
#include "service_timing.h"
#include "system_timing.h"
#include "tutorial_timing.h"
#include "drum_tutorial_timing.h"
#include "summary_timing.h"
#include "minigame_timing.h"
#include "arena_timing.h"
#include "spinner_timing.h"
#include "options_timing.h"

static std::vector<unsigned> tally_sounds;
static uint64_t __fastcall option_modifier() { return 0; }
static void controller_time_test() {
    const auto& p=gd::target::get();const auto hooks=gd::scene_inline_hooks();
    std::array<uint8_t,gd::target::layout::controller_size> object{};
    auto& index=anim_field<uint32_t>(object.data(),gd::target::layout::controller_index);
    auto& update=anim_field<uint32_t>(object.data(),gd::target::layout::controller_update_age);
    auto& draw=anim_field<uint32_t>(object.data(),gd::target::layout::controller_draw_age);
    anim_field<uint32_t>(object.data(),gd::target::layout::controller_phase)=1;
    for (unsigned hz:{95U,120U,144U,240U,360U}) {
        ++index;update=draw=0;
        gd::begin_timing_frame(0);gd::track_scene_controller(object.data());
        for (unsigned i=0;i<hz*4;++i) {
            gd::begin_timing_frame(1.0/hz);gd::track_scene_controller(object.data());++update;++draw;
        }
        for (size_t i=0;i<p.controller_time_reads.size();++i) {
            const auto& site=p.controller_time_reads[i];
            const auto hook=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==&site;});
            gd::InlineContext c{};c.rcx=c.rdx=c.rbx=c.r15=reinterpret_cast<uintptr_t>(object.data());
            c.rax=i==2?0x88888889:0x88888889;
            c.ip=reinterpret_cast<uintptr_t>(gd::at(site.address))+site.signature.length;hook->handler(c);
            check(i<2 ? (i==0?c.r15:c.rdi)==240 : c.rdx==(i==2?4:16),"service visual phases use elapsed controller time");
        }
        check(update==hz*4 && draw==hz*4,"controller entry ordinals remain independent from visual clocks");
    }
    gd::end_scene_frame();
    puts("PASS: service-screen phase consumers use elapsed time while native first-update ordinals stay intact");
}
static void native_option_test() {
    const auto& p=gd::target::get();
    timing_commit(p.option_keys,gd::target::layout::option_key_stride*2);
    timing_commit(p.secondary_player_base,sizeof(uint32_t));
    timing_memory<uint32_t>(p.secondary_player_base)=0;
    const auto saved_object=timing_memory<void*>(p.option_object);
    std::array<void*,2> modifiers;
    for (size_t i=0;i<modifiers.size();++i) {
        auto address=gd::at(p.option_modifiers+i*sizeof(void*));memcpy(&modifiers[i],address,sizeof(void*));
        void* stub=reinterpret_cast<void*>(option_modifier);gd::write_memory(address,&stub,sizeof(stub));
    }
    std::vector<uint8_t> object(gd::target::layout::option_size);
    timing_memory<void*>(p.option_object)=object.data();
    auto held=reinterpret_cast<uint32_t*>(object.data()+gd::target::layout::option_held);
    const auto repeat=reinterpret_cast<void*(__fastcall*)()>(gd::at(p.option_repeat.address));
    const auto speed_update=reinterpret_cast<void(__fastcall*)()>(gd::at(p.option_speed_update));
    const auto speed_value=reinterpret_cast<float(__fastcall*)(int)>(gd::at(p.option_speed_value));
    for (unsigned hz:{95U,120U,144U,240U,360U}) {
        memset(object.data(),0,object.size());*reinterpret_cast<uint32_t*>(object.data())=1;
        for (unsigned player=0;player<2;++player) timing_memory<uint32_t>(p.option_keys+player*gd::target::layout::option_key_stride)=0;
        gd::begin_timing_frame(0);repeat();
        for (unsigned player=0;player<2;++player) timing_memory<uint32_t>(p.option_keys+player*gd::target::layout::option_key_stride)=0x1e0;
        gd::begin_timing_frame(1.0/hz);repeat();
        for (size_t i=0;i<gd::target::layout::option_repeat_count;++i) check(held[i]==1,"held option key responds immediately");
        unsigned pulses=0,first=0;
        for (unsigned j=1;j<=hz;++j) {
            gd::begin_timing_frame(1.0/hz);repeat();
            if (held[0]>=15) {++pulses;if (!first)first=j;}
            for (size_t i=0;i<gd::target::layout::option_repeat_count;++i) check(held[i]==held[0],"two-player repeat channels agree");
            repeat();check(held[0]==2,"duplicate producer call cannot issue another repeat or key edge");
        }
        check(pulses==47 && std::abs(double(first)/hz-14.0/60)<=1.0/hz+1e-9,
            "option repeat delay and frequency use elapsed time");
        gd::begin_timing_frame(30);repeat();check(held[0]==15,"long held-key stall emits one current repeat");
        timing_memory<uint32_t>(p.option_keys)=0;repeat();check(held[0]==0,"key release is observed in the same render");
        timing_memory<uint32_t>(p.option_keys)=0x1e0;repeat();check(held[0]==1,"new key edge is immediate within the same render");
        for (unsigned player=0;player<2;++player) {
            timing_memory<uint32_t>(p.option_keys+player*gd::target::layout::option_key_stride)=4;
            object[gd::target::layout::option_speed_enabled+player]=1;
            object[gd::target::layout::option_speed+player]=2;
            object[gd::target::layout::option_speed_previous+player]=1;
            object[gd::target::layout::option_speed_age+player]=0;
            anim_field<float>(object.data(),gd::target::layout::option_speed_from+player*sizeof(float))=0;
        }
        gd::begin_timing_frame(0);speed_update();
        unsigned count=0;
        while (object[gd::target::layout::option_speed_age]<16 && count<hz) {
            gd::begin_timing_frame(1.0/hz);speed_update();speed_update();++count;
        }
        check(std::abs(double(count)/hz-16.0/60)<=1.0/hz+1e-9,"native speed transition lasts 16 authored ticks");
    }
    object[gd::target::layout::option_speed_age]=0;
    object[gd::target::layout::option_speed_previous]=1;
    gd::begin_timing_frame(0);speed_update();
    gd::begin_timing_frame(1.0/240);speed_update();const float partial=speed_value(0);
    gd::begin_timing_frame(1);speed_update();const float goal=speed_value(0);
    check(std::abs(partial-goal*(gd::config.animation_timing==2 ? 0.25f/16 : 0))<1e-6f,
        "speed transition draws a fractional elapsed sample in interpolation mode");
    gd::end_scene_frame();
    timing_memory<void*>(p.option_object)=saved_object;
    for (size_t i=0;i<modifiers.size();++i) gd::write_memory(gd::at(p.option_modifiers+i*sizeof(void*)),&modifiers[i],sizeof(void*));
    puts("PASS: native option key edges/repeat, two players, same-frame releases, stalls and fractional speed transitions");
}
static uint64_t __fastcall tally_sound(unsigned bank,unsigned sound) {
    tally_sounds.push_back(bank*1000+sound);return 0;
}
static void native_tally_test() {
    const auto& p=gd::target::get();
    timing_commit(p.tally_age,0x600);
    timing_commit(p.tally_enabled,0x40);
    timing_commit(p.player_active,2);
    TimingRedirect sound(p.keysound,reinterpret_cast<void*>(tally_sound));
    const auto update=reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.tally_update.address));
    auto reset=[&](uint32_t age=0) {
        memset(gd::at(p.tally_age),0,0x600);
        timing_memory<uint8_t>(p.tally_enabled)=1;
        timing_memory<uint16_t>(p.player_active)=0;
        timing_memory<uint32_t>(p.tally_state)=3;
        timing_memory<uint32_t>(p.tally_age)=age;
        for (size_t row=0;row<gd::target::layout::tally_rows;++row)
            for (size_t column=0;column<gd::target::layout::tally_columns;++column)
                timing_memory<int32_t>(p.tally_targets+row*gd::target::layout::tally_row_stride+column*4)=600;
        for (unsigned i=0;i<7;++i) timing_memory<int32_t>(p.tally_sound_options+i*4)=20000;
        tally_sounds.clear(); gd::begin_timing_frame(0);update();
    };
    for (unsigned hz:{95U,120U,144U,240U,360U}) {
        reset();
        for (unsigned i=0;i<hz*5;++i) {gd::begin_timing_frame(1.0/hz);update();update();}
        check(timing_memory<uint32_t>(p.tally_age)==300,"result tally elapsed age and duplicate calls");
        check(tally_sounds==std::vector<unsigned>({6008,3094,3094,3094,3094,6006}),
            "result sounds and final animation cue execute once at elapsed crossings");
        for (size_t row=0;row<gd::target::layout::tally_rows;++row)
            for (size_t column=0;column<gd::target::layout::tally_columns;++column)
                check(timing_memory<int32_t>(p.tally_values+row*gd::target::layout::tally_row_stride+column*4)==600,
                    "result numbers reach target at every render cadence");
    }
    reset();
    gd::begin_timing_frame(100.0/60);update();
    check(timing_memory<int32_t>(p.tally_values)==10,"result count-up includes its starting boundary once");
    update();check(timing_memory<int32_t>(p.tally_values)==10,"held result age does not repeat numerical increment");
    gd::begin_timing_frame(30.0/60);update();
    check(timing_memory<int32_t>(p.tally_values)==310 &&
        timing_memory<int32_t>(p.tally_values+gd::target::layout::tally_row_stride)==10,
        "stalled result count-up integrates only time after each row starts");
    reset();gd::begin_timing_frame(10);update();
    check(tally_sounds==std::vector<unsigned>({3094,6006}),"obsolete result sounds coalesce after a long stall");
    gd::begin_timing_frame(6);update();
    check(timing_memory<uint32_t>(p.tally_state)==4 && timing_memory<uint32_t>(p.tally_age)==0,
        "native result timeout crosses a stall and retains state reset");
    reset(258);gd::begin_timing_frame(2.0/60);update();
    check(tally_sounds==std::vector<unsigned>({6006}),"result seek bypasses old cues and retains the next transition");
    gd::end_scene_frame();const auto before=timing_memory<uint32_t>(p.tally_age);update();
    check(timing_memory<uint32_t>(p.tally_age)==before+1,"result non-render update keeps stock increment");
    puts("PASS: native result tally ages, sounds, count-up, skips, duplicate calls, stalls and timeout");
}

static unsigned light_outputs;
static uint64_t __fastcall light_observer(uintptr_t, uintptr_t, uintptr_t) { ++light_outputs; return 0; }
static unsigned secondary_input_calls;
static unsigned secondary_threshold_calls;
static void secondary_threshold(gd::InlineContext& c) {
    ++secondary_threshold_calls;
    c.ip=reinterpret_cast<uintptr_t>(gd::at(gd::target::get().secondary_age_step.address));
}
static uint64_t __fastcall secondary_input() { ++secondary_input_calls; return 0; }
static void* __fastcall secondary_input_object() { static std::array<uint8_t,256> object{}; return object.data(); }
static unsigned network_restarts, bookkeeping_calls;
static uint64_t __fastcall network_restart_observer() { ++network_restarts; return 0; }
static int64_t __fastcall bookkeeping_observer() { ++bookkeeping_calls; return 77; }
static void native_network_test() {
    const auto& p = gd::target::get();
    timing_commit(p.match_active,0x700);
    timing_commit(p.network_connection,0x100);
    TimingRedirect debug(p.debug_pause,reinterpret_cast<void*>(secondary_input));
    TimingRedirect restart(p.network_restart,reinterpret_cast<void*>(network_restart_observer));
    TimingRedirect broken(p.network_break,reinterpret_cast<void*>(secondary_input));
    const auto lobby = reinterpret_cast<void(__fastcall*)()>(gd::at(p.lobby_update.address));
    const auto match = reinterpret_cast<void(__fastcall*)()>(gd::at(p.match_update.address));
    for (unsigned hz : {95U,120U,144U,240U,360U}) {
        timing_memory<uint8_t>(p.lobby_active)=1;
        timing_memory<uint32_t>(p.lobby_state)=5;
        timing_memory<uint32_t>(p.lobby_age)=0;
        timing_memory<uint32_t>(p.network_connection)=0;
        timing_memory<uint32_t>(p.network_nodes)=0;
        gd::begin_timing_frame(0); lobby(); network_restarts=0;
        unsigned count=0;
        while (timing_memory<uint32_t>(p.lobby_state)==5 && count<hz) {
            gd::begin_timing_frame(1.0/hz); lobby(); ++count;
        }
        check(timing_memory<uint32_t>(p.lobby_state)==1 && network_restarts==1 &&
            std::abs(double(count)/hz-20.0/60)<=1.0/hz+1e-9,
            "native lobby restart deadline is independent of render cadence");
        timing_memory<uint8_t>(p.lobby_active)=0;
        timing_memory<uint8_t>(p.match_active)=1;
        timing_memory<uint32_t>(p.match_state)=5;
        timing_memory<uint32_t>(p.match_age)=0;
        timing_memory<uint32_t>(p.chat_age)=0;
        gd::begin_timing_frame(0); match(); count=0;
        while (timing_memory<uint32_t>(p.match_state)==5 && count<hz*2) {
            gd::begin_timing_frame(1.0/hz); match(); match(); ++count;
        }
        check(timing_memory<uint32_t>(p.match_state)==6 &&
            std::abs(double(count)/hz-61.0/60)<=1.0/hz+1e-9,
            "native matchmaking timeout and duplicate-call suppression");
        check(timing_memory<uint32_t>(p.chat_age)==61,"chat cooldown timestamp uses elapsed authored units");
    }
    timing_memory<uint8_t>(p.lobby_active)=1;
    timing_memory<uint32_t>(p.lobby_state)=5;
    timing_memory<uint32_t>(p.lobby_age)=0;
    gd::begin_timing_frame(0); lobby(); network_restarts=0;
    gd::begin_timing_frame(30); lobby();
    check(network_restarts==1,"long render stall triggers one lobby restart, without an I/O burst");
    gd::end_scene_frame();
    timing_memory<uint32_t>(p.lobby_state)=5; timing_memory<uint32_t>(p.lobby_age)=0; lobby();
    check(timing_memory<uint32_t>(p.lobby_age)==1,"lobby calls outside render scope retain original behavior");
    puts("PASS: native lobby/match deadlines, chat clock, duplicates, stalls and legacy non-render calls");
}
static void bookkeeping_test() {
    const auto& p=gd::target::get();
    auto hooks=gd::scene_timing_hooks();
    const auto hook=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.address==p.bookkeeping.address;});
    check(hook!=hooks.end(),"bookkeeping hook registered");
    const auto saved=*hook->original; *hook->original=reinterpret_cast<void*>(bookkeeping_observer);
    auto entry=static_cast<uint8_t*>(gd::at(p.bookkeeping_return))-9;
    std::array<uint8_t,14> original{}; memcpy(original.data(),entry,original.size());
    // Keep the real call and its return address; isolate it with a tiny ABI
    // prologue/epilogue so no engine globals or I/O are required for the test.
    const uint8_t prologue[]{0x48,0x83,0xec,0x28}, epilogue[]{0x48,0x83,0xc4,0x28,0xc3};
    gd::write_memory(entry,prologue,sizeof(prologue)); gd::write_memory(entry+9,epilogue,sizeof(epilogue));
    const auto tick=reinterpret_cast<void(__fastcall*)()>(entry);
    bookkeeping_calls=0; gd::begin_timing_frame(0); tick();
    for (unsigned i=0;i<240*120;++i) {gd::begin_timing_frame(1.0/240);tick();tick();}
    check(bookkeeping_calls==2,"bookkeeping is scheduled once per elapsed minute");
    gd::begin_timing_frame(180);tick();
    check(bookkeeping_calls==3,"bookkeeping coalesces missed intervals into one timestamp-based call");
    reinterpret_cast<void(__fastcall*)()>(gd::at(p.bookkeeping.address))();
    check(bookkeeping_calls==4,"other bookkeeping callers run immediately");
    gd::write_memory(entry,original.data(),original.size()); *hook->original=saved;
    gd::end_scene_frame();
    puts("PASS: periodic bookkeeping schedule, stalls and independent immediate callers");
}
static void preview_clock_test() {
    const auto& p=gd::target::get();
    const auto hooks=gd::scene_inline_hooks();
    auto invoke=[&](const gd::target::Site& site,gd::InlineContext& c) {
        const auto hook=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==&site;});
        check(hook!=hooks.end(),"preview inline site registered");
        c.ip=reinterpret_cast<uintptr_t>(gd::at(site.address))+site.signature.length;hook->handler(c);
    };
    std::vector<uint8_t> object(gd::target::layout::preview_positions+40);
    auto positions=reinterpret_cast<int32_t*>(object.data()+gd::target::layout::preview_positions);
    gd::InlineContext c{};c.r8=reinterpret_cast<uintptr_t>(object.data());
    for (unsigned hz : {95U,120U,144U,240U,360U}) {
        std::fill(positions,positions+10,1080);gd::begin_timing_frame(0);
        for(const auto& site:p.preview_steps)invoke(site,c);
        for(unsigned frame=0;frame<hz*21;++frame) {
            gd::begin_timing_frame(1.0/hz);
            for(const auto& site:p.preview_steps){invoke(site,c);invoke(site,c);}
        }
        for(unsigned i=0;i<10;++i)check(positions[i]==1020,"preview positions wrap with elapsed time at every cadence");
    }
    std::fill(positions,positions+10,0);gd::begin_timing_frame(0);invoke(p.preview_steps[0],c);
    gd::begin_timing_frame(1.0/240);invoke(p.preview_steps[0],c);
    struct SavedVectors { std::array<uint8_t,512> ymm;gd::InlineContext c; } draw{};
    static_assert(offsetof(SavedVectors,c)==512);
    draw.ymm.fill(0x5a);draw.c.rcx=reinterpret_cast<uintptr_t>(object.data());
    const auto mode=gd::config.animation_timing;gd::config.animation_timing=2;
    for(unsigned i=0;i<2;++i) {
        invoke(p.preview_samples[i],draw.c);float sampled=0;memcpy(&sampled,draw.c.ymm(i),sizeof(sampled));
        check(sampled==-0.25f && positions[0]==0,"preview draws fractional motion without changing logical position");
        check(draw.c.ymm(i)[4]==0x5a && draw.c.ymm(i)[16]==0,"preview sample preserves XMM merge lanes and VEX upper-zero behavior");
    }
    gd::config.animation_timing=mode;
    gd::begin_timing_frame(60);invoke(p.preview_steps[0],c);
    check(positions[0]==0,"preview motion retains multiple complete loops during a long stall");
    int32_t countdown=600;c.r8=reinterpret_cast<uintptr_t>(&countdown);c.r9=0;
    gd::begin_timing_frame(0);invoke(p.preview_countdown,c);
    unsigned events=0;
    for(unsigned i=0;i<240*25;++i) {
        gd::begin_timing_frame(1.0/240);invoke(p.preview_countdown,c);
        if(c.ip==reinterpret_cast<uintptr_t>(gd::at(p.preview_event)))++events;
        invoke(p.preview_countdown,c);check(c.ip==reinterpret_cast<uintptr_t>(gd::at(p.preview_next)),"duplicate preview update cannot replay an effect");
    }
    check(events==2 && countdown==300,"preview effects retain their ten-second period");
    gd::begin_timing_frame(26.5);invoke(p.preview_countdown,c);
    check(c.ip==reinterpret_cast<uintptr_t>(gd::at(p.preview_event)) && countdown==510,
        "missed preview effects coalesce once and preserve the remaining phase");
    gd::end_scene_frame();
    puts("PASS: preview positions/effects at arbitrary cadence, wraps, duplicates, full stalls and fractional drawing");
}
static void hit_effect_clock_test() {
    const auto& p=gd::target::get();const auto hooks=gd::scene_inline_hooks();
    for(const auto* site:{&p.hit_effect_short,&p.hit_effect_long}) {
        const auto hook=std::find_if(hooks.begin(),hooks.end(),[&](const auto& h){return h.site==site;});
        check(hook!=hooks.end(),"hit-effect inline site registered");
        const bool short_effect=site==&p.hit_effect_short;
        const unsigned terminal=short_effect?8:13;
        std::array<uint8_t,128> object{};
        auto& age=*reinterpret_cast<uint32_t*>(object.data()+(short_effect?0:gd::target::layout::hit_long_age));
        gd::InlineContext c{};c.rbx=c.r8=reinterpret_cast<uintptr_t>(object.data());
        for(unsigned hz:{95U,120U,144U,240U,360U}) {
            age=0;gd::begin_timing_frame(0);hook->handler(c);check(age==1,"new hit effect is visible immediately");
            unsigned frames=0;
            while(age<terminal && frames<hz) {
                gd::begin_timing_frame(1.0/hz);hook->handler(c);hook->handler(c);++frames;
            }
            check(age==terminal && std::abs(double(frames)/hz-double(terminal-1)/60)<=1.0/hz+1e-9,
                "hit-effect duration follows elapsed time while duplicate renders hold the same authored frame");
            if(short_effect)check(!(c.flags&64) && !(c.flags&128),"expired hit effect takes native greater-than branch");
            else check(c.rax==terminal,"long hit effect supplies corrected EAX to native comparison");
        }
        age=0;gd::begin_timing_frame(0);hook->handler(c);gd::begin_timing_frame(120);hook->handler(c);
        check(age==terminal,"hit effect expires after stall without replaying intermediate drawings");
    }
    gd::end_scene_frame();puts("PASS: short/long hit-effect deadlines, immediate response, duplicates and stalls");
}
static void native_secondary_test() {
    const auto& p = gd::target::get();
    timing_commit(p.secondary_player_base, sizeof(uint32_t));
    timing_commit(p.secondary_service_mode, sizeof(uint32_t));
    timing_memory<uint32_t>(p.secondary_player_base) = 0;
    timing_memory<uint32_t>(p.secondary_service_mode) = 1;
    std::array<void*,6> saved{};
    for (size_t i=0;i<saved.size();++i) {
        saved[i] = timing_memory<void*>(p.secondary_inputs[i]);
        timing_memory<void*>(p.secondary_inputs[i]) = reinterpret_cast<void*>(secondary_input);
    }
    TimingRedirect input_object(p.secondary_input_object,reinterpret_cast<void*>(secondary_input_object));
    // State 18 -> 19 has no renderer/backend dependencies and runs the real
    // controller, inline age hook, state reset and native stack-cookie check.
    std::array<uint8_t,256> object{}; object[0] = 1;
    auto& state = *reinterpret_cast<uint32_t*>(object.data()+gd::target::layout::secondary_state);
    auto& age = *reinterpret_cast<uint32_t*>(object.data()+gd::target::layout::secondary_age);
    const auto update = reinterpret_cast<int64_t(__fastcall*)(int,void*)>(gd::at(p.secondary_update.address));
    for (unsigned hz : {95U,120U,144U,240U,360U}) {
        state=18; age=0; gd::begin_timing_frame(0); update(0,object.data()); secondary_input_calls=0;
        unsigned frames=0;
        while (state==18 && frames<hz) {
            gd::begin_timing_frame(1.0/hz); update(0,object.data()); ++frames;
        }
        check(state==19 && age==0 && std::abs(double(frames)/hz-31.0/60) <= 1.0/hz+1e-9,
            "secondary scene deadline and native reset use elapsed time");
        check(secondary_input_calls==frames*6, "secondary scene input remains at full render cadence");
    }
    state=18; age=0; gd::begin_timing_frame(0); update(0,object.data());
    gd::begin_timing_frame(2); update(0,object.data());
    check(state==19 && age==0,"secondary deadline crosses after stall and native state reset wins");
    const auto& body=p.secondary_at20_body;
    auto bridge=gd::prepare_inline_hook({&body,secondary_threshold});
    gd::write_memory(gd::at(body.address),bridge.replacement.data(),bridge.replacement.size());
    for (double step : {1.0/120,1.0/144,1.0/240,2.0}) {
        state=5;age=0;secondary_threshold_calls=0;gd::begin_timing_frame(0);update(0,object.data());
        for (unsigned i=0;i<unsigned(std::ceil(1.0/step));++i) {
            gd::begin_timing_frame(step);update(0,object.data());update(0,object.data());
        }
        check(secondary_threshold_calls==1,"native equality branch fires once when elapsed time crosses 20, including stall");
    }
    gd::write_memory(gd::at(body.address),body.signature.storage.data(),body.signature.length);
    auto& remaining=*reinterpret_cast<uint32_t*>(object.data()+gd::target::layout::secondary_countdown);
    for (unsigned hz : {120U,144U,240U}) {
        state=9;age=0;remaining=60;gd::begin_timing_frame(0);update(0,object.data());
        for (unsigned i=0;i<hz;++i) {gd::begin_timing_frame(1.0/hz);update(0,object.data());}
        check(remaining==0,"native secondary countdown consumes exactly one second");
    }
    state=9;remaining=60;gd::begin_timing_frame(0);update(0,object.data());
    gd::begin_timing_frame(10);update(0,object.data());
    check(remaining==0,"secondary countdown saturates at zero after long stall");
    for (size_t i=0;i<saved.size();++i) timing_memory<void*>(p.secondary_inputs[i])=saved[i];
    gd::end_scene_frame();
    puts("PASS: native secondary scene transitions, in-function clock and per-render input");
}
static void native_scene_timing_test() {
    const auto& p = gd::target::get();
    timing_commit(p.song_state, 0x100);
    timing_commit(p.scene_pointer, sizeof(void*));
    std::array<uint32_t, gd::target::layout::scene_seek_index + 1> scene{};
    timing_memory<void*>(p.scene_pointer) = scene.data();
    const auto start = reinterpret_cast<int64_t(__fastcall*)(unsigned char)>(gd::at(p.song_start.address));
    auto& state = timing_memory<uint32_t>(p.song_start_state);
    auto& delay = timing_memory<uint32_t>(p.song_start_delay);
    for (unsigned hz : {95U,120U,144U,240U,360U}) {
        state = 3; delay = 0; gd::begin_timing_frame(0); start(0);
        unsigned count = 0;
        while (state == 3 && count < hz) {
            gd::begin_timing_frame(1.0 / hz); start(0); ++count;
        }
        check(state == 4 && std::abs(double(count) / hz - 21.0/60.0) <= 1.0 / hz + 1e-9,
            "native song startup delay uses elapsed duration at every render cadence");
    }
    state = 3; delay = 0; gd::begin_timing_frame(0); start(0);
    gd::begin_timing_frame(2); start(0);
    check(state == 4, "song startup crosses its deadline after a stall");
    state = 3; delay = 0; gd::begin_timing_frame(0); start(1);
    check(state == 4, "native alternate startup bypass retained");
    state = 3; delay = 0; gd::end_scene_frame(); start(0);
    check(delay == 1 && state == 3, "non-render startup retains original increment");

    // Output calls are observed; the real native lighting state/lookup and
    // wrap logic executes on every render, including duplicate invocations.
    timing_commit(p.light_enabled, 64);
    timing_commit(p.light_device, sizeof(void*));
    timing_memory<void*>(p.light_device) = nullptr;
    TimingRedirect rgb(p.light_rgb_output, reinterpret_cast<void*>(light_observer));
    TimingRedirect brightness(p.light_brightness_output, reinterpret_cast<void*>(light_observer));
    const auto lights = reinterpret_cast<uint64_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(gd::at(p.lights.address));
    {
    TimingRedirect pattern(p.light_pattern_output, reinterpret_cast<void*>(light_observer));
    for (unsigned hz : {120U,144U,240U}) {
        timing_memory<uint32_t>(p.light_mode) = 3;
        timing_memory<uint8_t>(p.light_enabled) = 1;
        for (uintptr_t address : {p.light_age,p.light_color,p.light_brightness}) timing_memory<uint32_t>(address) = 0;
        gd::begin_timing_frame(0); lights(0,0,0); light_outputs = 0;
        for (unsigned i = 0; i < hz * 5; ++i) {
            gd::begin_timing_frame(1.0/hz); lights(0,0,0); lights(0,0,0);
        }
        check(timing_memory<uint32_t>(p.light_age) == 300 && timing_memory<uint32_t>(p.light_color) == 300,
            "cabinet light phase is elapsed time, with no double charge");
        check(timing_memory<uint32_t>(p.light_brightness) == 300 % gd::target::layout::light_period,
            "native cabinet brightness wrap preserved");
        check(light_outputs == hz * 5 * 2 * 3, "cabinet output runs on every call");
    }
    }
    const auto pattern = reinterpret_cast<void(__fastcall*)()>(gd::at(p.light_pattern.address));
    timing_memory<uint8_t>(p.light_pattern_enabled) = 1;
    timing_memory<uint32_t>(p.light_pattern_mode) = 1;
    timing_memory<uint32_t>(p.light_pattern_index) = 0;
    timing_memory<uint32_t>(p.light_color) = 17; // Original modulo guard would reject it.
    gd::begin_timing_frame(0); pattern();
    for (unsigned i = 0; i < 240 * 8; ++i) { gd::begin_timing_frame(1.0 / 240); pattern(); pattern(); }
    check(timing_memory<uint32_t>(p.light_pattern_index) == 61,
        "native eight-tick light pattern fires once per elapsed boundary");
    gd::begin_timing_frame(4); pattern();
    check(timing_memory<uint32_t>(p.light_pattern_index) == 91 && timing_memory<uint32_t>(p.light_color) == 17,
        "pattern catches up to current phase after stall and restores shared color age");
    for (uintptr_t address : {p.session_age,p.report_age,p.color_phase}) {
        timing_commit(address,sizeof(uint32_t)); timing_memory<uint32_t>(address) = 0;
    }
    gd::begin_timing_frame(0);
    auto native_step = [&] {
        const auto before = gd::shared_counter_snapshot();
        ++timing_memory<uint32_t>(p.session_age); ++timing_memory<uint32_t>(p.report_age); ++timing_memory<uint32_t>(p.color_phase);
        gd::finish_shared_counters(before);
    };
    native_step();
    for (unsigned i = 0; i < 240 * 60; ++i) { gd::begin_timing_frame(1.0/240); native_step(); native_step(); }
    for (auto value : gd::shared_counter_snapshot()) check(value == 3600, "shared duration/color counters retain real seconds");
    gd::begin_timing_frame(3); native_step();
    for (auto value : gd::shared_counter_snapshot()) check(value == 3780, "shared counters keep full stalled interval");
    gd::end_scene_frame();
    native_secondary_test();
    native_network_test();
    bookkeeping_test();
    preview_clock_test();
    hit_effect_clock_test();
    native_tally_test();
    native_result_test();
    native_battle_test();
    native_effect_test();
    native_gf_overlay_test();
    native_song_scene_test();
    native_service_timing_test();
    native_diagnostic_schedule_test();
    native_system_timing_test();
    native_remaining_wait_test();
    native_tutorial_timing_test();
    native_drum_tutorial_test();
    native_summary_timing_test();
    native_minigame_timing_test();
    arena_timing_test();
    native_spinner_timing_test();
    native_options_preview_test();
    overlay_transition_test();
    native_option_test();
    controller_time_test();
    puts("PASS: native song-start delay, cabinet light phases and shared session/report/color clocks");
}
