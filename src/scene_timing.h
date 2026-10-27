#pragma once
#include "note_diagnostics.h"
#include "inline_hook.h"
#include <array>

namespace gd {
// One thread-local main-update context, shared by audio sampling, menus,
// scenes and animations. Consumers retain their own progress, not time sources.
struct SceneFrame {
    bool active = false;
    uint64_t serial = 0;
    double elapsed = 0;
};
const SceneFrame& scene_frame();
void begin_scene_frame(uint64_t serial, double elapsed);
void end_scene_frame();
void track_scene_controller(void* controller);
std::array<uint32_t, 3> shared_counter_snapshot();
void finish_shared_counters(const std::array<uint32_t, 3>& before);
std::vector<DiagnosticHook> scene_timing_hooks();
std::vector<const target::Patch*> scene_timing_patches();
std::vector<InlineHook> scene_inline_hooks();
} // namespace gd
