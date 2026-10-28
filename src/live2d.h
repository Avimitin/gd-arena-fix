#pragma once
#include "mod.h"

namespace gd {
struct Live2DPatch {
    uintptr_t address;
    std::vector<uint8_t> before, after;
};

// Suppress optional left/right cabinet characters, plus the guarded side
// compositing blocks when DisableSideCompositing is enabled. Character IDs use
// the game's existing "no character" branch. Keep global Cubism updates for
// motion-completion callbacks and model cleanup. Preferred VAs are for the
// hash-gated Delta build, relocated through at().
inline std::vector<Live2DPatch> live2d_patches() {
    std::vector<Live2DPatch> patches;
    const auto& profile = target::get();
    for (const auto* patch : {&profile.live2d_menu, &profile.live2d_alternate, &profile.live2d_inline})
        patches.push_back({patch->address, patch->before.copy(), patch->after.copy()});
    if (config.disable_side_compositing)
        for (const auto* patch : {&profile.side_copy_left, &profile.side_copy_right, &profile.side_composite_copy})
            patches.push_back({patch->address, patch->before.copy(), patch->after.copy()});
    return patches;
}
} // namespace gd
