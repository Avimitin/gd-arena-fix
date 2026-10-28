#include "../src/mod.h"
#include "../src/timing_math.h"
#include "../src/config.h"
#include "../src/live2d.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <fstream>
#include <iterator>
#include <vector>
#include "../third_party/minhook/include/MinHook.h"

static void check(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}
#include "method_lookup.h"
#include "animations.h"
#include "timing.h"
#include "scene_timing.h"
#include "inline_hook.h"
#include "selection_diagnostics.h"
#include "selection_motion.h"
extern "C" HRESULT gd_vs_constant_fixture(void*, IDirect3DDevice9Ex*, UINT, const float*, UINT);
extern "C" char gd_vs_constant_fixture_return;
static D3DSWAPEFFECT native_smoke_effect = D3DSWAPEFFECT_DISCARD;
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
static uintptr_t com_identity(IUnknown* object) {
    IUnknown* identity = nullptr;
    if (!object || FAILED(object->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&identity))))
        return 0;
    const auto result = reinterpret_cast<uintptr_t>(identity);
    identity->Release();
    return result;
}
static IDirect3DVertexShader9* make_afp_style_vertex_shader(IDirect3DDevice9Ex* device) {
    // D3DXAssembleShader output for `vs.1.1; mov oPos, c0; mov oD0, c1`.
    // c0 is set offscreen so a vertex shader that processes XYZRHW vertices
    // would clip this geometry; POSITIONT should bypass it.
    static constexpr DWORD bytecode[]{0xfffe0101, 0x00000001, 0xc00f0000, 0xa0e40000,
                                      0x00000001, 0xd00f0000, 0xa0e40001, 0x0000ffff};
    IDirect3DVertexShader9* shader = nullptr;
    hr(device->CreateVertexShader(bytecode, &shader), "create AFP-style vertex shader");
    check(shader != nullptr, "create AFP-style vertex shader");
    return shader;
}
static IDirect3DVertexShader9* make_afp_font_projection_shader(IDirect3DDevice9Ex* device) {
    // D3DXAssembleShader output for the game-like FVF 0x142 inputs:
    // vs.1.1; dcl_position v0; dcl_color v1; dcl_texcoord0 v2;
    // m4x4 oPos,v0,c0; mov oD0,v1; mov oT0.xy,v2
    static constexpr DWORD bytecode[]{0xfffe0101, 0x0000001f, 0x80000000, 0x900f0000, 0x0000001f, 0x8000000a,
                                      0x900f0001, 0x0000001f, 0x80000005, 0x900f0002, 0x00000014, 0xc00f0000,
                                      0x90e40000, 0xa0e40000, 0x00000001, 0xd00f0000, 0x90e40001, 0x00000001,
                                      0xe0030000, 0x90e40002, 0x0000ffff};
    IDirect3DVertexShader9* shader = nullptr;
    hr(device->CreateVertexShader(bytecode, &shader), "create AFP font-projection vertex shader");
    check(shader != nullptr, "create AFP font-projection vertex shader");
    return shader;
}
static void afp_font_projection_test(IDirect3DDevice9Ex* device, IDirect3DSurface9* logical,
                                     const D3DSURFACE_DESC& desc) {
    const auto saved_game = gd::game;
    const auto saved_profile = gd::target::selected;
    check(gd::target::select(gd::target::supported[0]->sha256), "select profile for AFP font projection test");
    const auto& profile = gd::target::get();
    const uintptr_t fixture_return = reinterpret_cast<uintptr_t>(&gd_vs_constant_fixture_return);
    const uintptr_t return_offset = profile.font_projection_constants_return - profile.preferred_base;
    gd::game = reinterpret_cast<HMODULE>(fixture_return - return_offset);
    check(reinterpret_cast<uintptr_t>(gd::at(profile.font_projection_constants_return)) == fixture_return,
          "synthetic game base relocates font-projection callsite to assembly fixture");

    auto vtable = *reinterpret_cast<void***>(device);
    void* set_constants = vtable[94];
    auto fixture_call = [&](UINT start, const float* values, UINT count) {
        return gd_vs_constant_fixture(set_constants, device, start, values, count);
    };
    const auto close = [](float a, float b) { return std::abs(a - b) < 1e-7f; };
    float physical_projection[16]{2.0f / 1920.0f, 0, 0, -1, 0, -2.0f / 1080.0f, 0, 1, 0, 0, -0.5f, 0.5f, 0, 0, 0, 1};
    float original_projection[16];
    memcpy(original_projection, physical_projection, sizeof(physical_projection));
    float observed[16]{};
    hr(device->SetVertexShaderConstantF(0, physical_projection, 4), "set physical-output font projection");
    hr(device->GetVertexShaderConstantF(0, observed, 4), "read non-font-caller projection constants");
    check(memcmp(observed, physical_projection, sizeof(observed)) == 0,
          "ordinary constant upload bypasses font projection correction");

    const float unrelated[16]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    hr(fixture_call(4, unrelated, 4), "fixture upload to unrelated vertex constants");
    float unrelated_observed[16]{};
    hr(device->GetVertexShaderConstantF(4, unrelated_observed, 4), "read unrelated vertex constants");
    check(memcmp(unrelated_observed, unrelated, sizeof(unrelated)) == 0,
          "font correction leaves nonzero-start constant uploads unchanged");
    hr(fixture_call(0, physical_projection, 3), "fixture short font-constant upload");
    hr(device->GetVertexShaderConstantF(0, observed, 4), "read short font-constant upload");
    check(memcmp(observed, physical_projection, sizeof(observed)) == 0,
          "font correction requires the complete four-register projection upload");

    IDirect3DSurface9* auxiliary = nullptr;
    hr(device->CreateRenderTarget(320, 240, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &auxiliary, nullptr),
       "create auxiliary target for font projection isolation");
    D3DVIEWPORT9 small_main_viewport{0, 0, 200, 160, 0.0f, 1.0f};
    hr(device->SetViewport(&small_main_viewport), "reduce main viewport before auxiliary target bind");
    hr(device->SetRenderTarget(0, auxiliary), "bind auxiliary target for font projection isolation");
    hr(fixture_call(0, physical_projection, 4), "fixture font constants on auxiliary target");
    hr(device->GetVertexShaderConstantF(0, observed, 4), "read auxiliary-target font constants");
    check(memcmp(observed, physical_projection, sizeof(observed)) == 0,
          "font projection is untouched on an auxiliary target");
    auxiliary->Release();
    hr(device->SetRenderTarget(0, logical), "rebind native canvas for font projection");
    D3DVIEWPORT9 full_viewport{0, 0, 3840, 2160, 0.0f, 1.0f};
    hr(device->SetViewport(&full_viewport), "restore logical viewport for font projection");
    RECT full_scissor{0, 0, 3840, 2160};
    hr(device->SetScissorRect(&full_scissor), "restore logical scissor for font projection");

    const bool native_rendering = gd::config.native_rendering;
    gd::config.native_rendering = false;
    hr(fixture_call(0, physical_projection, 4), "fixture font constants with native mode disabled");
    hr(device->GetVertexShaderConstantF(0, observed, 4), "read fallback-mode font constants");
    check(memcmp(observed, physical_projection, sizeof(observed)) == 0,
          "font projection remains unchanged when NativeRendering is disabled");
    gd::config.native_rendering = native_rendering;

    auto shader = make_afp_font_projection_shader(device);
    IDirect3DTexture9* atlas = nullptr;
    hr(device->CreateTexture(2, 2, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &atlas, nullptr),
       "create solid font-atlas texture");
    D3DLOCKED_RECT atlas_lock{};
    hr(atlas->LockRect(0, &atlas_lock, nullptr, D3DLOCK_DISCARD), "lock solid font-atlas texture");
    for (unsigned y = 0; y < 2; ++y) {
        auto row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(atlas_lock.pBits) + y * atlas_lock.Pitch);
        row[0] = row[1] = 0xffffffff;
    }
    atlas->UnlockRect(0);

    hr(device->SetRenderTarget(0, logical), "bind native canvas for font glyph repro");
    hr(device->SetViewport(&full_viewport), "set full logical viewport for font glyph repro");
    hr(device->SetScissorRect(&full_scissor), "set full logical scissor for font glyph repro");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor for font repro clear");
    hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0), "clear before clipped font repro");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "enable scissor for font repro");
    hr(device->SetRenderState(D3DRS_ZENABLE, FALSE), "disable depth for font repro");
    hr(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "disable culling for font repro");
    hr(device->SetPixelShader(nullptr), "select fixed-function font pixel path");
    hr(device->SetVertexShader(shader), "bind AFP font-projection vertex shader");
    hr(device->SetFVF(0x142), "set AFP XYZ diffuse texture FVF");
    hr(device->SetTexture(0, atlas), "bind solid font-atlas texture");
    hr(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE), "modulate glyph and diffuse color");
    hr(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE), "select glyph texture color");
    hr(device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE), "select glyph vertex color");
    hr(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE), "modulate glyph and diffuse alpha");
    hr(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE), "select glyph texture alpha");
    hr(device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE), "select glyph vertex alpha");
    hr(device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT), "set font atlas point minification");
    hr(device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT), "set font atlas point magnification");
    hr(device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE), "disable font atlas mip filtering");

    struct FontVertex {
        float x, y, z;
        D3DCOLOR color;
        float u, v;
    };
    static_assert(sizeof(FontVertex) == 24);
    const FontVertex glyph_quad[]{{3231.0f, 707.0f, 0.5f, 0xff00ff00, 0.0f, 0.0f},
                                  {3305.0f, 707.0f, 0.5f, 0xff00ff00, 1.0f, 0.0f},
                                  {3231.0f, 780.0f, 0.5f, 0xff00ff00, 0.0f, 1.0f},
                                  {3305.0f, 780.0f, 0.5f, 0xff00ff00, 1.0f, 1.0f}};
    const std::vector<uint8_t> original_glyph(reinterpret_cast<const uint8_t*>(glyph_quad),
                                              reinterpret_cast<const uint8_t*>(glyph_quad) + sizeof(glyph_quad));
    hr(device->SetVertexShaderConstantF(0, physical_projection, 4), "restore physical font projection for repro");
    hr(device->BeginScene(), "begin clipped font glyph repro");
    hr(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, glyph_quad, sizeof(FontVertex)),
       "draw AFP-style FVF 0x142 font glyph with physical projection");
    hr(device->EndScene(), "end clipped font glyph repro");
    auto pixels = read_surface(device, logical, desc);
    check(pixel(pixels, desc, 1625, 365) == 0x000000,
          "physical 1080p font projection clips far-right 4K glyph before correction");

    hr(fixture_call(0, physical_projection, 4), "fixture font projection correction at exact game return address");
    hr(device->GetVertexShaderConstantF(0, observed, 4), "read corrected font projection");
    check(close(observed[0], 2.0f / 3840.0f) && close(observed[5], -2.0f / 2160.0f),
          "font callsite uploads logical 4K projection scales");
    for (unsigned i = 0; i < 16; ++i)
        if (i != 0 && i != 5)
            check(observed[i] == physical_projection[i], "font projection correction preserves other constants");
    check(memcmp(physical_projection, original_projection, sizeof(physical_projection)) == 0,
          "font correction leaves the caller's matrix memory unchanged");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor for corrected font clear");
    hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0), "clear before corrected font glyph");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "enable scissor for corrected font glyph");
    hr(device->BeginScene(), "begin corrected font glyph repro");
    hr(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, glyph_quad, sizeof(FontVertex)),
       "draw corrected AFP-style FVF 0x142 font glyph");
    hr(device->EndScene(), "end corrected font glyph repro");
    check(memcmp(glyph_quad, original_glyph.data(), sizeof(glyph_quad)) == 0,
          "projection correction leaves glyph vertex bytes unchanged");
    pixels = read_surface(device, logical, desc);
    check(pixel(pixels, desc, 1625, 365) == 0x00ff00 && pixel(pixels, desc, 1200, 365) == 0x000000,
          "corrected 4K projection brings far-right dynamic text glyphs into the native canvas");

    hr(device->SetVertexShader(nullptr), "clear AFP font-projection shader");
    hr(device->SetTexture(0, nullptr), "clear font-atlas texture");
    atlas->Release();
    shader->Release();
    gd::game = saved_game;
    gd::target::selected = saved_profile;
    puts("PASS: AFP FVF 0x142 text projection correction, callsite gating, clipping, aux/fallback isolation");
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

    // AFP binds its programmable vertex shader, then selects FVF 0x144 before
    // submitting these screen-space vertices. POSITIONT must still bypass the
    // shader's clip-space transform.
    auto afp_shader = make_afp_style_vertex_shader(device);
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor for AFP shader test clear");
    hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1.0f, 0), "clear before AFP shader test");
    const float offscreen_position[4]{4.0f, 4.0f, 4.0f, 1.0f};
    const float shader_color[4]{1.0f, 1.0f, 1.0f, 1.0f};
    hr(device->SetVertexShaderConstantF(0, offscreen_position, 1), "set clipping position shader constant");
    hr(device->SetVertexShaderConstantF(1, shader_color, 1), "set shader color constant");
    hr(device->SetTexture(0, nullptr), "clear texture for AFP shader test");
    hr(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1), "select AFP vertex diffuse color");
    hr(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE), "set AFP diffuse color argument");
    hr(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1), "select AFP vertex alpha");
    hr(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE), "set AFP diffuse alpha argument");
    hr(device->SetVertexShader(afp_shader), "bind AFP-style vertex shader");
    hr(device->SetFVF(0x144), "select AFP screen-space FVF after vertex shader");
    IDirect3DVertexShader9* active_shader = nullptr;
    hr(device->GetVertexShader(&active_shader), "query AFP-style vertex shader after SetFVF");
    check(active_shader == afp_shader, "SetFVF retains AFP-style vertex shader binding");
    if (active_shader)
        active_shader->Release();
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE), "enable scissor for AFP shader quad");
    hr(device->BeginScene(), "begin AFP shader quad");
    hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, logical_quad, sizeof(TransformedVertex)),
       "draw AFP-style FVF 0x144 screen-space quad with vertex shader bound");
    hr(device->EndScene(), "end AFP shader quad");
    check(memcmp(logical_quad, original_quad.data(), sizeof(logical_quad)) == 0,
          "AFP-style shader draw leaves submitted text vertices unchanged");
    pixels = read_surface(device, logical, desc);
    check(pixel(pixels, desc, 1500, 800) == 0x00ff00 && pixel(pixels, desc, 1200, 800) == 0x000000,
          "bound vertex shader preserves native XYZRHW text-quad placement and clipping");
    hr(device->SetVertexShader(nullptr), "clear AFP-style vertex shader after test");
    afp_shader->Release();

    afp_font_projection_test(device, logical, desc);

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
static void direct_backbuffer_present_test(IDirect3DDevice9Ex* device, D3DSWAPEFFECT expected_effect,
                                           UINT expected_backbuffer_count) {
    IDirect3DSwapChain9* chain = nullptr;
    hr(device->GetSwapChain(0, &chain), "get main direct swapchain");
    D3DPRESENT_PARAMETERS parameters{};
    hr(chain->GetPresentParameters(&parameters), "get effective direct swapchain parameters");
    check(parameters.SwapEffect == expected_effect && parameters.BackBufferCount == expected_backbuffer_count &&
              parameters.BackBufferWidth == 1920 && parameters.BackBufferHeight == 1080 &&
              parameters.MultiSampleType == D3DMULTISAMPLE_NONE,
          "direct path fixture uses its native-size swap effect with non-MSAA buffers");
    chain->Release();

    IDirect3DSurface9* canvas = nullptr;
    hr(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &canvas), "get direct native canvas before present loop");
    check(canvas != nullptr, "direct native canvas exists");
    IDirect3DSurface9* physical = gd::physical_backbuffer();
    check(physical != nullptr, "direct physical backbuffer exists");
    const auto canvas_identity = com_identity(canvas);
    check(canvas_identity != 0 && canvas_identity == com_identity(physical),
          "native direct canvas is the original physical backbuffer");
    D3DSURFACE_DESC desc{};
    hr(canvas->GetDesc(&desc), "describe direct canvas");
    D3DSURFACE_DESC physical_desc{};
    hr(physical->GetDesc(&physical_desc), "describe direct physical backbuffer");
    check(desc.Width == 1920 && desc.Height == 1080 && physical_desc.Width == desc.Width &&
              physical_desc.Height == desc.Height,
          "direct canvas and physical backbuffer retain native 1080p dimensions");

    IDirect3DSurface9* auxiliary = nullptr;
    hr(device->CreateRenderTarget(320, 240, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &auxiliary, nullptr),
       "create compose-isolation target for direct backbuffer");
    hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor for full direct-frame clears");
    static constexpr D3DCOLOR frame_colors[]{0xff17391f, 0xff542073, 0xffa34119, 0xff206da8,
                                             0xff8f7412, 0xff3b8a66, 0xff80409e, 0xffc25e31};
    constexpr D3DCOLOR auxiliary_color = 0xff2c4d91;
    for (unsigned cycle = 0; cycle < std::size(frame_colors); ++cycle) {
        hr(device->SetRenderTarget(0, canvas), "bind direct game canvas before frame clear");
        D3DVIEWPORT9 logical_viewport{0, 0, 3840, 2160, 0.0f, 1.0f};
        hr(device->SetViewport(&logical_viewport), "set logical viewport for direct full-frame clear");
        hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, frame_colors[cycle], 1.0f, 0),
           "clear direct canvas to unique cycle color");
        const auto pixels = read_surface(device, canvas, desc);
        check(std::all_of(
                  pixels.begin(), pixels.end(),
                  [color = frame_colors[cycle] & 0xffffff](uint32_t value) { return (value & 0xffffff) == color; }),
              "cached direct canvas contains the expected full-frame cycle color");
        const auto physical_pixels = read_surface(device, physical, physical_desc);
        check(physical_pixels == pixels, "physical backbuffer exposes the same full-frame direct canvas pixels");

        // Composition may occur while an auxiliary target is current after the
        // game's main canvas has already finished rendering.
        hr(device->SetRenderTarget(0, auxiliary), "bind auxiliary target before direct compose");
        hr(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE), "disable scissor on auxiliary compose target");
        hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, auxiliary_color, 1.0f, 0),
           "paint auxiliary sentinel before direct compose");
        check(gd::compose(), "direct compose succeeds while auxiliary target is bound");
        IDirect3DSurface9* bound = nullptr;
        hr(device->GetRenderTarget(0, &bound), "query target after direct compose");
        check(com_identity(bound) == com_identity(auxiliary), "direct compose leaves auxiliary target bound");
        bound->Release();
        D3DSURFACE_DESC auxiliary_desc{};
        hr(auxiliary->GetDesc(&auxiliary_desc), "describe direct compose-isolation target");
        const auto auxiliary_pixels = read_surface(device, auxiliary, auxiliary_desc);
        check(std::all_of(auxiliary_pixels.begin(), auxiliary_pixels.end(),
                          [](uint32_t value) { return (value & 0xffffff) == (auxiliary_color & 0xffffff); }),
              "direct compose does not overwrite the auxiliary target");

        hr(device->SetRenderTarget(0, canvas), "restore direct canvas before PresentEx");
        IDirect3DSurface9* physical_before_present = gd::physical_backbuffer();
        check(physical_before_present != nullptr && com_identity(physical_before_present) == canvas_identity,
              "cached direct canvas identity remains the physical backbuffer before PresentEx");
        physical_before_present->Release();
        hr(device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0), "present direct backbuffer cycle");

        IDirect3DSurface9* current = nullptr;
        hr(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &current), "get direct canvas after PresentEx");
        check(com_identity(current) == canvas_identity, "cached direct canvas remains current after PresentEx");
        current->Release();
        IDirect3DSurface9* current_physical = gd::physical_backbuffer();
        check(current_physical != nullptr && com_identity(current_physical) == canvas_identity,
              "physical backbuffer identity is stable across PresentEx");
        current_physical->Release();
    }
    hr(device->SetRenderTarget(0, canvas), "restore direct canvas after present cycles");
    auxiliary->Release();
    physical->Release();
    canvas->Release();
    puts("PASS: direct backbuffer identity and alternating frame colors across PresentEx");
}
static int verify_image(const wchar_t* path, bool disable_live2d, unsigned fps, unsigned animations,
                        bool disable_side_compositing) {
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
    gd::config.disable_side_compositing = disable_side_compositing;
    gd::config.animation_timing = animations;
    const bool suppress_characters = disable_live2d || disable_side_compositing;
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
    if (suppress_characters) {
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
        const auto& expected = (disable_live2d || disable_side_compositing) ? patch.after : patch.before;
        check(memcmp(gd::at(patch.address), expected.data(), expected.size()) == 0,
              "selected Live2D or side-compositing switch controls each guarded patch");
    }
    const auto& profile = gd::target::get();
    for (const auto* patch : {&profile.side_copy_left, &profile.side_copy_right, &profile.side_composite_copy}) {
        const auto& expected = disable_side_compositing ? patch->after : patch->before;
        check(memcmp(gd::at(patch->address), expected.storage.data(), expected.length) == 0,
              "side-compositing option controls each guarded copy site");
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
    const unsigned id = suppress_characters ? 3 : 0;
    using PairBuilder = void(__fastcall*)(unsigned, unsigned);
    reinterpret_cast<PairBuilder>(gd::at(gd::target::get().menu_character_builder))(id, id);
    reinterpret_cast<PairBuilder>(gd::at(gd::target::get().alternate_character_builder))(id, id);
    check(menu_vector[0] == 0 && menu_vector[1] == 0 && menu_vector[2] == 0,
          "disabled menu creates no character controllers");
    check(selections[0] == 2 && selections[1] == 3, "saved character selections retained");
    if (suppress_characters) {
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
    printf("PASS: DisableLive2D=%d DisableSideCompositing=%d at %u FPS; native empty-character branches, saved "
           "selections, %zu guarded patch sites\n",
           disable_live2d, disable_side_compositing, fps, characters.size());
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
        if (argc == 2 && wcscmp(argv[1], L"--test-method-lookup") == 0) {
            method_lookup_test::test();
            puts("PASS: COM original-method lookup across misses, hooked vtables, indices and registry growth");
            return 0;
        }
        if (argc == 2 && wcscmp(argv[1], L"--benchmark-method-lookup") == 0) {
            method_lookup_test::benchmark();
            return 0;
        }
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
        if ((argc >= 3 && argc <= 6) && wcscmp(argv[1], L"--check-config") == 0) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            if (auto slash = wcsrchr(path, L'\\'))
                slash[1] = 0;
            wcscat_s(path, L"gd-arena-fix.ini");
            auto selected = gd::read_config(path);
            const auto expected = wcstoul(argv[2], nullptr, 10);
            printf("Native config: %ux%u @ %u; target FPS %u; DisableLive2D=%d; NativeRendering=%d; "
                   "DisableSideCompositing=%d\n",
                   selected.width, selected.height, selected.refresh, selected.fps, selected.disable_live2d,
                   selected.native_rendering, selected.disable_side_compositing);
            check(selected.refresh == expected && selected.fps == expected && gd::valid_config(selected),
                  "requested mode reaches native config reader");
            if (argc >= 4)
                check(selected.disable_live2d == (wcstoul(argv[3], nullptr, 10) != 0),
                      "INI Live2D preference reaches native reader");
            if (argc >= 5)
                check(selected.native_rendering == (wcstoul(argv[4], nullptr, 10) != 0),
                      "INI NativeRendering preference reaches native reader");
            if (argc == 6)
                check(selected.disable_side_compositing == (wcstoul(argv[5], nullptr, 10) != 0),
                      "side-compositing preference reaches native config reader");
            puts("PASS: native INI configuration");
            return 0;
        }
        if (argc >= 3 && wcscmp(argv[1], L"--verify-image") == 0)
            return verify_image(argv[2], argc >= 4 && wcstoul(argv[3], nullptr, 10) != 0,
                                argc >= 5 ? wcstoul(argv[4], nullptr, 10) : 120,
                                argc >= 6 ? wcstoul(argv[5], nullptr, 10) : 2,
                                argc >= 7 && wcstoul(argv[6], nullptr, 10) != 0);
        if (argc == 2 && (wcscmp(argv[1], L"--native-rendering") == 0 || wcscmp(argv[1], L"--native-direct") == 0 ||
                          wcscmp(argv[1], L"--native-direct-copy") == 0)) {
            gd::config.native_rendering = true;
            if (wcscmp(argv[1], L"--native-direct") == 0)
                native_smoke_effect = D3DSWAPEFFECT_FLIP;
            else if (wcscmp(argv[1], L"--native-direct-copy") == 0)
                native_smoke_effect = D3DSWAPEFFECT_COPY;
            if (native_smoke_effect == D3DSWAPEFFECT_FLIP)
                puts("NativeRendering direct-FLIP smoke path enabled for this process");
            else if (native_smoke_effect == D3DSWAPEFFECT_COPY)
                puts("NativeRendering direct-COPY smoke path enabled for this process");
            else
                puts("NativeRendering intermediate-canvas smoke path enabled for this process");
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
        const bool direct_swapchain = native_smoke_effect != D3DSWAPEFFECT_DISCARD;
        const UINT fixture_backbuffer_count = native_smoke_effect == D3DSWAPEFFECT_FLIP ? 2 : 1;
        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth = 3840;
        pp.BackBufferHeight = 2160;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.BackBufferCount = fixture_backbuffer_count;
        pp.SwapEffect = native_smoke_effect;
        pp.Windowed = TRUE;
        pp.hDeviceWindow = window;
        IDirect3DDevice9Ex* device = nullptr;
        hr(factory->CreateDeviceEx(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr,
                                   &device),
           "hooked CreateDeviceEx");
        check_input_extent(window, true);
        scaling_test(device);
        if (direct_swapchain)
            direct_backbuffer_present_test(device, native_smoke_effect, fixture_backbuffer_count);
        gd::config.refresh = 120;
        check(SetWindowPos(window, nullptr, 0, 0, 3840, 2160, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
              "simulate game restoring oversized window before reset");
        hr(device->ResetEx(&pp, nullptr), "hooked ResetEx");
        check_input_extent(window, true);
        scaling_test(device);
        if (direct_swapchain)
            direct_backbuffer_present_test(device, native_smoke_effect, fixture_backbuffer_count);
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
