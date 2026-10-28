#pragma once
#include "mod.h"
#include <cwchar>
#include <cstdlib>

namespace gd {
inline bool valid_config(const Config& value) {
    return value.width == 1920 && value.height == 1080 && value.fps >= 60 && value.refresh == value.fps;
}
inline unsigned read_setting(const wchar_t* key, unsigned fallback, const wchar_t* path) {
    unsigned result = GetPrivateProfileIntW(L"display", key, fallback, path);
    wchar_t environment[80], value[80];
    swprintf_s(environment, L"GDMOD_%s", key);
    if (GetEnvironmentVariableW(environment, value, 80))
        result = wcstoul(value, nullptr, 10);
    return result;
}
inline Config read_config(const wchar_t* path) {
    Config result;
    result.width = read_setting(L"Width", 1920, path);
    result.height = read_setting(L"Height", 1080, path);
    result.refresh = read_setting(L"Refresh", 60, path);
    result.fps = read_setting(L"FPS", 60, path);
    result.diagnostics = read_setting(L"Diagnostics", 0, path) != 0;
    result.disable_live2d = read_setting(L"DisableLive2D", 1, path) != 0;
    result.disable_side_compositing = read_setting(L"DisableSideCompositing", 1, path) != 0;
    result.animation_timing = read_setting(L"AnimationTiming", 0, path);
    result.native_rendering = read_setting(L"NativeRendering", 1, path) != 0;
    result.note_smoothing = read_setting(L"NoteSmoothing", 1, path) != 0;
    return result;
}
} // namespace gd
