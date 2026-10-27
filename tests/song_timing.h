#pragma once
#include "../src/song_timing.h"
static void native_song_scene_test() {
    const auto& p=gd::target::get();const auto hooks=gd::song_inline_hooks();
    std::vector<uint8_t> object(gd::target::layout::song_storage);
    const auto old=timing_memory<void*>(p.song_controller_object);timing_memory<void*>(p.song_controller_object)=object.data();
    const auto update=reinterpret_cast<uint64_t(__fastcall*)(uint64_t,int)>(gd::at(p.song_controller_update));
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        memset(object.data(),0,object.size());anim_field<uint32_t>(object.data(),gd::target::layout::song_state)=4;
        auto run=[&] {anim_field<uint32_t>(object.data(),gd::target::layout::song_outer_state)=2;check(update(0,0)==1,"native song load update returns normally");};
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);run();
        for(unsigned i=0;i<hz;++i) {gd::begin_timing_frame(1.0/hz);run();run();}
        check(anim_field<uint32_t>(object.data(),gd::target::layout::song_load_time)==60,"native five-minute timeout counts elapsed ticks");
        check(anim_field<uint32_t>(object.data(),gd::target::layout::song_ordinal)==hz*2+1,"native song update ordinal remains per call");
        gd::begin_timing_frame(200);run();check(anim_field<uint32_t>(object.data(),gd::target::layout::song_load_time)==12060,"native loading timeout retains full stalled time");
    }
    gd::InlineContext c{};c.r8=c.r9=c.rax=c.rbx=c.rcx=reinterpret_cast<uintptr_t>(object.data());
    auto invoke=[&](const gd::target::Site& site) {
        const auto h=std::find_if(hooks.begin(),hooks.end(),[&](const auto& item){return item.site==&site;});
        c.ip=reinterpret_cast<uintptr_t>(gd::at(site.address))+site.signature.length;h->handler(c);
    };
    for(unsigned hz:{95U,120U,144U,240U,360U}) for(unsigned special:{0U,1U}) {
        auto& value=anim_field<uint32_t>(object.data(),gd::target::layout::song_local_wait);value=0;
        object[gd::target::layout::song_special]=static_cast<uint8_t>(special);
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);invoke(p.song_ready_gate);
        unsigned frames=0;
        while(c.ip==reinterpret_cast<uintptr_t>(gd::at(p.song_ready_no)) && frames<hz*2) {
            gd::begin_timing_frame(1.0/hz);invoke(p.song_ready_gate);++frames;
        }
        const double expected=special?71.0/60:3.0/60;
        check(double(frames)/hz+1e-9>=expected && double(frames)/hz<expected+1.0/hz+1e-9,
            "song readiness preserves single/double increment branch durations at all refresh rates");
        value=0;gd::begin_timing_frame(0);invoke(p.song_local_waits[0]);
        for(unsigned i=0;i<hz;++i) {gd::begin_timing_frame(1.0/hz);invoke(p.song_local_waits[0]);invoke(p.song_local_waits[0]);}
        check(value==60 && c.rcx==60 && c.rax==60,"song state wait advances once per frame with native register outputs");
    }
    for(auto offset:gd::target::layout::song_hold_offsets) {
        c.rcx=reinterpret_cast<uintptr_t>(object.data());c.rbx=offset;
        auto& held=anim_field<uint32_t>(object.data(),offset);held=0;
        gd::begin_timing_frame(0);invoke(p.song_hold_steps[offset<40?0:1]);
        gd::begin_timing_frame(1.01);invoke(p.song_hold_steps[offset<40?0:1]);
        check(held==60 && (c.flags&64),"both player hold families retain one-second comparison boundary");
        gd::begin_timing_frame(0.02);invoke(p.song_hold_steps[offset<40?0:1]);
        check(held>60 && !(c.flags&64),"held input passes deadline after elapsed threshold");
    }
    for(const auto& item:std::array<std::pair<const gd::target::Site*,uintptr_t>,5>{{
        {&p.song_post_delay,p.song_post_age},{&p.song_progress_delay,p.song_progress_age},
        {&p.song_network_age,p.song_network_wait},{&p.song_visual_age,p.song_visual_phase},{&p.song_intro_age,p.song_intro_wait}}}) {
        timing_commit(item.second,4);auto& value=timing_memory<uint32_t>(item.second);value=0;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);invoke(*item.first);
        gd::begin_timing_frame(2);invoke(*item.first);invoke(*item.first);check(value==120,"song global wait/pulse counts full elapsed time once");
    }
    timing_memory<void*>(p.song_controller_object)=old;gd::end_scene_frame();
    puts("PASS: native song load timeout/ordinal; readiness branches, state waits, player holds, network delays and visual phase");
}
