#include "mod.h"
#include "song_timing.h"
#include "scene_timing.h"
#include "scene_timeline.h"
#include <unordered_map>

namespace gd {
namespace {
thread_local std::unordered_map<void*, ElapsedNativeCounter> clocks;
template <class T> T& field(uintptr_t base, size_t offset = 0) {
    return *reinterpret_cast<T*>(base + offset);
}
uint32_t& global(uintptr_t va) {
    return *static_cast<uint32_t*>(at(va));
}
uint32_t advance(uint32_t& value, uint32_t identity = 0, double rate = 60) {
    const auto& f = scene_frame();
    if (!f.active)
        return ++value;
    auto& clock = clocks[&value];
    const auto amount = clock.advance(value, identity, f.serial, f.elapsed * rate / 60);
    value = static_cast<uint32_t>(std::min(uint64_t(INT32_MAX), uint64_t(value) + amount));
    clock.finish(value);
    return value;
}
void load_timeout(InlineContext& c) {
    advance(field<uint32_t>(c.r8, target::layout::song_load_time));
}
void disconnect(InlineContext& c) {
    advance(field<uint32_t>(c.rax, target::layout::song_disconnect));
}
void phase(InlineContext& c) {
    c.rcx = advance(field<uint32_t>(c.r9, target::layout::song_phase_time));
}
void ready(InlineContext& c) {
    const auto& p = target::get();
    const auto special = field<uint8_t>(c.r9, target::layout::song_special);
    if (!scene_frame().active) {
        c.cmp32(special, uint8_t(c.rdi));
        return;
    }
    auto& value = field<uint32_t>(c.r9, target::layout::song_local_wait);
    // The ordinary branch increments twice until its fourth invocation;
    // special playback uses one increment and a 71-tick preincrement test.
    advance(value, field<uint32_t>(c.r9, target::layout::song_state), special ? 60 : 120);
    c.rcx = c.rax = value;
    c.ip = reinterpret_cast<uintptr_t>(at(value >= (special ? 71U : 6U) ? p.song_ready_yes : p.song_ready_no));
}
void local_wait(InlineContext& c) {
    auto& value = field<uint32_t>(c.r9, target::layout::song_local_wait);
    const auto old = value;
    advance(value, field<uint32_t>(c.r9, target::layout::song_state));
    c.rcx = scene_frame().active ? value : old;
    c.rax = value;
}
void hold(InlineContext& c) {
    c.cmp32(advance(field<uint32_t>(c.rcx + c.rbx)), 60);
}
void exit_age(InlineContext& c) {
    const auto& p = target::get();
    const auto value = advance(global(p.song_exit_age));
    if (c.ip == reinterpret_cast<uintptr_t>(at(p.song_exit_steps[1].address)) + p.song_exit_steps[1].signature.length) {
        c.rax = value;
        ++field<uint32_t>(c.r8, target::layout::song_ordinal);
    }
}
void post_delay(InlineContext& c) {
    c.rcx = advance(global(target::get().song_post_age));
}
void progress_delay(InlineContext& c) {
    c.rax = advance(global(target::get().song_progress_age));
    c.rdx = (c.rdx & ~uint64_t(255)) | 1;
}
void end_timeout(InlineContext& c) {
    advance(field<uint32_t>(c.rbx, target::layout::song_end_time));
}
void network_age(InlineContext& c) {
    c.r9 = advance(global(target::get().song_network_wait));
}
void visual_age(InlineContext&) {
    advance(global(target::get().song_visual_phase));
}
void intro_age(InlineContext&) {
    advance(global(target::get().song_intro_wait));
}
} // namespace
std::vector<InlineHook> song_inline_hooks() {
    const auto& p = target::get();
    std::vector<InlineHook> result{{&p.song_load_timeout, load_timeout}, {&p.song_disconnect_age, disconnect},
                                   {&p.song_phase_age, phase},           {&p.song_ready_gate, ready},
                                   {&p.song_post_delay, post_delay},     {&p.song_progress_delay, progress_delay},
                                   {&p.song_end_timeout, end_timeout},   {&p.song_network_age, network_age},
                                   {&p.song_visual_age, visual_age},     {&p.song_intro_age, intro_age}};
    for (const auto& site : p.song_local_waits)
        result.push_back({&site, local_wait});
    for (const auto& site : p.song_hold_steps)
        result.push_back({&site, hold});
    for (const auto& site : p.song_exit_steps)
        result.push_back({&site, exit_age});
    return result;
}
} // namespace gd
