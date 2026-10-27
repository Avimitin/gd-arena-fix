#pragma once
#include "../src/options_timing.h"
static unsigned preview_open_calls;
static uint64_t __fastcall preview_seek_stub(uintptr_t, const char* label) {
    if (!strcmp(label, "open"))
        ++preview_open_calls;
    return 0;
}
static void native_options_preview_test() {
    const auto& p = gd::target::get();
    std::vector<uint8_t> data(p.options_preview_stride * 4);
    timing_commit(p.options_preview_data, sizeof(void*));
    const auto saved = timing_memory<void*>(p.options_preview_data);
    timing_memory<void*>(p.options_preview_data) = data.data();
    TimingRedirect seek(p.options_preview_seek, reinterpret_cast<void*>(preview_seek_stub));
    const auto update = reinterpret_cast<uint64_t(__fastcall*)(unsigned)>(gd::at(p.options_preview_update));
    for (unsigned hz : {95U, 120U, 144U, 240U, 360U}) {
        memset(data.data(), 0, data.size());
        preview_open_calls = 0;
        for (size_t player = 0; player < 2; ++player) {
            auto* row = data.data() + player * p.options_preview_stride;
            for (size_t i = 0; i < p.options_preview_repeat_count; ++i)
                anim_field<int32_t>(row, p.options_preview_repeat_offset + i * 4) = 120;
        }
        gd::begin_timing_frame(0);
        gd::begin_timing_frame(0);
        update(0);
        update(1);
        // Opening the options panel itself seeks two unrelated "open" labels
        // per player. Count periodic preview cues after that initialization.
        preview_open_calls = 0;
        for (unsigned f = 0; f < hz; ++f) {
            gd::begin_timing_frame(1.0 / hz);
            update(0);
            update(1);
            update(0);
            update(1);
        }
        for (size_t player = 0; player < 2; ++player) {
            auto* row = data.data() + player * p.options_preview_stride;
            for (size_t i = 0; i < p.options_preview_scroll_count; ++i)
                check(anim_field<int32_t>(row, p.options_preview_scroll_offset + i * 4) == -60,
                      "native options preview scroll retains one elapsed second");
            for (size_t i = 0; i < p.options_preview_repeat_count; ++i)
                check(anim_field<int32_t>(row, p.options_preview_repeat_offset + i * 4) == 60,
                      "native options preview repeat retains one elapsed second");
        }
        check(preview_open_calls == 0, "options preview cannot fire repeat cues early");
        gd::begin_timing_frame(23);
        update(0);
        update(1);
        const auto cues = preview_open_calls;
        check(cues > 0, "options preview repeats survive a long stalled interval");
        update(0);
        update(1);
        check(preview_open_calls == cues, "options preview repeats are duplicate-safe");
        for (size_t player = 0; player < 2; ++player) {
            auto* row = data.data() + player * p.options_preview_stride;
            for (size_t i = 0; i < p.options_preview_scroll_count; ++i)
                check(anim_field<int32_t>(row, p.options_preview_scroll_offset + i * 4) == 960,
                      "options preview scroll retains wrap overshoot");
            for (size_t i = 0; i < p.options_preview_repeat_count; ++i)
                check(anim_field<int32_t>(row, p.options_preview_repeat_offset + i * 4) == 480,
                      "options preview repeat retains period overshoot");
        }
    }
    timing_memory<void*>(p.options_preview_data) = saved;
    gd::end_scene_frame();
    puts("PASS: native two-player options preview, all ten scroll phases and twenty periodic effects, wraps, stalls "
         "and duplicate updates");
}
