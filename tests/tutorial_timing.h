#pragma once
#include "../src/tutorial_timing.h"
static std::array<unsigned,64> tutorial_cues;
static unsigned tutorial_sounds;
static uint64_t __fastcall tutorial_narration_stub(unsigned cue) {if(cue<tutorial_cues.size())++tutorial_cues[cue];return 0;}
static uint64_t __fastcall tutorial_sound_stub(unsigned) {++tutorial_sounds;return 0;}
static void native_tutorial_timing_test() {
    const auto& p=gd::target::get();const auto hooks=gd::tutorial_inline_hooks();
    timing_commit(p.tutorial_players,p.tutorial_player_stride*2+64);
    memset(gd::at(p.tutorial_players),0,p.tutorial_player_stride*2+64);
    TimingRedirect narration(p.tutorial_narration,reinterpret_cast<void*>(tutorial_narration_stub));
    TimingRedirect sound(p.tutorial_sound,reinterpret_cast<void*>(tutorial_sound_stub));
    const auto update=reinterpret_cast<unsigned(__fastcall*)(uintptr_t)>(gd::at(p.tutorial_update));
    auto& state=timing_memory<uint32_t>(p.tutorial_state);auto& left=timing_memory<uint32_t>(p.tutorial_remaining);
    auto& position=timing_memory<uint32_t>(p.tutorial_position);auto& pause=timing_memory<uint8_t>(p.tutorial_pause);
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        state=54;left=901;position=0;pause=0;tutorial_cues={};tutorial_sounds=0;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);update(0);
        for(unsigned f=0;f<hz*12;++f) {gd::begin_timing_frame(1.0/hz);update(0);update(0);}
        check(left==181&&position==3600,"native tutorial clock/countdown retain twelve real seconds");
        for(unsigned cue=30;cue<=33;++cue)check(tutorial_cues[cue]==3,"native tutorial exact-time narration fires once per crossing");
        check(tutorial_sounds==12,"native tutorial timed sounds are duplicate-safe");
        // Paused position and continuing instructional countdown are separate.
        pause=1;gd::begin_timing_frame(0.5);update(0);
        check(position==3600&&left==151,"tutorial pause freezes only its practice-note clock");
        state=54;left=901;position=0;pause=0;tutorial_cues={};tutorial_sounds=0;
        gd::begin_timing_frame(0);update(0);gd::begin_timing_frame(12);update(0);
        check(left==181&&position==3600,"tutorial retains long stalled interval");
        for(unsigned cue=30;cue<=33;++cue)check(tutorial_cues[cue]==1,"obsolete tutorial narration coalesces after a stall");
        check(tutorial_sounds==4,"obsolete tutorial chimes coalesce by cue group");
        update(0);check(tutorial_sounds==4,"tutorial stalled chimes do not repeat on a duplicate call");
        state=p.tutorial_pause_states[0];left=600;position=0;pause=0;
        gd::begin_timing_frame(0);update(0);gd::begin_timing_frame(8);update(0);
        check(left==120&&position==1200,"stall crossing both tutorial pause boundaries integrates only running time");
        state=p.tutorial_pause_states[1];left=342;position=0;pause=0;
        gd::begin_timing_frame(0);update(0);gd::begin_timing_frame(3);update(0);
        check(left==162&&position==510,"tutorial late pause begins at its deadline inside a stalled interval");
    }
    auto invoke=[&](const gd::target::Site& site,gd::InlineContext& c) {
        auto h=std::find_if(hooks.begin(),hooks.end(),[&](const auto& x){return x.site==&site;});
        c.ip=reinterpret_cast<uintptr_t>(gd::at(site.address))+site.signature.length;h->handler(c);
    };
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        struct PracticeContext {gd::InlineContext c{};std::array<uint8_t,256> stack{};} fixture;
        auto& cue=fixture.c;cue.rdi=reinterpret_cast<uintptr_t>(&fixture);
        anim_field<uint64_t>(reinterpret_cast<void*>(cue.native_rsp()),p.tutorial_practice_saved_r13)=0x12345678;
        state=54;left=901;position=0;pause=0;
        gd::begin_timing_frame(0);update(0);unsigned hits=0;
        for(unsigned frame=0;frame<hz;++frame) {
            gd::begin_timing_frame(1.0/hz);update(0);
            cue.rcx=300;cue.rdx=position;cue.rax=150;invoke(p.tutorial_practice_cue,cue);
            if(cue.ip==reinterpret_cast<uintptr_t>(gd::at(p.tutorial_practice_cue_yes)))++hits;
            invoke(p.tutorial_practice_cue,cue);
            check(cue.ip==reinterpret_cast<uintptr_t>(gd::at(p.tutorial_practice_cue_no)),"practice cue does not repeat within its short window");
        }
        check(hits==1&&cue.r13==0x12345678,"practice cue crosses once and restores the native saved register");
        state=54;left=901;position=0;pause=0;gd::begin_timing_frame(0);update(0);
        gd::begin_timing_frame(1);update(0);cue.rcx=300;cue.rdx=position;cue.rax=150;invoke(p.tutorial_practice_cue,cue);
        check(cue.ip==reinterpret_cast<uintptr_t>(gd::at(p.tutorial_practice_cue_yes)),"practice cue survives a stall across its entire window");
        std::array<int32_t,3> gauge{3000,0,0};gd::InlineContext c{};c.r14=reinterpret_cast<uintptr_t>(&gauge[1]);
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);invoke(p.tutorial_gauge,c);
        int expected=0;for(unsigned i=0;i<60;++i)expected+=(3000-expected)/10;
        for(unsigned f=0;f<hz;++f){gd::begin_timing_frame(1.0/hz);invoke(p.tutorial_gauge,c);invoke(p.tutorial_gauge,c);}
        check(gauge[1]==expected,"tutorial gauge easing matches authored arithmetic at arbitrary cadence");
        gd::begin_timing_frame(1000);invoke(p.tutorial_gauge,c);
        check(gauge[1]>=2991&&gauge[1]<=3000,"tutorial gauge catches up to native fixed point across a long stall");
    }
    gd::end_scene_frame();puts("PASS: native tutorial countdown, practice-note clock, 30 cue crossings, pauses and stalled narration; gauge easing");
}
