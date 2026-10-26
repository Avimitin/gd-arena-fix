#include "note_diagnostics.h"
#include <algorithm>

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
};
static Stats stats[2];
static float previous_time[2]{};
static bool have_time[2]{};
struct NoteContext { bool active = false; float until = 0, speed = 0; };
static thread_local NoteContext context;

static int64_t __fastcall batch(int player, float time, unsigned mode, int64_t unused, unsigned char skin) {
    if (unsigned(player) < 2) {
        auto& s = stats[player];
        ++s.batches;
        if (have_time[player]) {
            if (time == previous_time[player]) ++s.repeated;
            else ++s.moving;
        }
        have_time[player] = true;
        previous_time[player] = s.song_time = time;
    }
    return original_batch(player, time, mode, unused, skin);
}
static int64_t __fastcall position(int player, int lane, float until, float tail,
        float speed, int hit, unsigned mode, unsigned char skin) {
    const auto saved = context;
    context = {true, until, speed};
    auto result = original_position(player, lane, until, tail, speed, hit, mode, skin);
    context = saved;
    return result;
}
static int64_t __fastcall draw(int player, float x, float y, float tail, int lane,
        unsigned char alpha, int hit, unsigned mode, unsigned char skin) {
    // Match the renderer's outer Y culling. Restrict to ordinary note heads.
    if (context.active && unsigned(player) < 2 && lane >= 0 && lane <= 8 &&
            y >= -100.0f && y <= 820.0f && (mode != 0 || y >= 300.0f)) {
        auto& s = stats[player];
        if (context.until >= 0) {
            ++s.upcoming;
            if (alpha == 255) ++s.opaque; else ++s.faded;
            s.minimum_alpha = std::min(s.minimum_alpha, unsigned(alpha));
            s.maximum_alpha = std::max(s.maximum_alpha, unsigned(alpha));
            s.speed_low = std::min(s.speed_low, context.speed);
            s.speed_high = std::max(s.speed_high, context.speed);
            const auto options = *static_cast<const uint8_t**>(at(0x1808C4A50));
            if (options) { s.fade1 = options[4188 + player]; s.fade2 = options[4190 + player]; }
        } else ++s.past;
    }
    // Observation only: pass every argument, including alpha, through unchanged.
    return original_draw(player, x, y, tail, lane, alpha, hit, mode, skin);
}
std::vector<DiagnosticHook> note_diagnostic_hooks() {
    return {
        {0x1803C06E0, {0x4c,0x8b,0xdc,0x55,0x57,0x41,0x57,0x48,0x81,0xec,0xa0,0,0,0},
            reinterpret_cast<void*>(batch), reinterpret_cast<void**>(&original_batch)},
        {0x1803C0350, {0x48,0x8b,0xc4,0x48,0x89,0x68,0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57},
            reinterpret_cast<void*>(position), reinterpret_cast<void**>(&original_position)},
        {0x1803BFCB0, {0x48,0x8b,0xc4,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xf0,0,0,0},
            reinterpret_cast<void*>(draw), reinterpret_cast<void**>(&original_draw)}
    };
}
void report_note_diagnostics(double elapsed) {
    for (unsigned player = 0; player < 2; ++player) {
        const auto& s = stats[player];
        log("NOTE P%u: batches %.2f/s, changed/repeated song positions %u/%u, song %.3f; upcoming %u opaque %u faded %u alpha %u..%u; past %u; speed scalar %.3f..%.3f; fade flags %u/%u",
            player + 1, s.batches / elapsed, s.moving, s.repeated, s.song_time,
            s.upcoming, s.opaque, s.faded, s.upcoming ? s.minimum_alpha : 0, s.maximum_alpha,
            s.past, s.upcoming ? s.speed_low : 0, s.speed_high, s.fade1, s.fade2);
        stats[player] = {};
    }
}
}
