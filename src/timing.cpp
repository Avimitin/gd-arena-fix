#include "mod.h"
#include "timing_math.h"
#include "note_diagnostics.h"
#include "selection_diagnostics.h"
#include "live2d.h"
#include "animations.h"
#include "timers.h"
#include "scene_timing.h"
#include "../third_party/minhook/include/MinHook.h"
#include <intrin.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace gd {
using Present = int64_t(__fastcall*)();
using Frame = int64_t(__fastcall*)(int64_t, int64_t, void*);
using AudioPosition = int64_t(__fastcall*)(unsigned, int);
using SongFilter = int64_t(__fastcall*)(unsigned char);
using Afp = int64_t(__fastcall*)(float, int, unsigned);
static Present original_present;
static Frame original_frame;
static AudioPosition original_audio;
static SongFilter original_filter;
static Afp original_afp;
static thread_local FractionalTicks audio_fraction, filter_fraction;
static thread_local uint64_t audio_fallback_frame = 0, filter_fallback_frame = 0;
struct PlaybackSample {
    uint64_t frame = 0;
    uint32_t integer = 0;
    double ticks = 0;
    bool valid = false;
};
static thread_local PlaybackSample samples[2];
struct Capture {
    bool called = false;
    int32_t position = 0;
};
static thread_local Capture* pending_capture;

static bool valid_note_projection_callers(const target::Profile& profile) {
    for (const auto return_va : profile.note_position_returns) {
        if (return_va < 5)
            return false;
        const auto instruction = static_cast<const uint8_t*>(at(return_va)) - 5;
        if (instruction[0] != 0xe8)
            return false;
        int32_t relative = 0;
        memcpy(&relative, instruction + 1, sizeof(relative));
        if (instruction + 5 + relative != at(profile.note_position.address))
            return false;
    }
    return true;
}
static bool valid_dm_clock_reads(const target::Profile& profile) {
    for (const auto& read : profile.dm_clock_reads) {
        if (read.site.signature.length != 8)
            return false;
        const auto instruction = static_cast<const uint8_t*>(at(read.site.address));
        int32_t relative = 0;
        memcpy(&relative, instruction + 4, sizeof(relative));
        if (reinterpret_cast<intptr_t>(instruction + 8) + relative !=
            reinterpret_cast<intptr_t>(at(profile.dm_clock_raw)))
            return false;
    }
    return true;
}

void begin_timing_frame(double elapsed) {
    begin_scene_frame(scene_frame().serial + 1, elapsed);
}
static int64_t __fastcall capture_cursor(int channel) {
    // Capture the main thread's existing poll; do not hook the sound worker or
    // poll the backend a second time. Keep the original sampler's side effects.
    const auto result = reinterpret_cast<int64_t(__fastcall*)(int)>(at(target::get().raw_cursor))(channel);
    if (pending_capture)
        *pending_capture = {true, static_cast<int32_t>(result)};
    return result;
}

static int64_t __fastcall present() {
    const double begin = seconds();
    const bool output_ready = compose();
    if (config.diagnostics)
        capture_diagnostic_frame();
    const double composed = seconds();
    auto result = original_present();
    const double done = seconds();
    static double last = 0, report_start = 0, total_compose = 0, total_present = 0, max_gap = 0;
    static unsigned frames = 0, ready_frames = 0;
    if (!report_start)
        report_start = done;
    if (last)
        max_gap = std::max(max_gap, done - last);
    last = done;
    ++frames;
    ready_frames += output_ready;
    total_compose += composed - begin;
    total_present += done - composed;
    if (done - report_start >= 5.0) {
        log("Present callbacks %.2f Hz; output ready %u/%u; max gap %.2f ms; frame prep avg %.3f ms; Present avg %.3f "
            "ms",
            frames / (done - report_start), ready_frames, frames, max_gap * 1000, total_compose * 1000 / frames,
            total_present * 1000 / frames);
        if (config.diagnostics)
            report_note_diagnostics(done - report_start);
        report_animations();
        report_start = done;
        frames = ready_frames = 0;
        total_compose = total_present = max_gap = 0;
    }
    return result;
}
static int64_t __fastcall frame(int64_t window, int64_t state, void* arg3) {
    static thread_local double last = 0;
    const double now = seconds();
    begin_timing_frame(last ? now - last : 0);
    track_scene_controller(reinterpret_cast<void*>(state));
    last = now;
    begin_animation_frame();
    const auto result = original_frame(window, state, arg3);
    end_animation_frame();
    end_scene_frame();
    return result;
}
__declspec(noinline) static int64_t __fastcall afp(float dt, int type, unsigned arg) {
    if (_ReturnAddress() == at(target::get().afp_return))
        dt = afp_step(scene_frame().elapsed);
    return original_afp(dt, type, arg);
}
static int64_t __fastcall audio(unsigned channel, int index) {
    // The loading/command thread also calls this sampler. It has no render
    // clock and must retain native behavior without sharing capture state.
    if (!scene_frame().serial)
        return original_audio(channel, index);
    auto fallback = static_cast<uint32_t*>(at(target::get().audio_alternate));
    const uint32_t previous = *fallback;
    Capture captured;
    auto previous_capture = pending_capture;
    pending_capture = &captured;
    auto result = original_audio(channel, index);
    pending_capture = previous_capture;
    if (index >= 0 && index < 2) {
        auto& sample = samples[index];
        sample = {};
        if (captured.called && captured.position > 0) {
            auto format =
                static_cast<uint8_t*>(at(target::get().audio_format)) + target::layout::audio_format_stride * index;
            const int64_t product = int64_t(*reinterpret_cast<int16_t*>(format + target::layout::audio_bits)) *
                                    *reinterpret_cast<int32_t*>(format + target::layout::audio_rate);
            if (product >= 4 && product <= INT32_MAX) {
                const double ticks = (double(captured.position) / (product / 4)) * 300.0;
                // The native divide-then-multiply can round a half tick a few
                // ulps either side of the boundary; validate its quantization
                // interval instead of requiring an identical recomputed int.
                if (ticks < INT32_MAX - 1.0 && std::abs(double(uint32_t(result)) - ticks) <= 0.5000001)
                    sample = {scene_frame().serial, uint32_t(result), ticks, true};
            }
        }
    }
    // Only the failure fallback writes this global inside the original function.
    if (channel == 2 && *fallback == previous + 5 && uint32_t(result) == *fallback) {
        const auto ticks =
            audio_fallback_frame == scene_frame().serial ? 0 : audio_fraction.advance(scene_frame().elapsed);
        audio_fallback_frame = scene_frame().serial;
        *fallback = previous + ticks;
        return *fallback;
    }
    if (channel == 2) {
        audio_fraction.reset();
        audio_fallback_frame = 0;
    }
    return result;
}
static int64_t __fastcall song_filter(unsigned char alternate) {
    if (!scene_frame().serial)
        return original_filter(alternate);
    auto source = static_cast<uint32_t*>(at(alternate ? target::get().audio_alternate : target::get().audio_main));
    const auto previous = *static_cast<uint32_t*>(at(target::get().song_previous));
    auto scene = *static_cast<uint32_t**>(at(target::get().scene_pointer));
    // Match only the original source==0 / previous>0 / +5 fallback path.
    // Initialization and alternate-mode seek handling still run untouched.
    if (!alternate && *source == 0 && previous > 0 && scene && scene[target::layout::scene_seek_index] == 0) {
        const auto ticks =
            filter_fallback_frame == scene_frame().serial ? 0 : filter_fraction.advance(scene_frame().elapsed);
        filter_fallback_frame = scene_frame().serial;
        *source = previous + ticks;
        auto result = original_filter(alternate);
        const auto raw = *static_cast<int64_t*>(at(target::get().song_raw));
        *static_cast<float*>(at(target::get().song_output)) = raw < 3 ? 0.0f : float(raw + filter_fraction.remainder);
        *source = 0;
        return result;
    }
    filter_fraction.reset();
    filter_fallback_frame = 0;
    const auto result = original_filter(alternate);
    const auto raw = *static_cast<int64_t*>(at(target::get().song_raw));
    const auto& sample = samples[alternate ? 1 : 0];
    // Retain native initialization/seek metadata. Replace only the recursive
    // filter output: musical time comes from playback, never target FPS.
    double ticks = double(raw);
    if (sample.valid && sample.frame == scene_frame().serial && sample.integer == *source && raw == *source)
        ticks = sample.ticks;
    *static_cast<float*>(at(target::get().song_output)) = raw < 3 ? 0.0f : static_cast<float>(ticks);
    return result;
}

struct Patch {
    void* address;
    std::vector<uint8_t> before, after;
};
static void require_bytes(uintptr_t va, const std::vector<uint8_t>& bytes) {
    if (memcmp(at(va), bytes.data(), bytes.size()) != 0)
        throw std::runtime_error("expected game instructions do not match; conflicting patch or different build");
}
static void* allocate_near(void* target) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const uintptr_t center = reinterpret_cast<uintptr_t>(target);
    const uintptr_t low = center > 0x70000000 ? center - 0x70000000 : info.dwAllocationGranularity;
    const uintptr_t high = center + 0x70000000;
    for (uintptr_t cursor = low; cursor < high;) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)))
            break;
        uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (region.State == MEM_FREE) {
            const uintptr_t aligned =
                (cursor + info.dwAllocationGranularity - 1) & ~(uintptr_t(info.dwAllocationGranularity) - 1);
            if (aligned + 4096 <= next && aligned < high) {
                if (void* p =
                        VirtualAlloc(reinterpret_cast<void*>(aligned), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
                    return p;
            }
        }
        if (next <= cursor)
            break;
        cursor = next;
    }
    throw std::runtime_error("cannot allocate audio capture bridge within call range");
}
void install_timing_and_present() {
    const auto& profile = target::get();
    const auto cursor_call = profile.cursor_call.signature.copy();
    require_bytes(profile.present.address, profile.present.signature.copy());
    const bool gf_callers_valid =
        !config.note_smoothing || config.refresh <= 60 || valid_note_projection_callers(profile);
    set_gf_projection_callers_valid(gf_callers_valid);
    if (config.note_smoothing && config.refresh > 60 && !gf_callers_valid) {
        log("GuitarFreaks note projection disabled: caller validation failed; DrumMania remains enabled");
    }
    const auto diagnostics = [] {
        if (!config.diagnostics && !(config.note_smoothing && gf_projection_callers_valid() && config.refresh > 60))
            return std::vector<DiagnosticHook>{};
        auto hooks = note_diagnostic_hooks();
        if (config.diagnostics) {
            const auto selection = selection_diagnostic_hooks();
            hooks.insert(hooks.end(), selection.begin(), selection.end());
        }
        return hooks;
    }();
    for (const auto& hook : diagnostics)
        require_bytes(hook.address, hook.signature);
    const auto optional_render_patches =
        (config.disable_live2d || config.disable_side_compositing) ? live2d_patches() : std::vector<Live2DPatch>{};
    for (const auto& patch : optional_render_patches)
        require_bytes(patch.address, patch.before);
    void** afp_slot = nullptr;
    const bool timing = config.refresh > 60;
    const auto timers = timing ? timer_hooks() : std::vector<DiagnosticHook>{};
    for (const auto& hook : timers)
        require_bytes(hook.address, hook.signature);
    const auto scenes = timing ? scene_timing_hooks() : std::vector<DiagnosticHook>{};
    auto inline_hooks = timing ? scene_inline_hooks() : std::vector<InlineHook>{};
    if (timing && (config.note_smoothing || config.diagnostics)) {
        const auto note_hooks = note_inline_hooks();
        inline_hooks.insert(inline_hooks.end(), note_hooks.begin(), note_hooks.end());
    }
    if (timing && config.animation_timing) {
        const auto sprite_hooks = animation_inline_hooks();
        inline_hooks.insert(inline_hooks.end(), sprite_hooks.begin(), sprite_hooks.end());
    }
    for (const auto& hook : inline_hooks)
        require_bytes(hook.site->address, hook.site->signature.copy());
    if (timing && (config.note_smoothing || config.diagnostics) && !valid_dm_clock_reads(profile))
        throw std::runtime_error("DM note clock reader does not reference the profiled raw source");
    for (const auto& hook : scenes)
        require_bytes(hook.address, hook.signature);
    const auto scene_patches = timing ? scene_timing_patches() : std::vector<const target::Patch*>{};
    for (const auto* patch : scene_patches)
        require_bytes(patch->address, patch->before.copy());
    const auto animations = timing && config.animation_timing ? animation_hooks() : std::vector<DiagnosticHook>{};
    for (const auto& hook : animations)
        require_bytes(hook.address, hook.signature);
    if (timing) {
        for (const auto* site : {&profile.frame, &profile.audio, &profile.song_filter, &profile.cursor_call})
            require_bytes(site->address, site->signature.copy());
        for (const auto* patch : {&profile.frame_wait, &profile.play_decrement})
            require_bytes(patch->address, patch->before.copy());
        afp_slot = import_ordinal(game, "afp-core.dll", target::layout::afp_ordinal); // XCd229cc00000e
        if (!afp_slot || afp_slot != at(target::get().afp_import))
            throw std::runtime_error("AFP ordinal import not found at expected slot");
        original_afp = reinterpret_cast<Afp>(*afp_slot);
    }
    // Validate the complete transaction, including function detours. Two
    // individually valid signatures must never authorize overlapping writes.
    std::vector<PatchRange> ranges{{profile.present.address, profile.present.signature.length}};
    for (const auto* group : {&diagnostics, &timers, &scenes, &animations})
        for (const auto& hook : *group)
            ranges.push_back({hook.address, hook.signature.size()});
    for (const auto& hook : inline_hooks)
        ranges.push_back({hook.site->address, hook.site->signature.length});
    for (const auto& patch : optional_render_patches)
        ranges.push_back({patch.address, patch.before.size()});
    for (const auto* patch : scene_patches)
        ranges.push_back({patch->address, patch->before.length});
    if (timing) {
        for (const auto* site : {&profile.frame, &profile.audio, &profile.song_filter, &profile.cursor_call})
            ranges.push_back({site->address, site->signature.length});
        for (const auto* patch : {&profile.frame_wait, &profile.play_decrement})
            ranges.push_back({patch->address, patch->before.length});
        ranges.push_back({profile.afp_import, sizeof(void*)});
    }
    require_disjoint_patches(std::move(ranges));
    std::vector<Patch> patches;
    for (const auto& patch : optional_render_patches)
        patches.push_back({at(patch.address), patch.before, patch.after});
    for (const auto* patch : scene_patches)
        patches.push_back({at(patch->address), patch->before.copy(), patch->after.copy()});
    std::vector<InlineBridge> inline_bridges;
    inline_bridges.reserve(inline_hooks.size());
    for (const auto& hook : inline_hooks) {
        auto bridge = prepare_inline_hook(hook);
        patches.push_back({at(hook.site->address), hook.site->signature.copy(), bridge.replacement});
        inline_bridges.push_back(std::move(bridge));
    }
    void* cursor_bridge = nullptr;
    if (timing) {
        for (const auto* patch : {&profile.frame_wait, &profile.play_decrement})
            patches.push_back({at(patch->address), patch->before.copy(), patch->after.copy()});
        auto hook = &afp;
        Patch import{afp_slot, std::vector<uint8_t>(sizeof(void*)), std::vector<uint8_t>(sizeof(void*))};
        memcpy(import.before.data(), afp_slot, sizeof(void*));
        memcpy(import.after.data(), &hook, sizeof(void*));
        patches.push_back(std::move(import));
    }
    if (MH_Initialize() != MH_OK)
        throw std::runtime_error("MinHook initialization failed");
    std::vector<void*> hooks;
    size_t applied = 0;
    auto add = [&](uintptr_t va, void* replacement, void** original) {
        void* target = at(va);
        if (MH_CreateHook(target, replacement, original) != MH_OK)
            throw std::runtime_error("MinHook cannot relocate a target function");
        hooks.push_back(target);
    };
    try {
        add(target::get().present.address, reinterpret_cast<void*>(present),
            reinterpret_cast<void**>(&original_present));
        for (const auto& hook : diagnostics)
            add(hook.address, hook.replacement, hook.original);
        for (const auto& hook : animations)
            add(hook.address, hook.replacement, hook.original);
        for (const auto& hook : timers)
            add(hook.address, hook.replacement, hook.original);
        for (const auto& hook : scenes)
            add(hook.address, hook.replacement, hook.original);
        if (timing) {
            add(target::get().frame.address, reinterpret_cast<void*>(frame), reinterpret_cast<void**>(&original_frame));
            add(target::get().audio.address, reinterpret_cast<void*>(audio), reinterpret_cast<void**>(&original_audio));
            add(target::get().song_filter.address, reinterpret_cast<void*>(song_filter),
                reinterpret_cast<void**>(&original_filter));
            cursor_bridge = allocate_near(at(target::get().cursor_call.address));
            uint8_t jump[14]{0xff, 0x25, 0, 0, 0, 0};
            auto capture = &capture_cursor;
            memcpy(jump + 6, &capture, sizeof(capture));
            memcpy(cursor_bridge, jump, sizeof(jump));
            DWORD old;
            if (!VirtualProtect(cursor_bridge, 4096, PAGE_EXECUTE_READ, &old))
                throw std::runtime_error("cannot protect audio bridge");
            FlushInstructionCache(GetCurrentProcess(), cursor_bridge, sizeof(jump));
            const auto displacement = reinterpret_cast<intptr_t>(cursor_bridge) -
                                      (reinterpret_cast<intptr_t>(at(target::get().cursor_call.address)) + 5);
            if (displacement < INT32_MIN || displacement > INT32_MAX)
                throw std::runtime_error("audio bridge out of call range");
            auto after = cursor_call;
            int32_t rel = static_cast<int32_t>(displacement);
            memcpy(after.data() + 1, &rel, sizeof(rel));
            patches.push_back({at(target::get().cursor_call.address), cursor_call, after});
        }
        for (auto& patch : patches) {
            ++applied;
            if (!write_memory(patch.address, patch.after.data(), patch.after.size()))
                throw std::runtime_error("memory patch failed");
        }
        for (void* hook : hooks)
            if (MH_QueueEnableHook(hook) != MH_OK)
                throw std::runtime_error("hook queue failed");
        if (MH_ApplyQueued() != MH_OK)
            throw std::runtime_error("hook activation failed");
    } catch (...) {
        for (void* hook : hooks) {
            MH_DisableHook(hook);
            MH_RemoveHook(hook);
        }
        while (applied) {
            auto& patch = patches[--applied];
            write_memory(patch.address, patch.before.data(), patch.before.size());
        }
        if (cursor_bridge)
            VirtualFree(cursor_bridge, 0, MEM_RELEASE);
        MH_Uninitialize();
        throw;
    }
    for (auto& bridge : inline_bridges)
        (void)bridge.memory.release(); // Pinned for process lifetime.
    log("Timing mode: %s",
        timing ? "elapsed-time menu/play timers, audio-anchored song clock, measured AFP delta; no fixed-FPS filter"
               : "original 60 Hz logic retained");
    if (timing)
        log("Song selection: readiness follows AFP label interval [%s, %s); native scroll motion retained.",
            profile.selection_idle_label, profile.selection_move_label);
    log("AnimationTiming=%u: %s", config.animation_timing,
        !timing || !config.animation_timing ? "animation hooks disabled"
        : config.animation_timing == 2      ? "60 Hz native/effect clocks with render interpolation (experimental)"
                                            : "60 Hz native/effect clocks (experimental)");
    log("NoteSmoothing=%d: %s", config.note_smoothing,
        config.note_smoothing && timing && gf_projection_callers_valid()
            ? "GuitarFreaks chart-note projection and DrumMania render-pass clocks; elapsed phase smoothing, +/-25 ms "
              "correction bound; raw song, judgment and audio clocks unchanged"
        : config.note_smoothing && timing && !gf_projection_callers_valid()
            ? "DrumMania render-pass smoothing active; GuitarFreaks caller validation failed; raw song, judgment "
              "and audio clocks unchanged"
        : config.note_smoothing ? "enabled preference is inactive at 60 Hz; raw song, judgment and audio clocks "
                                  "unchanged"
                                : "disabled; original note projection retained");
    log("DisableLive2D=%d: %s", config.disable_live2d,
        config.disable_live2d
            ? "left/right cabinet character creation disabled (3 guarded sites); saved selections retained"
        : config.disable_side_compositing
            ? "optional left/right characters disabled through DisableSideCompositing; saved selections retained"
            : "original character behavior retained");
    log("DisableSideCompositing=%d: %s", config.disable_side_compositing,
        config.disable_side_compositing ? "side video/effect compositing skipped (3 guarded sites: up to 4 copies and "
                                          "2 draws); main and touch retained; "
                                          "other side canvas work remains"
                                        : "original side compositing retained");
    if (config.diagnostics)
        log("Note diagnostics enabled (observation only). Ctrl+Shift+F8 saves main canvas/output BMPs beside the DLL; "
            "capture may briefly stall rendering.");
    if (config.diagnostics)
        log("Selection diagnostics enabled (observation only; scroll requests and shared list state, at most once per "
            "second).");
}
} // namespace gd
