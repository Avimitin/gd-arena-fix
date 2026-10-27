#pragma once
#include "mod.h"

namespace gd {
struct Live2DPatch {
    uintptr_t address;
    std::vector<uint8_t> before, after;
};

// Only suppress the optional left/right cabinet characters. Zero is the
// game's existing "no character" branch. Do not skip the global Cubism
// update: other scenes use motion-completion callbacks and model cleanup.
// Preferred VAs are for the hash-gated Delta build, relocated through at().
inline std::vector<Live2DPatch> live2d_patches() {
    std::vector<Live2DPatch> patches;
    const auto& profile = target::get();
    for (const auto* patch : {&profile.live2d_menu, &profile.live2d_alternate, &profile.live2d_inline})
        patches.push_back({patch->address, patch->before.copy(), patch->after.copy()});
    return patches;
}
} // namespace gd
