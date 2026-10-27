#include "mod.h"
#include "timers.h"
#include "timing_math.h"
#include "scene_timing.h"
#include "scene_timeline.h"
#include <algorithm>
#include <unordered_map>

namespace gd {
namespace {
template <class T> T& memory(uintptr_t va) {
    return *static_cast<T*>(at(va));
}
using Update = int64_t(__fastcall*)();
using Set = int64_t(__fastcall*)(unsigned);
using Play = int64_t(__fastcall*)(int64_t, int64_t);
Update originals[3]{}, original_clear{};
Set original_set{};
Play original_play{};
ElapsedCounter menu_clock, play_clock;
int expected_menu = 0, expected_subtick = 0, expected_play = 0, expected_mode = 0;

// Login/registration callbacks have private per-player countdowns. Keep their
// original state machines running and replace only the countdown arithmetic.
// Aliases for a native field share fractional progress from the main QPC frame.
struct EntryClock {
    ElapsedNativeCounter elapsed;
    uint32_t before = 0;
    bool warned = false;
};
thread_local std::unordered_map<uint32_t*, EntryClock> entry_clocks;
uint64_t& entry_register(InlineContext& c, target::EntryRegister r) {
    using R = target::EntryRegister;
    switch (r) {
    case R::Rax:
        return c.rax;
    case R::Rcx:
        return c.rcx;
    case R::R8:
        return c.r8;
    case R::Rbx:
        return c.rbx;
    case R::Rsi:
        return c.rsi;
    case R::Rdi:
        return c.rdi;
    case R::R14:
        return c.r14;
    case R::R15:
        return c.r15;
    case R::Rbp:
        return c.rbp;
    default:
        throw std::runtime_error("invalid entry operand register");
    }
}
void entry_step(InlineContext& c) {
    using T = target::EntryTail;
    const auto& steps = target::get().entry_steps;
    const auto it = std::find_if(steps.begin(), steps.end(), [&](const auto& s) {
        return c.ip == reinterpret_cast<uintptr_t>(at(s.site.address)) + s.site.signature.length;
    });
    if (it == steps.end())
        return;
    auto address = entry_register(c, it->base) + it->offset;
    if (it->tail == T::IndexRdi)
        address += c.rbp * sizeof(uint32_t);
    auto& value = *reinterpret_cast<uint32_t*>(address);
    const auto old = value;
    const auto& frame = scene_frame();
    if (frame.active) {
        auto& clock = entry_clocks[&value];
        const bool fresh = clock.elapsed.restarted(old, 0, frame.serial);
        if (fresh)
            clock.warned = false;
        if (fresh || clock.elapsed.clock.seen != frame.serial)
            clock.before = old;
        const auto ticks = clock.elapsed.advance(old, 0, frame.serial, frame.elapsed);
        value = old - std::min(old, ticks);
        clock.elapsed.finish(value);
    } else
        value = old - 1;
    if (it->tail == T::SaveR12CmpEdi)
        c.r12 = uint32_t(c.rax);
    if (it->output != target::EntryRegister::None)
        entry_register(c, it->output) = value;
    // A load before DEC keeps the native division/store sequence intact. All
    // other sites replace the DEC and reproduce its displaced instructions.
    if (it->tail == T::BeforeDecR8)
        c.r8 = uint32_t(value + 1);
    else
        c.step32(value, true);
    if (it->tail == T::MagicEax)
        c.rax = 0x88888889U;
    if (it->tail == T::IndexRdi)
        c.rdi = address;
    if (it->tail == T::CmpEdi || it->tail == T::SaveR12CmpEdi)
        c.cmp32(value, uint32_t(c.rdi));
}
void entry_display(InlineContext& c) {
    const auto& p = target::get();
    const auto it = std::find_if(p.entry_displays.begin(), p.entry_displays.end(), [&](const auto& s) {
        return c.ip == reinterpret_cast<uintptr_t>(at(s.site.address)) + s.site.signature.length;
    });
    if (it == p.entry_displays.end())
        return;
    c.cmp32(uint32_t(c.rcx), 59);
    bool show = uint32_t(c.rcx) == 59;
    if (scene_frame().active) {
        const auto player = uint32_t(entry_register(c, it->player));
        const auto owner = memory<uintptr_t>(p.entry_owner);
        // Native code already computed remaining/60 in EDX. Compare against
        // its own display cache: no repeated beeps and no missed modulo tick
        // when a slow frame skips over n*60-1.
        if (owner && player < 2)
            show = *reinterpret_cast<int*>(owner + target::layout::entry_display_cache + player * 4) != int32_t(c.rdx);
    }
    if (!show)
        c.ip = reinterpret_cast<uintptr_t>(at(it->skip));
}
void entry_warning(InlineContext& c) {
    auto& value = *reinterpret_cast<uint32_t*>(c.rsi + target::layout::entry_warning_counter);
    c.cmp32(value, 1200);
    bool due = value == 1200;
    if (scene_frame().active) {
        auto& clock = entry_clocks[&value];
        due = !clock.warned && clock.before > 1200 && value <= 1200;
        clock.warned |= due;
    }
    if (!due)
        c.ip = reinterpret_cast<uintptr_t>(at(target::get().entry_warning_skip));
}

int64_t update_menu(Update original) {
    auto& counter = memory<int>(target::get().menu_subtick);
    auto& remaining = memory<int>(target::get().menu_seconds);
    if (remaining != expected_menu || counter != expected_subtick)
        menu_clock.reset(scene_frame().serial);
    // Let the game's own pause check run every frame, without crossing its
    // 80-update boundary. Its UI, sounds and hold-at-one path still own expiry.
    counter = 0;
    auto result = original();
    const bool paused = memory<uint8_t>(target::get().menu_paused) != 0;
    auto due = menu_clock.advance(scene_frame().serial, scene_frame().elapsed, 1.0, paused);
    // Once negative, callers have already seen expiry. Bound catch-up work
    // after long stalls and while the service hold flag keeps the value at 1.
    due = std::min(due, static_cast<uint32_t>(std::max(remaining, 0)) + 1U);
    while (due--) {
        counter = 79;
        result = original();
    }
    counter = std::clamp(static_cast<int>(menu_clock.phase * 80.0), 0, 79);
    expected_subtick = counter;
    expected_menu = remaining;
    return result;
}
int64_t __fastcall update0() {
    return update_menu(originals[0]);
}
int64_t __fastcall update1() {
    return update_menu(originals[1]);
}
int64_t __fastcall update2() {
    return update_menu(originals[2]);
}
void reset_menu_clock() {
    menu_clock.reset(scene_frame().serial);
    expected_menu = memory<int>(target::get().menu_seconds);
    expected_subtick = memory<int>(target::get().menu_subtick);
}
int64_t __fastcall set_timer(unsigned value) {
    const auto result = original_set(value);
    reset_menu_clock();
    return result;
}
int64_t __fastcall clear_timer() {
    const auto result = original_clear();
    reset_menu_clock();
    return result;
}
int64_t __fastcall play_timer(int64_t a, int64_t b) {
    auto& remaining = memory<int>(target::get().play_ticks);
    const int mode = memory<int>(target::get().play_mode);
    if (remaining != expected_play || mode != expected_mode)
        play_clock.reset(scene_frame().serial);
    const bool paused = !mode || memory<uint8_t>(target::get().play_paused) || remaining <= 0;
    const auto ticks = play_clock.advance(scene_frame().serial, scene_frame().elapsed, 60.0, paused);
    if (!paused)
        remaining -= static_cast<int>(std::min(ticks, static_cast<uint32_t>(remaining)));
    expected_play = remaining;
    expected_mode = mode;
    // Only its dec eax instruction is removed. All other per-frame work runs.
    const auto before = shared_counter_snapshot();
    const auto result = original_play(a, b);
    finish_shared_counters(before);
    return result;
}
} // namespace
std::vector<DiagnosticHook> timer_hooks() {
    return {
        bind_hook(target::get().menu_update0, reinterpret_cast<void*>(update0),
                  reinterpret_cast<void**>(&originals[0])),
        bind_hook(target::get().menu_update1, reinterpret_cast<void*>(update1),
                  reinterpret_cast<void**>(&originals[1])),
        bind_hook(target::get().menu_update2, reinterpret_cast<void*>(update2),
                  reinterpret_cast<void**>(&originals[2])),
        bind_hook(target::get().menu_set, reinterpret_cast<void*>(set_timer), reinterpret_cast<void**>(&original_set)),
        bind_hook(target::get().menu_clear, reinterpret_cast<void*>(clear_timer),
                  reinterpret_cast<void**>(&original_clear)),
        bind_hook(target::get().play_timer, reinterpret_cast<void*>(play_timer),
                  reinterpret_cast<void**>(&original_play))};
}
std::vector<InlineHook> timer_inline_hooks() {
    const auto& p = target::get();
    std::vector<InlineHook> hooks;
    for (const auto& step : p.entry_steps)
        hooks.push_back({&step.site, entry_step});
    for (const auto& display : p.entry_displays)
        hooks.push_back({&display.site, entry_display});
    hooks.push_back({&p.entry_warning, entry_warning});
    return hooks;
}
} // namespace gd
