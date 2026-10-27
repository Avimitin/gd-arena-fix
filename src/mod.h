#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <string>
#include <vector>
#include "game_profile.h"

namespace gd {
struct Config {
    unsigned width = 1920, height = 1080, refresh = 60, fps = 60;
    bool diagnostics = false;
    bool disable_live2d = false;
    unsigned animation_timing = 0;
};
extern Config config;
extern HMODULE self, game;
void log(const char* format, ...);
double seconds();
bool write_memory(void* destination, const void* source, size_t size);
void** import_slot(HMODULE module, const char* name);
void** import_ordinal(HMODULE module, const char* dll, unsigned ordinal);
std::string sha256_file(const wchar_t* path);
bool validate_game(HMODULE module);
void* at(uintptr_t preferred_va);
void hook_method(void* object, size_t count, size_t index, void* replacement);
void* original_method(void* object, size_t index);
void wrap_factory(IDirect3D9Ex* factory);
bool fit_output_window(HWND window, bool windowed, HMONITOR monitor);
bool compose();
void release_canvas();
IDirect3DSurface9* physical_backbuffer(); // AddRef'd result; used by smoke test
void install_timing_and_present();
void initialize();
void capture_diagnostic_frame();
bool save_surface_bmp(IDirect3DDevice9Ex* owner, IDirect3DSurface9* surface, const wchar_t* path);
} // namespace gd
