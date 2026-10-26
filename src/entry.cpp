#include "mod.h"
#include "config.h"
#include <atomic>
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
    if (auto dot = wcsrchr(ini, L'.')) *dot = 0;
    wcscat_s(ini, L".ini");
    config = read_config(ini);
    log("Config file: %ls", ini);
    log("gd-arena-fix prototype 0.1.4: %ux%u @ %u; target FPS %u; experimental=%d",
        config.width, config.height, config.refresh, config.fps, config.experimental);
    if (config.width != 1920 || config.height != 1080 || (config.refresh != 60 && config.refresh != 120) ||
        (config.fps != 60 && config.fps != 120) || (config.fps == 120 && (!config.experimental || config.refresh != 120)))
        throw std::runtime_error("unsupported settings (120 FPS requires Experimental=1 and Refresh=120)");
    if (!validate_game(game)) throw std::runtime_error("unsupported gdxg.dll hash; no game patches installed");
    install_timing_and_present();
    // No hot unload: COM tables and trampolines must remain valid for process life.
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&initialize), &pinned);
    active = true;
    log("Hooks installed. Internal main rendering remains 3840x2160. Runtime gameplay validation is required.");
}
static HRESULT WINAPI create9ex(UINT version, IDirect3D9Ex** output) {
    std::call_once(initialization, [] {
        try { initialize(); }
        catch (const std::exception& e) {
            log("DISABLED: %s", e.what());
            if (create_slot) write_memory(create_slot, &original_create, sizeof(original_create));
        }
    });
    HRESULT hr = original_create(version, output);
    if (active && SUCCEEDED(hr) && output && *output) wrap_factory(*output);
    return hr;
}
bool bootstrap(HMODULE module) {
    self = module;
    game = GetModuleHandleW(L"gdxg.dll");
    if (!game) { OutputDebugStringA("gd-arena-fix: gdxg.dll is not loaded; use -k, not -z.\n"); return true; }
    create_slot = import_slot(game, "Direct3DCreate9Ex");
    if (!create_slot) return true;
    original_create = reinterpret_cast<Create9Ex>(*create_slot);
    auto hook = &create9ex;
    // Only this pointer swap occurs under loader lock. Hashing, allocation,
    // logging and MinHook initialization wait for the first D3D call.
    return write_memory(create_slot, &hook, sizeof(hook));
}
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) return gd::bootstrap(module) ? TRUE : FALSE;
    return TRUE;
}
