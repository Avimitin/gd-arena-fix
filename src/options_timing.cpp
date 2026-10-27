#include "mod.h"
#include "options_timing.h"
#include "animations.h"
namespace gd {
namespace {
void selection_cue(InlineContext& c) {
    const auto& p=target::get();c.cmp32(uint32_t(c.rcx),uint32_t(c.rsi));
    const bool due=animation_event(*static_cast<void**>(at(p.selection_frame_object)),int32_t(c.rsi),p.selection_frame_cue.address);
    if(due)c.rdx=p.selection_frame_sound;
    else c.ip=reinterpret_cast<uintptr_t>(at(p.selection_frame_no));
}
}
std::vector<InlineHook> options_inline_hooks() {
    const auto& p=target::get();return {{&p.selection_frame_cue,selection_cue}};
}
}
