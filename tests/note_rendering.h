#pragma once
#include "../src/note_diagnostics.h"
#include "../src/timers.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

// note_fixture.asm calls a supplied function pointer, with this exact Win64
// layout, and exports the return PC used by the profile's caller allow-list.
struct NotePositionCall {
    void* function;
    int player, lane;
    float until, tail;
    float speed;
    int hit;
    unsigned mode;
    unsigned char skin;
};
static_assert(offsetof(NotePositionCall, player) == 8 && offsetof(NotePositionCall, lane) == 12 &&
                  offsetof(NotePositionCall, until) == 16 && offsetof(NotePositionCall, tail) == 20 &&
                  offsetof(NotePositionCall, speed) == 24 && offsetof(NotePositionCall, hit) == 28 &&
                  offsetof(NotePositionCall, mode) == 32 && offsetof(NotePositionCall, skin) == 36 &&
                  sizeof(NotePositionCall) == 40,
              "note fixture argument layout must match its MASM offsets");
extern "C" int64_t gd_note_position_fixture(const NotePositionCall* call);
extern "C" char gd_note_position_return;

struct NotePositionSeen {
    int player = 0, lane = 0;
    float until = 0, tail = 0, speed = 0;
    int hit = 0;
    unsigned mode = 0;
    unsigned char skin = 0;
};
static std::array<NotePositionSeen, 5> note_position_seen{};
static std::array<int64_t, 4> note_position_returns{};
static std::array<NotePositionCall, 4> note_position_calls{};
static unsigned note_position_seen_count = 0;
static void* note_unlisted_position = nullptr;
static int64_t note_unlisted_return = 0;
static bool note_call_unlisted_inside_batch = false;
static int note_batch_player = 0;
static float note_batch_time = 0;
static unsigned note_batch_mode = 0;
static int64_t note_batch_unused = 0;
static unsigned char note_batch_skin = 0;
static constexpr int64_t note_observer_return = 0x123456789abcLL;

static int64_t __fastcall note_position_observer(int player, int lane, float until, float tail, float speed, int hit,
                                                 unsigned mode, unsigned char skin) {
    if (note_position_seen_count < note_position_seen.size()) {
        const auto i = note_position_seen_count++;
        note_position_seen[i] = {player, lane, until, tail, speed, hit, mode, skin};
    }
    return note_observer_return;
}
static int64_t __fastcall note_batch_observer(int player, float time, unsigned mode, int64_t unused,
                                              unsigned char skin) {
    note_batch_player = player;
    note_batch_time = time;
    note_batch_mode = mode;
    note_batch_unused = unused;
    note_batch_skin = skin;
    note_position_seen_count = 0;
    for (unsigned i = 0; i < note_position_calls.size(); ++i)
        note_position_returns[i] = gd_note_position_fixture(&note_position_calls[i]);
    using Position = int64_t(__fastcall*)(int, int, float, float, float, int, unsigned, unsigned char);
    if (note_call_unlisted_inside_batch) {
        const auto call = reinterpret_cast<Position>(note_unlisted_position);
        const auto& args = note_position_calls[0];
        note_unlisted_return =
            call(args.player, args.lane, args.until, args.tail, args.speed, args.hit, args.mode, args.skin);
    }
    return note_observer_return;
}

static void note_prepare_calls(void* position, int player, unsigned mode, unsigned char skin) {
    for (unsigned i = 0; i < note_position_calls.size(); ++i) {
        note_position_calls[i] = {position, player, 4, 100.25f, 201.75f, 1.5f, 7, mode, skin};
    }
    ++note_position_calls[1].player;
    ++note_position_calls[2].mode;
    ++note_position_calls[3].skin;
}

static void note_rendering_test() {
    using Batch = int64_t(__fastcall*)(int, float, unsigned, int64_t, unsigned char);
    using Position = int64_t(__fastcall*)(int, int, float, float, float, int, unsigned, unsigned char);
    struct Restore {
        bool smoothing;
        bool diagnostics;
        unsigned fps;
        std::array<void*, 3> original;
        std::array<void**, 3> slots;
        const gd::target::Profile* profile;
        HMODULE game;
        bool have_hooks = false;
        ~Restore() {
            gd::config.note_smoothing = smoothing;
            gd::config.diagnostics = diagnostics;
            gd::config.refresh = fps;
            if (have_hooks) {
                for (size_t i = 0; i < original.size(); ++i)
                    if (slots[i])
                        *slots[i] = original[i];
            }
            gd::target::selected = profile;
            gd::game = game;
            gd::end_scene_frame();
        }
    } restore{
        gd::config.note_smoothing, gd::config.diagnostics, gd::config.refresh, {}, {}, gd::target::selected, gd::game};

    gd::target::select(gd::target::supported[0]->sha256);
    gd::target::Profile fixture_profile = gd::target::get();
    gd::target::selected = &fixture_profile;
    gd::game = GetModuleHandleW(nullptr);
    fixture_profile.note_position_returns.fill(0);
    fixture_profile.note_position_returns[0] = fixture_profile.preferred_base +
                                               reinterpret_cast<uintptr_t>(&gd_note_position_return) -
                                               reinterpret_cast<uintptr_t>(gd::game);

    // Keep the legacy observer hooks for Diagnostics, while installing only
    // the two render hooks for smoothing above 60 Hz.
    gd::config.diagnostics = false;
    gd::config.note_smoothing = false;
    gd::config.refresh = 60;
    check(gd::note_diagnostic_hooks().empty(), "disabled smoothing and diagnostics install no note hooks");
    gd::config.note_smoothing = true;
    gd::config.refresh = 120;
    check(gd::note_diagnostic_hooks().size() == 2, "high-FPS smoothing installs batch and position hooks only");
    gd::config.refresh = 60;
    check(gd::note_diagnostic_hooks().empty(), "60 FPS smoothing does not install note hooks");
    gd::config.note_smoothing = false;
    gd::config.diagnostics = true;
    check(gd::note_diagnostic_hooks().size() == 3, "60 FPS diagnostics retain all three observer hooks");
    gd::config.diagnostics = false;
    gd::config.note_smoothing = true;
    gd::config.refresh = 120;
    const auto hooks = gd::note_diagnostic_hooks();
    check(hooks.size() == 2, "smoothing-only note hooks expose batch and position");
    for (size_t i = 0; i < hooks.size(); ++i) {
        restore.original[i] = *hooks[i].original;
        restore.slots[i] = hooks[i].original;
    }
    restore.have_hooks = true;

    // Diagnostics are deliberately disabled: these observers never touch
    // options/global game memory, and draw is not invoked by the fixture.
    gd::config.diagnostics = false;
    note_call_unlisted_inside_batch = true;
    *hooks[0].original = reinterpret_cast<void*>(note_batch_observer);
    *hooks[1].original = reinterpret_cast<void*>(note_position_observer);
    const auto batch = reinterpret_cast<Batch>(hooks[0].replacement);
    const auto position = reinterpret_cast<Position>(hooks[1].replacement);
    note_unlisted_position = hooks[1].replacement;
    const unsigned mode = 3;
    const unsigned char skin = 2;
    constexpr int64_t unused = 0x7654321;

    auto run_batch = [&](int player, float time, unsigned selected_mode, unsigned char selected_skin) {
        note_prepare_calls(hooks[1].replacement, player, selected_mode, selected_skin);
        const auto result = batch(player, time, selected_mode, unused, selected_skin);
        check(result == note_observer_return, "note batch wrapper preserves its return value");
        check(note_batch_player == player && note_batch_time == time && note_batch_mode == selected_mode &&
                  note_batch_unused == unused && note_batch_skin == selected_skin,
              "note batch wrapper forwards every original argument unchanged");
        check(note_position_seen_count == note_position_seen.size(),
              "batch invokes four fixture and one unlisted nested position cases");
        for (unsigned i = 0; i < note_position_calls.size(); ++i) {
            check(note_position_returns[i] == note_observer_return,
                  "position wrapper preserves the original position return value");
            const auto& input = note_position_calls[i];
            const auto& seen = note_position_seen[i];
            check(seen.player == input.player && seen.lane == input.lane && seen.speed == input.speed &&
                      seen.hit == input.hit && seen.mode == input.mode && seen.skin == input.skin,
                  "position smoothing preserves non-time arguments");
            if (i != 0)
                check(seen.until == input.until && seen.tail == input.tail,
                      "caller/player/mode/skin mismatch leaves note times untouched");
        }
        check(note_unlisted_return == note_observer_return && note_position_seen[4].until == 100.25f &&
                  note_position_seen[4].tail == 201.75f,
              "unlisted C++ caller inside batch context passes through");
        return std::pair<float, float>{note_position_seen[0].until, note_position_seen[0].tail};
    };

    // The opt-in switch is inert when disabled, even inside a matched batch.
    gd::config.refresh = 120;
    gd::config.note_smoothing = false;
    gd::begin_timing_frame(1.0 / 120.0);
    auto seen = run_batch(0, 1.0f, mode, skin);
    check(seen.first == 100.25f && seen.second == 201.75f, "smoothing-off note times pass through unchanged");
    gd::end_scene_frame();

    gd::config.note_smoothing = true;
    bool fractional_shift = false;
    float previous_shift = 0;
    for (unsigned frame = 0; frame < 16; ++frame) {
        gd::begin_timing_frame(1.0 / 120.0);
        // The native chart cursor is measured in 300 ticks/sec. Quantize it
        // to 60 Hz while sampling at 120 Hz, giving repeated values and
        // five-tick steps for the render-only cursor to smooth.
        const float time = float(std::floor((300.0 + double(frame) * 2.5) / 5.0) * 5.0);
        seen = run_batch(0, time, mode, skin);
        const float shift = 100.25f - seen.first;
        check(std::abs(shift) <= 7.5f + 1e-3f, "note smoothing stays inside the 25 ms correction bound");
        check(std::abs((201.75f - seen.second) - shift) < 1e-4f,
              "head and tail receive the same visual correction and retain note length");
        if (std::abs(shift) > 1e-4f && std::abs(shift - std::round(shift)) > 1e-3f)
            fractional_shift = true;
        if (frame > 0)
            check(std::abs(shift - previous_shift) < 5.1f,
                  "coarse song-time samples produce continuous bounded note motion");
        previous_shift = shift;
        gd::end_scene_frame();
    }
    check(fractional_shift, "coarse note-time source produces fractional visual movement");

    // A fresh player gets its own raw anchor; advancing player zero afterward
    // must retain its phase instead of inheriting player one's value.
    gd::begin_timing_frame(1.0 / 120.0);
    const float p0_time = float(std::floor((300.0 + 16.0 * 2.5) / 5.0) * 5.0);
    seen = run_batch(0, p0_time, mode, skin);
    const bool player_zero_shifted = seen.first != 100.25f;
    seen = run_batch(1, 20.0f, mode, skin);
    check(player_zero_shifted && seen.first == 100.25f,
          "players keep independent note-clock corrections within one render frame");
    seen = run_batch(0, p0_time + 5.0f, mode + 1, skin);
    check(seen.first == 100.25f && seen.second == 201.75f, "mode identity change reanchors the first visual sample");
    gd::end_scene_frame();

    // The assembly caller is allow-listed, but context expires with the batch.
    note_prepare_calls(hooks[1].replacement, 0, mode, skin);
    note_position_seen_count = 0;
    gd::begin_timing_frame(1.0 / 120.0);
    const auto outside = gd_note_position_fixture(&note_position_calls[0]);
    check(outside == note_observer_return && note_position_seen[0].until == 100.25f &&
              note_position_seen[0].tail == 201.75f,
          "matched fixture caller outside batch context passes through");
    gd::end_scene_frame();
    check(position(0, 4, 100.25f, 201.75f, 1.5f, 7, mode, skin) == note_observer_return &&
              note_position_seen[note_position_seen_count - 1].until == 100.25f &&
              note_position_seen[note_position_seen_count - 1].tail == 201.75f,
          "unlisted C++ caller is never smoothed");

    // The feature remains gated at the native 60 Hz mode.
    gd::config.refresh = 60;
    gd::begin_timing_frame(1.0 / 60.0);
    seen = run_batch(0, p0_time, mode, skin);
    check(seen.first == 100.25f && seen.second == 201.75f, "60 FPS disables note smoothing");
    gd::end_scene_frame();
    puts("PASS: optional note smoothing is caller-scoped, per-player and render-only");
}
