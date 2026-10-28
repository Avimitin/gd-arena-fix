#include "note_diagnostics.h"
#include "note_clock.h"
#include "scene_timing.h"
#include <algorithm>
#include <cstring>
#include <intrin.h>

namespace gd {
// These three game functions use standard Win64 float argument slots.
// IDA's inferred double/int types are incorrect; verified against MOVSS
// loads at the call sites and original function bodies.
using Batch = int64_t(__fastcall*)(int, float, unsigned, int64_t, unsigned char);
using Position = int64_t(__fastcall*)(int, int, float, float, float, int, unsigned, unsigned char);
using Draw = int64_t(__fastcall*)(int, float, float, float, int, unsigned char, int, unsigned, unsigned char);
static Batch original_batch;
static Position original_position;
static Draw original_draw;
struct Stats {
    unsigned batches = 0, moving = 0, repeated = 0;
    unsigned upcoming = 0, opaque = 0, faded = 0, past = 0;
    unsigned minimum_alpha = 255, maximum_alpha = 0;
    float speed_low = 1000000, speed_high = 0, song_time = 0;
    unsigned fade1 = 0, fade2 = 0;
    unsigned visual_moving = 0, visual_repeated = 0, projections = 0, shifted = 0;
    double correction_low = 0, correction_high = 0;
    bool have_correction = false;
};
static Stats stats[2];
static float previous_time[2]{};
static bool have_time[2]{};
static float previous_visual[2]{};
struct VisualClock {
    NoteClock clock;
    unsigned mode = 0;
    unsigned char skin = 0;
    bool identified = false;
};
static thread_local VisualClock clocks[2];
struct BatchContext {
    bool active = false;
    int player = 0;
    unsigned mode = 0;
    unsigned char skin = 0;
    float correction = 0;
};
static thread_local BatchContext batch_context;
struct NoteContext {
    bool active = false;
    float until = 0, speed = 0;
};
static thread_local NoteContext context;
static thread_local NoteClock dm_note_clock;
struct DMCadenceStats {
    uint64_t frame = 0;
    unsigned samples = 0, raw_changed = 0, raw_repeated = 0, visual_changed = 0, visual_repeated = 0;
    float previous_raw = 0, previous_visual = 0;
    double correction_low = 0, correction_high = 0;
    bool have_frame = false, have_previous = false;
    bool have_correction = false;
};
static thread_local DMCadenceStats dm_cadence;
static bool gf_callers_valid = true;
void set_gf_projection_callers_valid(bool valid) {
    gf_callers_valid = valid;
}
bool gf_projection_callers_valid() {
    return gf_callers_valid;
}

static void dm_note_clock_read(InlineContext& c) {
    const auto& profile = target::get();
    const auto site = std::find_if(profile.dm_clock_reads.begin(), profile.dm_clock_reads.end(), [&](const auto& read) {
        return c.ip == reinterpret_cast<uintptr_t>(at(read.site.address)) + read.site.signature.length;
    });
    if (site == profile.dm_clock_reads.end())
        return;

    const auto& frame = scene_frame();
    const float raw = *static_cast<const float*>(at(profile.dm_clock_raw));
    float visual = raw;
    const bool frame_known = frame.active && frame.serial != 0;
    const bool smoothing = config.note_smoothing && config.refresh > 60 && frame_known;
    if (smoothing && std::isfinite(raw) && raw >= 0)
        visual = static_cast<float>(dm_note_clock.advance(frame.serial, raw, frame.elapsed));
    else
        dm_note_clock.reset();

    if (config.diagnostics && frame_known && std::isfinite(raw) && std::isfinite(visual) &&
        (!dm_cadence.have_frame || dm_cadence.frame != frame.serial)) {
        if (dm_cadence.have_previous) {
            if (raw == dm_cadence.previous_raw)
                ++dm_cadence.raw_repeated;
            else
                ++dm_cadence.raw_changed;
            if (visual == dm_cadence.previous_visual)
                ++dm_cadence.visual_repeated;
            else
                ++dm_cadence.visual_changed;
        }
        ++dm_cadence.samples;
        const double correction = double(visual) - raw;
        if (!dm_cadence.have_correction) {
            dm_cadence.correction_low = dm_cadence.correction_high = correction;
            dm_cadence.have_correction = true;
        } else {
            dm_cadence.correction_low = std::min(dm_cadence.correction_low, correction);
            dm_cadence.correction_high = std::max(dm_cadence.correction_high, correction);
        }
        dm_cadence.frame = frame.serial;
        dm_cadence.previous_raw = raw;
        dm_cadence.previous_visual = visual;
        dm_cadence.have_frame = dm_cadence.have_previous = true;
    }

    auto* destination = c.ymm(site->destination);
    memcpy(destination, &visual, sizeof(visual));
    memset(destination + sizeof(visual), 0, 32 - sizeof(visual));
}

static int64_t __fastcall batch(int player, float time, unsigned mode, int64_t unused, unsigned char skin) {
    const auto saved_batch = batch_context;
    batch_context = {};
    if (unsigned(player) < 2) {
        float visual = time;
        auto& state = clocks[player];
        const auto& frame = scene_frame();
        if (config.note_smoothing && gf_callers_valid && config.refresh > 60 && frame.active && std::isfinite(time) &&
            time >= 0) {
            if (!state.identified || state.mode != mode || state.skin != skin)
                state.clock.reset();
            state.identified = true;
            state.mode = mode;
            state.skin = skin;
            const auto smoothed = state.clock.advance(frame.serial, time, frame.elapsed);
            const auto correction = static_cast<float>(smoothed - double(time));
            visual = time + correction;
            batch_context = {true, player, mode, skin, correction};
        } else {
            state.clock.reset();
            state.identified = false;
        }
        if (config.diagnostics) {
            auto& s = stats[player];
            ++s.batches;
            if (have_time[player]) {
                if (time == previous_time[player])
                    ++s.repeated;
                else
                    ++s.moving;
                if (visual == previous_visual[player])
                    ++s.visual_repeated;
                else
                    ++s.visual_moving;
            }
            have_time[player] = true;
            previous_time[player] = s.song_time = time;
            previous_visual[player] = visual;
            if (!s.have_correction) {
                s.correction_low = s.correction_high = batch_context.correction;
                s.have_correction = true;
            } else {
                s.correction_low = std::min(s.correction_low, double(batch_context.correction));
                s.correction_high = std::max(s.correction_high, double(batch_context.correction));
            }
        }
    }
    // Leave traversal, hit state and the shared song clock on the native time.
    const auto result = original_batch(player, time, mode, unused, skin);
    batch_context = saved_batch;
    return result;
}
__declspec(noinline) static int64_t __fastcall position(int player, int lane, float until, float tail, float speed,
                                                        int hit, unsigned mode, unsigned char skin) {
    if (batch_context.active && batch_context.player == player && batch_context.mode == mode &&
        batch_context.skin == skin && std::isfinite(until) && std::isfinite(tail)) {
        const auto caller = _ReturnAddress();
        const auto& calls = target::get().note_position_returns;
        if (std::any_of(calls.begin(), calls.end(), [&](uintptr_t va) { return caller == at(va); })) {
            // Both endpoints are relative chart times. Shift them equally before
            // native projection/fades; keep pinned/equal ends and durations intact.
            until -= batch_context.correction;
            tail -= batch_context.correction;
            if (config.diagnostics) {
                auto& s = stats[player];
                ++s.projections;
                s.shifted += batch_context.correction != 0;
            }
        }
    }
    if (!config.diagnostics)
        return original_position(player, lane, until, tail, speed, hit, mode, skin);
    const auto saved = context;
    context = {true, until, speed};
    auto result = original_position(player, lane, until, tail, speed, hit, mode, skin);
    context = saved;
    return result;
}
static int64_t __fastcall draw(int player, float x, float y, float tail, int lane, unsigned char alpha, int hit,
                               unsigned mode, unsigned char skin) {
    // Match the renderer's outer Y culling. Restrict to ordinary note heads.
    if (config.diagnostics && context.active && unsigned(player) < 2 && lane >= 0 && lane <= 8 && y >= -100.0f &&
        y <= 820.0f && (mode != 0 || y >= 300.0f)) {
        auto& s = stats[player];
        if (context.until >= 0) {
            ++s.upcoming;
            if (alpha == 255)
                ++s.opaque;
            else
                ++s.faded;
            s.minimum_alpha = std::min(s.minimum_alpha, unsigned(alpha));
            s.maximum_alpha = std::max(s.maximum_alpha, unsigned(alpha));
            s.speed_low = std::min(s.speed_low, context.speed);
            s.speed_high = std::max(s.speed_high, context.speed);
            const auto options = *static_cast<const uint8_t**>(at(target::get().note_options));
            if (options) {
                s.fade1 = options[target::layout::note_fade1 + player];
                s.fade2 = options[target::layout::note_fade2 + player];
            }
        } else
            ++s.past;
    }
    // Observation only: pass every argument, including alpha, through unchanged.
    return original_draw(player, x, y, tail, lane, alpha, hit, mode, skin);
}
std::vector<DiagnosticHook> note_diagnostic_hooks() {
    std::vector<DiagnosticHook> hooks;
    if ((config.note_smoothing && gf_callers_valid && config.refresh > 60) || config.diagnostics) {
        hooks.push_back(bind_hook(target::get().note_batch, reinterpret_cast<void*>(batch),
                                  reinterpret_cast<void**>(&original_batch)));
        hooks.push_back(bind_hook(target::get().note_position, reinterpret_cast<void*>(position),
                                  reinterpret_cast<void**>(&original_position)));
    }
    if (config.diagnostics)
        hooks.push_back(bind_hook(target::get().note_draw, reinterpret_cast<void*>(draw),
                                  reinterpret_cast<void**>(&original_draw)));
    return hooks;
}
std::vector<InlineHook> note_inline_hooks() {
    std::vector<InlineHook> hooks;
    if (config.refresh > 60 && (config.note_smoothing || config.diagnostics)) {
        for (const auto& read : target::get().dm_clock_reads)
            hooks.push_back({&read.site, dm_note_clock_read});
    }
    return hooks;
}
void report_note_diagnostics(double elapsed) {
    for (unsigned player = 0; player < 2; ++player) {
        const auto& s = stats[player];
        log("NOTE P%u: batches %.2f/s, changed/repeated song positions %u/%u, song %.3f; upcoming %u opaque %u faded "
            "%u alpha %u..%u; past %u; speed scalar %.3f..%.3f; fade flags %u/%u",
            player + 1, s.batches / elapsed, s.moving, s.repeated, s.song_time, s.upcoming, s.opaque, s.faded,
            s.upcoming ? s.minimum_alpha : 0, s.maximum_alpha, s.past, s.upcoming ? s.speed_low : 0, s.speed_high,
            s.fade1, s.fade2);
        if (config.note_smoothing && gf_callers_valid && config.refresh > 60)
            log("NOTE CLOCK P%u: raw changed/repeated %u/%u; visual changed/repeated %u/%u; correction %.3f..%.3f ms; "
                "chart projections %u, shifted %u (visual only)",
                player + 1, s.moving, s.repeated, s.visual_moving, s.visual_repeated, s.correction_low / 0.3,
                s.correction_high / 0.3, s.projections, s.shifted);
        stats[player] = {};
    }
    if (dm_cadence.samples)
        log("DM NOTE CLOCK: %u render frames (%.2f/s), raw changed/held %u/%u, visual changed/held %u/%u, "
            "correction %.3f..%.3f ms",
            dm_cadence.samples, dm_cadence.samples / elapsed, dm_cadence.raw_changed, dm_cadence.raw_repeated,
            dm_cadence.visual_changed, dm_cadence.visual_repeated, dm_cadence.correction_low / 0.3,
            dm_cadence.correction_high / 0.3);
    dm_cadence.samples = dm_cadence.raw_changed = dm_cadence.raw_repeated = 0;
    dm_cadence.visual_changed = dm_cadence.visual_repeated = 0;
    dm_cadence.correction_low = dm_cadence.correction_high = 0;
    dm_cadence.have_correction = false;
}
} // namespace gd
