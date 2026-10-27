#pragma once
#include "note_diagnostics.h"
#include "inline_hook.h"

namespace gd {
std::vector<DiagnosticHook> animation_hooks();
std::vector<InlineHook> animation_inline_hooks();
void begin_animation_frame();
void end_animation_frame();
void report_animations();
// One-shot scene cue at an authored frame, including frames crossed in a stall.
bool animation_event(void* object, int frame, uintptr_t consumer);
// Held-label predicates retain their level semantics and accept a crossed frame.
bool animation_predicate(void* object, int frame, uintptr_t consumer);
} // namespace gd
