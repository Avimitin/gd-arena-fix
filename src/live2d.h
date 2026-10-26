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
    return {
        // Menu pair builder sub_180306170: mask both IDs in local registers.
        // Its existing sub_1803060C0 cleanup still runs before the zero tests.
        {0x180306192, {0x8b,0xfa,0x8b,0xd9}, {0x31,0xff,0x31,0xdb}},
        // Alternate gameplay pair builder: its first test uses ECX, so change
        // that test to the masked EBX as well; the second tests masked EDI.
        {0x180288DB4, {0x8b,0xfa,0x8b,0xd9,0x85,0xc9},
                       {0x31,0xff,0x31,0xdb,0x85,0xdb}},
        // Inlined pair creation in sub_18028AD50. Replace reads of the saved
        // IDs (1829322B4/B0) with zero; the profile globals remain untouched.
        {0x18028B29B, {0x8b,0x3d,0x13,0x70,0x6a,0x02,0x8b,0x1d,0x09,0x70,0x6a,0x02},
                      {0x31,0xff,0x90,0x90,0x90,0x90,0x31,0xdb,0x90,0x90,0x90,0x90}},
    };
}
}
