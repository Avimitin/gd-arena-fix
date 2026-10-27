#pragma once
#include "../src/result_timing.h"
static unsigned result_cues, result_inputs;
static uint64_t __fastcall result_sound(unsigned, unsigned, unsigned) {
    ++result_cues;
    return 0;
}
static uint64_t __fastcall result_input(unsigned) {
    ++result_inputs;
    return 0;
}
static void native_result_test() {
    const auto& p = gd::target::get();
    const auto hooks = gd::result_inline_hooks();
    timing_commit(p.result_age, gd::target::layout::result_size);
    timing_commit(p.overlay_age, 16);
    timing_commit(p.player_active, 2);
    timing_commit(p.result_guitar_flag, 1);
    timing_commit(p.result_gauge_dm_goal, 64);
    TimingRedirect skip(p.result_skip_input, reinterpret_cast<void*>(result_input));
    TimingRedirect sound(p.keysound, reinterpret_cast<void*>(result_sound));
    TimingRedirect enqueue(p.result_sound_enqueue, reinterpret_cast<void*>(result_sound));
    const auto update = reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.result_update));
    const auto request = reinterpret_cast<bool(__fastcall*)()>(gd::at(p.result_request));
    const auto numbers0 = reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.result_countup_update0));
    const auto numbers1 = reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.result_countup_update1));
    const auto gauges = reinterpret_cast<uint64_t(__fastcall*)(int, int)>(gd::at(p.result_gauge_update));
    const auto countdowns = reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.result_countdown_update));
    auto invoke = [&](const gd::target::Site& site, gd::InlineContext& c) {
        const auto hook = std::find_if(hooks.begin(), hooks.end(), [&](const auto& h) { return h.site == &site; });
        check(hook != hooks.end(), "result inline handler found");
        c.ip = reinterpret_cast<uintptr_t>(gd::at(site.address)) + site.signature.length;
        hook->handler(c);
    };
    auto frame = [&](double dt) {
        gd::begin_timing_frame(dt);
        gd::InlineContext c{};
        invoke(p.result_begin, c);
    };
    auto reset = [&](unsigned state = 99, unsigned age = 0) {
        memset(gd::at(p.result_age), 0, gd::target::layout::result_size);
        timing_memory<uint32_t>(p.result_age) = age;
        timing_memory<uint32_t>(p.result_state) = state;
        timing_memory<uint16_t>(p.player_active) = 0;
        timing_memory<uint32_t>(p.result_game_stage) = 0;
        result_cues = result_inputs = 0;
        frame(0);
    };
    for (unsigned hz : {95U, 120U, 144U, 240U, 360U}) {
        reset();
        auto& delay = timing_memory<uint32_t>(p.result_sound_queue + gd::target::layout::result_queue_delay);
        delay = 120;
        timing_memory<int32_t>(p.result_sound_queue + 2 * sizeof(uint32_t)) = -1;
        update();
        for (unsigned i = 0; i < hz * 4; ++i) {
            gd::begin_timing_frame(1.0 / hz);
            update();
            update();
        }
        check(timing_memory<uint32_t>(p.result_age) == 240 && timing_memory<uint32_t>(p.result_color) == 240,
              "native regular result and color clocks follow elapsed time");
        check(delay == 0 && result_cues == 1, "native result sound queue expires once");
        reset(65);
        timing_memory<uint32_t>(p.result_request_active) = 0;
        unsigned requests = 0;
        for (unsigned i = 0; i < hz; ++i) {
            frame(1.0 / hz);
            request();
            requests += timing_memory<uint32_t>(p.result_request_active) != 0;
            timing_memory<uint32_t>(p.result_request_active) = 0;
            request();
            check(!timing_memory<uint32_t>(p.result_request_active) || timing_memory<uint32_t>(p.result_age) == 10,
                  "same-call event result is shared by native player branches");
            timing_memory<uint32_t>(p.result_request_active) = 0;
        }
        check(requests == 1, "native result request fires once at its elapsed crossing");
        reset(3);
        timing_memory<uint16_t>(p.player_active) = 0x101;
        for (unsigned player = 0; player < 2; ++player) {
            timing_memory<int32_t>(p.result_countup0 + player * 4 + gd::target::layout::result_countup_step) = 10;
            timing_memory<int32_t>(p.result_countup0 + player * 4 + gd::target::layout::result_countup_target) = 1000;
            timing_memory<int32_t>(p.result_countup1 + player * 4 - 16) = 1000;
            timing_memory<int32_t>(p.result_countup1 + player * 4 + 16) = 10;
            timing_memory<int32_t>(p.result_gauge_targets + player * 4) = 8000;
            timing_memory<int32_t>(p.result_gauge_dm_goal + player * 4) = 8000;
        }
        timing_memory<uint8_t>(p.result_guitar_flag) = 0;
        for (unsigned i = 0; i < hz; ++i) {
            frame(1.0 / hz);
            numbers0();
            numbers1();
            gauges(0, 0);
            gauges(1, 0);
        }
        for (unsigned player = 0; player < 2; ++player) {
            check(timing_memory<int32_t>(p.result_countup0 + player * 4) == 550 &&
                      timing_memory<int32_t>(p.result_countup1 + player * 4) == 400,
                  "native result count-ups include only elapsed steps after start");
            check(timing_memory<int32_t>(p.result_gauges + player * 4) == 6000 &&
                      timing_memory<int32_t>(p.result_gauges + 8 + player * 4) == 6000,
                  "native result gauges advance at the same speed at every cadence");
        }
        check(result_inputs >= hz * 2, "result skip inputs still polled each render");
        frame(5);
        numbers0();
        numbers1();
        gauges(0, 0);
        gauges(1, 0);
        check(timing_memory<int32_t>(p.result_countup0) == 1000 && timing_memory<int32_t>(p.result_countup1) == 1000 &&
                  timing_memory<int32_t>(p.result_gauges) == 8000,
              "stalled result values reach their native goals");
        reset(2);
        timing_memory<uint16_t>(p.player_active) = 0x101;
        timing_memory<uint8_t>(p.result_guitar_flag) = 1;
        for (auto offset : gd::target::layout::result_countdown_offsets)
            for (unsigned player = 0; player < 2; ++player)
                timing_memory<uint32_t>(p.result_age + offset + player * 4) = 61;
        countdowns();
        for (unsigned i = 0; i < hz; ++i) {
            frame(1.0 / hz);
            countdowns();
        }
        for (auto offset : gd::target::layout::result_countdown_offsets)
            for (unsigned player = 0; player < 2; ++player)
                check(timing_memory<uint32_t>(p.result_age + offset + player * 4) == 1,
                      "result player countdowns use elapsed time");
    }
    reset(65);
    frame(2);
    timing_memory<uint32_t>(p.result_request_active) = 0;
    check(request() && timing_memory<uint32_t>(p.result_request_active) == 1,
          "native exact-age request survives a stall and preserves SETG return flags");
    frame(0);
    timing_memory<uint32_t>(p.result_request_active) = 0;
    request();
    check(!timing_memory<uint32_t>(p.result_request_active), "held result age cannot repeat request");
    for (const auto& event : p.result_events) {
        reset(99, event.tick - 1);
        frame(2.0 / 60);
        gd::InlineContext c{};
        invoke(event.site, c);
        const auto i = static_cast<size_t>(&event - p.result_events.data());
        check(i < 9 ? (c.flags & 0x40) != 0 : c.ip == reinterpret_cast<uintptr_t>(gd::at(p.result_event_yes[i - 9])),
              "all result exact-age cues survive an overshot tick");
        frame(0);
        invoke(event.site, c);
        check(i < 9 ? (c.flags & 0x40) == 0 : c.ip == reinterpret_cast<uintptr_t>(gd::at(p.result_event_no[i - 9])),
              "result exact-age cues are one-shot within each scene epoch");
    }
    reset(99);
    frame(0.5);
    gd::InlineContext c{};
    invoke(p.result_periods[0], c);
    check((c.flags & 0x40) != 0, "result periodic sound coalesces elapsed boundaries");
    frame(0);
    invoke(p.result_periods[0], c);
    check((c.flags & 0x40) == 0, "result periodic sound does not repeat on held age");
    reset(99);
    gd::end_scene_frame();
    update();
    check(timing_memory<uint32_t>(p.result_age) == 1, "non-render result path retains stock behavior");
    puts("PASS: native regular results: elapsed ages, queued sounds, requests, score/gauge count-ups, two-player "
         "countdowns, crossings and stalls");
}
