#pragma once
#include "../src/service_timing.h"
static unsigned service_polls;
static uint64_t __fastcall service_poll_stub(unsigned) {++service_polls;return 0;}
static uint64_t __fastcall service_card_stub() {return 0;}
static void native_service_timing_test() {
    const auto& p=gd::target::get();const auto hooks=gd::service_inline_hooks();
    timing_commit(p.service_dialog_state,8);timing_commit(p.service_dialog_resource,4);
    timing_memory<uint32_t>(p.service_dialog_resource)=0;
    for(unsigned hz:{95U,120U,144U,240U,360U}) for(unsigned scenario=0;scenario<3;++scenario) {
        auto& state=timing_memory<uint32_t>(p.service_dialog_state);auto& age=timing_memory<uint32_t>(p.service_dialog_age);
        state=scenario==2?1:scenario+3;age=0;
        const auto update=reinterpret_cast<unsigned(__fastcall*)()>(gd::at(p.service_dialog_updates[scenario==2?1:0]));
        const double duration=timing_memory<float>(p.service_dialog_limits[scenario==0?0:1])/60;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);check(update()==1,"native service dialog starts active");
        check(age==1,"service dialog initialization sentinel consumed immediately");
        double elapsed=0;unsigned result=1;
        while(result && elapsed<duration+1) {gd::begin_timing_frame(1.0/hz);elapsed+=1.0/hz;result=update();if(result) result=update();}
        check(!result && elapsed+1e-9>=duration && elapsed<duration+1.0/hz+1e-9,"native service dialog deadline is cadence independent");
        state=scenario==2?1:scenario+3;age=0;gd::begin_timing_frame(0);update();gd::begin_timing_frame(10);check(!update(),"service dialog completes across a stall");
    }
    timing_commit(p.service_poll_age,4);timing_commit(p.service_poll_disabled,1);timing_commit(p.service_poll_enabled,1);
    timing_memory<uint8_t>(p.service_poll_disabled)=0;timing_memory<uint8_t>(p.service_poll_enabled)=1;
    TimingRedirect output(p.service_poll_output,reinterpret_cast<void*>(service_poll_stub));
    const auto poll=reinterpret_cast<void(__fastcall*)()>(gd::at(p.service_poll_updates[0]));
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        service_polls=0;timing_memory<uint32_t>(p.service_poll_age)=0;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);poll();poll();check(service_polls==4,"network output first sample is immediate and duplicate-safe");
        for(unsigned i=0;i<hz;++i) {gd::begin_timing_frame(1.0/hz);poll();poll();}
        check(service_polls==31*4,"network output follows a 30 Hz schedule independently of render rate");
        gd::begin_timing_frame(100);poll();check(service_polls==32*4,"stalled network output coalesces obsolete polls");
    }
    gd::InlineContext c{};
    auto invoke=[&](const gd::target::Site& site) {
        const auto h=std::find_if(hooks.begin(),hooks.end(),[&](const auto& x){return x.site==&site;});
        c.ip=reinterpret_cast<uintptr_t>(gd::at(site.address))+site.signature.length;h->handler(c);
    };
    for(unsigned i=0;i<2;++i) {
        timing_commit(p.service_words[i],2);auto& value=timing_memory<uint16_t>(p.service_words[i]);value=60;
        gd::begin_timing_frame(0);invoke(p.service_word_steps[i]);gd::begin_timing_frame(1);invoke(p.service_word_steps[i]);
        check(value==0 && c.rax==0,"16-bit service countdown saturates at zero");
    }
    std::array<uint8_t,gd::target::layout::service_io_size> io{};c.rbx=reinterpret_cast<uintptr_t>(io.data());
    for(unsigned i=0;i<3;++i) {
        const size_t offset=i==0?0:i==1?gd::target::layout::service_io_pending:gd::target::layout::service_io_hold;
        auto& value=anim_field<uint32_t>(io.data(),offset);value=i==2?0:60;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);invoke(p.service_io_steps[i]);
        gd::begin_timing_frame(1);invoke(p.service_io_steps[i]);invoke(p.service_io_steps[i]);
        check(value==(i==2?60U:0U),"I/O test countdowns and hold use elapsed time once per update");
    }
    timing_commit(p.judgment_hold,8);timing_commit(p.judgment_cooldown,4);timing_commit(p.service_light_age,4);
    for(unsigned player=0;player<2;++player) {
        c.r15=player;auto& value=timing_memory<uint32_t>(p.judgment_hold+player*4);value=0;
        gd::begin_timing_frame(0);invoke(p.judgment_hold_step);gd::begin_timing_frame(2);invoke(p.judgment_hold_step);check(value==120,"per-player debug hold uses two elapsed seconds");
    }
    std::vector<uint8_t> player(gd::target::layout::judgment_player_size);c.r14=reinterpret_cast<uintptr_t>(player.data());
    gd::begin_timing_frame(0);invoke(p.judgment_feedback_step);gd::begin_timing_frame(1);invoke(p.judgment_feedback_step);
    check(anim_field<uint32_t>(player.data(),gd::target::layout::judgment_feedback)==60,"judgment feedback duration is one second");
    gd::end_scene_frame();puts("PASS: native service dialog sentinels/deadlines and 30 Hz output; I/O holds, word countdowns and player feedback");
}
static void native_diagnostic_schedule_test() {
    const auto& p=gd::target::get();const auto hooks=gd::service_inline_hooks();
    timing_commit(p.qc_age,64);timing_commit(p.qc_audio_state,4);timing_commit(p.card_error_age,4);
    timing_memory<uint32_t>(p.qc_audio_state)=0;timing_memory<uint8_t>(p.qc_sound_ready)=1;
    const auto update=reinterpret_cast<uint64_t(__fastcall*)(unsigned,unsigned)>(gd::at(p.qc_update));
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        auto& age=timing_memory<uint32_t>(p.qc_age);auto& phase=timing_memory<uint32_t>(p.qc_phase);
        auto& brightness=timing_memory<uint8_t>(p.qc_brightness);age=phase=0;brightness=127;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);update(0,0);update(0,0);
        check(phase==1&&brightness==1,"native diagnostic first phase is immediate and duplicate-safe");
        for(unsigned f=0;f<hz;++f){gd::begin_timing_frame(1.0/hz);update(0,0);update(0,0);}
        check(age==60&&phase==2&&brightness==127,"native diagnostic output follows elapsed seconds");
        gd::begin_timing_frame(10);update(0,0);check(age==660&&phase==4&&brightness==127,"diagnostic phase and brightness retain missed periods without replaying I/O");
    }
    gd::InlineContext c{};
    auto invoke=[&](const gd::target::Site& site) {
        auto h=std::find_if(hooks.begin(),hooks.end(),[&](const auto& x){return x.site==&site;});
        c.ip=reinterpret_cast<uintptr_t>(gd::at(site.address))+site.signature.length;h->handler(c);
    };
    auto& age=timing_memory<uint32_t>(p.card_error_age);age=0;c.r8=0;
    gd::begin_timing_frame(0);invoke(p.card_error_step);
    gd::begin_timing_frame(3.1);invoke(p.card_error_step);invoke(p.card_error_period);
    check(age==186&&c.ip==reinterpret_cast<uintptr_t>(gd::at(p.card_error_due)),"card retry schedule detects crossed three-second boundary");
    invoke(p.card_error_step);invoke(p.card_error_period);
    check(c.ip==reinterpret_cast<uintptr_t>(gd::at(p.card_error_idle)),"card retry schedule is duplicate-safe");
    timing_commit(p.card_slots,p.card_slot_stride*2);timing_commit(p.card_count,4);
    TimingRedirect pump(p.card_pump,reinterpret_cast<void*>(service_card_stub));
    TimingRedirect errors(p.card_error_update,reinterpret_cast<void*>(service_card_stub));
    const auto cards=reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.card_update));
    for(unsigned hz:{95U,120U,144U,240U,360U}) {
        memset(gd::at(p.card_slots),0,p.card_slot_stride*2);timing_memory<uint32_t>(p.card_count)=2;
        for(unsigned slot=0;slot<2;++slot)timing_memory<uint32_t>(p.card_slots+slot*p.card_slot_stride+p.card_slot_age)=1800;
        gd::begin_timing_frame(0);gd::begin_timing_frame(0);cards();
        for(unsigned f=0;f<hz;++f){gd::begin_timing_frame(1.0/hz);cards();cards();}
        for(unsigned slot=0;slot<2;++slot)check(timing_memory<uint32_t>(p.card_slots+slot*p.card_slot_stride+p.card_slot_age)==1740,"both card-reader slots retain their thirty-second timeout");
        gd::begin_timing_frame(29);cards();
        for(unsigned slot=0;slot<2;++slot)check(timing_memory<uint32_t>(p.card_slots+slot*p.card_slot_stride)==9,"native card-reader timeout state survives a long stall");
    }
    age=0;c.rax=0;gd::begin_timing_frame(0);invoke(p.card_alternate_step);
    gd::begin_timing_frame(10.1);invoke(p.card_alternate_step);check(age==606,"alternate card-error path shares the elapsed clock");
    gd::end_scene_frame();puts("PASS: native diagnostic phase, both card-reader timeout slots, alternate error clock and retry crossings");
}
