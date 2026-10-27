#pragma once
#include "mod.h"

namespace gd {
struct DiagnosticHook {
    uintptr_t address;
    std::vector<uint8_t> signature;
    void* replacement;
    void** original;
};
inline DiagnosticHook bind_hook(const target::Site& site, void* replacement, void** original) {
    return {site.address, site.signature.copy(), replacement, original};
}
std::vector<DiagnosticHook> note_diagnostic_hooks();
void report_note_diagnostics(double elapsed);
}
