#pragma once
#include "../src/timers.h"
#include "../src/scene_timeline.h"
#include <array>
#include <limits>
#include <thread>

static void timing_math_test() {
    const auto* saved_profile = gd::target::selected;
    for (const auto* profile : gd::target::supported) {
        check(gd::target::select(profile->sha256) && &gd::target::get() == profile,
              "supported hash selects the corresponding central profile");
    }
    check(!gd::target::select("unsupported"), "unknown image clears the selected profile");
    bool rejected = false;
    try {
        (void)gd::target::get();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    check(rejected, "unverified image cannot access game addresses");
    gd::target::selected = saved_profile;
    for (unsigned hz : {60U, 90U, 120U, 144U, 240U, 360U}) {
        gd::Config cfg;
        cfg.fps = cfg.refresh = hz;
        check(gd::valid_config(cfg), "matching high-refresh configuration accepted");
        cfg.refresh = hz + 1;
        check(!gd::valid_config(cfg), "mismatched FPS and refresh still rejected");
        gd::ElapsedCounter seconds, play;
        gd::FractionalTicks chart;
        seconds.reset(1);
        play.reset(1);
        uint32_t seconds_total = 0, play_total = 0, chart_total = 0;
        for (uint64_t i = 2; i <= 60ULL * hz + 1; ++i) {
            seconds_total += seconds.advance(i, 1.0 / hz, 1, false);
            play_total += play.advance(i, 1.0 / hz, 60, false);
            chart_total += chart.advance(1.0 / hz);
            check(seconds.advance(i, 1.0 / hz, 1, false) == 0, "duplicate timer calls do not charge time");
        }
        check(seconds_total == 60 && play_total == 3600 && chart_total == 18000,
              "one minute is independent of requested render cadence");
    }
    gd::ElapsedCounter clock;
    clock.reset(1);
    check(clock.advance(2, 0.5, 1, false) == 0, "partial second retained");
    check(clock.advance(3, 10, 1, true) == 0, "pause freezes clock");
    check(clock.advance(4, 0.5, 1, false) == 1, "resume retains fractional phase");
    check(clock.advance(5, 2.25, 1, false) == 2, "countdown keeps elapsed time during a stall");
    check(clock.advance(10, 99, 1, false) == 0, "inactive scene does not accrue a backlog");
    check(clock.advance(11, std::numeric_limits<double>::quiet_NaN(), 1, false) == 0, "invalid delta rejected");
    gd::FractionalTicks chart;
    check(chart.advance(0.5) == 150, "song fallback keeps long frame duration");
    // Different owners can share a time source while retaining independent
    // restarts. Multiple call sites for the same owner share its progress.
    gd::ElapsedNativeCounter first, second;
    first.reset(10, 1, 1);
    second.reset(20, 1, 1);
    check(first.advance(10, 1, 2, 1.0 / 120) == 0 && second.advance(20, 1, 2, 1.0 / 120) == 0,
          "independent counters retain fractional progress from the same interval");
    first.finish(10);
    second.finish(20);
    check(first.advance(10, 2, 3, 1.0 / 120) == 0 && second.advance(20, 1, 3, 1.0 / 120) == 1,
          "restarting one owner leaves the other owner's phase intact");
    first.finish(10);
    second.finish(21);
    check(second.advance(21, 1, 3, 1.0 / 120) == 0, "aliases for a counter cannot charge the same interval twice");
    check(first.advance(10, 2, 4, 1.0 / 120) == 0, "restarted owner accumulates its own fractional progress");
    first.finish(10);
    check(first.advance(10, 2, 6, 30) == 0 && first.clock.phase == 0,
          "inactive owner starts a fresh epoch without stale phase or backlog");
    first.finish(10);
    check(first.advance(10, 2, 7, 1.0 / 120) == 0, "reactivated owner waits a full authored tick");
    puts("PASS: elapsed timer/music clocks at 60/90/120/144/240/360 Hz, duplicate calls, pause, stall and inactivity");
}

template <class T> static T& timing_memory(uintptr_t va) {
    return *static_cast<T*>(gd::at(va));
}
static void timing_commit(uintptr_t va, size_t bytes) {
    check(VirtualAlloc(gd::at(va), bytes, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit isolated timing state");
}
struct TimingRedirect {
    void* address;
    std::array<uint8_t, 14> before;
    TimingRedirect(uintptr_t va, void* replacement) : address(gd::at(va)) {
        memcpy(before.data(), address, before.size());
        uint8_t jump[14]{0xff, 0x25, 0, 0, 0, 0};
        memcpy(jump + 6, &replacement, sizeof(replacement));
        check(gd::write_memory(address, jump, sizeof(jump)), "redirect isolated UI/backend dependency");
    }
    ~TimingRedirect() {
        gd::write_memory(address, before.data(), before.size());
    }
};
static bool timer_debug_pause;
static unsigned timer_beeps, play_calls;
static bool __fastcall timer_pause(unsigned) {
    return timer_debug_pause;
}
static int64_t __fastcall timer_beep() {
    ++timer_beeps;
    return 0;
}
static int64_t __fastcall play_observer(int64_t a, int64_t b) {
    check(a == 123 && b == 456, "timed-play callback arguments preserved");
    ++play_calls;
    return 789;
}

static void native_timer_test() {
    timing_commit(gd::target::get().session_age, sizeof(uint32_t));
    timing_commit(gd::target::get().report_age, sizeof(uint32_t));
    timing_commit(gd::target::get().color_phase, sizeof(uint32_t));
    timing_commit(gd::target::get().menu_subtick, 32);
    timing_commit(gd::target::get().service_pause, 1);
    timing_commit(gd::target::get().play_mode, 12);
    TimingRedirect debug(gd::target::get().debug_pause, reinterpret_cast<void*>(timer_pause));
    TimingRedirect beep(gd::target::get().countdown_beep, reinterpret_cast<void*>(timer_beep));
    TimingRedirect set_beep(gd::target::get().keysound, reinterpret_cast<void*>(timer_beep));
    std::array<uint8_t, 1024> ui0{}, ui1{};
    auto ui = [&] {
        timing_memory<void*>(gd::target::get().menu_ui0) = ui0.data();
        timing_memory<void*>(gd::target::get().menu_ui1) = ui1.data();
    };
    ui();
    using Update = int64_t(__fastcall*)();
    using Set = int64_t(__fastcall*)(unsigned);
    const auto set = reinterpret_cast<Set>(gd::at(gd::target::get().menu_set.address));
    const Update updates[]{reinterpret_cast<Update>(gd::at(gd::target::get().menu_update0.address)),
                           reinterpret_cast<Update>(gd::at(gd::target::get().menu_update1.address)),
                           reinterpret_cast<Update>(gd::at(gd::target::get().menu_update2.address))};
    auto& remaining = timing_memory<int>(gd::target::get().menu_seconds);
    auto tick = [&](double dt, bool aliases = false) {
        gd::begin_timing_frame(dt);
        updates[2]();
        if (aliases) {
            updates[0]();
            updates[1]();
        }
    };
    for (unsigned hz : {95U, 120U, 144U, 240U}) {
        gd::begin_timing_frame(0);
        set(60);
        timer_beeps = 0;
        for (unsigned i = 0; i < 60 * hz; ++i) {
            tick(1.0 / hz, true);
            if (i + 1 == 30 * hz)
                check(remaining == 30, "60-second menu is still open after 30 seconds");
        }
        check(remaining == 0 && timer_beeps == 10, "native menu reaches zero after 60 seconds with original beeps");
        tick(1.0);
        check(remaining == -1, "native negative expiry sentinel preserved");
    }
    set(60);
    tick(0.5);
    timer_debug_pause = true;
    tick(5);
    check(remaining == 60, "debug pause freezes native timer");
    timer_debug_pause = false;
    timing_memory<uint8_t>(gd::target::get().service_pause) = 1;
    tick(5);
    check(remaining == 60, "service pause freezes native timer");
    timing_memory<uint8_t>(gd::target::get().service_pause) = 0;
    tick(0.5);
    check(remaining == 59, "native pause retains partial second");
    set(60);
    tick(2.25);
    check(remaining == 58, "native menu catches up after a render stall");
    set(60);
    tick(0.75);
    check(remaining == 60, "reset discards old phase");
    tick(0.25);
    check(remaining == 59, "reset starts full second");
    set(1);
    timing_memory<uint8_t>(gd::target::get().menu_hold) = 1;
    tick(10);
    check(remaining == 1, "native hold-at-one retained");
    timing_memory<uint8_t>(gd::target::get().menu_hold) = 0;
    tick(1);
    check(remaining == 0, "native hold releases normally");
    // Bind only the native clear routine's external memset import.
    auto clear_import = static_cast<void**>(gd::at(gd::target::get().memset_import));
    void* saved = *clear_import;
    *clear_import = reinterpret_cast<void*>(memset);
    reinterpret_cast<Update>(gd::at(gd::target::get().menu_clear.address))();
    *clear_import = saved;
    ui();
    set(60);
    tick(1);
    check(remaining == 59, "native clear and reinitialize reset timer clock");

    // Other per-frame work in this large function is represented by an observer;
    // native menu/sampler/filter code above and below runs without substitution.
    auto hooks = gd::timer_hooks();
    auto play_slot = hooks.back().original;
    saved = *play_slot;
    *play_slot = reinterpret_cast<void*>(play_observer);
    const auto play =
        reinterpret_cast<int64_t(__fastcall*)(int64_t, int64_t)>(gd::at(gd::target::get().play_timer.address));
    auto& left = timing_memory<int>(gd::target::get().play_ticks);
    for (unsigned hz : {120U, 144U, 240U}) {
        timing_memory<int>(gd::target::get().play_mode) = 1;
        left = 3600;
        gd::begin_timing_frame(0);
        play(123, 456);
        play_calls = 0;
        for (unsigned i = 0; i < 60 * hz; ++i) {
            gd::begin_timing_frame(1.0 / hz);
            check(play(123, 456) == 789, "timed-play return preserved");
        }
        check(left == 0 && play_calls == 60 * hz, "timed-play countdown is real time while other work runs each frame");
    }
    left = 600;
    gd::begin_timing_frame(0);
    play(123, 456);
    timing_memory<uint8_t>(gd::target::get().play_paused) = 1;
    gd::begin_timing_frame(3);
    play(123, 456);
    check(left == 600, "timed-play pause retained");
    timing_memory<uint8_t>(gd::target::get().play_paused) = 0;
    gd::begin_timing_frame(2.5);
    play(123, 456);
    check(left == 450, "timed-play stall catch-up");
    *play_slot = saved;
    puts("PASS: native shared menu aliases/reset/clear/pause/hold/beeps and timed-play wrapper at multiple cadences");
}

static unsigned entry_displays[2], entry_inputs;
static bool entry_pressed;
static void __fastcall entry_ui(unsigned player, int seconds) {
    const auto owner = timing_memory<uintptr_t>(gd::target::get().entry_owner);
    auto& displayed =
        anim_field<int>(reinterpret_cast<void*>(owner), gd::target::layout::entry_display_cache + player * 4);
    check(displayed != seconds, "entry display gate does not repeat a held digit");
    displayed = seconds;
    ++entry_displays[player];
}
static uint64_t __fastcall entry_button(unsigned) {
    ++entry_inputs;
    return entry_pressed;
}
static void native_entry_timer_test() {
    const auto& p = gd::target::get();
    const auto hooks = gd::timer_inline_hooks();
    auto invoke = [&](const gd::target::Site& site, gd::InlineContext& c) {
        c.ip = reinterpret_cast<uintptr_t>(gd::at(site.address)) + site.signature.length;
        std::find_if(hooks.begin(), hooks.end(), [&](const auto& h) { return h.site == &site; })->handler(c);
    };
    // Check the operands and displaced instructions of every guarded producer,
    // including callbacks using different registers for the same native field.
    std::array<uint8_t, 4096> operands{};
    for (const auto& s : p.entry_steps) {
        using T = gd::target::EntryTail;
        using R = gd::target::EntryRegister;
        const auto base = reinterpret_cast<uintptr_t>(operands.data() + 32);
        auto& value = *reinterpret_cast<uint32_t*>(base + s.offset + (s.tail == T::IndexRdi ? 4 : 0));
        auto run = [&] {
            gd::InlineContext c{};
            c.rbx = c.rsi = c.rdi = c.r14 = base;
            c.rbp = 1;
            c.rax = 7;
            c.flags = 0x203;
            invoke(s.site, c);
            if (s.output == R::Rax)
                check(c.rax == value, "entry EAX result preserved");
            if (s.output == R::Rcx)
                check(c.rcx == value, "entry ECX result preserved");
            if (s.output == R::R8)
                check(c.r8 == value + (s.tail == T::BeforeDecR8 ? 1U : 0U), "entry R8 load/decrement result preserved");
            if (s.tail == T::MagicEax)
                check(c.rax == 0x88888889U, "native signed-division multiplier restored");
            if (s.tail == T::IndexRdi)
                check(c.rdi == base + 4, "native per-player pointer advance restored");
            if (s.tail == T::SaveR12CmpEdi)
                check(c.r12 == 7, "native backend return preserved in R12");
            if (s.tail == T::CmpEdi || s.tail == T::SaveR12CmpEdi) {
                gd::InlineContext comparison{};
                comparison.flags = 0x203;
                comparison.cmp32(value, uint32_t(base));
                check(c.flags == comparison.flags, "entry native comparison flags preserved");
            } else
                check(c.flags & 1, "entry decrement leaves carry unchanged");
        };
        value = 180;
        gd::begin_timing_frame(0);
        gd::begin_timing_frame(0);
        run();
        run();
        for (unsigned n = 0; n < 144; ++n) {
            gd::begin_timing_frame(1.0 / 144);
            run();
            run();
        }
        check(value == 120, "every login producer and duplicate alias uses elapsed time");
        gd::begin_timing_frame(3);
        run();
        check(value == 0, "every login countdown catches up and saturates after stalls");
    }
    const auto& warning_step = *std::find_if(p.entry_steps.begin(), p.entry_steps.end(),
                                             [](const auto& s) { return s.tail == gd::target::EntryTail::CmpEdi; });
    auto& warning_left = anim_field<uint32_t>(operands.data(), gd::target::layout::entry_warning_counter);
    gd::InlineContext warning_context{};
    warning_context.rsi = reinterpret_cast<uintptr_t>(operands.data());
    warning_left = 1210;
    gd::begin_timing_frame(0);
    gd::begin_timing_frame(0);
    invoke(warning_step.site, warning_context);
    gd::begin_timing_frame(0.25);
    invoke(warning_step.site, warning_context);
    invoke(p.entry_warning, warning_context);
    check(warning_left == 1195 && warning_context.ip == reinterpret_cast<uintptr_t>(gd::at(p.entry_warning.address)) +
                                                            p.entry_warning.signature.length,
          "login warning survives a skipped exact-1200 tick");
    invoke(p.entry_warning, warning_context);
    check(warning_context.ip == reinterpret_cast<uintptr_t>(gd::at(p.entry_warning_skip)),
          "login warning fires only once per reset");
    timing_commit(p.entry_owner, sizeof(uintptr_t));
    timing_commit(p.entry_confirm_ticks, 8);
    timing_commit(p.entry_touch_enabled, 1);
    auto& owner = timing_memory<uintptr_t>(p.entry_owner);
    const auto saved = owner;
    std::vector<uint8_t> fixture(gd::target::layout::entry_fixture_size);
    owner = reinterpret_cast<uintptr_t>(fixture.data());
    timing_memory<uint8_t>(p.entry_touch_enabled) = 0;
    TimingRedirect ui(p.entry_display, reinterpret_cast<void*>(entry_ui));
    TimingRedirect input(p.entry_input, reinterpret_cast<void*>(entry_button));
    const auto update = reinterpret_cast<uint64_t(__fastcall*)(unsigned)>(gd::at(p.entry_confirm_update));
    auto& remaining = timing_memory<uint32_t>(p.entry_confirm_ticks);
    auto& other = timing_memory<uint32_t>(p.entry_confirm_ticks + sizeof(uint32_t));
    auto reset = [&] {
        remaining = 3600;
        other = 1800;
        entry_pressed = false;
        entry_inputs = 0;
        for (unsigned player = 0; player < 2; ++player) {
            anim_field<int>(fixture.data(), gd::target::layout::entry_display_cache + player * 4) = -1;
            entry_displays[player] = 0;
        }
    };
    // Run the actual native callback, including its timeout, input guard,
    // division and UI branch. Only UI/backend dependencies are substituted.
    reset();
    gd::end_scene_frame();
    for (unsigned i = 0; i < 3600; ++i)
        check(update(0) == (i == 3599 ? 9U : 10U), "stock entry callback expires after 3600 calls");
    std::vector<std::vector<double>> cadences;
    for (unsigned hz : {60U, 75U, 95U, 120U, 144U, 165U, 240U, 360U})
        cadences.push_back({1.0 / hz});
    cadences.push_back({1.0 / 240, 1.0 / 75, 1.0 / 144, 1.0 / 60});
    for (const auto& cadence : cadences) {
        reset();
        gd::begin_timing_frame(0);
        gd::begin_timing_frame(0);
        update(0);
        update(1);
        double elapsed = 0, roundoff = 0;
        unsigned n = 0;
        while (elapsed < 60 - 1e-9) {
            const auto dt = std::min(cadence[n++ % cadence.size()], 60 - elapsed);
            // Keep the fixture's minute endpoint from drifting by thousands
            // of additions; MSVC's long double has the same width as double.
            const auto adjusted = dt - roundoff, total = elapsed + adjusted;
            roundoff = (total - elapsed) - adjusted;
            elapsed = total;
            gd::begin_timing_frame(dt);
            const auto result = update(0);
            update(1);
            const auto expected = uint32_t(std::max(0.0, 3600 - std::floor(elapsed * 60 + 1e-7)));
            if (remaining != expected)
                printf("Entry mismatch cadence=%.9f frame=%u elapsed=%.12f remaining=%u expected=%u other=%u\n",
                       cadence[0], n, double(elapsed), remaining, expected, other);
            check(remaining == expected, "native login countdown follows elapsed time at every cadence");
            const auto value = remaining;
            update(0);
            check(remaining == value, "duplicate login callback cannot charge time twice");
            if (elapsed < 59.99)
                check(result == 10, "60-second login remains open until its actual deadline");
        }
        check(remaining == 0 && other == 0 && update(0) == 9, "both players expire at independent real-time deadlines");
        check(entry_displays[0] == 61 && entry_displays[1] == 31,
              "countdown digits update once per second without repeated beeps");
    }
    reset();
    gd::begin_timing_frame(0);
    gd::begin_timing_frame(0);
    update(0);
    gd::begin_timing_frame(2.25);
    update(0);
    check(remaining == 3465 && anim_field<int>(fixture.data(), gd::target::layout::entry_display_cache) == 57,
          "entry display catches up even when a stall skips its modulo-59 tick");
    const auto shown = entry_displays[0];
    update(0);
    check(entry_displays[0] == shown, "stalled frame displays only current seconds");
    remaining = 2250;
    gd::begin_timing_frame(0);
    update(0);
    gd::begin_timing_frame(1);
    update(0);
    entry_pressed = true;
    check(update(0) == 9, "entry input is handled immediately between timer ticks");
    check(entry_inputs > 0, "native entry input guard still polls its backend");
    gd::begin_timing_frame(10);
    gd::begin_timing_frame(10);
    entry_pressed = false;
    const auto held = remaining;
    update(0);
    check(remaining == held, "inactive login callback does not accumulate a backlog");
    owner = saved;
    gd::end_scene_frame();
    puts("PASS: native per-player login countdown, digits, timeout and immediate input at 60..360 Hz, variable cadence "
         "and stalls");
}

static int64_t playback_bytes;
static unsigned playback_polls;
static int64_t __fastcall playback_position(void*) {
    ++playback_polls;
    return playback_bytes;
}
static int64_t __fastcall playback_wallclock() {
    return 1000000;
}
static void native_song_clock_test() {
    timing_commit(gd::target::get().song_state, 0xB0);
    timing_commit(gd::target::get().audio_states, 0x300);
    timing_commit(gd::target::get().audio_object, 8);
    timing_commit(gd::target::get().voice_tree, 32);
    timing_commit(gd::target::get().voice_channels, gd::target::layout::voice_stride * 2 + sizeof(uint32_t));
    timing_commit(gd::target::get().scene_pointer, 8);
    std::array<uint32_t, gd::target::layout::scene_seek_index + 1> scene{};
    timing_memory<void*>(gd::target::get().scene_pointer) = scene.data();
    std::array<void*, gd::target::layout::voice_cursor_slot + 1> vtable{};
    vtable[gd::target::layout::voice_cursor_slot] = reinterpret_cast<void*>(playback_position);
    void** voice = vtable.data();
    timing_memory<void*>((gd::target::get().voice_tree + gd::target::layout::voice_node_object)) = &voice;
    timing_memory<void*>(gd::target::get().audio_object) = nullptr;
    for (unsigned channel = 0; channel < 3; ++channel)
        timing_memory<uint32_t>(gd::target::get().audio_states + 4 * channel) = 3;
    for (unsigned index = 0; index < 2; ++index) {
        timing_memory<int16_t>(gd::target::get().audio_format + gd::target::layout::audio_format_stride * index +
                               gd::target::layout::audio_bits) = 16;
        timing_memory<int32_t>(gd::target::get().audio_format + gd::target::layout::audio_format_stride * index +
                               gd::target::layout::audio_rate) = 48000;
    }
    auto& clock_import = timing_memory<void*>(gd::target::get().audio_clock_import);
    auto saved_clock = clock_import;
    clock_import = reinterpret_cast<void*>(playback_wallclock);
    const auto sample = reinterpret_cast<int64_t(__fastcall*)(unsigned, int)>(gd::at(gd::target::get().audio.address));
    const auto filter =
        reinterpret_cast<int64_t(__fastcall*)(unsigned char)>(gd::at(gd::target::get().song_filter.address));
    const auto reset = reinterpret_cast<void(__fastcall*)()>(gd::at(gd::target::get().song_reset));
    auto& output = timing_memory<float>(gd::target::get().song_output);
    auto measure = [&](double dt, double position, unsigned alternate = 0) {
        gd::begin_timing_frame(dt);
        playback_bytes = static_cast<int64_t>(std::floor(position * 192000.0));
        playback_polls = 0;
        const auto integer = sample(alternate ? 2 : 0, alternate);
        timing_memory<uint32_t>(alternate ? gd::target::get().audio_alternate : gd::target::get().audio_main) =
            static_cast<uint32_t>(integer);
        filter(static_cast<unsigned char>(alternate));
        check(playback_polls == 1, "one original backend poll, no added audio-worker poll");
        const double exact = playback_bytes * 300.0 / 192000.0;
        if (std::abs(output - exact) > 0.016)
            printf(
                "Clock mismatch: dt=%.9f position=%lld native=%lld raw=%llu output=%.9f expected=%.9f alternate=%u\n",
                dt, playback_bytes, integer, timing_memory<uint64_t>(gd::target::get().song_raw), double(output), exact,
                alternate);
        check(std::abs(output - exact) <= 0.016, "display clock tracks sampled playback without frame-cadence drift");
        check(timing_memory<uint64_t>(gd::target::get().song_raw) == uint32_t(integer),
              "native integer/seek metadata preserved");
    };
    for (unsigned hz : {95U, 110U, 119U, 120U, 144U, 240U}) {
        reset();
        double time = 100;
        for (unsigned i = 0; i < hz * 10; ++i) {
            time += 1.0 / hz;
            measure(1.0 / hz, time);
        }
    }
    reset();
    double time = 100;
    for (unsigned i = 0; i < 1200; ++i) {
        double dt = i % 120 == 0 ? 0.25 : (i % 2 ? 0.006 : 0.0106666666667);
        time += dt;
        measure(dt, time);
    }
    measure(0.5, 25);
    measure(0.5, 25); // backward seek followed by held playback
    measure(0.5, 50, 1);
    measure(0.5, 50, 1);
    check(timing_memory<int64_t>(gd::target::get().audio_timestamp) == 1000000 &&
              timing_memory<int64_t>(gd::target::get().audio_epoch) == 950000,
          "native audio sampler timestamps preserved");
    reset();
    timing_memory<uint32_t>(gd::target::get().audio_main) = 0;
    gd::begin_timing_frame(0.01);
    filter(0);
    check(output == 0, "initial zero playback stays zero");
    scene[gd::target::layout::scene_seek_index] = 600;
    timing_memory<uint32_t>(gd::target::get().audio_alternate) = 0;
    gd::begin_timing_frame(0.01);
    filter(1);
    check(output == 600, "native alternate scene seek retained");
    scene[gd::target::layout::scene_seek_index] = 0;
    measure(0.01, 20);
    timing_memory<uint32_t>(gd::target::get().audio_main) = 0;
    gd::begin_timing_frame(0.5);
    filter(0);
    check(output == 6150, "missing main clock uses full elapsed time, not frame count");
    filter(0);
    check(output == 6150, "duplicate filter fallback does not advance music twice");
    check(timing_memory<uint32_t>(gd::target::get().audio_main) == 0, "fallback does not overwrite source ownership");
    playback_bytes = 0;
    timing_memory<uint32_t>(gd::target::get().audio_alternate) = 6000;
    gd::begin_timing_frame(0.5);
    check(sample(2, 1) == 6150, "alternate audio fallback keeps elapsed time");
    check(sample(2, 1) == 6150, "duplicate alternate fallback does not advance music twice");
    timing_memory<uint32_t>((gd::target::get().audio_states + 2 * sizeof(uint32_t))) = 5;
    check(sample(2, 1) == 0, "alternate inactive state does not synthesize playback");
    timing_memory<uint32_t>((gd::target::get().audio_states + 2 * sizeof(uint32_t))) = 3;
    timing_memory<uint32_t>(gd::target::get().audio_alternate) = 6000;
    int64_t loader_result = 0;
    std::thread loader([&] { loader_result = sample(2, 1); });
    loader.join();
    check(loader_result == 6005, "non-render loading thread retains native fallback and isolated capture state");
    // A stale capture must never be used when the native source was updated
    // without a new audio sample in this frame.
    gd::begin_timing_frame(0.01);
    timing_memory<uint32_t>(gd::target::get().audio_main) = 12345;
    filter(0);
    check(output == 12345, "stale fractional capture cannot override a new source");
    timing_memory<int16_t>(gd::target::get().audio_format + gd::target::layout::audio_bits) = 0;
    playback_bytes = 4000000;
    check(sample(0, 0) == 0, "invalid format retains original zero return");
    timing_memory<int16_t>(gd::target::get().audio_format + gd::target::layout::audio_bits) = 16;
    clock_import = saved_clock;
    puts("PASS: native audio capture/filter at 95/110/119/120/144/240 Hz, jitter, stalls, holds, seeks and fallbacks");
}
