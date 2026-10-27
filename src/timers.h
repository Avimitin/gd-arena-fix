#pragma once
#include "note_diagnostics.h"
#include "inline_hook.h"

namespace gd {
void begin_timing_frame(double elapsed);
std::vector<DiagnosticHook> timer_hooks();
std::vector<InlineHook> timer_inline_hooks();
}
