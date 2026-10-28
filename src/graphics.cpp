#include "mod.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <memory>

namespace gd {
using FactoryCreate = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                                  D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
using GetBuffer = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**);
using GetChain = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, UINT, IDirect3DSwapChain9**);
using ChainBuffer = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**);
using ChainRelease = ULONG(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*);
using ResetEx = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
using Reset = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*);
using SetRenderTarget = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, DWORD, IDirect3DSurface9*);
using SetViewport = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, const D3DVIEWPORT9*);
using GetViewport = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DVIEWPORT9*);
using SetScissor = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, const RECT*);
using GetScissor = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, RECT*);
using Clear = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, DWORD, const D3DRECT*, DWORD, DWORD, float, DWORD);
using GetFVF = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, DWORD*);
using DrawPrimitiveUP = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using SetVertexShaderConstantF = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, UINT, const float*, UINT);

static IDirect3DDevice9Ex* device = nullptr;
static IDirect3DSurface9* canvas = nullptr;
static bool canvas_bound = false;
static bool canvas_is_backbuffer = false;
static thread_local std::vector<uint8_t> native_up_vertices;
static thread_local std::vector<D3DRECT> native_clear_rects;
static std::atomic_flag warned_unhandled_up = ATOMIC_FLAG_INIT;
static HWND main_window = nullptr;
static HMONITOR main_monitor = nullptr;
static unsigned logical_width = 3840, logical_height = 2160;
static std::vector<IDirect3DSwapChain9*> main_chains;
static thread_local bool internal_call = false;
static bool native_target(IDirect3DDevice9Ex* object) {
    return config.native_rendering && object == device && canvas_bound;
}
static int64_t floor_ratio(int64_t value, unsigned numerator, unsigned denominator) {
    const int64_t scaled = value * numerator;
    const int64_t quotient = scaled / denominator;
    return quotient - (scaled % denominator < 0 ? 1 : 0);
}
static int64_t ceil_ratio(int64_t value, unsigned numerator, unsigned denominator) {
    const int64_t scaled = value * numerator;
    const int64_t quotient = scaled / denominator;
    return quotient + (scaled % denominator > 0 ? 1 : 0);
}
static void scale_viewport(D3DVIEWPORT9& viewport, unsigned width, unsigned height) {
    const uint64_t right = uint64_t(viewport.X) + viewport.Width;
    const uint64_t bottom = uint64_t(viewport.Y) + viewport.Height;
    const UINT left = UINT(floor_ratio(viewport.X, width, logical_width));
    const UINT top = UINT(floor_ratio(viewport.Y, height, logical_height));
    viewport.Width = UINT(ceil_ratio(int64_t(right), width, logical_width)) - left;
    viewport.Height = UINT(ceil_ratio(int64_t(bottom), height, logical_height)) - top;
    viewport.X = left;
    viewport.Y = top;
}
static void unscale_viewport(D3DVIEWPORT9& viewport) {
    const uint64_t right = uint64_t(viewport.X) + viewport.Width;
    const uint64_t bottom = uint64_t(viewport.Y) + viewport.Height;
    const UINT left = UINT(floor_ratio(viewport.X, logical_width, config.width));
    const UINT top = UINT(floor_ratio(viewport.Y, logical_height, config.height));
    viewport.Width = UINT(ceil_ratio(int64_t(right), logical_width, config.width)) - left;
    viewport.Height = UINT(ceil_ratio(int64_t(bottom), logical_height, config.height)) - top;
    viewport.X = left;
    viewport.Y = top;
}
static void scale_rect(RECT& rect, unsigned width, unsigned height) {
    rect.left = LONG(floor_ratio(rect.left, width, logical_width));
    rect.right = LONG(ceil_ratio(rect.right, width, logical_width));
    rect.top = LONG(floor_ratio(rect.top, height, logical_height));
    rect.bottom = LONG(ceil_ratio(rect.bottom, height, logical_height));
}
static void unscale_rect(RECT& rect) {
    rect.left = LONG(floor_ratio(rect.left, logical_width, config.width));
    rect.right = LONG(ceil_ratio(rect.right, logical_width, config.width));
    rect.top = LONG(floor_ratio(rect.top, logical_height, config.height));
    rect.bottom = LONG(ceil_ratio(rect.bottom, logical_height, config.height));
}
struct InternalCall {
    bool previous = internal_call;
    InternalCall() {
        internal_call = true;
    }
    ~InternalCall() {
        internal_call = previous;
    }
};

void release_canvas() {
    canvas_bound = false;
    canvas_is_backbuffer = false;
    if (canvas) {
        canvas->Release();
        canvas = nullptr;
    }
}
IDirect3DSurface9* physical_backbuffer() {
    InternalCall bypass;
    IDirect3DSurface9* surface = nullptr;
    if (device)
        reinterpret_cast<GetBuffer>(original_method(device, 18))(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &surface);
    return surface;
}
static bool get_main_present_parameters(D3DPRESENT_PARAMETERS& parameters) {
    auto get_chain = reinterpret_cast<GetChain>(original_method(device, 14));
    IDirect3DSwapChain9* chain = nullptr;
    if (FAILED(get_chain(device, 0, &chain)) || !chain)
        return false;
    const HRESULT hr = chain->GetPresentParameters(&parameters);
    chain->Release();
    return SUCCEEDED(hr);
}
static bool make_canvas() {
    InternalCall bypass;
    if (canvas)
        return true;
    auto physical = physical_backbuffer();
    if (!physical)
        return false;
    D3DSURFACE_DESC desc{};
    const HRESULT description = physical->GetDesc(&desc);
    if (FAILED(description)) {
        physical->Release();
        return false;
    }
    const unsigned canvas_width = config.native_rendering ? desc.Width : logical_width;
    const unsigned canvas_height = config.native_rendering ? desc.Height : logical_height;
    if (desc.Width != config.width || desc.Height != config.height || desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        log("ERROR: physical output differs from requested dimensions, or MSAA is enabled. Check Spice display "
            "overrides.");
        physical->Release();
        return false;
    }

    D3DPRESENT_PARAMETERS parameters{};
    const bool have_parameters = config.native_rendering && get_main_present_parameters(parameters);
    const bool supported_effect =
        have_parameters && (parameters.SwapEffect == D3DSWAPEFFECT_FLIP || parameters.SwapEffect == D3DSWAPEFFECT_COPY);
    const bool matching_parameters = have_parameters && parameters.BackBufferWidth == desc.Width &&
                                     parameters.BackBufferHeight == desc.Height &&
                                     parameters.MultiSampleType == D3DMULTISAMPLE_NONE;
    if (supported_effect && matching_parameters) {
        canvas = physical;
        canvas_is_backbuffer = true;
        log("Native main target reuses physical backbuffer: %ux%u; swap effect=%u, buffers=%u; logical=%ux%u",
            desc.Width, desc.Height, unsigned(parameters.SwapEffect), parameters.BackBufferCount, logical_width,
            logical_height);
    } else {
        physical->Release();
        log("Main canvas uses intermediate target: physical=%ux%u; canvas=%ux%u; logical=%ux%u; "
            "native=%u, presentParameters=%u, swapEffect=%u, buffers=%u, matching=%u",
            desc.Width, desc.Height, canvas_width, canvas_height, logical_width, logical_height,
            unsigned(config.native_rendering), unsigned(have_parameters), unsigned(parameters.SwapEffect),
            parameters.BackBufferCount, unsigned(matching_parameters));
        HRESULT hr = device->CreateRenderTarget(canvas_width, canvas_height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                &canvas, nullptr);
        if (FAILED(hr)) {
            log("ERROR: CreateRenderTarget canvas failed: %08lx", hr);
            return false;
        }
    }
    HRESULT hr = device->SetRenderTarget(0, canvas);
    if (FAILED(hr)) {
        release_canvas();
        return false;
    }
    canvas_bound = true;
    D3DVIEWPORT9 viewport{0, 0, canvas_width, canvas_height, 0.0f, 1.0f};
    hr = device->SetViewport(&viewport);
    if (FAILED(hr)) {
        log("ERROR: setting initial main canvas viewport failed: %08lx", hr);
        release_canvas();
        return false;
    }
    return true;
}
bool compose() {
    InternalCall bypass;
    if (!device || !canvas)
        return false;
    if (canvas_is_backbuffer)
        return true;
    auto physical = physical_backbuffer();
    if (!physical)
        return false;
    const HRESULT hr = device->StretchRect(canvas, nullptr, physical, nullptr, D3DTEXF_LINEAR);
    physical->Release();
    if (FAILED(hr)) {
        static unsigned errors = 0;
        if (errors++ < 8)
            log("ERROR: main output StretchRect failed: %08lx", hr);
    }
    return SUCCEEDED(hr);
}
bool save_surface_bmp(IDirect3DDevice9Ex* owner, IDirect3DSurface9* surface, const wchar_t* path) {
    D3DSURFACE_DESC desc{};
    if (!owner || !surface || FAILED(surface->GetDesc(&desc)) ||
        (desc.Format != D3DFMT_X8R8G8B8 && desc.Format != D3DFMT_A8R8G8B8))
        return false;
    IDirect3DSurface9* readable = nullptr;
    if (FAILED(owner->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &readable,
                                                  nullptr)))
        return false;
    const auto cleanup = [](IDirect3DSurface9* p) { p->Release(); };
    std::unique_ptr<IDirect3DSurface9, decltype(cleanup)> held(readable, cleanup);
    if (FAILED(owner->GetRenderTargetData(surface, readable)))
        return false;
    D3DLOCKED_RECT lock{};
    if (FAILED(readable->LockRect(&lock, nullptr, D3DLOCK_READONLY)))
        return false;
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
        if (stride > sizeof(row))
            ok = false;
        for (unsigned y = 0; ok && y < desc.Height; ++y) {
            const auto source = static_cast<const uint8_t*>(lock.pBits) + y * lock.Pitch;
            for (unsigned x = 0; x < desc.Width; ++x) {
                row[x * 3] = source[x * 4];
                row[x * 3 + 1] = source[x * 4 + 1];
                row[x * 3 + 2] = source[x * 4 + 2];
            }
            ok = fwrite(row, stride, 1, file) == 1;
        }
        if (fclose(file) != 0)
            ok = false;
    }
    readable->UnlockRect();
    return ok;
}
static HRESULT STDMETHODCALLTYPE set_render_target(IDirect3DDevice9Ex* object, DWORD index,
                                                   IDirect3DSurface9* surface) {
    auto original = reinterpret_cast<SetRenderTarget>(original_method(object, 37));
    const HRESULT hr = original(object, index, surface);
    if (object == device && index == 0 && SUCCEEDED(hr))
        canvas_bound = surface == canvas;
    return hr;
}
static HRESULT STDMETHODCALLTYPE set_viewport(IDirect3DDevice9Ex* object, const D3DVIEWPORT9* viewport) {
    auto original = reinterpret_cast<SetViewport>(original_method(object, 47));
    if (!viewport || internal_call || !native_target(object))
        return original(object, viewport);
    D3DVIEWPORT9 adjusted = *viewport;
    scale_viewport(adjusted, config.width, config.height);
    return original(object, &adjusted);
}
static HRESULT STDMETHODCALLTYPE get_viewport(IDirect3DDevice9Ex* object, D3DVIEWPORT9* viewport) {
    auto original = reinterpret_cast<GetViewport>(original_method(object, 48));
    const HRESULT hr = original(object, viewport);
    if (SUCCEEDED(hr) && viewport && !internal_call && native_target(object))
        unscale_viewport(*viewport);
    return hr;
}
static HRESULT STDMETHODCALLTYPE set_scissor(IDirect3DDevice9Ex* object, const RECT* rect) {
    auto original = reinterpret_cast<SetScissor>(original_method(object, 75));
    if (!rect || internal_call || !native_target(object))
        return original(object, rect);
    RECT adjusted = *rect;
    scale_rect(adjusted, config.width, config.height);
    return original(object, &adjusted);
}
static HRESULT STDMETHODCALLTYPE get_scissor(IDirect3DDevice9Ex* object, RECT* rect) {
    auto original = reinterpret_cast<GetScissor>(original_method(object, 76));
    const HRESULT hr = original(object, rect);
    if (SUCCEEDED(hr) && rect && !internal_call && native_target(object))
        unscale_rect(*rect);
    return hr;
}
static HRESULT STDMETHODCALLTYPE clear(IDirect3DDevice9Ex* object, DWORD count, const D3DRECT* rects, DWORD flags,
                                       DWORD color, float z, DWORD stencil) {
    auto original = reinterpret_cast<Clear>(original_method(object, 43));
    if (!count || !rects || internal_call || !native_target(object))
        return original(object, count, rects, flags, color, z, stencil);
    // The game normally supplies a small rect list. Preserve large lists without
    // imposing a fixed limit on diagnostic or future callers.
    D3DRECT stack_rects[16];
    D3DRECT* adjusted = stack_rects;
    if (count > sizeof(stack_rects) / sizeof(stack_rects[0])) {
        try {
            native_clear_rects.resize(count);
        } catch (...) {
            return E_OUTOFMEMORY;
        }
        adjusted = native_clear_rects.data();
    }
    for (DWORD i = 0; i < count; ++i) {
        adjusted[i] = rects[i];
        RECT converted{adjusted[i].x1, adjusted[i].y1, adjusted[i].x2, adjusted[i].y2};
        scale_rect(converted, config.width, config.height);
        adjusted[i] = {converted.left, converted.top, converted.right, converted.bottom};
    }
    return original(object, count, adjusted, flags, color, z, stencil);
}
static HRESULT STDMETHODCALLTYPE draw_primitive_up(IDirect3DDevice9Ex* object, D3DPRIMITIVETYPE type,
                                                   UINT primitive_count, const void* vertices, UINT stride) {
    auto original = reinterpret_cast<DrawPrimitiveUP>(original_method(object, 83));
    if (!vertices || !primitive_count || internal_call || !native_target(object))
        return original(object, type, primitive_count, vertices, stride);
    DWORD fvf = 0;
    auto get_fvf = reinterpret_cast<GetFVF>(original_method(object, 90));
    if (FAILED(get_fvf(object, &fvf)) || (fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW)
        return original(object, type, primitive_count, vertices, stride);
    if (stride < 16) {
        if (!warned_unhandled_up.test_and_set())
            log("WARNING: native main canvas encountered short XYZRHW DrawPrimitiveUP stride %u; "
                "those vertices remain unscaled",
                stride);
        return original(object, type, primitive_count, vertices, stride);
    }
    uint64_t vertex_count = primitive_count;
    switch (type) {
    case D3DPT_POINTLIST:
        break;
    case D3DPT_LINELIST:
        vertex_count *= 2;
        break;
    case D3DPT_LINESTRIP:
        vertex_count += 1;
        break;
    case D3DPT_TRIANGLELIST:
        vertex_count *= 3;
        break;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        vertex_count += 2;
        break;
    default:
        return original(object, type, primitive_count, vertices, stride);
    }
    if (vertex_count > std::numeric_limits<size_t>::max() / stride)
        return E_OUTOFMEMORY;
    const size_t bytes = size_t(vertex_count) * stride;
    try {
        native_up_vertices.resize(bytes);
    } catch (...) {
        if (!warned_unhandled_up.test_and_set())
            log("ERROR: native main canvas could not allocate reusable XYZRHW scratch storage");
        return E_OUTOFMEMORY;
    }
    std::memcpy(native_up_vertices.data(), vertices, bytes);
    const float sx = float(config.width) / float(logical_width);
    const float sy = float(config.height) / float(logical_height);
    for (uint64_t i = 0; i < vertex_count; ++i) {
        auto* vertex = native_up_vertices.data() + size_t(i) * stride;
        float x, y;
        std::memcpy(&x, vertex, sizeof(x));
        std::memcpy(&y, vertex + sizeof(x), sizeof(y));
        // Preserve D3D's pixel-center convention while changing the pixel grid.
        x = (x + 0.5f) * sx - 0.5f;
        y = (y + 0.5f) * sy - 0.5f;
        std::memcpy(vertex, &x, sizeof(x));
        std::memcpy(vertex + sizeof(x), &y, sizeof(y));
    }
    return original(object, type, primitive_count, native_up_vertices.data(), stride);
}
static HRESULT STDMETHODCALLTYPE set_vertex_shader_constant_f(IDirect3DDevice9Ex* object, UINT start_register,
                                                              const float* values, UINT vector4f_count) {
    auto original = reinterpret_cast<SetVertexShaderConstantF>(original_method(object, 94));
    if (!game || !values || internal_call || !native_target(object) || start_register != 0 || vector4f_count != 4)
        return original(object, start_register, values, vector4f_count);

    const auto& profile = target::get();
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) !=
        reinterpret_cast<uintptr_t>(at(profile.font_projection_constants_return)))
        return original(object, start_register, values, vector4f_count);

    float adjusted[16];
    memcpy(adjusted, values, sizeof(adjusted));
    // Keep dynamic font vertices in the 3840x2160 logical canvas coordinate space.
    adjusted[0] = 2.0f / float(logical_width);
    adjusted[5] = -2.0f / float(logical_height);
    static std::atomic_flag logged = ATOMIC_FLAG_INIT;
    if (!logged.test_and_set())
        log("Native font projection corrected at %p: c0.x %g -> %g, c1.y %g -> %g", _ReturnAddress(), values[0],
            adjusted[0], values[5], adjusted[5]);
    return original(object, start_register, adjusted, vector4f_count);
}
void capture_diagnostic_frame() {
    static bool previously_down = false;
    const bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                      (GetAsyncKeyState(VK_F8) & 0x8000);
    const bool pressed = down && !previously_down;
    previously_down = down;
    DWORD foreground_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
    if (!pressed || foreground_process != GetCurrentProcessId() || !device || !canvas)
        return;
    InternalCall bypass;
    wchar_t directory[MAX_PATH]{};
    GetModuleFileNameW(self, directory, MAX_PATH);
    auto slash = wcsrchr(directory, L'\\');
    if (!slash)
        return;
    slash[1] = 0;
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t name[128]{};
    swprintf_s(name, L"notes-%04u%02u%02u-%02u%02u%02u-%03u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond, now.wMilliseconds);
    const auto prefix = std::wstring(directory) + name;
    const auto logical_path = prefix + (config.native_rendering ? L"-canvas.bmp" : L"-4k.bmp");
    const auto physical_path = prefix + L"-1080p.bmp";
    const bool logical_saved = save_surface_bmp(device, canvas, logical_path.c_str());
    auto physical = physical_backbuffer();
    const bool physical_saved = save_surface_bmp(device, physical, physical_path.c_str());
    if (physical)
        physical->Release();
    log("NOTE capture %ls: main canvas=%s, physical output=%s", prefix.c_str(), logical_saved ? "OK" : "FAILED",
        physical_saved ? "OK" : "FAILED");
}
static HRESULT STDMETHODCALLTYPE get_buffer(IDirect3DDevice9Ex* object, UINT chain, UINT index, D3DBACKBUFFER_TYPE type,
                                            IDirect3DSurface9** output) {
    if (!internal_call && object == device && chain == 0 && index == 0 && type == D3DBACKBUFFER_TYPE_MONO && output) {
        if (!make_canvas())
            return D3DERR_NOTAVAILABLE;
        canvas->AddRef();
        *output = canvas;
        return D3D_OK;
    }
    return reinterpret_cast<GetBuffer>(original_method(object, 18))(object, chain, index, type, output);
}
static HRESULT STDMETHODCALLTYPE chain_buffer(IDirect3DSwapChain9* object, UINT index, D3DBACKBUFFER_TYPE type,
                                              IDirect3DSurface9** output) {
    if (!internal_call && index == 0 && type == D3DBACKBUFFER_TYPE_MONO && output &&
        std::find(main_chains.begin(), main_chains.end(), object) != main_chains.end()) {
        if (!make_canvas())
            return D3DERR_NOTAVAILABLE;
        canvas->AddRef();
        *output = canvas;
        return D3D_OK;
    }
    return reinterpret_cast<ChainBuffer>(original_method(object, 5))(object, index, type, output);
}
static ULONG STDMETHODCALLTYPE chain_release(IDirect3DSwapChain9* object) {
    auto original = reinterpret_cast<ChainRelease>(original_method(object, 2));
    const ULONG remaining = original(object);
    if (!remaining)
        main_chains.erase(std::remove(main_chains.begin(), main_chains.end(), object), main_chains.end());
    return remaining;
}
static HRESULT STDMETHODCALLTYPE get_chain(IDirect3DDevice9Ex* object, UINT index, IDirect3DSwapChain9** output) {
    HRESULT hr = reinterpret_cast<GetChain>(original_method(object, 14))(object, index, output);
    if (!internal_call && SUCCEEDED(hr) && output && *output && object == device && index == 0) {
        hook_method(*output, 10, 5, reinterpret_cast<void*>(chain_buffer));
        hook_method(*output, 10, 2, reinterpret_cast<void*>(chain_release));
        if (std::find(main_chains.begin(), main_chains.end(), *output) == main_chains.end())
            main_chains.push_back(*output);
    }
    return hr;
}
struct MainParameters {
    D3DPRESENT_PARAMETERS* pp;
    D3DDISPLAYMODEEX* mode;
    UINT width, height;
    explicit MainParameters(D3DPRESENT_PARAMETERS* p, D3DDISPLAYMODEEX* m)
        : pp(p), mode(m), width(p->BackBufferWidth), height(p->BackBufferHeight) {
        p->BackBufferWidth = config.width;
        p->BackBufferHeight = config.height;
        p->FullScreen_RefreshRateInHz = p->Windowed ? 0 : config.refresh;
        p->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        if (m && !p->Windowed) {
            m->Width = config.width;
            m->Height = config.height;
            m->RefreshRate = config.refresh;
        }
    }
    ~MainParameters() {
        // The game retains logical dimensions; physical dimensions belong to D3D.
        pp->BackBufferWidth = width;
        pp->BackBufferHeight = height;
        if (mode) {
            mode->Width = width;
            mode->Height = height;
        }
    }
};
bool fit_output_window(HWND window, bool windowed, HMONITOR monitor) {
    if (!window)
        return false;
    RECT before{};
    if (!GetClientRect(window, &before))
        return false;
    RECT outer{0, 0, LONG(config.width), LONG(config.height)};
    if (!AdjustWindowRectEx(&outer, DWORD(GetWindowLongPtrW(window, GWL_STYLE)), GetMenu(window) != nullptr,
                            DWORD(GetWindowLongPtrW(window, GWL_EXSTYLE))))
        return false;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    LONG x = 0, y = 0;
    if (windowed) {
        flags |= SWP_NOMOVE;
    } else {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!monitor || !GetMonitorInfoW(monitor, &info)) {
            monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            if (!GetMonitorInfoW(monitor, &info))
                return false;
        }
        x = info.rcMonitor.left + outer.left;
        y = info.rcMonitor.top + outer.top;
    }
    // D3DCREATE_NOWINDOWCHANGES leaves the game's original 4K HWND intact
    // in fullscreen. WM_MOUSE/WM_TOUCH normalize by GetClientRect, so that
    // stale size makes a 1080p pointer reach only half of each logical axis.
    // Change the real window, allowing Spice's normal resize/touch handling
    // to observe the same dimensions. Do not rescale input a second time.
    if (!SetWindowPos(window, nullptr, x, y, outer.right - outer.left, outer.bottom - outer.top, flags)) {
        log("ERROR: main window resize failed: %lu", GetLastError());
        return false;
    }
    RECT after{};
    if (!GetClientRect(window, &after))
        return false;
    log("Main input client (%s): %ldx%ld -> %ldx%ld; requested %ux%u", windowed ? "windowed" : "fullscreen",
        before.right - before.left, before.bottom - before.top, after.right - after.left, after.bottom - after.top,
        config.width, config.height);
    const LONG width = after.right - after.left, height = after.bottom - after.top;
    // Windows can cap a decorated window at the monitor's maximum tracking
    // size. Windowed Present scales to that actual client and the game's
    // existing normalization follows it. Fullscreen must match exactly.
    if (windowed)
        return width > 0 && height > 0 && width <= LONG(config.width) && height <= LONG(config.height);
    return width == LONG(config.width) && height == LONG(config.height);
}
static void fit_main_window(D3DPRESENT_PARAMETERS* pp) {
    if (pp->hDeviceWindow)
        main_window = pp->hDeviceWindow;
    if (!fit_output_window(main_window, pp->Windowed != FALSE, main_monitor))
        log("ERROR: main input client does not match physical output; check window sizing overrides");
}
static HRESULT STDMETHODCALLTYPE reset_ex(IDirect3DDevice9Ex* object, D3DPRESENT_PARAMETERS* pp,
                                          D3DDISPLAYMODEEX* mode) {
    auto original = reinterpret_cast<ResetEx>(original_method(object, 132));
    if (object != device || !pp)
        return original(object, pp, mode);
    release_canvas();
    main_chains.clear();
    MainParameters adjusted(pp, mode);
    HRESULT hr;
    {
        InternalCall bypass;
        hr = original(object, pp, pp->Windowed ? nullptr : mode);
    }
    log("ResetEx returned %08lx", hr);
    if (SUCCEEDED(hr)) {
        fit_main_window(pp);
        if (!make_canvas())
            return D3DERR_NOTAVAILABLE;
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9Ex* object, D3DPRESENT_PARAMETERS* pp) {
    auto original = reinterpret_cast<Reset>(original_method(object, 16));
    if (object != device || !pp)
        return original(object, pp);
    release_canvas();
    main_chains.clear();
    MainParameters adjusted(pp, nullptr);
    HRESULT hr;
    {
        InternalCall bypass;
        hr = original(object, pp);
    }
    log("Reset returned %08lx", hr);
    if (SUCCEEDED(hr)) {
        fit_main_window(pp);
        if (!make_canvas())
            return D3DERR_NOTAVAILABLE;
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE create_device(IDirect3D9Ex* object, UINT adapter, D3DDEVTYPE type, HWND window,
                                               DWORD flags, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode,
                                               IDirect3DDevice9Ex** output) {
    auto original = reinterpret_cast<FactoryCreate>(original_method(object, 20));
    if (!pp || !output || pp->BackBufferWidth != 3840 || pp->BackBufferHeight != 2160)
        return original(object, adapter, type, window, flags, pp, mode, output);
    // Modify only head 0. Spice keeps responsibility for cabinet head emulation.
    logical_width = pp->BackBufferWidth;
    logical_height = pp->BackBufferHeight;
    MainParameters adjusted(pp, mode);
    log("CreateDeviceEx main: output %ux%u @ %u, windowed=%d", config.width, config.height, config.refresh,
        pp->Windowed);
    HRESULT hr = original(object, adapter, type, window, flags, pp, pp->Windowed ? nullptr : mode, output);
    if (FAILED(hr)) {
        log("ERROR: CreateDeviceEx failed: %08lx", hr);
        return hr;
    }
    release_canvas();
    main_chains.clear();
    device = *output;
    main_window = pp->hDeviceWindow ? pp->hDeviceWindow : window;
    main_monitor = object->GetAdapterMonitor(adapter);
    hook_method(device, 134, 14, reinterpret_cast<void*>(get_chain));
    hook_method(device, 134, 18, reinterpret_cast<void*>(get_buffer));
    hook_method(device, 134, 16, reinterpret_cast<void*>(reset));
    hook_method(device, 134, 132, reinterpret_cast<void*>(reset_ex));
    if (config.native_rendering) {
        hook_method(device, 134, 37, reinterpret_cast<void*>(set_render_target));
        hook_method(device, 134, 43, reinterpret_cast<void*>(clear));
        hook_method(device, 134, 47, reinterpret_cast<void*>(set_viewport));
        hook_method(device, 134, 48, reinterpret_cast<void*>(get_viewport));
        hook_method(device, 134, 75, reinterpret_cast<void*>(set_scissor));
        hook_method(device, 134, 76, reinterpret_cast<void*>(get_scissor));
        hook_method(device, 134, 83, reinterpret_cast<void*>(draw_primitive_up));
        hook_method(device, 134, 94, reinterpret_cast<void*>(set_vertex_shader_constant_f));
    }
    if (!make_canvas()) {
        release_canvas();
        device->Release();
        device = nullptr;
        *output = nullptr;
        return D3DERR_NOTAVAILABLE;
    }
    D3DDISPLAYMODE actual{};
    if (SUCCEEDED(device->GetDisplayMode(0, &actual)))
        log("Reported display mode: %ux%u @ %u", actual.Width, actual.Height, actual.RefreshRate);
    fit_main_window(pp);
    return hr;
}
void wrap_factory(IDirect3D9Ex* factory) {
    hook_method(factory, 22, 20, reinterpret_cast<void*>(create_device));
}
} // namespace gd
