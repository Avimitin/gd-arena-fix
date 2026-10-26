#include "mod.h"
#include <algorithm>
#include <cstdio>
#include <memory>

namespace gd {
using FactoryCreate = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD,
    D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
using GetBuffer = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**);
using GetChain = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, UINT, IDirect3DSwapChain9**);
using ChainBuffer = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**);
using ChainRelease = ULONG(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*);
using ResetEx = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
using Reset = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*);

static IDirect3DDevice9Ex* device = nullptr;
static IDirect3DSurface9* canvas = nullptr;
static HWND main_window = nullptr;
static HMONITOR main_monitor = nullptr;
static unsigned logical_width = 3840, logical_height = 2160;
static std::vector<IDirect3DSwapChain9*> main_chains;
static thread_local bool internal_call = false;
struct InternalCall {
    bool previous = internal_call;
    InternalCall() { internal_call = true; }
    ~InternalCall() { internal_call = previous; }
};

void release_canvas() {
    if (canvas) { canvas->Release(); canvas = nullptr; }
}
IDirect3DSurface9* physical_backbuffer() {
    InternalCall bypass;
    IDirect3DSurface9* surface = nullptr;
    if (device) reinterpret_cast<GetBuffer>(original_method(device, 18))(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &surface);
    return surface;
}
static bool make_canvas() {
    InternalCall bypass;
    if (canvas) return true;
    auto physical = physical_backbuffer();
    if (!physical) return false;
    D3DSURFACE_DESC desc{};
    const HRESULT description = physical->GetDesc(&desc);
    physical->Release();
    if (FAILED(description)) return false;
    log("Physical main backbuffer: %ux%u; logical canvas: %ux%u", desc.Width, desc.Height, logical_width, logical_height);
    if (desc.Width != config.width || desc.Height != config.height || desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        log("ERROR: physical output differs from requested dimensions, or MSAA is enabled. Check Spice display overrides.");
        return false;
    }
    HRESULT hr = device->CreateRenderTarget(logical_width, logical_height, desc.Format,
        D3DMULTISAMPLE_NONE, 0, FALSE, &canvas, nullptr);
    if (FAILED(hr)) { log("ERROR: CreateRenderTarget canvas failed: %08lx", hr); return false; }
    hr = device->SetRenderTarget(0, canvas);
    if (FAILED(hr)) { release_canvas(); return false; }
    D3DVIEWPORT9 viewport{0, 0, logical_width, logical_height, 0.0f, 1.0f};
    device->SetViewport(&viewport);
    return true;
}
bool compose() {
    InternalCall bypass;
    if (!device || !canvas) return false;
    auto physical = physical_backbuffer();
    if (!physical) return false;
    const HRESULT hr = device->StretchRect(canvas, nullptr, physical, nullptr, D3DTEXF_LINEAR);
    physical->Release();
    if (FAILED(hr)) {
        static unsigned errors = 0;
        if (errors++ < 8) log("ERROR: main output StretchRect failed: %08lx", hr);
    }
    return SUCCEEDED(hr);
}
bool save_surface_bmp(IDirect3DDevice9Ex* owner, IDirect3DSurface9* surface, const wchar_t* path) {
    D3DSURFACE_DESC desc{};
    if (!owner || !surface || FAILED(surface->GetDesc(&desc)) ||
        (desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8)) return false;
    IDirect3DSurface9* readable = nullptr;
    if (FAILED(owner->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
            D3DPOOL_SYSTEMMEM, &readable, nullptr))) return false;
    const auto cleanup = [](IDirect3DSurface9* p) { p->Release(); };
    std::unique_ptr<IDirect3DSurface9, decltype(cleanup)> held(readable, cleanup);
    if (FAILED(owner->GetRenderTargetData(surface, readable))) return false;
    D3DLOCKED_RECT lock{};
    if (FAILED(readable->LockRect(&lock, nullptr, D3DLOCK_READONLY))) return false;
    const DWORD stride = (desc.Width * 3 + 3) & ~3u;
    BITMAPFILEHEADER file_header{};
    BITMAPINFOHEADER info{};
    file_header.bfType = 0x4d42;
    file_header.bfOffBits = sizeof(file_header) + sizeof(info);
    file_header.bfSize = file_header.bfOffBits + stride * desc.Height;
    info.biSize = sizeof(info);
    info.biWidth = desc.Width;
    info.biHeight = -LONG(desc.Height); // top-down BGR; ignore backbuffer alpha
    info.biPlanes = 1;
    info.biBitCount = 24;
    info.biSizeImage = stride * desc.Height;
    FILE* file = nullptr;
    bool ok = _wfopen_s(&file, path, L"wb") == 0;
    if (ok) {
        ok = fwrite(&file_header, sizeof(file_header), 1, file) == 1 && fwrite(&info, sizeof(info), 1, file) == 1;
        // Allocation-free conversion keeps the surface lock exception-safe.
        uint8_t row[3840 * 3]{};
        if (stride > sizeof(row)) ok = false;
        for (unsigned y = 0; ok && y < desc.Height; ++y) {
            const auto source = static_cast<const uint8_t*>(lock.pBits) + y * lock.Pitch;
            for (unsigned x = 0; x < desc.Width; ++x) {
                row[x * 3] = source[x * 4];
                row[x * 3 + 1] = source[x * 4 + 1];
                row[x * 3 + 2] = source[x * 4 + 2];
            }
            ok = fwrite(row, stride, 1, file) == 1;
        }
        if (fclose(file) != 0) ok = false;
    }
    readable->UnlockRect();
    return ok;
}
void capture_diagnostic_frame() {
    static bool previously_down = false;
    const bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) && (GetAsyncKeyState(VK_F8) & 0x8000);
    const bool pressed = down && !previously_down;
    previously_down = down;
    DWORD foreground_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
    if (!pressed || foreground_process != GetCurrentProcessId() || !device || !canvas) return;
    InternalCall bypass;
    wchar_t directory[MAX_PATH]{};
    GetModuleFileNameW(self, directory, MAX_PATH);
    auto slash = wcsrchr(directory, L'\\');
    if (!slash) return;
    slash[1] = 0;
    SYSTEMTIME now; GetLocalTime(&now);
    wchar_t name[128]{};
    swprintf_s(name, L"notes-%04u%02u%02u-%02u%02u%02u-%03u", now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    const auto prefix = std::wstring(directory) + name;
    const auto logical_path = prefix + L"-4k.bmp", physical_path = prefix + L"-1080p.bmp";
    const bool logical_saved = save_surface_bmp(device, canvas, logical_path.c_str());
    auto physical = physical_backbuffer();
    const bool physical_saved = save_surface_bmp(device, physical, physical_path.c_str());
    if (physical) physical->Release();
    log("NOTE capture %ls: 4K=%s, 1080p=%s", prefix.c_str(), logical_saved ? "OK" : "FAILED", physical_saved ? "OK" : "FAILED");
}
static HRESULT STDMETHODCALLTYPE get_buffer(IDirect3DDevice9Ex* object, UINT chain, UINT index,
    D3DBACKBUFFER_TYPE type, IDirect3DSurface9** output) {
    if (!internal_call && object == device && chain == 0 && index == 0 && type == D3DBACKBUFFER_TYPE_MONO && output) {
        if (!make_canvas()) return D3DERR_NOTAVAILABLE;
        canvas->AddRef(); *output = canvas; return D3D_OK;
    }
    return reinterpret_cast<GetBuffer>(original_method(object, 18))(object, chain, index, type, output);
}
static HRESULT STDMETHODCALLTYPE chain_buffer(IDirect3DSwapChain9* object, UINT index,
    D3DBACKBUFFER_TYPE type, IDirect3DSurface9** output) {
    if (!internal_call && index == 0 && type == D3DBACKBUFFER_TYPE_MONO && output &&
        std::find(main_chains.begin(), main_chains.end(), object) != main_chains.end()) {
        if (!make_canvas()) return D3DERR_NOTAVAILABLE;
        canvas->AddRef(); *output = canvas; return D3D_OK;
    }
    return reinterpret_cast<ChainBuffer>(original_method(object, 5))(object, index, type, output);
}
static ULONG STDMETHODCALLTYPE chain_release(IDirect3DSwapChain9* object) {
    auto original = reinterpret_cast<ChainRelease>(original_method(object, 2));
    const ULONG remaining = original(object);
    if (!remaining) main_chains.erase(std::remove(main_chains.begin(), main_chains.end(), object), main_chains.end());
    return remaining;
}
static HRESULT STDMETHODCALLTYPE get_chain(IDirect3DDevice9Ex* object, UINT index, IDirect3DSwapChain9** output) {
    HRESULT hr = reinterpret_cast<GetChain>(original_method(object, 14))(object, index, output);
    if (!internal_call && SUCCEEDED(hr) && output && *output && object == device && index == 0) {
        hook_method(*output, 10, 5, reinterpret_cast<void*>(chain_buffer));
        hook_method(*output, 10, 2, reinterpret_cast<void*>(chain_release));
        if (std::find(main_chains.begin(), main_chains.end(), *output) == main_chains.end()) main_chains.push_back(*output);
    }
    return hr;
}
struct MainParameters {
    D3DPRESENT_PARAMETERS* pp;
    D3DDISPLAYMODEEX* mode;
    UINT width, height;
    explicit MainParameters(D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* m) : pp(p), mode(m),
        width(p->BackBufferWidth), height(p->BackBufferHeight) {
        p->BackBufferWidth = config.width;
        p->BackBufferHeight = config.height;
        p->FullScreen_RefreshRateInHz = p->Windowed ? 0 : config.refresh;
        p->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        if (m && !p->Windowed) { m->Width = config.width; m->Height = config.height; m->RefreshRate = config.refresh; }
    }
    ~MainParameters() {
        // The game retains logical dimensions; physical dimensions belong to D3D.
        pp->BackBufferWidth = width; pp->BackBufferHeight = height;
        if (mode) { mode->Width = width; mode->Height = height; }
    }
};
bool fit_output_window(HWND window, bool windowed, HMONITOR monitor) {
    if (!window) return false;
    RECT before{};
    if (!GetClientRect(window, &before)) return false;
    RECT outer{0, 0, LONG(config.width), LONG(config.height)};
    if (!AdjustWindowRectEx(&outer, DWORD(GetWindowLongPtrW(window, GWL_STYLE)),
            GetMenu(window) != nullptr, DWORD(GetWindowLongPtrW(window, GWL_EXSTYLE)))) return false;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    LONG x = 0, y = 0;
    if (windowed) {
        flags |= SWP_NOMOVE;
    } else {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!monitor || !GetMonitorInfoW(monitor, &info)) {
            monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            if (!GetMonitorInfoW(monitor, &info)) return false;
        }
        x = info.rcMonitor.left + outer.left;
        y = info.rcMonitor.top + outer.top;
    }
    // D3DCREATE_NOWINDOWCHANGES leaves the game's original 4K HWND intact
    // in fullscreen. WM_MOUSE/WM_TOUCH normalize by GetClientRect, so that
    // stale size makes a 1080p pointer reach only half of each logical axis.
    // Change the real window, allowing Spice's normal resize/touch handling
    // to observe the same dimensions. Do not rescale input a second time.
    if (!SetWindowPos(window, nullptr, x, y, outer.right - outer.left,
            outer.bottom - outer.top, flags)) {
        log("ERROR: main window resize failed: %lu", GetLastError());
        return false;
    }
    RECT after{};
    if (!GetClientRect(window, &after)) return false;
    log("Main input client (%s): %ldx%ld -> %ldx%ld; requested %ux%u",
        windowed ? "windowed" : "fullscreen", before.right - before.left, before.bottom - before.top,
        after.right - after.left, after.bottom - after.top, config.width, config.height);
    const LONG width = after.right - after.left, height = after.bottom - after.top;
    // Windows can cap a decorated window at the monitor's maximum tracking
    // size. Windowed Present scales to that actual client and the game's
    // existing normalization follows it. Fullscreen must match exactly.
    if (windowed) return width > 0 && height > 0 && width <= LONG(config.width) && height <= LONG(config.height);
    return width == LONG(config.width) && height == LONG(config.height);
}
static void fit_main_window(D3DPRESENT_PARAMETERS* pp) {
    if (pp->hDeviceWindow) main_window = pp->hDeviceWindow;
    if (!fit_output_window(main_window, pp->Windowed != FALSE, main_monitor))
        log("ERROR: main input client does not match physical output; check window sizing overrides");
}
static HRESULT STDMETHODCALLTYPE reset_ex(IDirect3DDevice9Ex* object, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode) {
    auto original = reinterpret_cast<ResetEx>(original_method(object, 132));
    if (object != device || !pp) return original(object, pp, mode);
    release_canvas();
    main_chains.clear();
    MainParameters adjusted(pp, mode);
    HRESULT hr;
    { InternalCall bypass; hr = original(object, pp, pp->Windowed ? nullptr : mode); }
    log("ResetEx returned %08lx", hr);
    if (SUCCEEDED(hr)) {
        fit_main_window(pp);
        if (!make_canvas()) return D3DERR_NOTAVAILABLE;
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9Ex* object, D3DPRESENT_PARAMETERS* pp) {
    auto original = reinterpret_cast<Reset>(original_method(object, 16));
    if (object != device || !pp) return original(object, pp);
    release_canvas();
    main_chains.clear();
    MainParameters adjusted(pp, nullptr);
    HRESULT hr;
    { InternalCall bypass; hr = original(object, pp); }
    log("Reset returned %08lx", hr);
    if (SUCCEEDED(hr)) {
        fit_main_window(pp);
        if (!make_canvas()) return D3DERR_NOTAVAILABLE;
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE create_device(IDirect3D9Ex* object, UINT adapter, D3DDEVTYPE type, HWND window,
    DWORD flags, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode, IDirect3DDevice9Ex** output) {
    auto original = reinterpret_cast<FactoryCreate>(original_method(object, 20));
    if (!pp || !output || pp->BackBufferWidth != 3840 || pp->BackBufferHeight != 2160)
        return original(object, adapter, type, window, flags, pp, mode, output);
    // Modify only head 0. Spice keeps responsibility for cabinet head emulation.
    logical_width = pp->BackBufferWidth; logical_height = pp->BackBufferHeight;
    MainParameters adjusted(pp, mode);
    log("CreateDeviceEx main: output %ux%u @ %u, windowed=%d", config.width, config.height, config.refresh, pp->Windowed);
    HRESULT hr = original(object, adapter, type, window, flags, pp, pp->Windowed ? nullptr : mode, output);
    if (FAILED(hr)) { log("ERROR: CreateDeviceEx failed: %08lx", hr); return hr; }
    release_canvas();
    main_chains.clear();
    device = *output;
    main_window = pp->hDeviceWindow ? pp->hDeviceWindow : window;
    main_monitor = object->GetAdapterMonitor(adapter);
    hook_method(device, 134, 14, reinterpret_cast<void*>(get_chain));
    hook_method(device, 134, 18, reinterpret_cast<void*>(get_buffer));
    hook_method(device, 134, 16, reinterpret_cast<void*>(reset));
    hook_method(device, 134, 132, reinterpret_cast<void*>(reset_ex));
    if (!make_canvas()) {
        release_canvas();
        device->Release(); device = nullptr; *output = nullptr;
        return D3DERR_NOTAVAILABLE;
    }
    D3DDISPLAYMODE actual{};
    if (SUCCEEDED(device->GetDisplayMode(0, &actual))) log("Reported display mode: %ux%u @ %u", actual.Width, actual.Height, actual.RefreshRate);
    fit_main_window(pp);
    return hr;
}
void wrap_factory(IDirect3D9Ex* factory) {
    hook_method(factory, 22, 20, reinterpret_cast<void*>(create_device));
}
}
