#pragma once

namespace selection_fixture {
namespace layout = gd::target::layout::selection;
struct Clip {
    void** table;
    unsigned handle;
    int frame;
};
static unsigned stops, plays;
static bool readable = true;
static bool labels_present = true;
static uint32_t idle_begin = 420, move_begin = 600;
static uint8_t __fastcall label(Clip*, uint32_t* frame, const char* name) {
    if (!labels_present)
        return 0;
    if (!strcmp(name, "loop"))
        *frame = idle_begin;
    else if (!strcmp(name, "move"))
        *frame = move_begin;
    else
        throw std::runtime_error("unexpected selection label lookup");
    return 1;
}
static uint8_t __fastcall valid(void*) {
    return 1;
}
static uint8_t __fastcall get(Clip* clip, int* frame) {
    *frame = clip->frame;
    return readable;
}
static uint8_t __fastcall seek(Clip* clip, unsigned frame) {
    clip->frame = int(frame);
    return 1;
}
static unsigned __fastcall operation(unsigned handle, unsigned command) {
    check(handle == 123 && (command == layout::afp_stop || command == layout::afp_play),
          "selection retains original clip operation");
    if (command == layout::afp_stop)
        ++stops;
    else
        ++plays;
    return 0;
}
} // namespace selection_fixture

static void native_selection_motion_test(bool category = false) {
    namespace fixture = selection_fixture;
    namespace layout = gd::target::layout::selection;
    const auto& p = gd::target::get();
    const auto update = [&](void* object, uint8_t hide) {
        return category
                   ? reinterpret_cast<uint8_t(__fastcall*)(void*)>(gd::at(p.category_motion))(object)
                   : reinterpret_cast<uint8_t(__fastcall*)(void*, uint8_t)>(gd::at(p.selection_motion))(object, hide);
    };
    auto import = gd::import_ordinal(gd::game, "afp-core.dll", layout::afp_operation_ordinal);
    check(import != nullptr, "selection clip operation import exists");
    const auto saved_import = *import;
    *import = reinterpret_cast<void*>(fixture::operation);
    std::array<void*, layout::clip_seek_slot + 1> table{};
    table[layout::clip_valid_slot] = reinterpret_cast<void*>(fixture::valid);
    table[layout::clip_frame_slot] = reinterpret_cast<void*>(fixture::get);
    table[layout::clip_seek_slot] = reinterpret_cast<void*>(fixture::seek);
    table[layout::clip_label_slot] = reinterpret_cast<void*>(fixture::label);
    alignas(16) std::array<uint8_t, layout::ui_fixture_size> ui{};
    static_assert(layout::clip + sizeof(fixture::Clip) <= ui.size());
    auto& clip = *reinterpret_cast<fixture::Clip*>(ui.data() + (category ? layout::category_clip : layout::clip));
    const auto ready = [&] { return category ? (fixture::stops || fixture::plays) : bool(ui[layout::ready]); };
    const auto reset = [&](int frame, int direction) {
        ui.fill(0);
        ui[category ? layout::category_valid : layout::valid] = 1;
        anim_field<int>(ui.data(), layout::direction) = direction;
        clip = {table.data(), 123, frame};
        fixture::stops = 0;
        fixture::plays = 0;
        fixture::readable = true;
        fixture::labels_present = true;
        fixture::idle_begin = 420;
        fixture::move_begin = 600;
    };
    const auto& site = category ? p.category_ready : p.selection_ready;
    auto gate = gd::at(site.address);
    const auto signature = site.signature.copy();
    std::vector<uint8_t> patched(signature.size());
    memcpy(patched.data(), gate, patched.size());
    check(gd::write_memory(gate, signature.data(), signature.size()), "restore isolated native readiness check");
    reset(421, 2); // Captured in the live freeze; not a constant used by the fix.
    for (unsigned i = 0; i < 240; ++i)
        check(update(ui.data(), 0) == 0, "original selection cannot complete from observed frame 421");
    check(!ready() && anim_field<int>(ui.data(), layout::direction) == 2 &&
              anim_field<int>(ui.data(), layout::phase) == 0 && fixture::stops == 0,
          "reproduce live freeze: unreadied queued scroll stays busy with no progress");
    check(gd::write_memory(gate, patched.data(), patched.size()), "restore selection readiness fix");
    if (gd::config.fps > 60) {
        // Feed the captured stalled state directly into the actual native engine.
        unsigned completed = 0;
        for (unsigned i = 0; i < 64; ++i)
            completed += update(ui.data(), 0) != 0;
        check(completed == 1 && ready() && anim_field<int>(ui.data(), layout::direction) == 0 &&
                  anim_field<int>(ui.data(), layout::phase) == 0 && anim_field<int>(ui.data(), layout::velocity) == 0,
              "readiness fix drains the queued scroll exactly once and clears native busy state");

        for (unsigned hz : {120U, 144U, 240U, 360U}) {
            for (int direction : {-2, -1, 0, 1, 2}) {
                reset(int(p.selection_ready_frame) - 1, direction);
                gd::begin_timing_frame(1.0 / hz);
                check(!update(ui.data(), 0) && !ready(), "intro cannot unlock before completion");
                clip.frame = 421; // Never sample exact frame 420.
                completed = 0;
                for (unsigned i = 0; i < 64; ++i) {
                    gd::begin_timing_frame(i == 1 ? 0.25 : 1.0 / hz);
                    completed += update(ui.data(), 0) != 0;
                }
                check(ready() && completed == unsigned(direction != 0) &&
                          anim_field<int>(ui.data(), layout::direction) == 0,
                      "skipped completion frame supports either direction, idle, any cadence and a stalled interval");
            }
        }
        for (int frame : {-1,   0,    419,  420,  421,  422,  500,  589,  590,  599,  600, 1601,
                          1602, 1799, 1800, 2801, 2802, 2809, 2810, 2820, 2821, 3300, 3331}) {
            reset(frame, 0);
            update(ui.data(), 0);
            const bool expected = (frame >= 420 && frame <= 1601) || (frame >= 1800 && frame <= 2801) ||
                                  (category && frame >= 2810 && frame <= 2820);
            check(ready() == expected, "idle interval and native movement ranges are ready; intro and close are not");
            check(bool(fixture::plays) ==
                      (category && ((frame >= 590 && frame < 600) || (frame >= 2810 && frame <= 2820))),
                  "category open/close playback retains its native behavior");
        }
        // Asset labels determine readiness, not observed 420/421 frame numbers.
        for (int frame : {419, 420, 421, 479, 480, 481, 500, 549, 550, 551}) {
            reset(frame, 0);
            fixture::idle_begin = 480;
            fixture::move_begin = 550;
            update(ui.data(), 0);
            check(ready() == (frame >= 480 && frame < 550),
                  "readiness follows relocated asset labels rather than a numeric exception");
        }
        for (int frame : {419, 420, 421, 500}) {
            reset(frame, 0);
            fixture::labels_present = false;
            update(ui.data(), 0);
            check(ready() == (frame == 420), "missing labels retain native behavior");
        }
        reset(421, 1);
        fixture::move_begin = fixture::idle_begin;
        check(!update(ui.data(), 0) && !ready(), "invalid label interval cannot unlock scrolling");
        reset(421, 1);
        fixture::readable = false;
        check(!update(ui.data(), 0) && !ready(), "failed AFP query cannot unlock scrolling");
        fixture::readable = true;
    }
    *import = saved_import;
    gd::end_scene_frame();
    printf("PASS: native %s scroll stall reproduced; label-based readiness, relocated labels, completion and phase "
           "guards\n",
           category ? "category" : "song-list");
}
