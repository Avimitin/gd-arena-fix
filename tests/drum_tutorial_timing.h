#pragma once
#include "../src/drum_tutorial_timing.h"
static unsigned drum_polls;
static uint64_t __fastcall drum_input_stub() {
    ++drum_polls;
    return 0;
}
static void native_drum_tutorial_test() {
    const auto& p = gd::target::get();
    const auto hooks = gd::drum_tutorial_inline_hooks();
    const auto bytes = p.dm_tutorial_feedback_delay + 16 - p.dm_tutorial_objects;
    timing_commit(p.dm_tutorial_objects, bytes);
    timing_commit(p.dm_tutorial_lights, 32);
    memset(gd::at(p.dm_tutorial_objects), 0, bytes);
    std::vector<std::unique_ptr<TimingRedirect>> inputs;
    for (auto va : p.dm_tutorial_inputs)
        inputs.push_back(std::make_unique<TimingRedirect>(va, reinterpret_cast<void*>(drum_input_stub)));
    TimingRedirect narration(p.dm_tutorial_narration, reinterpret_cast<void*>(tutorial_narration_stub));
    TimingRedirect sound(p.tutorial_sound, reinterpret_cast<void*>(tutorial_sound_stub));
    const auto update = reinterpret_cast<unsigned(__fastcall*)()>(gd::at(p.dm_tutorial_update));
    auto& state = timing_memory<uint32_t>(p.dm_tutorial_state);
    auto& left = timing_memory<uint32_t>(p.dm_tutorial_remaining);
    auto& position = timing_memory<uint32_t>(p.dm_tutorial_position);
    auto& pause = timing_memory<uint8_t>(p.dm_tutorial_pause);
    auto& phase = timing_memory<uint32_t>(p.dm_tutorial_phase);
    auto& goal = timing_memory<int32_t>(p.dm_tutorial_gauge_target);
    auto& value = timing_memory<int32_t>(p.dm_tutorial_gauge_value);
    auto initialize = [&](unsigned s, unsigned remaining) {
        state = s;
        left = remaining;
        position = 0;
        pause = 0;
        phase = 0;
        tutorial_cues = {};
        tutorial_sounds = 0;
        gd::begin_timing_frame(0);
        gd::begin_timing_frame(0);
        update();
    };
    for (unsigned hz : {95U, 120U, 144U, 240U, 360U}) {
        initialize(70, 1141);
        drum_polls = 0;
        for (unsigned f = 0; f < hz * 16; ++f) {
            gd::begin_timing_frame(1.0 / hz);
            update();
            update();
        }
        check(left == 181 && position == 4800 && phase == 0,
              "DM tutorial countdown, position and modulo phase retain sixteen real seconds");
        for (unsigned cue = 25; cue <= 28; ++cue)
            check(tutorial_cues[cue] == 4, "DM exact-time narration fires once at each crossing");
        check(tutorial_sounds == 16, "DM scheduled chimes retain their cadence without duplicate calls");
        check(drum_polls == hz * 16 * 2 * 18, "DM tutorial input remains polled on every native invocation");
        initialize(70, 1141);
        gd::begin_timing_frame(16);
        update();
        update();
        check(left == 181 && position == 4800, "DM tutorial clocks retain a long stalled interval");
        for (unsigned cue = 25; cue <= 28; ++cue)
            check(tutorial_cues[cue] == 1, "DM overdue narration coalesces by cue group");
        check(tutorial_sounds == 4, "DM overdue chimes coalesce by cue group");
        initialize(46, 600);
        gd::begin_timing_frame(8);
        update();
        check(left == 120 && position == 1200 && !pause,
              "DM pause integration crosses both boundaries during one stall");
        initialize(13, 601);
        unsigned pulses = 0;
        for (unsigned f = 0; f < hz; ++f) {
            gd::begin_timing_frame(1.0 / hz);
            memset(gd::at(p.dm_tutorial_lights), 0, 32);
            update();
            if (timing_memory<uint32_t>(p.dm_tutorial_lights + 24))
                ++pulses;
            memset(gd::at(p.dm_tutorial_lights), 0, 32);
            update();
            check(!timing_memory<uint32_t>(p.dm_tutorial_lights + 24), "DM tutorial cabinet pulse is duplicate-safe");
        }
        check(pulses == 5, "DM twelve-tick cabinet pulse follows elapsed time");
        for (unsigned demo : {52U, 54U, 56U}) {
            goal = demo == 52 ? 2200 : demo == 54 ? 3000 : 0;
            value = 0;
            int expected_goal = goal, expected_value = value;
            initialize(demo, 250);
            for (unsigned tick = 0; tick < 60; ++tick) {
                if (demo == 52 && expected_goal < 3000)
                    expected_goal = std::min(3000, expected_goal + 16);
                if (demo == 54 && expected_goal > 0)
                    expected_goal = std::max(0, expected_goal - 32);
                if (demo == 56 && expected_goal < 2200)
                    expected_goal += 32;
                expected_value += (expected_goal - expected_value) / 10;
            }
            for (unsigned f = 0; f < hz; ++f) {
                gd::begin_timing_frame(1.0 / hz);
                update();
                update();
            }
            check(goal == expected_goal && value == expected_value,
                  "DM gauge demo and easing retain the authored recurrence at every cadence");
            goal = demo == 52 ? 2200 : demo == 54 ? 3000 : 0;
            value = 0;
            initialize(demo, 250);
            gd::begin_timing_frame(1);
            update();
            check(goal == expected_goal && value == expected_value,
                  "DM gauge demo handles a stalled interval identically");
        }
    }
    // Execute the native result-panel sound queue with every other panel flag
    // cleared. Its backend is observed, while queue traversal remains native.
    timing_commit(p.result_panel_phase, 0x400);
    memset(gd::at(p.result_panel_phase), 0, 0x400);
    TimingRedirect audio(p.keysound, reinterpret_cast<void*>(tutorial_sound_stub));
    const auto panel = reinterpret_cast<uint64_t(__fastcall*)()>(gd::at(p.result_panel_update));
    for (unsigned hz : {95U, 120U, 144U, 240U, 360U}) {
        timing_memory<uint32_t>(p.result_panel_age) = 0;
        timing_memory<uint32_t>(p.result_panel_phase) = 0;
        timing_memory<uint32_t>(p.result_panel_state) = 0;
        auto& queue = timing_memory<uint32_t>(p.result_panel_queue_data);
        queue = 120;
        tutorial_sounds = 0;
        gd::begin_timing_frame(0);
        gd::begin_timing_frame(0);
        panel();
        for (unsigned f = 0; f < hz; ++f) {
            gd::begin_timing_frame(1.0 / hz);
            panel();
            panel();
        }
        check(queue == 60 && timing_memory<uint32_t>(p.result_panel_age) == 61 &&
                  timing_memory<uint32_t>(p.result_panel_phase) == 61,
              "native result-panel ages and queued sound retain elapsed time");
        gd::begin_timing_frame(3);
        panel();
        panel();
        check(queue == 0 && tutorial_sounds == 1, "native result-panel sound fires once across a stalled deadline");
    }
    gd::end_scene_frame();
    puts("PASS: native DM tutorial clocks, 35 cue crossings, cabinet pulses, pause windows, input polling and gauge "
         "demonstration; result-panel queue");
}
