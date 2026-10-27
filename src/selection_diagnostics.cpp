#include "selection_diagnostics.h"

namespace gd {
namespace {
namespace layout = target::layout::selection;
using Request = int64_t(__fastcall*)(int, int);
using Update = int64_t(__fastcall*)(int64_t, uint64_t, uint64_t);
Request original_request;
Update original_update;
template <class T> T field(const void* object, size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(object) + offset);
}
struct Snapshot {
    void* owner = nullptr;
    void* ui = nullptr;
    int stage = 0, enabled = 0, input = 0, lock1 = 0, lock2 = 0;
    unsigned skip1 = 0, skip2 = 0, valid = 0, ready = 0, continuation = 0;
    int direction = 0, phase = 0, velocity = 0, idle = 0;
};
Snapshot snapshot() {
    Snapshot s;
    s.owner = *static_cast<void**>(at(target::get().selection_owner));
    if (!s.owner)
        return s;
    s.stage = field<int>(s.owner, layout::stage);
    s.enabled = field<int>(s.owner, layout::enabled);
    s.input = field<int>(s.owner, layout::input_mode);
    s.lock1 = field<int>(s.owner, layout::lock1);
    s.lock2 = field<int>(s.owner, layout::lock2);
    s.skip1 = field<uint8_t>(s.owner, layout::skip1);
    s.skip2 = field<uint8_t>(s.owner, layout::skip2);
    s.ui = field<void*>(s.owner, layout::main_ui);
    if (!s.ui)
        return s;
    s.valid = field<uint8_t>(s.ui, layout::valid);
    s.ready = field<uint8_t>(s.ui, layout::ready);
    s.continuation = field<uint8_t>(s.ui, layout::continuation);
    s.direction = field<int>(s.ui, layout::direction);
    s.phase = field<int>(s.ui, layout::phase);
    s.velocity = field<int>(s.ui, layout::velocity);
    s.idle = field<int>(s.ui, layout::idle);
    return s;
}
struct Stats {
    unsigned requests = 0, accepted = 0, rejected[7]{}, updates = 0, progress = 0, completed = 0;
    int last_direction = 0;
};
thread_local Stats stats;
thread_local double last_report = 0, last_request = -100;

void report(const Snapshot& s) {
    // This clock only rate-limits logging. It never changes game time or state.
    const double now = seconds();
    if (now - last_report < 1 || now - last_request > 10)
        return;
    int frame = -1;
    if (s.ui && s.valid) {
        auto clip = static_cast<uint8_t*>(s.ui) + layout::clip;
        auto table = *reinterpret_cast<void***>(clip);
        using Valid = uint8_t(__fastcall*)(void*);
        using Get = uint8_t(__fastcall*)(void*, int*);
        if (table && reinterpret_cast<Valid>(table[layout::clip_valid_slot])(clip)) {
            int value = 0;
            if (reinterpret_cast<Get>(table[layout::clip_frame_slot])(clip, &value))
                frame = value;
        }
    }
    auto pending = static_cast<const int*>(at(target::get().selection_pending));
    log("SELECT dt=%.3f req=%u accepted=%u reject(lock1/null/lock2/invalid/phase/busy/other)=%u/%u/%u/%u/%u/%u/%u "
        "last=%d updates=%u progress=%u completed=%u; owner=%p ui=%p stage=%d enabled=%d input=%d locks=%d/%d "
        "skip=%u/%u valid=%u ready=%u dir=%d continue=%u phase=%d velocity=%d idle=%d afp=%d song=%d pending=%d/%d",
        now - last_report, stats.requests, stats.accepted, stats.rejected[0], stats.rejected[1], stats.rejected[2],
        stats.rejected[3], stats.rejected[4], stats.rejected[5], stats.rejected[6], stats.last_direction, stats.updates,
        stats.progress, stats.completed, s.owner, s.ui, s.stage, s.enabled, s.input, s.lock1, s.lock2, s.skip1, s.skip2,
        s.valid, s.ready, s.direction, s.continuation, s.phase, s.velocity, s.idle, frame,
        *static_cast<const int*>(at(target::get().selection_index)), pending[0], pending[1]);
    stats = {};
    last_report = now;
}
int64_t __fastcall request(int direction, int sound) {
    const auto before = snapshot();
    const auto result = original_request(direction, sound);
    if (direction) {
        ++stats.requests;
        stats.last_direction = direction;
        last_request = seconds();
        if (result)
            ++stats.accepted;
        else {
            const unsigned reason = before.lock1          ? 0
                                    : !before.ui          ? 1
                                    : before.lock2        ? 2
                                    : !before.valid       ? 3
                                    : before.phase >= 500 ? 4
                                    : before.direction    ? 5
                                                          : 6;
            ++stats.rejected[reason];
        }
        report(snapshot());
    }
    return result;
}
int64_t __fastcall update(int64_t a, uint64_t b, uint64_t c) {
    const auto before = snapshot();
    const auto result = original_update(a, b, c);
    const auto after = snapshot(); // Reacquire after native code; the owner can change.
    ++stats.updates;
    if (before.ui && before.ui == after.ui) {
        if (before.phase != after.phase)
            ++stats.progress;
        if (before.direction && !after.direction)
            ++stats.completed;
    }
    report(after);
    return result;
}
} // namespace
std::vector<DiagnosticHook> selection_diagnostic_hooks() {
    return {bind_hook(target::get().selection_request, reinterpret_cast<void*>(request),
                      reinterpret_cast<void**>(&original_request)),
            bind_hook(target::get().selection_update, reinterpret_cast<void*>(update),
                      reinterpret_cast<void**>(&original_update))};
}
} // namespace gd
