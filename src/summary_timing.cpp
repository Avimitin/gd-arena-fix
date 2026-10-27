#include "mod.h"
#include "summary_timing.h"
#include "scene_timing.h"
#include "scene_timeline.h"
namespace gd {
namespace {
thread_local SceneTimeline timeline;
thread_local SceneTimeline::Event reveal;
thread_local ElapsedNativeCounter overall_clock;
uint32_t& memory(uintptr_t va) {
    return *static_cast<uint32_t*>(at(va));
}
void step(InlineContext& c) {
    const auto& p = target::get();
    const auto& f = scene_frame();
    auto& overall = memory(p.summary_overall);
    if (f.active) {
        overall = static_cast<uint32_t>(
            std::min(uint64_t(INT32_MAX), uint64_t(overall) + overall_clock.advance(overall, 0, f.serial, f.elapsed)));
        overall_clock.finish(overall);
        c.r9 = timeline.begin(uint32_t(c.r9), memory(p.summary_state), f.serial, f.elapsed);
    } else {
        ++overall;
        c.r9 = uint32_t(c.r9) + 1;
    }
    c.step32(uint32_t(c.r9));
}
void period(InlineContext& c) {
    if (!scene_frame().active) {
        c.rax = uint32_t(c.rax) & 0x80000003U;
        c.cmp32(uint32_t(c.rax), 0);
        return;
    }
    // The surrounding native gate limits these chimes to the first second.
    c.rax = timeline.periods(4, 4) ? 0 : 1;
    c.cmp32(uint32_t(c.rax), 0);
}
void event(InlineContext& c) {
    const auto& p = target::get();
    const bool due = scene_frame().active ? timeline.event(reveal, 60) : uint32_t(c.rcx) == 60;
    c.cmp32(uint32_t(c.rcx), 60);
    c.ip = reinterpret_cast<uintptr_t>(at(due ? p.summary_event_yes : p.summary_event_no));
}
} // namespace
std::vector<InlineHook> summary_inline_hooks() {
    const auto& p = target::get();
    std::vector<InlineHook> hooks{{&p.summary_step, step}, {&p.summary_event, event}};
    for (const auto& s : p.summary_periods)
        hooks.push_back({&s, period});
    return hooks;
}
} // namespace gd
