#pragma once
#include "../src/summary_timing.h"
static unsigned summary_sounds;
static uint64_t __fastcall summary_zero_stub() {
    return 0;
}
static std::array<uint32_t, 5> summary_numbers;
static uint64_t __fastcall summary_sound_stub(unsigned, unsigned) {
    ++summary_sounds;
    return 0;
}
static uint64_t __fastcall summary_row_stub(unsigned row, unsigned value, unsigned) {
    if (row < summary_numbers.size())
        summary_numbers[row] = value;
    return 0;
}
static void native_summary_timing_test() {
    const auto& p = gd::target::get();
    timing_commit(p.summary_state, 4096);
    memset(gd::at(p.summary_state), 0, 4096);
    TimingRedirect sound(p.summary_sound, reinterpret_cast<void*>(summary_sound_stub));
    TimingRedirect skip(p.summary_skip, reinterpret_cast<void*>(summary_zero_stub));
    TimingRedirect rows(p.summary_row_display, reinterpret_cast<void*>(summary_row_stub));
    TimingRedirect digits(p.summary_digit_display, reinterpret_cast<void*>(summary_zero_stub));
    const auto update = reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.summary_update));
    auto& state = timing_memory<uint32_t>(p.summary_state);
    auto& age = timing_memory<uint32_t>(p.summary_age);
    for (unsigned hz : {95U, 120U, 144U, 240U, 360U}) {
        state = 3;
        age = 0;
        summary_sounds = 0;
        summary_numbers = {};
        for (unsigned row = 0; row < 5; ++row)
            timing_memory<uint32_t>(p.summary_row_targets + row * p.summary_row_stride) = 600 * (row + 1);
        gd::begin_timing_frame(0);
        gd::begin_timing_frame(0);
        update();
        for (unsigned frame = 0; frame < hz; ++frame) {
            gd::begin_timing_frame(1.0 / hz);
            update();
            update();
            for (unsigned row = 0; row < 5; ++row)
                check(summary_numbers[row] == age * 10 * (row + 1),
                      "native summary result rows retain their authored count-up duration");
        }
        check(age == 60 && state == 3 && summary_sounds == 15,
              "native summary result count-up chimes follow elapsed four-tick periods");
        state = 19;
        age = 0;
        gd::begin_timing_frame(0);
        update();
        gd::begin_timing_frame(6);
        update();
        check(state == 20, "native summary result wait ends across a six-second stall");
    }
    // The score reveal uses exact age 60 in the original and must not be skipped.
    const auto hooks = gd::summary_inline_hooks();
    gd::InlineContext c{};
    auto invoke = [&](const gd::target::Site& site) {
        auto h = std::find_if(hooks.begin(), hooks.end(), [&](const auto& x) { return x.site == &site; });
        c.ip = reinterpret_cast<uintptr_t>(gd::at(site.address)) + site.signature.length;
        h->handler(c);
    };
    state = 15;
    age = 0;
    c.r9 = 0;
    gd::begin_timing_frame(0);
    invoke(p.summary_step);
    gd::begin_timing_frame(1.1);
    c.r9 = 0;
    invoke(p.summary_step);
    c.rcx = c.r9;
    invoke(p.summary_event);
    check(c.ip == reinterpret_cast<uintptr_t>(gd::at(p.summary_event_yes)),
          "summary result reveal survives a skipped equality tick");
    age = uint32_t(c.r9);
    c.r9 = age;
    invoke(p.summary_step);
    invoke(p.summary_event);
    check(c.ip == reinterpret_cast<uintptr_t>(gd::at(p.summary_event_no)),
          "summary result reveal fires once in its stage");
    gd::end_scene_frame();
    puts("PASS: native summary result row count-ups, chime cadence, exit deadlines and exact-age reveal crossing");
}
