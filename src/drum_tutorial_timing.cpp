#include "mod.h"
#include "drum_tutorial_timing.h"
#include "scene_timing.h"
#include "scene_timeline.h"
#include <unordered_map>
namespace gd {
namespace {
thread_local ElapsedNativeCounter remaining_clock, phase_clock, gauge_clock;
thread_local ElapsedCounter position_clock;
thread_local uint32_t previous = 0, current = 0, entry_state = 0, position_expected = 0;
thread_local bool first = false;
thread_local std::unordered_map<uint32_t*, ElapsedNativeCounter> delays;
template <class T> T& memory(uintptr_t va) {
    return *static_cast<T*>(at(va));
}
template <class T> T& field(uintptr_t base, ptrdiff_t offset = 0) {
    return *reinterpret_cast<T*>(base + offset);
}
bool matches(const InlineContext& c, const target::Site& s) {
    return c.ip == reinterpret_cast<uintptr_t>(at(s.address)) + s.signature.length;
}
uint32_t countdown(uint32_t& value, uint32_t supplied) {
    const auto& f = scene_frame();
    auto& clock = delays[&value];
    const auto amount = f.active ? clock.advance(supplied, 0, f.serial, f.elapsed) : 1;
    value = supplied - std::min(supplied, amount);
    if (f.active)
        clock.finish(value);
    return value;
}
void begin(InlineContext& c) {
    const auto& p = target::get();
    entry_state = memory<uint32_t>(p.dm_tutorial_state);
    c.rdx = uint64_t(int64_t(int32_t(entry_state)));
    const auto& f = scene_frame();
    if (!f.active)
        return;
    auto& left = memory<uint32_t>(p.dm_tutorial_remaining);
    first = remaining_clock.restarted(left, entry_state, f.serial);
    const double dt = !first && remaining_clock.clock.seen != f.serial ? valid_elapsed(f.elapsed) : 0;
    previous = left;
    const double from = double(left) - remaining_clock.clock.phase;
    left -= std::min(left, remaining_clock.advance(left, entry_state, f.serial, f.elapsed));
    remaining_clock.finish(left);
    current = left;
    auto& position = memory<uint32_t>(p.dm_tutorial_position);
    if (position != position_expected)
        position_clock.reset(f.serial);
    double running = memory<uint8_t>(p.dm_tutorial_pause) ? 0 : dt;
    if (entry_state == 46) {
        const double paused = std::max(0.0, std::min(from, 480.0) - std::max(from - dt * 60, 240.0)) / 60;
        running = std::max(0.0, dt - paused);
    }
    position = uint32_t(
        std::min(uint64_t(INT32_MAX), uint64_t(position) + position_clock.advance(f.serial, running, 300, false)));
    position_expected = position;
}
void position_step(InlineContext&) {
    if (!scene_frame().active)
        memory<uint32_t>(target::get().dm_tutorial_position) += 5;
}
void remaining_step(InlineContext& c) {
    if (!scene_frame().active) {
        c.rax = uint32_t(c.rax) - 1;
        memory<uint32_t>(target::get().dm_tutorial_remaining) = uint32_t(c.rax);
        c.step32(uint32_t(c.rax), true);
    }
}
void end(InlineContext& c) {
    const auto& p = target::get();
    auto& phase = memory<uint32_t>(p.dm_tutorial_phase);
    const auto& f = scene_frame();
    if (!f.active) {
        c.rax = phase;
        return;
    }
    const auto left = memory<uint32_t>(p.dm_tutorial_remaining), state = memory<uint32_t>(p.dm_tutorial_state);
    remaining_clock.advance(left, state, f.serial, 0);
    remaining_clock.finish(left);
    // With no active player, native pause branches return before the second
    // boundary. Reconcile the final pause state after a stalled interval.
    if (entry_state == 46)
        memory<uint8_t>(p.dm_tutorial_pause) = current <= 480 && current > 240;
    phase = (phase + phase_clock.advance(phase, 0, f.serial, f.elapsed)) % 32;
    phase_clock.finish(phase);
    c.rax = phase;
    c.ip = reinterpret_cast<uintptr_t>(at(p.dm_tutorial_end_next));
}
void event(InlineContext& c) {
    for (const auto& e : target::get().dm_tutorial_events)
        if (matches(c, e.site)) {
            c.cmp32(uint32_t(c.rax), e.tick);
            if (scene_frame().active) {
                const bool due = (first && current == e.tick) || (previous > e.tick && current <= e.tick);
                c.flags = (c.flags & ~uint64_t(64)) | (due ? 64 : 0);
            }
            return;
        }
}
void period(InlineContext& c) {
    c.cmp32(uint32_t(c.rcx), 1);
    bool due = uint32_t(c.rcx) == 1;
    if (scene_frame().active) {
        const auto through = [](uint32_t x) { return x >= 1 ? (uint64_t(x) - 1) / 12 + 1 : 0; };
        due = (first && current % 12 == 1) ||
              (previous > current && through(previous - 1) > through(current ? current - 1 : 0));
        c.flags = (c.flags & ~uint64_t(64)) | (due ? 64 : 0);
    }
    if (!due)
        c.ip = reinterpret_cast<uintptr_t>(at(target::get().dm_tutorial_input_next));
}
void delay(InlineContext& c) {
    const auto& p = target::get();
    const auto va = matches(c, p.dm_tutorial_delays[0]) ? p.dm_tutorial_input_delay : p.dm_tutorial_feedback_delay;
    c.rax = countdown(memory<uint32_t>(va), uint32_t(c.rax));
    c.step32(uint32_t(c.rax), true);
}
void demo(InlineContext& c) {
    const auto& p = target::get();
    if (scene_frame().active) {
        c.ip = reinterpret_cast<uintptr_t>(at(p.dm_tutorial_input_next));
        return;
    }
    if (matches(c, p.dm_tutorial_demo[0])) {
        c.rax = uint32_t(c.rax) + 16;
        c.cmp32(uint32_t(c.rax), uint32_t(c.r15));
    } else if (matches(c, p.dm_tutorial_demo[1])) {
        c.rcx = uint32_t(c.rcx) - 32;
        c.rax = uint32_t(c.rbx);
        c.cmp32(uint32_t(c.rcx), 0);
    } else {
        c.rax = uint32_t(c.rax) + 32;
        memory<uint32_t>(p.dm_tutorial_gauge_target) = uint32_t(c.rax);
    }
}
void gauge(InlineContext& c) {
    const auto& p = target::get();
    auto& value = memory<int32_t>(p.dm_tutorial_gauge_value);
    c.rcx = uint32_t(value);
    const auto& f = scene_frame();
    if (!f.active)
        return;
    auto& goal = memory<int32_t>(p.dm_tutorial_gauge_target);
    auto amount = gauge_clock.advance(uint32_t(value), 0, f.serial, f.elapsed);
    const auto step = [&](int& g, int v) {
        if (entry_state == 52 && g < 3000)
            g = std::min(3000, g + 16);
        if (entry_state == 54 && g > 0)
            g = std::max(0, g - 32);
        if (entry_state == 56 && g < 2200)
            g += 32;
        return v + (g - v) / 10;
    };
    // The target ramp and displayed easing form one authored recurrence.
    // Advancing both together avoids a cadence-dependent lag during the demo.
    while (amount--) {
        const auto old_goal = goal, next = step(goal, value);
        if (next == value && goal == old_goal)
            break;
        value = next;
    }
    gauge_clock.finish(uint32_t(value));
    double sample = value;
    if (config.animation_timing == 2) {
        auto g = goal;
        sample += (step(g, value) - value) * gauge_clock.clock.phase;
    }
    c.rcx = uint32_t(int32_t(sample));
    c.r8 = uint32_t(goal);
    c.ip = reinterpret_cast<uintptr_t>(at(p.dm_tutorial_gauge_next));
}
void effect(InlineContext& c) {
    const auto& p = target::get();
    auto& left = field<uint32_t>(c.rsi);
    if (!scene_frame().active) {
        c.rax = left;
        c.rcx = field<uintptr_t>(c.rdi, p.dm_tutorial_effect_object_offset);
        return;
    }
    countdown(left, left);
    const uint32_t sample = left ? 8 - left : 0;
    const auto set = reinterpret_cast<uint64_t(__fastcall*)(uintptr_t, uint32_t)>(at(p.dm_tutorial_clip_frame));
    set(field<uintptr_t>(c.rdi, p.dm_tutorial_effect_object_offset), sample);
    c.rax = set(field<uintptr_t>(c.rdi), sample);
    if (left)
        c.rbx = sample;
    c.ip = reinterpret_cast<uintptr_t>(at(p.dm_tutorial_effect_next));
}
} // namespace
std::vector<InlineHook> drum_tutorial_inline_hooks() {
    const auto& p = target::get();
    std::vector<InlineHook> h{{&p.dm_tutorial_begin, begin},         {&p.dm_tutorial_end, end},
                              {&p.dm_tutorial_step, remaining_step}, {&p.dm_tutorial_position_step, position_step},
                              {&p.dm_tutorial_gauge, gauge},         {&p.dm_tutorial_effect, effect}};
    for (const auto& e : p.dm_tutorial_events)
        h.push_back({&e.site, event});
    for (const auto& s : p.dm_tutorial_periods)
        h.push_back({&s, period});
    for (const auto& s : p.dm_tutorial_delays)
        h.push_back({&s, delay});
    for (const auto& s : p.dm_tutorial_demo)
        h.push_back({&s, demo});
    return h;
}
} // namespace gd
