#pragma once
#include "../src/arena_timing.h"
static unsigned arena_stop_calls;
static uint64_t __fastcall arena_stop_stub() {
    ++arena_stop_calls;
    return 17;
}
static void arena_timing_test() {
    const auto& p = gd::target::get();
    const auto hooks = gd::arena_inline_hooks();
    timing_commit(p.arena_age, 128);
    timing_commit(p.arena_state, 4);
    auto& age = timing_memory<uint32_t>(p.arena_age);
    auto& state = timing_memory<uint32_t>(p.arena_state);
    auto invoke = [&](const gd::target::Site& site, gd::InlineContext& c) {
        auto h = std::find_if(hooks.begin(), hooks.end(), [&](const auto& x) { return x.site == &site; });
        c.ip = reinterpret_cast<uintptr_t>(gd::at(site.address)) + site.signature.length;
        h->handler(c);
    };
    for (unsigned hz : {95U, 120U, 144U, 240U, 360U}) {
        for (size_t index = 0; index < p.arena_steps.size(); ++index) {
            age = 0;
            state = unsigned(index);
            gd::InlineContext c{};
            gd::begin_timing_frame(0);
            gd::begin_timing_frame(0);
            invoke(p.arena_steps[index], c);
            for (unsigned frame = 0; frame < hz; ++frame) {
                gd::begin_timing_frame(1.0 / hz);
                invoke(p.arena_steps[index], c);
                invoke(p.arena_steps[index], c);
            }
            check(age == 60, "arena stage waits follow elapsed time at every producer");
            gd::begin_timing_frame(6);
            invoke(p.arena_steps[index], c);
            c.rax = age;
            invoke(p.arena_event, c);
            check(c.flags & 64, "arena 300-tick event survives a long stall");
            invoke(p.arena_steps[index], c);
            c.rax = age;
            invoke(p.arena_event, c);
            check(!(c.flags & 64), "arena event does not repeat after its deadline");
        }
        for (const auto* site : {&p.arena_gesture0, &p.arena_gesture1, &p.arena_voice_delay}) {
            uint32_t voice = 120;
            auto& value =
                site == &p.arena_voice_delay
                    ? voice
                    : timing_memory<uint32_t>(site == &p.arena_gesture0 ? p.arena_gesture_age0 : p.arena_gesture_age1);
            value = 120;
            gd::InlineContext c{};
            c.rsi = reinterpret_cast<uintptr_t>(&voice);
            c.rcx = value;
            c.rax = 7;
            gd::begin_timing_frame(0);
            gd::begin_timing_frame(0);
            invoke(*site, c);
            for (unsigned frame = 0; frame < hz; ++frame) {
                gd::begin_timing_frame(1.0 / hz);
                invoke(*site, c);
                invoke(*site, c);
            }
            check(value == 60, "arena gesture and voice delays retain their duration");
            gd::begin_timing_frame(10);
            invoke(*site, c);
            check(value == (site == &p.arena_voice_delay ? 0U : UINT32_MAX),
                  "arena countdowns preserve zero versus negative expiry semantics");
        }
    }
    std::array<uint8_t, gd::target::layout::animation_size> animation{};
    anim_field<int16_t>(animation.data(), gd::target::layout::frame) = 10;
    for (const auto& event : p.frame_events) {
        timing_commit(event.object, sizeof(void*));
        const auto saved = timing_memory<void*>(event.object);
        timing_memory<void*>(event.object) = animation.data();
        arena_stop_calls = 0;
        std::unique_ptr<TimingRedirect> stop;
        if (event.call)
            stop = std::make_unique<TimingRedirect>(event.call, reinterpret_cast<void*>(arena_stop_stub));
        struct {
            gd::InlineContext c{};
            std::array<uint64_t, 32> stack{};
        } fixture;
        auto& c = fixture.c;
        c.rbp = 10;
        c.rax = c.rsi = c.rdi = c.r14 = 10;
        c.rbx = 123;
        c.r8 = 99;
        animation_test_frame(0);
        invoke(event.site, c);
        check(c.ip == reinterpret_cast<uintptr_t>(gd::at(event.site.address)) + event.site.signature.length,
              "animation-frame event resumes its native true branch");
        if (event.tail == gd::target::FrameEventTail::StackRbx)
            check(*reinterpret_cast<uint32_t*>(c.native_rsp() + event.stack_offset) == 123,
                  "animation-frame bridge restores displaced native stack argument");
        if (event.tail == gd::target::FrameEventTail::ZeroR8)
            check(c.r8 == 0, "animation-frame bridge restores displaced argument register");
        if (event.call)
            check(arena_stop_calls == 1 && c.rax == 17, "animation-frame bridge preserves displaced native call");
        c.rax = c.rsi = c.rdi = c.r14 = 10;
        invoke(event.site, c);
        if (gd::config.animation_timing)
            check(c.ip == reinterpret_cast<uintptr_t>(gd::at(event.no)),
                  "held animation label cannot repeat a scene event");
        gd::end_animation_frame();
        timing_memory<void*>(event.object) = saved;
    }
    gd::end_scene_frame();
    puts("PASS: arena stage/gesture/voice clocks, skipped 300-tick event and animation-label event bridges");
}
