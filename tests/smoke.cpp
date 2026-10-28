#include "../src/mod.h"
#include "../src/timing_math.h"
#include "../src/config.h"
#include "../src/live2d.h"
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <vector>
#include "../third_party/minhook/include/MinHook.h"

static void check(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}
#include "animations.h"
#include "timing.h"
#include "scene_timing.h"
#include "inline_hook.h"
#include "selection_diagnostics.h"
#include "selection_motion.h"
static void hr(HRESULT result, const char* text) {
    if (FAILED(result)) {
        printf("HRESULT %08lx: %s\n", result, text);
        throw std::runtime_error(text);
    }
}
static void check_input_extent(HWND window, bool windowed = false) {
    RECT client{};
    check(GetClientRect(window, &client) != FALSE, "input client rectangle available");
    if (windowed)
        check(client.right > 0 && client.bottom > 0 && client.right <= 1920 && client.bottom <= 1080,
              "windowed client fits within output extent (Windows may constrain decorations)");
    else
        check(client.right == 1920 && client.bottom == 1080, "fullscreen input client matches physical 1080p output");
    // The real game normalizes WM_MOUSE/WM_TOUCH coordinates to 800x1280.
    // On a 1080p screen with a stale 4K HWND these max out at 399 and 639.
    const int input_x = int(float(client.right - 1) / client.right * 800.0f);
    const int input_y = int(float(client.bottom - 1) / client.bottom * 1280.0f);
    check(input_x == 799 && input_y >= 1278, "physical right/bottom reach full game input range");
}
static void fullscreen_window_test() {
    HWND window = CreateWindowExW(0, L"STATIC", L"gd-arena-fix hidden fullscreen geometry test", WS_POPUP, 0, 0, 3840,
                                  2160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window != nullptr, "hidden fullscreen-style window");
    RECT before{};
    GetClientRect(window, &before);
    check(before.right == 3840 && before.bottom == 2160, "reproduce stale 4K fullscreen client");
    check(int(1919.0f / before.right * 800.0f) == 399, "reproduce half-width input reach");
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    check(gd::fit_output_window(window, false, monitor), "resize fullscreen client independently of backbuffer");
    check_input_extent(window);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    check(GetMonitorInfoW(monitor, &info) != FALSE, "fullscreen target monitor");
    POINT origin{};
    check(ClientToScreen(window, &origin) != FALSE && origin.x == info.rcMonitor.left && origin.y == info.rcMonitor.top,
          "fullscreen client starts at target monitor origin");
    DestroyWindow(window);
    puts("PASS: fullscreen-style 4K HWND resized to 1080p, pointer reaches right/bottom, correct monitor origin");
}
static std::vector<uint32_t> read_surface(IDirect3DDevice9Ex* device, IDirect3DSurface9* surface,
                                          const D3DSURFACE_DESC& desc) {
    IDirect3DSurface9* readable = nullptr;
    hr(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &readable, nullptr),
       "create readback surface");
    hr(device->GetRenderTargetData(surface, readable), "read surface data");
    D3DLOCKED_RECT lock{};
    hr(readable->LockRect(&lock, nullptr, D3DLOCK_READONLY), "lock surface readback");
    std::vector<uint32_t> pixels(size_t(desc.Width) * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        memcpy(pixels.data() + size_t(y) * desc.Width, static_cast<const uint8_t*>(lock.pBits) + size_t(y) * lock.Pitch,
               size_t(desc.Width) * 4);
    readable->UnlockRect();
    readable->Release();
    return pixels;
}
static uint32_t pixel(const std::vector<uint32_t>& pixels, const D3DSURFACE_DESC& desc, unsigned x, unsigned y) {
    check(x < desc.Width && y < desc.Height, "pixel lies within surface");
    return pixels[size_t(y) * desc.Width + x] & 0xffffff;
}
static void native_rendering_test(IDirect3DDevice9Ex* device, IDirect3DSurface9* logical, const D3DSURFACE_DESC& desc) {
    check(desc.Width == 1920 && desc.Height == 1080, "native main canvas has physical dimensions");
    D3DVIEWPORT9 observed_viewport{};
    hr(device->GetViewport(&observed_viewport), "get viewport immediately after native canvas creation");
    check(observed_viewport.X == 0 && observed_viewport.Y == 0 && observed_viewport.Width == 3840 &&
              observed_viewport.Height == 2160,
          "initial native viewport is reported in logical 4K coordinates");
    D3DVIEWPORT9 viewport{0, 0, 3840, 2160, 0.0f, 1.0f};
    hr(device->SetViewport(&viewport), "set logical viewport");
    hr(device->GetViewport(&observed_viewport), "get logical viewport");
    check(observed_viewport.X == viewport.X && observed_viewport.Y == viewport.Y &&
              observed_viewport.Width == viewport.Width && observed_viewport.Height == viewport.Height,
          "viewport reads in logical coordinates");
    RECT scissor{0, 0, 1920, 1080};
    hr(device->SetScissorRect(&scissor), "set logical scissor");
    RECT observed_scissor{};
    hr(device->GetScissorRect(&observed_scissor), "get logical scissor");
    check(memcmp(&scissor, &observed_scissor, sizeof(scissor)) == 0, "scissor reads in logical coordinates");
    RECT oversized_scissor{0, 0, 7680, 4320};
    hr(device->SetScissorRect(&oversized_scissor), "set oversized logical AFP scissor");
    hr(device->GetScissorRect(&observed_scissor), "get oversized logical AFP scissor");
    check(memcmp(&oversized_scissor, &observed_scissor, sizeof(oversized_scissor)) == 0,
          "oversized AFP scissor is accepted and remains in logical coordinates");
    hr(device->SetScissorRect(&scissor), "restore half-size logical scissor");

    IDirect3DStateBlock9* block = nullptr;
    hr(device->CreateStateBlock(D3DSBT_ALL, &block), "create native state block");
    D3DVIEWPORT9 other_viewport{320, 216, 960, 540, 0.0f, 1.0f};
    RECT other_scissor{320, 216, 1280, 756};
    hr(device->SetViewport(&other_viewport), "change viewport before state capture");
    hr(device->SetScissorRect(&other_scissor), "change scissor before state capture");
    hr(block->Capture(), "capture native state block");
    hr(device->SetViewport(&viewport), "change viewport after state capture");
    RECT changed_scissor{0, 0, 3840, 2160};
    hr(device->SetScissorRect(&changed_scissor), "change scissor after state capture");
    hr(block->Apply(), "apply native state block");
    block->Release();
    hr(device->GetViewport(&observed_viewport), "get viewport after state restore");
    hr(device->GetScissorRect(&observed_scissor), "get scissor after state restore");
    check(observed_viewport.X == other_viewport.X && observed_viewport.Y == other_viewport.Y &&
              observed_viewport.Width == other_viewport.Width && observed_viewport.Height == other_viewport.Height,
          "state block restores viewport once in logical coordinates");
    check(memcmp(&other_scissor, &observed_scissor, sizeof(other_scissor)) == 0,
          "state block restores scissor once in logical coordinates");

    hr(device->SetRenderTarget(0, logical), "bind native canvas");
    viewport = {0, 0, 3840, 2160, 0.0f, 1.0f};
    hr(device->SetViewport(&viewport), "set full logical viewport for projected drawing");
    scissor = {0, 0, 1920, 1080};
    hr(device->SetScissorRect(&scissor), "set half-size logical scissor");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "enable scissor test");
    hr(device->SetRenderState(D3DRS_LIGHTING, FALSE), "disable fixed-function lighting");
    hr(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "disable triangle culling");
    hr(device->SetRenderState(D3DRS_ZENABLE, FALSE), "disable depth testing for smoke geometry");
    D3DMATRIX identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    hr(device->SetTransform(D3DTS_WORLD, &identity), "set identity world transform");
    hr(device->SetTransform(D3DTS_VIEW, &identity), "set identity view transform");
    hr(device->SetTransform(D3DTS_PROJECTION, &identity), "set identity projection transform");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor while testing Clear rectangles");
    hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff0000ff, 1.0f, 0), "clear native canvas blue");
    D3DRECT logical_clear{0, 0, 1920, 1080};
    hr(device->Clear(1, &logical_clear, D3DCLEAR_TARGET, 0xffff0000, 1.0f, 0), "clear logical half-size rectangle");
    auto clear_pixels = read_surface(device, logical, desc);
    check(pixel(clear_pixels, desc, 900, 400) == 0xff0000 && pixel(clear_pixels, desc, 1200, 400) == 0x0000ff &&
              pixel(clear_pixels, desc, 900, 700) == 0x0000ff,
          "clear rectangle scales from logical to native coordinates");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor for full canvas clear");
    hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0), "clear native canvas black");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "enable scissor for projected drawing");
    struct Vertex {
        float x, y, z;
        D3DCOLOR color;
    };
    const Vertex triangle[]{
        {-1.0f, -1.0f, 0.5f, 0xff00ff00}, {1.0f, -1.0f, 0.5f, 0xff00ff00}, {0.0f, 1.0f, 0.5f, 0xff00ff00}};
    hr(device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE), "set projected triangle format");
    hr(device->BeginScene(), "begin projected drawing");
    hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(Vertex)), "draw projected triangle");
    hr(device->EndScene(), "end projected drawing");
    auto pixels = read_surface(device, logical, desc);
    check(pixel(pixels, desc, 900, 200) == 0x00ff00, "logical projection reaches expected native pixel");
    check(pixel(pixels, desc, 1200, 200) == 0x000000, "logical scissor clips projected native drawing");
    check(pixel(pixels, desc, 480, 700) == 0x000000, "logical scissor clips projected drawing vertically");

    // The game also submits XYZRHW vertices in its 4K screen coordinate space.
    // This FVF is 28 bytes: XYZRHW, diffuse color, and one texture coordinate.
    struct TransformedVertex {
        float x, y, z, rhw;
        D3DCOLOR color;
        float u, v;
    };
    static_assert(sizeof(TransformedVertex) == 28);
    const TransformedVertex logical_quad[]{
        {2800.5f, 1400.5f, 0.5f, 1.0f, 0xff00ff00, 0, 0}, {3400.5f, 1400.5f, 0.5f, 1.0f, 0xff00ff00, 1, 0},
        {2800.5f, 2000.5f, 0.5f, 1.0f, 0xff00ff00, 0, 1}, {3400.5f, 1400.5f, 0.5f, 1.0f, 0xff00ff00, 1, 0},
        {3400.5f, 2000.5f, 0.5f, 1.0f, 0xff00ff00, 1, 1}, {2800.5f, 2000.5f, 0.5f, 1.0f, 0xff00ff00, 0, 1}};
    const std::vector<uint8_t> original_quad(reinterpret_cast<const uint8_t*>(logical_quad),
                                             reinterpret_cast<const uint8_t*>(logical_quad) + sizeof(logical_quad));
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor for transformed drawing clear");
    hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0), "clear before transformed drawing");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "enable scissor for transformed drawing");
    RECT full_scissor{0, 0, 3840, 2160};
    hr(device->SetScissorRect(&full_scissor), "set full logical scissor for transformed drawing");
    hr(device->SetFVF(0x144), "set game's XYZRHW diffuse texture FVF");
    hr(device->BeginScene(), "begin transformed drawing");
    hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, logical_quad, sizeof(TransformedVertex)),
       "draw 28-byte transformed quad");
    hr(device->EndScene(), "end transformed drawing");
    check(memcmp(logical_quad, original_quad.data(), sizeof(logical_quad)) == 0,
          "native vertex scaling leaves caller vertex memory unchanged");
    pixels = read_surface(device, logical, desc);
    check(pixel(pixels, desc, 1500, 800) == 0x00ff00 && pixel(pixels, desc, 1380, 800) == 0x000000 &&
              pixel(pixels, desc, 1720, 800) == 0x000000,
          "logical XYZRHW quad maps to the lower-right native area");
    check(pixel(pixels, desc, 1399, 800) == 0x000000 && pixel(pixels, desc, 1400, 800) == 0x00ff00 &&
              pixel(pixels, desc, 1699, 800) == 0x00ff00 && pixel(pixels, desc, 1700, 800) == 0x000000,
          "half-pixel mapping places transformed quad edges on native pixel boundaries");

    // Two real AFP paths use the 20-byte XYZRHW + diffuse FVF 0x44.
    struct TransformedColorVertex {
        float x, y, z, rhw;
        D3DCOLOR color;
    };
    static_assert(sizeof(TransformedColorVertex) == 20);
    const TransformedColorVertex logical_triangle[]{{2200.5f, 1000.5f, 0.5f, 1.0f, 0xff00ff00},
                                                    {2600.5f, 1000.5f, 0.5f, 1.0f, 0xff00ff00},
                                                    {2200.5f, 1400.5f, 0.5f, 1.0f, 0xff00ff00}};
    const TransformedColorVertex logical_line[]{{2800.5f, 1800.5f, 0.5f, 1.0f, 0xffffff00},
                                                {3400.5f, 1800.5f, 0.5f, 1.0f, 0xffffff00}};
    const std::vector<uint8_t> original_triangle(reinterpret_cast<const uint8_t*>(logical_triangle),
                                                 reinterpret_cast<const uint8_t*>(logical_triangle) +
                                                     sizeof(logical_triangle));
    const std::vector<uint8_t> original_line(reinterpret_cast<const uint8_t*>(logical_line),
                                             reinterpret_cast<const uint8_t*>(logical_line) + sizeof(logical_line));
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor for 20-byte geometry clear");
    hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0), "clear before 20-byte geometry");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "enable scissor for 20-byte geometry");
    hr(device->SetFVF(0x44), "set game's XYZRHW diffuse FVF");
    hr(device->BeginScene(), "begin 20-byte transformed drawing");
    hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, logical_triangle, sizeof(TransformedColorVertex)),
       "draw 20-byte transformed triangle");
    hr(device->DrawPrimitiveUP(D3DPT_LINELIST, 1, logical_line, sizeof(TransformedColorVertex)),
       "draw 20-byte transformed line");
    hr(device->EndScene(), "end 20-byte transformed drawing");
    check(memcmp(logical_triangle, original_triangle.data(), sizeof(logical_triangle)) == 0 &&
              memcmp(logical_line, original_line.data(), sizeof(logical_line)) == 0,
          "20-byte XYZRHW scaling leaves caller vertex memory unchanged");
    pixels = read_surface(device, logical, desc);
    check(pixel(pixels, desc, 1150, 550) == 0x00ff00 && pixel(pixels, desc, 1500, 900) == 0xffff00,
          "20-byte triangle and line list map from logical to native coordinates");

    // Auxiliary targets retain their original sizes and unscaled D3D state.
    IDirect3DSurface9* auxiliary = nullptr;
    hr(device->CreateRenderTarget(320, 240, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &auxiliary, nullptr),
       "create auxiliary render target");
    D3DSURFACE_DESC auxiliary_desc{};
    hr(auxiliary->GetDesc(&auxiliary_desc), "describe auxiliary target");
    check(auxiliary_desc.Width == 320 && auxiliary_desc.Height == 240, "auxiliary render target size is unchanged");
    D3DVIEWPORT9 small_main_viewport{0, 0, 200, 160, 0.0f, 1.0f};
    hr(device->SetViewport(&small_main_viewport), "reduce viewport before binding auxiliary target");
    hr(device->SetRenderTarget(0, auxiliary), "bind auxiliary target");
    D3DVIEWPORT9 auxiliary_viewport{10, 12, 120, 80, 0.0f, 1.0f};
    hr(device->SetViewport(&auxiliary_viewport), "set auxiliary viewport");
    RECT auxiliary_scissor{4, 6, 200, 180};
    hr(device->SetScissorRect(&auxiliary_scissor), "set auxiliary scissor");
    hr(device->GetViewport(&observed_viewport), "get auxiliary viewport");
    hr(device->GetScissorRect(&observed_scissor), "get auxiliary scissor");
    check(observed_viewport.X == auxiliary_viewport.X && observed_viewport.Y == auxiliary_viewport.Y &&
              observed_viewport.Width == auxiliary_viewport.Width &&
              observed_viewport.Height == auxiliary_viewport.Height,
          "auxiliary viewport remains unscaled");
    check(memcmp(&auxiliary_scissor, &observed_scissor, sizeof(auxiliary_scissor)) == 0,
          "auxiliary scissor remains unscaled");
    D3DVIEWPORT9 full_auxiliary_viewport{0, 0, 320, 240, 0.0f, 1.0f};
    hr(device->SetViewport(&full_auxiliary_viewport), "restore full auxiliary viewport");
    RECT full_auxiliary_scissor{0, 0, 320, 240};
    hr(device->SetScissorRect(&full_auxiliary_scissor), "restore full auxiliary scissor");
    D3DRECT auxiliary_clear{0, 0, 320, 240};
    hr(device->Clear(1, &auxiliary_clear, D3DCLEAR_TARGET, 0xffff0000, 1.0f, 0), "clear auxiliary target");
    const TransformedVertex auxiliary_triangle[]{{40.5f, 40.5f, 0.5f, 1.0f, 0xff00ff00, 0, 0},
                                                 {180.5f, 40.5f, 0.5f, 1.0f, 0xff00ff00, 1, 0},
                                                 {40.5f, 180.5f, 0.5f, 1.0f, 0xff00ff00, 0, 1}};
    const TransformedColorVertex auxiliary_line[]{{40.5f, 30.5f, 0.5f, 1.0f, 0xffffff00},
                                                  {180.5f, 30.5f, 0.5f, 1.0f, 0xffffff00}};
    hr(device->SetFVF(0x44), "set auxiliary 20-byte XYZRHW FVF");
    hr(device->BeginScene(), "begin unscaled auxiliary line");
    hr(device->DrawPrimitiveUP(D3DPT_LINELIST, 1, auxiliary_line, sizeof(TransformedColorVertex)),
       "draw unscaled auxiliary line list");
    hr(device->EndScene(), "end unscaled auxiliary line");
    hr(device->SetFVF(0x144), "set auxiliary XYZRHW FVF");
    hr(device->BeginScene(), "begin auxiliary transformed drawing");
    hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, auxiliary_triangle, sizeof(TransformedVertex)),
       "draw unscaled auxiliary XYZRHW triangle");
    hr(device->EndScene(), "end auxiliary transformed drawing");
    auto auxiliary_pixels = read_surface(device, auxiliary, auxiliary_desc);
    check(pixel(auxiliary_pixels, auxiliary_desc, 160, 120) == 0xff0000 &&
              pixel(auxiliary_pixels, auxiliary_desc, 70, 70) == 0x00ff00 &&
              pixel(auxiliary_pixels, auxiliary_desc, 80, 30) == 0xffff00,
          "auxiliary clear and 20/28-byte transformed vertices remain unscaled");
    auxiliary->Release();

    HWND auxiliary_window = CreateWindowExW(0, L"STATIC", L"gd-arena-fix auxiliary swapchain", WS_OVERLAPPEDWINDOW, 0,
                                            0, 640, 360, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(auxiliary_window != nullptr, "create auxiliary swapchain window");
    D3DPRESENT_PARAMETERS auxiliary_pp{};
    auxiliary_pp.BackBufferWidth = 640;
    auxiliary_pp.BackBufferHeight = 360;
    auxiliary_pp.BackBufferFormat = desc.Format;
    auxiliary_pp.BackBufferCount = 1;
    auxiliary_pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    auxiliary_pp.Windowed = TRUE;
    auxiliary_pp.hDeviceWindow = auxiliary_window;
    IDirect3DSwapChain9* auxiliary_chain = nullptr;
    hr(device->CreateAdditionalSwapChain(&auxiliary_pp, &auxiliary_chain), "create auxiliary swapchain");
    IDirect3DSurface9* auxiliary_backbuffer = nullptr;
    hr(auxiliary_chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &auxiliary_backbuffer),
       "get auxiliary swapchain backbuffer");
    D3DSURFACE_DESC auxiliary_backbuffer_desc{};
    hr(auxiliary_backbuffer->GetDesc(&auxiliary_backbuffer_desc), "describe auxiliary swapchain backbuffer");
    check(auxiliary_backbuffer_desc.Width == 640 && auxiliary_backbuffer_desc.Height == 360,
          "auxiliary swapchain dimensions remain unchanged");
    hr(device->SetRenderTarget(0, auxiliary_backbuffer), "bind auxiliary swapchain backbuffer");
    D3DVIEWPORT9 auxiliary_chain_viewport{10, 12, 120, 80, 0.0f, 1.0f};
    hr(device->SetViewport(&auxiliary_chain_viewport), "set auxiliary swapchain viewport");
    hr(device->GetViewport(&observed_viewport), "get auxiliary swapchain viewport");
    check(observed_viewport.X == auxiliary_chain_viewport.X && observed_viewport.Y == auxiliary_chain_viewport.Y &&
              observed_viewport.Width == auxiliary_chain_viewport.Width &&
              observed_viewport.Height == auxiliary_chain_viewport.Height,
          "auxiliary swapchain viewport remains unscaled");
    auxiliary_backbuffer->Release();
    auxiliary_chain->Release();
    DestroyWindow(auxiliary_window);

    hr(device->SetRenderTarget(0, logical), "restore native canvas after auxiliary target");
    hr(device->GetViewport(&observed_viewport), "get viewport after rebinding native canvas");
    hr(device->GetScissorRect(&observed_scissor), "get scissor after rebinding native canvas");
    check(observed_viewport.X == 0 && observed_viewport.Y == 0 && observed_viewport.Width == 3840 &&
              observed_viewport.Height == 2160,
          "rebinding native canvas restores its full logical viewport after auxiliary target");
    check(observed_scissor.left == 0 && observed_scissor.top == 0 && observed_scissor.right == 3840 &&
              observed_scissor.bottom == 2160,
          "rebinding native canvas restores its full logical scissor after auxiliary target");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor test");
    check(gd::compose(), "compose native canvas to physical output");
    auto physical = gd::physical_backbuffer();
    check(physical != nullptr, "native physical backbuffer available");
    D3DSURFACE_DESC physical_desc{};
    hr(physical->GetDesc(&physical_desc), "describe native physical output");
    check(physical_desc.Width == 1920 && physical_desc.Height == 1080, "native output remains 1080p");
    const auto physical_pixels = read_surface(device, physical, physical_desc);
    check(physical_pixels == pixels, "native composition copies the complete main canvas without auxiliary leakage");
    physical->Release();
}
static void scaling_test(IDirect3DDevice9Ex* device) {
    IDirect3DSurface9* logical = nullptr;
    hr(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &logical), "logical backbuffer");
    D3DSURFACE_DESC desc{};
    hr(logical->GetDesc(&desc), "logical description");
    check(desc.Width == (gd::config.native_rendering ? 1920u : 3840u) &&
              desc.Height == (gd::config.native_rendering ? 1080u : 2160u),
          gd::config.native_rendering ? "native mode exposes 1080p main canvas" : "game sees 4K canvas");
    IDirect3DSwapChain9* chain = nullptr;
    hr(device->GetSwapChain(0, &chain), "get main swapchain");
    IDirect3DSurface9* chain_surface = nullptr;
    hr(chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &chain_surface), "swapchain canvas");
    check(chain_surface == logical, "device and swapchain return same logical canvas");
    chain_surface->Release();
    chain->Release();
    if (gd::config.native_rendering) {
        native_rendering_test(device, logical, desc);
        logical->Release();
        puts("PASS: native 1080p target projection, clipping, state blocks, auxiliary target isolation");
        return;
    }
    hr(device->SetRenderTarget(0, logical), "set logical target");
    const D3DCOLOR colors[]{0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff};
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        D3DRECT rect{(quadrant % 2) * 1920, (quadrant / 2) * 1080, (quadrant % 2 + 1) * 1920,
                     (quadrant / 2 + 1) * 1080};
        hr(device->Clear(1, &rect, D3DCLEAR_TARGET, colors[quadrant], 1, 0), "paint quadrant");
    }
    check(gd::compose(), "downsample to physical backbuffer");
    auto physical = gd::physical_backbuffer();
    check(physical != nullptr, "physical backbuffer available");
    hr(physical->GetDesc(&desc), "physical description");
    check(desc.Width == 1920 && desc.Height == 1080, "output is 1080p");
    hr(device->ColorFill(physical, nullptr, 0xffff0000), "diagnostic snapshot solid red");
    check(gd::save_surface_bmp(device, physical, L"diagnostic-test.bmp"), "save diagnostic BMP");
    {
        std::ifstream bmp("diagnostic-test.bmp", std::ios::binary);
        BITMAPFILEHEADER header{};
        BITMAPINFOHEADER info{};
        unsigned char pixel[3]{};
        bmp.read(reinterpret_cast<char*>(&header), sizeof(header));
        bmp.read(reinterpret_cast<char*>(&info), sizeof(info));
        bmp.read(reinterpret_cast<char*>(pixel), sizeof(pixel));
        check(header.bfType == 0x4d42 && info.biWidth == 1920 && info.biHeight == -1080 && info.biBitCount == 24,
              "capture BMP dimensions and orientation");
        check(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255, "capture BGR red pixel ignores alpha");
    }
    check(gd::compose(), "restore quadrants after snapshot verification");
    IDirect3DSurface9* readable = nullptr;
    hr(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &readable, nullptr),
       "readback surface");
    hr(device->GetRenderTargetData(physical, readable), "readback");
    D3DLOCKED_RECT lock{};
    hr(readable->LockRect(&lock, nullptr, D3DLOCK_READONLY), "lock readback");
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const unsigned x = (quadrant % 2) * 960 + 480, y = (quadrant / 2) * 540 + 270;
        auto row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lock.pBits) + y * lock.Pitch);
        check((row[x] & 0xffffff) == (colors[quadrant] & 0xffffff), "quadrant survives downsampling without cropping");
    }
    readable->UnlockRect();
    readable->Release();
    physical->Release();
    logical->Release();
    puts("PASS: device/swapchain 4K canvas -> 1080p output, four corner colors verified");
}
static int verify_image(const wchar_t* path, bool disable_live2d, unsigned fps, unsigned animations) {
    const auto image_hash = gd::sha256_file(path);
    check(gd::target::select(image_hash), "supported game file selects one central address profile");
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    check(bytes.size() >= 4096, "PE file size");
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    // Reserve the large BSS without committing it. Only file-backed sections
    // are needed for instruction validation and trampoline creation.
    auto image =
        static_cast<uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_RESERVE, PAGE_NOACCESS));
    check(image != nullptr, "reserve isolated PE image");
    check(VirtualAlloc(image, nt->OptionalHeader.SizeOfHeaders, MEM_COMMIT, PAGE_READWRITE) != nullptr,
          "commit headers");
    memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!section->SizeOfRawData)
            continue;
        check(VirtualAlloc(image + section->VirtualAddress, section->SizeOfRawData, MEM_COMMIT,
                           PAGE_EXECUTE_READWRITE) != nullptr,
              "commit file section");
        memcpy(image + section->VirtualAddress, bytes.data() + section->PointerToRawData, section->SizeOfRawData);
    }
    gd::game = reinterpret_cast<HMODULE>(image);
    check(fps >= 60, "supported verification FPS");
    gd::config.fps = gd::config.refresh = fps;
    gd::config.diagnostics = true;
    gd::config.disable_live2d = disable_live2d;
    gd::config.animation_timing = animations;
    for (const auto& hook : gd::selection_diagnostic_hooks()) {
        auto site = static_cast<uint8_t*>(gd::at(hook.address));
        *site ^= 1;
        bool rejected = false;
        try {
            gd::install_timing_and_present();
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        *site ^= 1;
        check(rejected && *static_cast<uint8_t*>(gd::at(gd::target::get().present.address)) ==
                              gd::target::get().present.signature.storage[0],
              "selection diagnostic conflict rejected before any patches");
    }
    const auto animation_hooks = gd::animation_hooks();
    if (fps > 60 && animations) {
        for (const auto& hook : animation_hooks) {
            auto site = static_cast<uint8_t*>(gd::at(hook.address));
            *site ^= 1;
            bool rejected = false;
            try {
                gd::install_timing_and_present();
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            *site ^= 1;
            check(rejected, "conflicting animation hook rejected");
            check(*static_cast<uint8_t*>(gd::at(gd::target::get().present.address)) ==
                      gd::target::get().present.signature.storage[0],
                  "animation conflict leaves Present unhooked");
        }
    }
    if (fps > 60) {
        auto timed_hooks = gd::timer_hooks();
        const auto scene_hooks = gd::scene_timing_hooks();
        timed_hooks.insert(timed_hooks.end(), scene_hooks.begin(), scene_hooks.end());
        for (const auto& hook : timed_hooks) {
            auto site = static_cast<uint8_t*>(gd::at(hook.address));
            *site ^= 1;
            bool rejected = false;
            try {
                gd::install_timing_and_present();
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            *site ^= 1;
            check(rejected && *static_cast<uint8_t*>(gd::at(gd::target::get().present.address)) ==
                                  gd::target::get().present.signature.storage[0],
                  "timer conflict rejected before any hooks");
        }
        std::vector<uintptr_t> patch_sites{gd::target::get().cursor_call.address,
                                           gd::target::get().play_decrement.address};
        for (const auto* patch : gd::scene_timing_patches())
            patch_sites.push_back(patch->address);
        for (const auto& hook : gd::scene_inline_hooks())
            patch_sites.push_back(hook.site->address);
        if (animations)
            for (const auto& hook : gd::animation_inline_hooks())
                patch_sites.push_back(hook.site->address);
        for (uintptr_t va : patch_sites) {
            auto site = static_cast<uint8_t*>(gd::at(va));
            *site ^= 1;
            bool rejected = false;
            try {
                gd::install_timing_and_present();
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            *site ^= 1;
            check(rejected && *static_cast<uint8_t*>(gd::at(gd::target::get().present.address)) ==
                                  gd::target::get().present.signature.storage[0],
                  "timing patch conflict rejected before any hooks");
        }
    }
    const auto characters = gd::live2d_patches();
    if (disable_live2d) {
        // Each conflicting site must abort before any hook or patch is applied.
        for (const auto& patch : characters) {
            auto site = static_cast<uint8_t*>(gd::at(patch.address));
            const uint8_t original = *site;
            *site ^= 1;
            bool rejected = false;
            try {
                gd::install_timing_and_present();
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            check(rejected, "conflicting Live2D patch rejected");
            check(*static_cast<uint8_t*>(gd::at(gd::target::get().present.address)) ==
                      gd::target::get().present.signature.storage[0],
                  "conflict leaves Present unhooked");
            *site = original;
            for (const auto& candidate : characters)
                check(memcmp(gd::at(candidate.address), candidate.before.data(), candidate.before.size()) == 0,
                      "conflict leaves all character patch sites unchanged");
        }
    }
    gd::install_timing_and_present();
    selection_diagnostics_test();
    native_selection_motion_test();
    native_selection_motion_test(true);
    for (const auto& hook : animation_hooks)
        check(*static_cast<uint8_t*>(gd::at(hook.address)) == (fps > 60 && animations ? 0xe9 : hook.signature[0]),
              "animation switch controls all detours");
    if (fps > 60 && animations) {
        native_animation_test(animations);
        native_sprite_test();
    }
    for (const auto& patch : characters) {
        const auto& expected = disable_live2d ? patch.after : patch.before;
        check(memcmp(gd::at(patch.address), expected.data(), expected.size()) == 0,
              "Live2D switch controls each character creation patch");
    }
    // Exercise the real two pair builders on their empty-vector paths. No
    // dependency imports, model loaders or renderer are called on these paths.
    // Nonzero IDs with the switch enabled must take the same branches as zero
    // IDs with the switch disabled, including the existing menu cleanup.
    auto menu_vector = static_cast<uint64_t*>(gd::at(gd::target::get().menu_character_vector));
    check(VirtualAlloc(menu_vector, 24, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit isolated empty menu vector");
    auto selections = static_cast<uint32_t*>(gd::at(gd::target::get().saved_characters));
    check(VirtualAlloc(selections, 8, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit isolated saved selections");
    selections[0] = 2;
    selections[1] = 3;
    const unsigned id = disable_live2d ? 3 : 0;
    using PairBuilder = void(__fastcall*)(unsigned, unsigned);
    reinterpret_cast<PairBuilder>(gd::at(gd::target::get().menu_character_builder))(id, id);
    reinterpret_cast<PairBuilder>(gd::at(gd::target::get().alternate_character_builder))(id, id);
    check(menu_vector[0] == 0 && menu_vector[1] == 0 && menu_vector[2] == 0,
          "disabled menu creates no character controllers");
    check(selections[0] == 2 && selections[1] == 3, "saved character selections retained");
    if (disable_live2d) {
        // Run the patched inlined gameplay instruction block in a tiny harness
        // with nonzero EBX/EDI, preserving the caller's nonvolatile registers.
        std::vector<uint8_t> code{0x53, 0x57, 0xbb, 0x02, 0, 0, 0, 0xbf, 0x03, 0, 0, 0};
        auto block = static_cast<uint8_t*>(gd::at(gd::target::get().live2d_inline.address));
        code.insert(code.end(), block, block + gd::target::get().live2d_inline.after.length);
        const uint8_t tail[]{0x48, 0x89, 0xf8, 0x48, 0xc1, 0xe0, 0x20, 0x48, 0x09, 0xd8, 0x5f, 0x5b, 0xc3};
        code.insert(code.end(), std::begin(tail), std::end(tail));
        void* stub = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        check(stub != nullptr, "allocate character ID instruction harness");
        memcpy(stub, code.data(), code.size());
        DWORD old;
        check(VirtualProtect(stub, 4096, PAGE_EXECUTE_READ, &old) != FALSE, "protect instruction harness");
        FlushInstructionCache(GetCurrentProcess(), stub, code.size());
        check(reinterpret_cast<uint64_t (*)()>(stub)() == 0, "gameplay block masks both character IDs");
        VirtualFree(stub, 0, MEM_RELEASE);
    }
    printf("PASS: DisableLive2D=%d at %u FPS; native empty-character branches, saved selections, three guarded patch "
           "sites\n",
           disable_live2d, fps);
    if (fps > 60) {
        for (const auto* patch : {&gd::target::get().frame_wait, &gd::target::get().play_decrement})
            check(memcmp(gd::at(patch->address), patch->after.storage.data(), patch->after.length) == 0,
                  "timing instruction replacements match selected profile");
        for (const auto* site : {&gd::target::get().filter_division0, &gd::target::get().filter_division1})
            check(memcmp(gd::at(site->address), site->signature.storage.data(), site->signature.length) == 0,
                  "native filter divisions retained; output is audio anchored");
        native_timer_test();
        native_entry_timer_test();
        native_song_clock_test();
        native_scene_timing_test();
    } else {
        const auto& patch = gd::target::get().frame_wait;
        check(memcmp(gd::at(patch.address), patch.before.storage.data(), patch.before.length) == 0,
              "60 FPS retains original wait");
    }
    check(*static_cast<float*>(gd::at(gd::target::get().constant_60)) == 60.0f, "shared 60 constant unchanged");
    check(*static_cast<float*>(gd::at(gd::target::get().constant_afp)) == 0.01667666807770729f,
          "shared AFP step unchanged");
    for (uintptr_t va : {gd::target::get().present.address, gd::target::get().note_batch.address,
                         gd::target::get().note_position.address, gd::target::get().note_draw.address})
        check(*static_cast<uint8_t*>(gd::at(va)) == 0xe9, "function detour installed");
    for (const auto* site : {&gd::target::get().frame, &gd::target::get().audio, &gd::target::get().song_filter})
        check(*static_cast<uint8_t*>(gd::at(site->address)) == (fps > 60 ? 0xe9 : site->signature.storage[0]),
              "timing detours follow FPS setting");
    MH_Uninitialize();
    VirtualFree(image, 0, MEM_RELEASE);
    check(gd::sha256_file(path) == image_hash, "original file unchanged");
    puts("PASS: instruction guards, MinHook trampolines, selected timing mode and note diagnostics");
    puts("PASS: shared constants and original game file unchanged; no game dependencies loaded");
    return 0;
}
static void dump_timing_sites() {
    gd::target::select(gd::target::supported[0]->sha256);
    gd::config.fps = 240;
    gd::config.animation_timing = 2;
    const auto& p = gd::target::get();
    bool first = true;
    printf("{\"sha256\":\"%s\",\"sites\":[", p.sha256.data());
    auto emit = [&](uintptr_t address, size_t size, const char* kind) {
        printf("%s{\"address\":\"0x%llx\",\"size\":%zu,\"kind\":\"%s\"}", first ? "" : ",",
               static_cast<unsigned long long>(address), size, kind);
        first = false;
    };
    for (const auto& h : gd::scene_inline_hooks())
        emit(h.site->address, h.site->signature.length, "inline");
    for (const auto& h : gd::animation_inline_hooks())
        emit(h.site->address, h.site->signature.length, "inline");
    for (const auto& group : {gd::timer_hooks(), gd::scene_timing_hooks(), gd::animation_hooks()})
        for (const auto& h : group)
            emit(h.address, h.signature.size(), "function");
    for (const auto* s : {&p.present, &p.frame, &p.audio, &p.song_filter})
        emit(s->address, s->signature.length, "function");
    for (const auto* s : gd::scene_timing_patches())
        emit(s->address, s->before.length, "byte");
    for (const auto* s : {&p.frame_wait, &p.play_decrement})
        emit(s->address, s->before.length, "byte");
    emit(p.cursor_call.address, p.cursor_call.signature.length, "call");
    emit(p.afp_import, sizeof(void*), "import");
    puts("]}");
}
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e) -> LONG {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(e->ExceptionRecord->ExceptionAddress), &module);
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        fprintf(stderr, "Native exception %08lx at %p (module %ls + %llx)\n", e->ExceptionRecord->ExceptionCode,
                e->ExceptionRecord->ExceptionAddress, path,
                uintptr_t(e->ExceptionRecord->ExceptionAddress) - uintptr_t(module));
        fprintf(stderr, "Access=%llu address=%llx RCX=%llx RDX=%llx R8=%llx R9=%llx R10=%llx R11=%llx\n",
                e->ExceptionRecord->ExceptionInformation[0], e->ExceptionRecord->ExceptionInformation[1],
                e->ContextRecord->Rcx, e->ContextRecord->Rdx, e->ContextRecord->R8, e->ContextRecord->R9,
                e->ContextRecord->R10, e->ContextRecord->R11);
        return EXCEPTION_EXECUTE_HANDLER;
    });
    try {
        gd::self = GetModuleHandleW(nullptr);
        if (argc == 2 && wcscmp(argv[1], L"--dump-timing-sites") == 0) {
            dump_timing_sites();
            return 0;
        }
        if (argc == 2 && wcscmp(argv[1], L"--test-timing") == 0) {
            timing_math_test();
            return 0;
        }
        if (argc == 2 && wcscmp(argv[1], L"--test-inline-hook") == 0) {
            inline_hook_test();
            return 0;
        }
        if (argc == 2 && wcscmp(argv[1], L"--test-animation-clock") == 0) {
            animation_clock_test();
            return 0;
        }
        if ((argc >= 3 && argc <= 5) && wcscmp(argv[1], L"--check-config") == 0) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            if (auto slash = wcsrchr(path, L'\\'))
                slash[1] = 0;
            wcscat_s(path, L"gd-arena-fix.ini");
            auto selected = gd::read_config(path);
            const auto expected = wcstoul(argv[2], nullptr, 10);
            printf("Native config: %ux%u @ %u; target FPS %u; DisableLive2D=%d; NativeRendering=%d\n", selected.width,
                   selected.height, selected.refresh, selected.fps, selected.disable_live2d, selected.native_rendering);
            check(selected.refresh == expected && selected.fps == expected && gd::valid_config(selected),
                  "requested mode reaches native config reader");
            if (argc >= 4)
                check(selected.disable_live2d == (wcstoul(argv[3], nullptr, 10) != 0),
                      "INI Live2D preference reaches native reader");
            if (argc == 5)
                check(selected.native_rendering == (wcstoul(argv[4], nullptr, 10) != 0),
                      "INI NativeRendering preference reaches native reader");
            puts("PASS: native INI configuration");
            return 0;
        }
        if (argc >= 3 && wcscmp(argv[1], L"--verify-image") == 0)
            return verify_image(argv[2], argc >= 4 && wcstoul(argv[3], nullptr, 10) != 0,
                                argc >= 5 ? wcstoul(argv[4], nullptr, 10) : 120,
                                argc >= 6 ? wcstoul(argv[5], nullptr, 10) : 2);
        if (argc == 2 && wcscmp(argv[1], L"--native-rendering") == 0) {
            gd::config.native_rendering = true;
            puts("NativeRendering smoke path enabled for this process");
        }
        gd::FractionalTicks ticks;
        unsigned total = 0;
        for (int i = 0; i < 1200; ++i)
            total += ticks.advance(1.0 / 120.0);
        check(total == 3000, "fractional 120 Hz fallback preserves 300 units/sec");
        check(std::abs(gd::afp_step(1.0 / 120.0) * 2 - gd::afp_step(1.0 / 60.0)) < 1e-7, "AFP equal elapsed time");
        check(!gd::validate_game(GetModuleHandleW(nullptr)), "unknown binary rejected by hash gate");
        puts("PASS: timing arithmetic and unsupported-build rejection");
        fullscreen_window_test();
        IDirect3D9Ex* factory = nullptr;
        hr(Direct3DCreate9Ex(D3D_SDK_VERSION, &factory), "Direct3DCreate9Ex");
        bool supports120 = false;
        D3DDISPLAYMODEFILTER filter{sizeof(filter), D3DFMT_X8R8G8B8, D3DSCANLINEORDERING_UNKNOWN};
        const UINT count = factory->GetAdapterModeCountEx(0, &filter);
        for (UINT i = 0; i < count; ++i) {
            D3DDISPLAYMODEEX mode{};
            mode.Size = sizeof(mode);
            if (SUCCEEDED(factory->EnumAdapterModesEx(0, &filter, i, &mode)) && mode.Width == 1920 &&
                mode.Height == 1080 && mode.RefreshRate == 120)
                supports120 = true;
        }
        printf("Native adapter 0 enumerates 1920x1080@120: %s\n", supports120 ? "YES" : "NO");
        gd::wrap_factory(factory);
        HWND window = CreateWindowExW(0, L"STATIC", L"gd-arena-fix hidden smoke test", WS_OVERLAPPEDWINDOW, 0, 0, 640,
                                      480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(window != nullptr, "hidden test window");
        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth = 3840;
        pp.BackBufferHeight = 2160;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.Windowed = TRUE;
        pp.hDeviceWindow = window;
        IDirect3DDevice9Ex* device = nullptr;
        hr(factory->CreateDeviceEx(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr,
                                   &device),
           "hooked CreateDeviceEx");
        check_input_extent(window, true);
        scaling_test(device);
        gd::config.refresh = 120;
        check(SetWindowPos(window, nullptr, 0, 0, 3840, 2160, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
              "simulate game restoring oversized window before reset");
        hr(device->ResetEx(&pp, nullptr), "hooked ResetEx");
        check_input_extent(window, true);
        scaling_test(device);
        puts("PASS: ResetEx recreates the logical canvas and restores physical input dimensions");
        auto physical = gd::physical_backbuffer();
        device->SetRenderTarget(0, physical);
        physical->Release();
        gd::release_canvas();
        device->Release();
        factory->Release();
        DestroyWindow(window);
        puts("ALL SMOKE TESTS PASSED (windowed; game timing and fullscreen remain untested)");
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
