#include "animations.h"
#include "animation_clock.h"
#include "scene_timing.h"
#include <array>
#include <cstring>
#include <unordered_map>

namespace gd {
namespace {
using ObjectCall = void(__fastcall*)(void*);
using SampleCall = int64_t(__fastcall*)(void*);
using SpriteCall = char(__fastcall*)(void*);
using GameplayCall = int64_t(__fastcall*)(unsigned char);
ObjectCall original_update, original_draw;
SampleCall original_sample;
SpriteCall original_sprite;
GameplayCall original_gameplay;

template <class T> T& field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
}
struct Root {
    AnimationClock clock;
    void* track = nullptr;
    int16_t expected = 0;
    bool initialized = false;
};
struct Sample {
    void* owner = nullptr;
    void* track = nullptr;
    void* asset = nullptr;
    uint64_t serial = 0;
    int16_t frame = 0, expected = 0;
    bool continuous = false;
    std::array<float, target::layout::pose_floats> pose{};
};
struct Sprite {
    AnimationClock clock;
    void* object = nullptr;
    int counter = 0;
    bool active = false;
};
struct FrameHistory {
    struct Range {
        int first = 0, last = 0;
        uint64_t stamp = 0;
    };
    uint64_t serial = 0, sequence = 0;
    int last = -1;
    void* track = nullptr;
    void* asset = nullptr;
    std::vector<Range> ranges;
    std::unordered_map<uintptr_t, uint64_t> consumed;
};
struct State {
    bool active = false;
    unsigned depth = 0;
    void* owner = nullptr;
    std::unordered_map<void*, Root> roots;
    std::unordered_map<void*, Sample> samples;
    std::unordered_map<void*, Sprite> sprites;
    std::unordered_map<void*, FrameHistory> history;
    AnimationClock gameplay;
    uint64_t updates = 0, holds = 0, interpolated = 0, sprite_updates = 0;
};
thread_local State state;

void record_frame(void* object, int before) {
    auto& h = state.history[object];
    const auto frame = field<int16_t>(object, target::layout::frame);
    const auto track = field<void*>(object, target::layout::track), asset = field<void*>(object, target::layout::asset);
    const bool replaced = h.track != track || h.asset != asset;
    if (h.serial != scene_frame().serial || replaced) {
        h.ranges.clear();
        h.serial = scene_frame().serial;
    }
    if (h.sequence && h.last == frame && before == frame && !replaced)
        return;
    h.track = track;
    h.asset = asset;
    h.last = frame;
    ++h.sequence;
    if (!h.ranges.empty() && h.ranges.back().last + 1 == frame)
        h.ranges.back().last = frame;
    else
        h.ranges.push_back({frame, frame, h.sequence});
}

int64_t __fastcall sample(void* object) {
    const int16_t frame = field<int16_t>(object, target::layout::frame);
    const auto result = original_sample(object);
    if (state.active && state.depth) {
        auto& s = state.samples[object];
        const auto track = field<void*>(object, target::layout::track);
        const auto asset = field<void*>(object, target::layout::asset);
        s.continuous = s.serial && s.owner == state.owner && s.track == track && s.asset == asset &&
                       int(frame) == int(s.frame) + 1;
        s.owner = state.owner;
        s.track = track;
        s.asset = asset;
        s.serial = scene_frame().serial;
        s.frame = frame;
        memcpy(s.pose.data(), object, sizeof(s.pose));
    }
    return result;
}
void update_once(void* object) {
    const int before = field<int16_t>(object, target::layout::frame);
    original_update(object);
    record_frame(object, before);
    const auto found = state.samples.find(object);
    if (found != state.samples.end() && found->second.serial == scene_frame().serial)
        found->second.expected = field<int16_t>(object, target::layout::frame);
}
void __fastcall update(void* object) {
    if (!object || !state.active) {
        original_update(object);
        return;
    }
    // Children must follow the parent's tick, not accumulate another clock.
    if (state.depth) {
        update_once(object);
        return;
    }
    auto& root = state.roots[object];
    const auto track = field<void*>(object, target::layout::track);
    const unsigned ticks = root.clock.advance(scene_frame().serial, scene_frame().elapsed,
                                              !root.initialized || root.track != track ||
                                                  field<int16_t>(object, target::layout::frame) != root.expected ||
                                                  field<uint16_t>(object, target::layout::active) == 0);
    root.initialized = true;
    root.track = track;
    if (!ticks) {
        ++state.holds;
        return;
    }
    state.owner = object;
    ++state.depth;
    for (unsigned i = 0; i < ticks; ++i) {
        update_once(object);
        ++state.updates;
        // The native engine can free/zero a completed pool object.
        if (field<uint16_t>(object, target::layout::active) != 1)
            break;
    }
    --state.depth;
    state.owner = nullptr;
    root.expected = field<int16_t>(object, target::layout::frame);
}

bool can_interpolate(void* object, const Sample& s) {
    if (!s.continuous || s.track != field<void*>(object, target::layout::track) ||
        s.asset != field<void*>(object, target::layout::asset) ||
        s.expected != field<int16_t>(object, target::layout::frame) ||
        field<uint16_t>(object, target::layout::active) != 1 ||
        !(field<uint8_t>(object, target::layout::transform_flags) & 8) ||
        (field<uint8_t>(object, target::layout::playback_flags) & 2) || s.frame < 0 || s.frame >= INT16_MAX ||
        memcmp(s.pose.data(), object, sizeof(s.pose)) != 0)
        return false;
    // Sampling a clone must never enter the texture lookup/cache path.
    if (field<uint16_t>(object, target::layout::kind) == 0) {
        auto texture = field<void*>(object, target::layout::texture);
        if (!texture || !field<uint16_t>(texture, target::layout::texture_ready) ||
            (field<uint8_t>(object, target::layout::playback_flags) & 0x10))
            return false;
    }
    if (s.track) {
        auto timeline = field<void*>(s.track, target::layout::track_timeline);
        if (timeline && (s.frame + 1 >= field<int16_t>(timeline, target::layout::timeline_end) ||
                         (field<uint16_t>(object, target::layout::kind) == 1 &&
                          field<uint16_t>(timeline, target::layout::timeline_scale) != 4096)))
            return false;
    }
    return true;
}
void __fastcall draw(void* object) {
    if (!state.active || config.animation_timing != 2) {
        original_draw(object);
        return;
    }
    const auto found = state.samples.find(object);
    if (found == state.samples.end() || !can_interpolate(object, found->second)) {
        original_draw(object);
        return;
    }
    // Copy map values: a draw callback is allowed to create/update other nodes.
    const Sample s = found->second;
    const auto owner = state.roots.find(s.owner);
    if (owner == state.roots.end() || owner->second.clock.seen != scene_frame().serial) {
        original_draw(object);
        return;
    }
    const float phase = static_cast<float>(owner->second.clock.phase);
    if (phase <= 0) {
        original_draw(object);
        return;
    }
    alignas(16) std::array<unsigned char, target::layout::animation_size> current{}, next{};
    memcpy(current.data(), object, current.size());
    memcpy(next.data(), object, next.size());
    field<int16_t>(current.data(), target::layout::frame) = s.frame;
    field<int16_t>(next.data(), target::layout::frame) = s.frame + 1;
    // The original evaluator only writes to its object on these paths. It
    // invokes no timeline callbacks, child updates, labels or texture loaders.
    original_sample(current.data());
    original_sample(next.data());
    std::array<float, target::layout::pose_floats> applied = s.pose;
    for (size_t i = 0; i < applied.size(); ++i) {
        if (i == target::layout::pose_unknown_index)
            continue; // Not a known transform/color field.
        const float a = field<float>(current.data(), i * sizeof(float));
        const float b = field<float>(next.data(), i * sizeof(float));
        applied[i] = a + (b - a) * phase;
    }
    memcpy(object, applied.data(), sizeof(applied));
    ++state.interpolated;
    original_draw(object);
    // Restore the logical pose, while retaining any writes by draw callbacks.
    for (size_t i = 0; i < applied.size(); ++i)
        if (memcmp(static_cast<unsigned char*>(object) + i * sizeof(float), &applied[i], sizeof(float)) == 0)
            field<float>(object, i * sizeof(float)) = s.pose[i];
}

int64_t __fastcall gameplay(unsigned char mode) {
    if (!state.active)
        return original_gameplay(mode);
    const unsigned ticks = state.gameplay.advance(scene_frame().serial, scene_frame().elapsed);
    std::array<int*, target::layout::dm_lanes * 2> counters{};
    std::array<int, target::layout::dm_lanes * 2> before{};
    const bool drums = **static_cast<unsigned char**>(at(target::get().game_mode_pointer)) == 0;
    if (drums) {
        for (size_t i = 0; i < target::layout::dm_lanes; ++i) {
            counters[i] = static_cast<int*>(at(target::get().dm_effects + i * target::layout::dm_effect_stride));
            counters[i + target::layout::dm_lanes] =
                static_cast<int*>(at(target::get().dm_judgments + i * sizeof(int)));
        }
        for (size_t i = 0; i < counters.size(); ++i)
            before[i] = *counters[i];
    }
    const auto result = original_gameplay(mode); // Input/gameplay still runs every frame.
    if (drums && ticks != 1) {
        for (size_t i = 0; i < counters.size(); ++i)
            if (before[i] >= 1 && before[i] <= 8 && *counters[i] == effect_counter(before[i], 1))
                *counters[i] = effect_counter(before[i], ticks);
    }
    return result;
}
// Sprite records only select an atlas cell and visibility. Skip intermediate
// cells arithmetically, leaving one final native update to apply the result.
// Stateful object-animation callbacks above still execute in authored order.
void skip_sprite_ticks(void* record, unsigned ticks) {
    if (!ticks || !field<void*>(record, target::layout::sprite_object) ||
        !field<uint8_t>(record, target::layout::sprite_active))
        return;
    auto& counter = field<int32_t>(record, target::layout::sprite_frame);
    const auto delay = field<int32_t>(record, target::layout::sprite_step);
    auto& subframe = field<uint8_t>(record, target::layout::sprite_subframe);
    auto& cell = field<uint8_t>(record, target::layout::sprite_cell);
    const unsigned period = std::max(1U, unsigned(field<uint8_t>(record, target::layout::sprite_limit0)));
    const unsigned limit = field<uint8_t>(record, target::layout::sprite_limit1);
    const uint64_t steps = sprite_steps(counter, ticks, delay);
    bool expired = false;
    if (subframe < period && cell < limit) {
        const uint64_t left = (limit - cell) * period - subframe;
        expired = steps >= left;
        if (!expired) {
            const auto phase = subframe + steps;
            subframe = static_cast<uint8_t>(phase % period);
            cell = static_cast<uint8_t>(cell + phase / period);
        }
    } else {
        // Noncanonical byte phases recover exactly as the native INC/CMPs do.
        // At most 65,536 cell changes are possible before expiry.
        const auto raw_period = field<uint8_t>(record, target::layout::sprite_limit0);
        for (uint64_t i = 0; i < steps; ++i) {
            subframe = static_cast<uint8_t>(subframe + 1);
            if (subframe >= raw_period) {
                subframe = 0;
                cell = static_cast<uint8_t>(cell + 1);
            }
            if (cell >= limit) {
                expired = true;
                break;
            }
        }
    }
    if (expired) {
        // The next native update expires the effect and applies visibility.
        subframe = static_cast<uint8_t>(period - 1);
        cell = static_cast<uint8_t>(limit ? limit - 1 : 0);
        counter = static_cast<int32_t>(static_cast<uint32_t>(delay) - 1);
    } else {
        counter = static_cast<int32_t>(uint32_t(counter) + ticks);
    }
}
} // namespace

extern "C" char __fastcall gd_sprite_update(void* record) {
    if (!state.active)
        return original_sprite(record);
    if (!field<void*>(record, target::layout::sprite_object) ||
        !field<uint8_t>(record, target::layout::sprite_active)) {
        state.sprites.erase(record);
        return original_sprite(record);
    }
    auto& s = state.sprites[record];
    const auto object = field<void*>(record, target::layout::sprite_object);
    const unsigned ticks = s.clock.advance(scene_frame().serial, scene_frame().elapsed,
                                           !s.active || s.object != object ||
                                               field<int>(record, target::layout::sprite_frame) < s.counter);
    s.object = object;
    char result = 0;
    if (ticks) {
        skip_sprite_ticks(record, ticks - 1);
        result = original_sprite(record);
        ++state.sprite_updates;
    }
    s.counter = field<int>(record, target::layout::sprite_frame);
    s.active = field<uint8_t>(record, target::layout::sprite_active) != 0;
    return result;
}
extern "C" char gd_sprite_shim(void* record);
static void sprite_inlined(InlineContext& c) {
    const auto& p = target::get();
    const bool drums =
        c.ip == reinterpret_cast<uintptr_t>(at(p.sprite_inline[0].address)) + p.sprite_inline[0].signature.length;
    void* record = drums ? reinterpret_cast<void*>(c.r8 - target::layout::sprite_cell) : at(p.gf_sprite_records);
    const auto result = gd_sprite_update(record);
    c.rax = (c.rax & ~uint64_t(255)) | static_cast<uint8_t>(result);
    c.ip = reinterpret_cast<uintptr_t>(at(drums ? p.dm_sprite_next : p.gf_sprite_next));
}
std::vector<InlineHook> animation_inline_hooks() {
    std::vector<InlineHook> hooks{{&target::get().sprite_inline[0], sprite_inlined},
                                  {&target::get().sprite_inline[1], sprite_inlined}};
    for (const auto& q : target::get().frame_queries)
        hooks.push_back({&q.site, [](InlineContext& c) {
                             const auto& list = target::get().frame_queries;
                             const auto found = std::find_if(list.begin(), list.end(), [&](const auto& q) {
                                 return c.ip ==
                                        reinterpret_cast<uintptr_t>(at(q.site.address)) + q.site.signature.length;
                             });
                             if (found == list.end())
                                 return;
                             void* object = nullptr;
                             uint32_t lhs = 0, rhs = 0;
                             int32_t label = 0;
                             switch (found->form) {
                             case target::FrameQueryForm::RestoreRsi:
                                 object = reinterpret_cast<void*>(c.rdi);
                                 lhs = uint32_t(c.rax);
                                 rhs = uint32_t(c.rsi);
                                 label = int32_t(lhs);
                                 break;
                             case target::FrameQueryForm::IndirectLabel:
                                 object = reinterpret_cast<void*>(c.rax);
                                 lhs = uint32_t(c.rcx);
                                 rhs = *reinterpret_cast<uint32_t*>(c.r13 + found->label_offset);
                                 label = int32_t(rhs);
                                 break;
                             case target::FrameQueryForm::RegisterLabel:
                                 object = reinterpret_cast<void*>(c.rbx);
                                 lhs = uint32_t(c.rax);
                                 rhs = uint32_t(c.rcx);
                                 label = int32_t(rhs);
                                 break;
                             }
                             const bool due = animation_predicate(object, label, found->site.address);
                             c.cmp32(lhs, rhs);
                             c.flags = (c.flags & ~uint64_t(64)) | (due ? 64 : 0);
                             if (found->form == target::FrameQueryForm::RestoreRsi)
                                 c.rsi = *reinterpret_cast<uint64_t*>(c.native_rsp() + found->restore_rsi);
                             else if (!due)
                                 c.ip = reinterpret_cast<uintptr_t>(at(found->no));
                         }});
    return hooks;
}

bool animation_event(void* object, int frame, uintptr_t consumer) {
    if (!state.active)
        return object ? field<int16_t>(object, target::layout::frame) == frame : frame == -1;
    if (!object)
        return frame == -1;
    if (frame < 0)
        return field<int16_t>(object, target::layout::frame) == frame;
    // Also observe a scene's direct seek; it may occur after the engine update.
    const auto current = field<int16_t>(object, target::layout::frame);
    record_frame(object, current);
    auto& h = state.history[object];
    uint64_t latest = h.last == frame ? h.sequence : 0;
    for (const auto& range : h.ranges)
        if (frame >= range.first && frame <= range.last)
            latest = std::max(latest, range.stamp + uint64_t(frame - range.first));
    auto& consumed = h.consumed[consumer];
    if (latest <= consumed)
        return false;
    consumed = latest;
    return true;
}
bool animation_predicate(void* object, int frame, uintptr_t consumer) {
    const bool at_label = object ? field<int16_t>(object, target::layout::frame) == frame : frame == -1;
    const bool crossed = animation_event(object, frame, consumer);
    return at_label || crossed;
}

void begin_animation_frame() {
    state.active = scene_frame().active && config.refresh > 60 && config.animation_timing != 0;
    if (!state.active)
        return;
    if (scene_frame().serial % 600 == 0) {
        const auto stale = [](uint64_t seen) { return scene_frame().serial - seen > 120; };
        std::erase_if(state.roots, [&](const auto& p) { return stale(p.second.clock.seen); });
        std::erase_if(state.samples, [&](const auto& p) { return stale(p.second.serial); });
        std::erase_if(state.sprites, [&](const auto& p) { return stale(p.second.clock.seen); });
        std::erase_if(state.history, [&](const auto& p) { return stale(p.second.serial); });
    }
}
void end_animation_frame() {
    state.active = false;
}
void report_animations() {
    if (config.refresh <= 60 || !config.animation_timing)
        return;
    log("Animations: native ticks %llu, held updates %llu, interpolated draws %llu, sprite ticks %llu", state.updates,
        state.holds, state.interpolated, state.sprite_updates);
    state.updates = state.holds = state.interpolated = state.sprite_updates = 0;
}
std::vector<DiagnosticHook> animation_hooks() {
    return {bind_hook(target::get().animation_update, reinterpret_cast<void*>(update),
                      reinterpret_cast<void**>(&original_update)),
            bind_hook(target::get().animation_sample, reinterpret_cast<void*>(sample),
                      reinterpret_cast<void**>(&original_sample)),
            bind_hook(target::get().animation_draw, reinterpret_cast<void*>(draw),
                      reinterpret_cast<void**>(&original_draw)),
            bind_hook(target::get().sprite_update, reinterpret_cast<void*>(gd_sprite_shim),
                      reinterpret_cast<void**>(&original_sprite)),
            bind_hook(target::get().gameplay_visual, reinterpret_cast<void*>(gameplay),
                      reinterpret_cast<void**>(&original_gameplay))};
}
} // namespace gd
