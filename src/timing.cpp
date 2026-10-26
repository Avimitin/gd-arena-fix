#include "mod.h"
#include "timing_math.h"
#include "note_diagnostics.h"
#include "live2d.h"
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
static double frame_dt = 1.0 / 120.0;
static FractionalTicks audio_fraction, filter_fraction;

static int64_t __fastcall present() {
    const double begin = seconds();
    const bool copied = compose();
    if (config.diagnostics) capture_diagnostic_frame();
    const double composed = seconds();
    auto result = original_present();
    const double done = seconds();
    static double last = 0, report_start = 0, total_compose = 0, total_present = 0, max_gap = 0;
    static unsigned frames = 0, copies = 0;
    if (!report_start) report_start = done;
    if (last) max_gap = std::max(max_gap, done - last);
    last = done;
    ++frames; copies += copied;
    total_compose += composed - begin; total_present += done - composed;
    if (done - report_start >= 5.0) {
        log("Present callbacks %.2f Hz; copies %u/%u; max gap %.2f ms; copy avg %.3f ms; Present avg %.3f ms",
            frames / (done - report_start), copies, frames, max_gap * 1000,
            total_compose * 1000 / frames, total_present * 1000 / frames);
        if (config.diagnostics) report_note_diagnostics(done - report_start);
        report_start = done; frames = copies = 0; total_compose = total_present = max_gap = 0;
    }
    return result;
}
static int64_t __fastcall frame(int64_t window, int64_t state, void* arg3) {
    static double last = 0;
    const double now = seconds();
    frame_dt = last ? std::clamp(now - last, 0.001, 0.100) : 1.0 / config.fps;
    last = now;
    return original_frame(window, state, arg3);
}
__declspec(noinline) static int64_t __fastcall afp(float dt, int type, unsigned arg) {
    if (_ReturnAddress() == at(0x1801A3F3B)) dt = afp_step(frame_dt);
    return original_afp(dt, type, arg);
}
static int64_t __fastcall audio(unsigned channel, int index) {
    auto fallback = static_cast<uint32_t*>(at(0x195E36C24));
    const uint32_t previous = *fallback;
    auto result = original_audio(channel, index);
    // Only the failure fallback writes this global inside the original function.
    if (channel == 2 && *fallback == previous + 5 && uint32_t(result) == *fallback) {
        *fallback = previous + audio_fraction.advance(frame_dt);
        return *fallback;
    }
    if (channel == 2) audio_fraction.reset();
    return result;
}
static int64_t __fastcall song_filter(unsigned char alternate) {
    auto source = static_cast<uint32_t*>(at(alternate ? 0x195E36C24 : 0x195E36C20));
    const auto previous = *static_cast<uint32_t*>(at(0x1B008B42C));
    auto scene = *static_cast<uint32_t**>(at(0x1808C23B8));
    // Match only the original source==0 / previous>0 / +5 fallback path.
    // Initialization and alternate-mode seek handling still run untouched.
    if (!alternate && *source == 0 && previous > 0 && scene && scene[1863] == 0) {
        *source = previous + filter_fraction.advance(frame_dt);
        auto result = original_filter(alternate);
        *source = 0;
        return result;
    }
    filter_fraction.reset();
    return original_filter(alternate);
}

struct Patch { void* address; std::vector<uint8_t> before, after; };
static void require_bytes(uintptr_t va, const std::vector<uint8_t>& bytes) {
    if (memcmp(at(va), bytes.data(), bytes.size()) != 0) throw std::runtime_error("expected game instructions do not match; conflicting patch or different build");
}
static void* allocate_near(void* target) {
    SYSTEM_INFO info; GetSystemInfo(&info);
    const uintptr_t center = reinterpret_cast<uintptr_t>(target);
    const uintptr_t low = center > 0x70000000 ? center - 0x70000000 : info.dwAllocationGranularity;
    const uintptr_t high = center + 0x70000000;
    for (uintptr_t cursor = low; cursor < high;) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region))) break;
        uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (region.State == MEM_FREE) {
            const uintptr_t aligned = (cursor + info.dwAllocationGranularity - 1) & ~(uintptr_t(info.dwAllocationGranularity) - 1);
            if (aligned + 4096 <= next && aligned < high) {
                if (void* p = VirtualAlloc(reinterpret_cast<void*>(aligned), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return p;
            }
        }
        if (next <= cursor) break;
        cursor = next;
    }
    throw std::runtime_error("cannot allocate private song-filter constant within RIP-relative range");
}
void install_timing_and_present() {
    const std::vector<uint8_t> present_start{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30};
    const std::vector<uint8_t> frame_start{0x48,0x8b,0xc4,0x4c,0x89,0x40,0x18,0x48,0x89,0x48,0x08};
    const std::vector<uint8_t> audio_start{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
    const std::vector<uint8_t> filter_start{0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x20};
    const std::vector<uint8_t> wait_bytes{0xff,0x15,0x89,0x15,0x3e,0x00,0x48,0x2b,0x05,0xba,0x33,0xbf,0x14,
        0x48,0x83,0xf8,0x0e,0x73,0x10,0xb9,0x0e,0x00,0x00,0x00,0x48,0x2b,0xc8,0x74,0x06,0xff,0x15,0xc4,0x14,0x3e,0x00};
    const std::vector<uint8_t> div1{0xc5,0xf2,0x5e,0x15,0x24,0x19,0x3e,0x00};
    const std::vector<uint8_t> div2{0xc5,0xf2,0x5e,0x15,0x51,0x18,0x3e,0x00};
    require_bytes(0x180420360, present_start);
    const auto diagnostics = config.diagnostics ? note_diagnostic_hooks() : std::vector<DiagnosticHook>{};
    for (const auto& hook : diagnostics) require_bytes(hook.address, hook.signature);
    const auto characters = config.disable_live2d ? live2d_patches() : std::vector<Live2DPatch>{};
    for (const auto& patch : characters) require_bytes(patch.address, patch.before);
    void** afp_slot = nullptr;
    const bool timing = config.fps == 120;
    if (timing) {
        require_bytes(0x1801A3910, frame_start); require_bytes(0x1801B60F0, audio_start);
        require_bytes(0x180286EC0, filter_start); require_bytes(0x1801A39F1, wait_bytes);
        require_bytes(0x180287060, div1); require_bytes(0x180287133, div2);
        afp_slot = import_ordinal(game, "afp-core.dll", 15); // XCd229cc00000e
        if (!afp_slot || afp_slot != at(0x180584AF8)) throw std::runtime_error("AFP ordinal import not found at expected slot");
        original_afp = reinterpret_cast<Afp>(*afp_slot);
    }
    std::vector<Patch> patches;
    for (const auto& patch : characters) patches.push_back({at(patch.address), patch.before, patch.after});
    void* rate_storage = nullptr;
    if (timing) {
        rate_storage = allocate_near(at(0x180287060));
        *static_cast<float*>(rate_storage) = float(config.fps);
        patches.push_back({at(0x1801A39F1), wait_bytes, std::vector<uint8_t>(wait_bytes.size(), 0x90)});
        for (auto pair : {std::pair<uintptr_t,std::vector<uint8_t>>(0x180287060, div1), {0x180287133, div2}}) {
            auto after = pair.second;
            const auto displacement = reinterpret_cast<intptr_t>(rate_storage) - (reinterpret_cast<intptr_t>(at(pair.first)) + 8);
            if (displacement < INT32_MIN || displacement > INT32_MAX) throw std::runtime_error("song-filter constant out of range");
            int32_t rel = static_cast<int32_t>(displacement);
            memcpy(after.data() + 4, &rel, sizeof(rel));
            patches.push_back({at(pair.first), pair.second, after});
        }
        auto hook = &afp;
        Patch import{afp_slot, std::vector<uint8_t>(sizeof(void*)), std::vector<uint8_t>(sizeof(void*))};
        memcpy(import.before.data(), afp_slot, sizeof(void*));
        memcpy(import.after.data(), &hook, sizeof(void*));
        patches.push_back(std::move(import));
    }
    if (MH_Initialize() != MH_OK) throw std::runtime_error("MinHook initialization failed");
    std::vector<void*> hooks;
    size_t applied = 0;
    auto add = [&](uintptr_t va, void* replacement, void** original) {
        void* target = at(va);
        if (MH_CreateHook(target, replacement, original) != MH_OK) throw std::runtime_error("MinHook cannot relocate a target function");
        hooks.push_back(target);
    };
    try {
        add(0x180420360, reinterpret_cast<void*>(present), reinterpret_cast<void**>(&original_present));
        for (const auto& hook : diagnostics) add(hook.address, hook.replacement, hook.original);
        if (timing) {
            add(0x1801A3910, reinterpret_cast<void*>(frame), reinterpret_cast<void**>(&original_frame));
            add(0x1801B60F0, reinterpret_cast<void*>(audio), reinterpret_cast<void**>(&original_audio));
            add(0x180286EC0, reinterpret_cast<void*>(song_filter), reinterpret_cast<void**>(&original_filter));
        }
        for (auto& patch : patches) {
            ++applied;
            if (!write_memory(patch.address, patch.after.data(), patch.after.size())) throw std::runtime_error("memory patch failed");
        }
        for (void* hook : hooks) if (MH_QueueEnableHook(hook) != MH_OK) throw std::runtime_error("hook queue failed");
        if (MH_ApplyQueued() != MH_OK) throw std::runtime_error("hook activation failed");
    } catch (...) {
        for (void* hook : hooks) { MH_DisableHook(hook); MH_RemoveHook(hook); }
        while (applied) { auto& patch = patches[--applied]; write_memory(patch.address, patch.before.data(), patch.before.size()); }
        if (rate_storage) VirtualFree(rate_storage, 0, MEM_RELEASE);
        MH_Uninitialize();
        throw;
    }
    log("Timing mode: %s", timing ? "EXPERIMENTAL 120: limiter removed, AFP delta measured, private filter divisor 120, fractional fallback ticks" : "original 60 Hz logic retained");
    log("DisableLive2D=%d: %s", config.disable_live2d,
        config.disable_live2d ? "left/right cabinet character creation disabled (3 guarded sites); saved selections retained"
                             : "original character behavior retained");
    if (config.diagnostics) log("Note diagnostics enabled (observation only). Ctrl+Shift+F8 saves main 4K/1080p BMPs beside the DLL; capture may briefly stall rendering.");
}
}
