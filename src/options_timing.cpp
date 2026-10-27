#include "mod.h"
#include "options_timing.h"
#include "animations.h"
namespace gd {
namespace {
void selection_ready(InlineContext& c) {
    const auto& p = target::get();
    const auto frame = uint32_t(c.rcx);
    // AFP executes every timeline action during catch-up, but this C++ poll
    // sees only the final position. Readiness is a persistent timeline state,
    // not an event requiring the render loop to sample the exact "loop" label.
    // Resolve the asset's own idle interval; no FPS scale, extra timer, seek or
    // special case for the observed stop frame is needed.
    // Both song and category controllers keep their AFP clip in RDI here.
    auto clip = reinterpret_cast<void*>(c.rdi);
    auto table = *reinterpret_cast<void***>(clip);
    using Label = uint8_t(__fastcall*)(void*, uint32_t*, const char*);
    auto label = reinterpret_cast<Label>(table[target::layout::selection::clip_label_slot]);
    uint32_t begin = 0, end = 0;
    if (frame && label(clip, &begin, p.selection_idle_label) && label(clip, &end, p.selection_move_label) &&
        begin < end) {
        const bool idle = frame >= begin && frame < end;
        c.cmp32(idle ? p.selection_ready_frame : ~p.selection_ready_frame, p.selection_ready_frame);
    } else {
        // A failed property/label query retains the original readiness test.
        c.cmp32(frame, p.selection_ready_frame);
    }
}
void selection_cue(InlineContext& c) {
    const auto& p = target::get();
    c.cmp32(uint32_t(c.rcx), uint32_t(c.rsi));
    const bool due = animation_event(*static_cast<void**>(at(p.selection_frame_object)), int32_t(c.rsi),
                                     p.selection_frame_cue.address);
    if (due)
        c.rdx = p.selection_frame_sound;
    else
        c.ip = reinterpret_cast<uintptr_t>(at(p.selection_frame_no));
}
} // namespace
std::vector<InlineHook> options_inline_hooks() {
    const auto& p = target::get();
    return {{&p.selection_frame_cue, selection_cue},
            {&p.selection_ready, selection_ready},
            {&p.category_ready, selection_ready}};
}
} // namespace gd
