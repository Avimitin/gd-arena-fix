#include "mod.h"
#include "config.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace gd {
using Create9Ex = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
static Create9Ex original_create = nullptr;
static void** create_slot = nullptr;
static bool active = false;
static std::once_flag initialization;

void initialize() {
    wchar_t ini[MAX_PATH];
    GetModuleFileNameW(self, ini, MAX_PATH);
    if (auto dot = wcsrchr(ini, L'.'))
        *dot = 0;
    wcscat_s(ini, L".ini");
    config = read_config(ini);
    log("Config file: %ls", ini);
    if (!valid_config(config))
        throw std::runtime_error("unsupported settings (1920x1080, FPS >= 60, Refresh=FPS)");
    if (config.animation_timing > 2)
        throw std::runtime_error("AnimationTiming must be 0, 1 or 2");
    if (!validate_game(game))
        throw std::runtime_error("unsupported gdxg.dll hash; no game patches installed");
    if (config.native_rendering) {
        const auto& site = target::get().font_projection_constants;
        if (std::memcmp(at(site.address), site.signature.storage.data(), site.signature.length) != 0) {
            config.native_rendering = false;
            log("NativeRendering disabled: font projection signature mismatch; using 3840x2160 fallback");
        }
    }
    log("gd-arena-fix 0.1.9: %ux%u @ %u; target FPS %u; native main rendering %s", config.width, config.height,
        config.refresh, config.fps, config.native_rendering ? "enabled (experimental)" : "disabled");
    install_timing_and_present();
    // No hot unload: COM tables and trampolines must remain valid for process life.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&initialize), &pinned);
    active = true;
    log("Hooks installed. Main logical coordinates: 3840x2160; render target: %s. Runtime gameplay validation is "
        "required.",
        config.native_rendering ? "1920x1080" : "3840x2160");
}
static HRESULT WINAPI create9ex(UINT version, IDirect3D9Ex** output) {
    std::call_once(initialization, [] {
        try {
            initialize();
        } catch (const std::exception& e) {
            log("DISABLED: %s", e.what());
            if (create_slot)
                write_memory(create_slot, &original_create, sizeof(original_create));
        }
    });
    HRESULT hr = original_create(version, output);
    if (active && SUCCEEDED(hr) && output && *output)
        wrap_factory(*output);
    return hr;
}
bool bootstrap(HMODULE module) {
    self = module;
    game = GetModuleHandleW(L"gdxg.dll");
    if (!game) {
        OutputDebugStringA("gd-arena-fix: gdxg.dll is not loaded; use -k, not -z.\n");
        return true;
    }
    create_slot = import_slot(game, "Direct3DCreate9Ex");
    if (!create_slot)
        return true;
    original_create = reinterpret_cast<Create9Ex>(*create_slot);
    auto hook = &create9ex;
    // Only this pointer swap occurs under loader lock. Hashing, allocation,
    // logging and MinHook initialization wait for the first D3D call.
    return write_memory(create_slot, &hook, sizeof(hook));
}
} // namespace gd
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        return gd::bootstrap(module) ? TRUE : FALSE;
    return TRUE;
}
