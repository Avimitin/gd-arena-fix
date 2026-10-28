#pragma once
#include "../src/inline_hook.h"
#include "../src/note_clock.h"
#include "../src/note_diagnostics.h"
#include "../src/timers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

extern "C" void gd_inline_probe(void*, void*, const void*);

struct DMNoteProbeOutput {
    std::array<uint64_t, 15> regs{};
    uint64_t flags = 0;
    uint8_t padding[128]{};
    std::array<uint8_t, 512> ymm{};
};
static_assert(offsetof(DMNoteProbeOutput, ymm) == 256);

static float dm_probe_float(const DMNoteProbeOutput& output, unsigned reg) {
    float value;
    memcpy(&value, output.ymm.data() + reg * 32, sizeof(value));
    return value;
}

static void dm_note_rendering_test() {
    constexpr uintptr_t raw_offset = 0x400;
    constexpr uintptr_t first_function = 0x100;
    constexpr uintptr_t function_stride = 0x20;
    constexpr float initial_raw = 1000.0f;

    struct Restore {
        const gd::target::Profile* profile;
        HMODULE game;
        bool smoothing, diagnostics;
        unsigned fps;
        ~Restore() {
            gd::target::selected = profile;
            gd::game = game;
            gd::config.note_smoothing = smoothing;
            gd::config.diagnostics = diagnostics;
            gd::config.refresh = fps;
            gd::end_scene_frame();
        }
    } restore{gd::target::selected, gd::game, gd::config.note_smoothing, gd::config.diagnostics, gd::config.refresh};

    gd::target::select(gd::target::supported[0]->sha256);
    gd::target::Profile fixture_profile = gd::target::get();
    gd::target::selected = &fixture_profile;
    auto memory = std::unique_ptr<void, gd::FreeBridge>(
        VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    check(memory != nullptr, "allocate synthetic DM note renderer and raw-clock fixture");
    auto* code = static_cast<uint8_t*>(memory.get());
    gd::game = reinterpret_cast<HMODULE>(code);
    auto* raw = reinterpret_cast<float*>(code + raw_offset);
    *raw = initial_raw;
    fixture_profile.dm_clock_raw = fixture_profile.preferred_base + raw_offset;

    for (size_t i = 0; i < fixture_profile.dm_clock_reads.size(); ++i) {
        auto& read = fixture_profile.dm_clock_reads[i];
        const uintptr_t function = first_function + i * function_stride;
        // Seed all arithmetic flags before each native/patched call. STC alone
        // leaves flags from the probe's stack allocation, which can differ
        // between callers and caused a machine-dependent baseline comparison.
        const uint8_t flag_setup[]{0x39, 0xc0, 0xf9}; // CMP EAX,EAX; STC (no GPR changes).
        const uintptr_t site_offset = function + sizeof(flag_setup);
        const unsigned dest = read.destination;
        check(dest < 16, "DM profile destination is a valid YMM register");
        memcpy(code + function, flag_setup, sizeof(flag_setup));
        uint8_t instruction[8]{0xc5, static_cast<uint8_t>(dest >= 8 ? 0x7a : 0xfa),
                               0x10, static_cast<uint8_t>(((dest & 7) << 3) | 5),
                               0,    0,
                               0,    0};
        const int32_t displacement = static_cast<int32_t>(raw_offset - (site_offset + sizeof(instruction)));
        memcpy(instruction + 4, &displacement, sizeof(displacement));
        memcpy(code + site_offset, instruction, sizeof(instruction));
        code[site_offset + sizeof(instruction)] = 0xb8; // Continuation returns a visible sentinel.
        const uint32_t sentinel = 0x3456;
        memcpy(code + site_offset + sizeof(instruction) + 1, &sentinel, sizeof(sentinel));
        code[site_offset + sizeof(instruction) + 5] = 0xc3;

        read.site.address = fixture_profile.preferred_base + site_offset;
        read.site.signature.length = sizeof(instruction);
        memcpy(read.site.signature.storage.data(), instruction, sizeof(instruction));
    }

    // Capture the native VMOVSS/VEX result before replacing each instruction.
    alignas(32) std::array<uint8_t, 512> vectors{};
    for (size_t i = 0; i < vectors.size(); ++i)
        vectors[i] = static_cast<uint8_t>(i * 17 + i / 32);
    std::array<DMNoteProbeOutput, 5> native{};
    for (size_t i = 0; i < fixture_profile.dm_clock_reads.size(); ++i) {
        const auto function = reinterpret_cast<void*>(code + first_function + i * function_stride);
        gd_inline_probe(function, &native[i], vectors.data());
        check(native[i].regs[0] == 0x3456 && (native[i].flags & 0x8d5) == 0x45,
              "native VEX instruction reaches its continuation with deterministic arithmetic flags");
    }

    gd::config.note_smoothing = true;
    gd::config.diagnostics = false;
    gd::config.refresh = 120;
    const auto hooks = gd::note_inline_hooks();
    check(hooks.size() == fixture_profile.dm_clock_reads.size(), "high-FPS DM smoothing exposes all five clock reads");
    std::vector<gd::InlineBridge> bridges;
    bridges.reserve(hooks.size());
    for (const auto& hook : hooks) {
        auto bridge = gd::prepare_inline_hook(hook);
        check(bridge.replacement.size() == hook.site->signature.length, "DM inline bridge covers the full VEX read");
        check(gd::write_memory(gd::at(hook.site->address), bridge.replacement.data(), bridge.replacement.size()),
              "patch synthetic DM VEX clock read through the real inline bridge");
        bridges.push_back(std::move(bridge));
    }

    auto run = [&](size_t index, float raw_value) {
        *raw = raw_value;
        uint32_t raw_bits_before = 0, raw_bits_after = 0;
        memcpy(&raw_bits_before, raw, sizeof(raw_bits_before));
        DMNoteProbeOutput output{};
        gd_inline_probe(reinterpret_cast<void*>(code + first_function + index * function_stride), &output,
                        vectors.data());
        memcpy(&raw_bits_after, raw, sizeof(raw_bits_after));
        check(raw_bits_after == raw_bits_before, "DM visual hook leaves its shared raw global bitwise unchanged");
        check(output.regs == native[index].regs,
              "DM inline handler preserves every GPR while continuing at the native tail");
        check(output.flags == native[index].flags, "DM inline handler preserves all incoming flags");
        check(output.regs[0] == 0x3456, "DM inline bridge resumes after the replaced eight-byte instruction");
        const unsigned destination = fixture_profile.dm_clock_reads[index].destination;
        for (unsigned reg = 0; reg < 16; ++reg) {
            const auto* actual = output.ymm.data() + reg * 32;
            const auto* baseline = native[index].ymm.data() + reg * 32;
            if (reg == destination) {
                check(dm_probe_float(output, reg) == raw_value || gd::config.note_smoothing,
                      "DM raw passthrough returns the current shared clock value");
                check(std::all_of(actual + 4, actual + 32, [](uint8_t b) { return b == 0; }),
                      "DM VMOVSS replacement zeros the upper 28 bytes of its destination YMM register");
            } else
                check(memcmp(actual, baseline, 32) == 0, "DM inline handler preserves every other YMM register");
        }
        return dm_probe_float(output, destination);
    };

    gd::begin_timing_frame(0);
    // The first site anchors at raw; all five same-frame reads must see the
    // same source sample and avoid advancing the render clock more than once.
    for (size_t i = 0; i < hooks.size(); ++i) {
        const float visual = run(i, initial_raw);
        check(visual == initial_raw, "same-frame DM clock reads share one visual sample without re-advancing");
    }
    gd::end_scene_frame();

    gd::begin_timing_frame(1.0 / 120.0);
    const float next_raw = initial_raw + 10.0f;
    const float next_visual = run(0, next_raw);
    check(next_visual != next_raw && std::abs(next_visual - next_raw) <= gd::NoteClock::max_correction + 1e-4,
          "DM chart clock moves fractionally while staying within the phase bound");
    for (size_t i = 1; i < hooks.size(); ++i)
        check(run(i, next_raw) == next_visual, "all five DM sites reuse the same filtered value for this frame");
    check(run(0, next_raw + 1.0f) == next_raw + 1.0f,
          "same-frame DM source change reanchors immediately without another elapsed advance");
    gd::end_scene_frame();

    // A long held source reanchors instead of freewheeling; seeks also snap to
    // raw phase. These are exercised through the same patched native sites.
    bool held_reanchored = false;
    for (unsigned frame = 0; frame < 16; ++frame) {
        gd::begin_timing_frame(1.0 / 120.0);
        const float visual = run(0, 1600.0f);
        if (frame >= 13 && visual == 1600.0f)
            held_reanchored = true;
        gd::end_scene_frame();
    }
    check(held_reanchored, "DM held clock reanchors after its bounded grace period");
    gd::begin_timing_frame(1.0 / 120.0);
    check(run(0, 1590.0f) == 1590.0f, "DM backward seek immediately reanchors to raw chart time");
    gd::end_scene_frame();
    gd::begin_timing_frame(1.0 / 120.0);
    check(run(0, 1700.0f) == 1700.0f, "DM large forward seek immediately reanchors to raw chart time");
    gd::end_scene_frame();

    gd::end_scene_frame(); // Inactive calls must not retain a visual phase.
    check(run(0, 1800.0f) == 1800.0f, "inactive DM renderer uses raw chart time");
    gd::begin_timing_frame(1.0 / 120.0);
    check(run(0, 1810.0f) == 1810.0f, "DM clock reanchors on the first frame after inactivity");
    gd::end_scene_frame();

    gd::config.note_smoothing = false;
    gd::config.diagnostics = true;
    gd::config.refresh = 120;
    check(gd::note_inline_hooks().size() == 5, "Diagnostics retains DM sites for raw-time observation");
    gd::begin_timing_frame(1.0 / 120.0);
    for (size_t i = 0; i < hooks.size(); ++i)
        check(run(i, 1900.0f + static_cast<float>(i)) == 1900.0f + static_cast<float>(i),
              "NoteSmoothing=0 with Diagnostics keeps raw DM timing");
    gd::end_scene_frame();

    gd::config.note_smoothing = true;
    gd::config.refresh = 60;
    check(gd::note_inline_hooks().empty(), "60 Hz does not install DM timing hooks even with Diagnostics enabled");
    gd::begin_timing_frame(1.0 / 60.0);
    for (size_t i = 0; i < hooks.size(); ++i)
        check(run(i, 2000.0f + static_cast<float>(i)) == 2000.0f + static_cast<float>(i),
              "60 Hz with Diagnostics preserves raw DM chart time");
    gd::end_scene_frame();
    puts("PASS: DM note VEX bridges preserve machine state, share one visual clock, and leave raw time intact");
}
