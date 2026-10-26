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

static void check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
static void hr(HRESULT result, const char* text) {
    if (FAILED(result)) { printf("HRESULT %08lx: %s\n", result, text); throw std::runtime_error(text); }
}
static void check_input_extent(HWND window, bool windowed = false) {
    RECT client{};
    check(GetClientRect(window, &client) != FALSE, "input client rectangle available");
    if (windowed)
        check(client.right > 0 && client.bottom > 0 && client.right <= 1920 && client.bottom <= 1080,
            "windowed client fits within output extent (Windows may constrain decorations)");
    else check(client.right == 1920 && client.bottom == 1080, "fullscreen input client matches physical 1080p output");
    // The real game normalizes WM_MOUSE/WM_TOUCH coordinates to 800x1280.
    // On a 1080p screen with a stale 4K HWND these max out at 399 and 639.
    const int input_x = int(float(client.right - 1) / client.right * 800.0f);
    const int input_y = int(float(client.bottom - 1) / client.bottom * 1280.0f);
    check(input_x == 799 && input_y >= 1278, "physical right/bottom reach full game input range");
}
static void fullscreen_window_test() {
    HWND window = CreateWindowExW(0, L"STATIC", L"gd-arena-fix hidden fullscreen geometry test", WS_POPUP,
        0, 0, 3840, 2160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window != nullptr, "hidden fullscreen-style window");
    RECT before{}; GetClientRect(window, &before);
    check(before.right == 3840 && before.bottom == 2160, "reproduce stale 4K fullscreen client");
    check(int(1919.0f / before.right * 800.0f) == 399, "reproduce half-width input reach");
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    check(gd::fit_output_window(window, false, monitor), "resize fullscreen client independently of backbuffer");
    check_input_extent(window);
    MONITORINFO info{}; info.cbSize = sizeof(info);
    check(GetMonitorInfoW(monitor, &info) != FALSE, "fullscreen target monitor");
    POINT origin{};
    check(ClientToScreen(window, &origin) != FALSE && origin.x == info.rcMonitor.left && origin.y == info.rcMonitor.top,
        "fullscreen client starts at target monitor origin");
    DestroyWindow(window);
    puts("PASS: fullscreen-style 4K HWND resized to 1080p, pointer reaches right/bottom, correct monitor origin");
}
static void scaling_test(IDirect3DDevice9Ex* device) {
    IDirect3DSurface9* logical = nullptr;
    hr(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &logical), "logical backbuffer");
    D3DSURFACE_DESC desc{};
    hr(logical->GetDesc(&desc), "logical description");
    check(desc.Width == 3840 && desc.Height == 2160, "game sees 4K canvas");
    IDirect3DSwapChain9* chain = nullptr;
    hr(device->GetSwapChain(0, &chain), "get main swapchain");
    IDirect3DSurface9* chain_surface = nullptr;
    hr(chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &chain_surface), "swapchain canvas");
    check(chain_surface == logical, "device and swapchain return same logical canvas");
    chain_surface->Release(); chain->Release();
    hr(device->SetRenderTarget(0, logical), "set logical target");
    const D3DCOLOR colors[]{0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff};
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        D3DRECT rect{(quadrant % 2) * 1920, (quadrant / 2) * 1080,
            (quadrant % 2 + 1) * 1920, (quadrant / 2 + 1) * 1080};
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
        BITMAPFILEHEADER header{}; BITMAPINFOHEADER info{}; unsigned char pixel[3]{};
        bmp.read(reinterpret_cast<char*>(&header), sizeof(header));
        bmp.read(reinterpret_cast<char*>(&info), sizeof(info));
        bmp.read(reinterpret_cast<char*>(pixel), sizeof(pixel));
        check(header.bfType == 0x4d42 && info.biWidth == 1920 && info.biHeight == -1080 && info.biBitCount == 24,
            "capture BMP dimensions and orientation");
        check(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255, "capture BGR red pixel ignores alpha");
    }
    check(gd::compose(), "restore quadrants after snapshot verification");
    IDirect3DSurface9* readable = nullptr;
    hr(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &readable, nullptr), "readback surface");
    hr(device->GetRenderTargetData(physical, readable), "readback");
    D3DLOCKED_RECT lock{};
    hr(readable->LockRect(&lock, nullptr, D3DLOCK_READONLY), "lock readback");
    for (int quadrant = 0; quadrant < 4; ++quadrant) {
        const unsigned x = (quadrant % 2) * 960 + 480, y = (quadrant / 2) * 540 + 270;
        auto row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lock.pBits) + y * lock.Pitch);
        check((row[x] & 0xffffff) == (colors[quadrant] & 0xffffff), "quadrant survives downsampling without cropping");
    }
    readable->UnlockRect(); readable->Release(); physical->Release(); logical->Release();
    puts("PASS: device/swapchain 4K canvas -> 1080p output, four corner colors verified");
}
static int verify_image(const wchar_t* path, bool disable_live2d, unsigned fps) {
    check(gd::sha256_file(path) == "0ed4aabc75f5bd997c237759d5fcf17143aa69073110b5c9235d1515cf0fd32c", "supported game file hash");
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    check(bytes.size() >= 4096, "PE file size");
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    // Reserve the large BSS without committing it. Only file-backed sections
    // are needed for instruction validation and trampoline creation.
    auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_RESERVE, PAGE_NOACCESS));
    check(image != nullptr, "reserve isolated PE image");
    check(VirtualAlloc(image, nt->OptionalHeader.SizeOfHeaders, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit headers");
    memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!section->SizeOfRawData) continue;
        check(VirtualAlloc(image + section->VirtualAddress, section->SizeOfRawData, MEM_COMMIT, PAGE_EXECUTE_READWRITE) != nullptr, "commit file section");
        memcpy(image + section->VirtualAddress, bytes.data() + section->PointerToRawData, section->SizeOfRawData);
    }
    gd::game = reinterpret_cast<HMODULE>(image);
    check(fps == 60 || fps == 120, "supported verification FPS");
    gd::config.fps = gd::config.refresh = fps;
    gd::config.experimental = fps == 120;
    gd::config.diagnostics = true;
    gd::config.disable_live2d = disable_live2d;
    const auto characters = gd::live2d_patches();
    if (disable_live2d) {
        // Each conflicting site must abort before any hook or patch is applied.
        for (const auto& patch : characters) {
            auto site = static_cast<uint8_t*>(gd::at(patch.address));
            const uint8_t original = *site;
            *site ^= 1;
            bool rejected = false;
            try { gd::install_timing_and_present(); }
            catch (const std::runtime_error&) { rejected = true; }
            check(rejected, "conflicting Live2D patch rejected");
            check(*static_cast<uint8_t*>(gd::at(0x180420360)) == 0x48, "conflict leaves Present unhooked");
            *site = original;
            for (const auto& candidate : characters)
                check(memcmp(gd::at(candidate.address), candidate.before.data(), candidate.before.size()) == 0,
                    "conflict leaves all character patch sites unchanged");
        }
    }
    gd::install_timing_and_present();
    for (const auto& patch : characters) {
        const auto& expected = disable_live2d ? patch.after : patch.before;
        check(memcmp(gd::at(patch.address), expected.data(), expected.size()) == 0,
            "Live2D switch controls each character creation patch");
    }
    // Exercise the real two pair builders on their empty-vector paths. No
    // dependency imports, model loaders or renderer are called on these paths.
    // Nonzero IDs with the switch enabled must take the same branches as zero
    // IDs with the switch disabled, including the existing menu cleanup.
    auto menu_vector = static_cast<uint64_t*>(gd::at(0x182F3FF88));
    check(VirtualAlloc(menu_vector, 24, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit isolated empty menu vector");
    auto selections = static_cast<uint32_t*>(gd::at(0x1829322B0));
    check(VirtualAlloc(selections, 8, MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit isolated saved selections");
    selections[0] = 2; selections[1] = 3;
    const unsigned id = disable_live2d ? 3 : 0;
    using PairBuilder = void(__fastcall*)(unsigned, unsigned);
    reinterpret_cast<PairBuilder>(gd::at(0x180306170))(id, id);
    reinterpret_cast<PairBuilder>(gd::at(0x180288DA0))(id, id);
    check(menu_vector[0] == 0 && menu_vector[1] == 0 && menu_vector[2] == 0, "disabled menu creates no character controllers");
    check(selections[0] == 2 && selections[1] == 3, "saved character selections retained");
    if (disable_live2d) {
        // Run the patched inlined gameplay instruction block in a tiny harness
        // with nonzero EBX/EDI, preserving the caller's nonvolatile registers.
        std::vector<uint8_t> code{0x53,0x57,0xbb,0x02,0,0,0,0xbf,0x03,0,0,0};
        auto block = static_cast<uint8_t*>(gd::at(0x18028B29B));
        code.insert(code.end(), block, block + 12);
        const uint8_t tail[]{0x48,0x89,0xf8,0x48,0xc1,0xe0,0x20,0x48,0x09,0xd8,0x5f,0x5b,0xc3};
        code.insert(code.end(), std::begin(tail), std::end(tail));
        void* stub = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        check(stub != nullptr, "allocate character ID instruction harness");
        memcpy(stub, code.data(), code.size());
        DWORD old; check(VirtualProtect(stub, 4096, PAGE_EXECUTE_READ, &old) != FALSE, "protect instruction harness");
        FlushInstructionCache(GetCurrentProcess(), stub, code.size());
        check(reinterpret_cast<uint64_t(*)()>(stub)() == 0, "gameplay block masks both character IDs");
        VirtualFree(stub, 0, MEM_RELEASE);
    }
    printf("PASS: DisableLive2D=%d at %u FPS; native empty-character branches, saved selections, three guarded patch sites\n", disable_live2d, fps);
    if (fps == 120) {
    for (unsigned i = 0; i < 35; ++i) check(static_cast<uint8_t*>(gd::at(0x1801A39F1))[i] == 0x90, "14ms wait removed");
    for (uintptr_t va : {0x180287060ULL, 0x180287133ULL}) {
        auto instruction = static_cast<uint8_t*>(gd::at(va));
        int32_t displacement; memcpy(&displacement, instruction + 4, sizeof(displacement));
        check(*reinterpret_cast<float*>(instruction + 8 + displacement) == 120.0f, "private filter divisor is 120");
    }
    } else {
        check(*static_cast<uint8_t*>(gd::at(0x1801A39F1)) == 0xff, "60 FPS retains original wait");
    }
    check(*static_cast<float*>(gd::at(0x18066898C)) == 60.0f, "shared 60 constant unchanged");
    check(*static_cast<float*>(gd::at(0x180668644)) == 0.01667666807770729f, "shared AFP step unchanged");
    for (uintptr_t va : {0x180420360ULL, 0x1803C06E0ULL, 0x1803C0350ULL, 0x1803BFCB0ULL})
        check(*static_cast<uint8_t*>(gd::at(va)) == 0xe9, "function detour installed");
    for (uintptr_t va : {0x1801A3910ULL, 0x1801B60F0ULL, 0x180286EC0ULL})
        check(*static_cast<uint8_t*>(gd::at(va)) == (fps == 120 ? 0xe9 : 0x48), "timing detours follow FPS setting");
    MH_Uninitialize();
    VirtualFree(image, 0, MEM_RELEASE);
    check(gd::sha256_file(path) == "0ed4aabc75f5bd997c237759d5fcf17143aa69073110b5c9235d1515cf0fd32c", "original file unchanged");
    puts("PASS: instruction guards, MinHook trampolines, selected timing mode and note diagnostics");
    puts("PASS: shared constants and original game file unchanged; no game dependencies loaded");
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e) -> LONG {
        HMODULE module = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(e->ExceptionRecord->ExceptionAddress), &module);
        wchar_t path[MAX_PATH]{}; GetModuleFileNameW(module, path, MAX_PATH);
        fprintf(stderr, "Native exception %08lx at %p (module %ls + %llx)\n", e->ExceptionRecord->ExceptionCode,
            e->ExceptionRecord->ExceptionAddress, path,
            uintptr_t(e->ExceptionRecord->ExceptionAddress) - uintptr_t(module));
        return EXCEPTION_EXECUTE_HANDLER;
    });
    try {
        gd::self = GetModuleHandleW(nullptr);
        if ((argc == 3 || argc == 4) && wcscmp(argv[1], L"--check-config") == 0) {
            wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
            if (auto slash = wcsrchr(path, L'\\')) slash[1] = 0;
            wcscat_s(path, L"gd-arena-fix.ini");
            auto selected = gd::read_config(path);
            const auto expected = wcstoul(argv[2], nullptr, 10);
            printf("Native config: %ux%u @ %u; target FPS %u; experimental=%d; DisableLive2D=%d\n",
                selected.width, selected.height, selected.refresh, selected.fps, selected.experimental, selected.disable_live2d);
            check(selected.refresh == expected && selected.fps == expected && selected.experimental == (expected == 120),
                "requested mode reaches native config reader");
            if (argc == 4) check(selected.disable_live2d == (wcstoul(argv[3], nullptr, 10) != 0), "INI Live2D preference reaches native reader");
            puts("PASS: native INI configuration");
            return 0;
        }
        if (argc >= 3 && wcscmp(argv[1], L"--verify-image") == 0)
            return verify_image(argv[2], argc >= 4 && wcstoul(argv[3], nullptr, 10) != 0,
                argc >= 5 ? wcstoul(argv[4], nullptr, 10) : 120);
        gd::FractionalTicks ticks;
        unsigned total = 0;
        for (int i = 0; i < 1200; ++i) total += ticks.advance(1.0 / 120.0);
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
            D3DDISPLAYMODEEX mode{}; mode.Size = sizeof(mode);
            if (SUCCEEDED(factory->EnumAdapterModesEx(0, &filter, i, &mode)) && mode.Width == 1920 && mode.Height == 1080 && mode.RefreshRate == 120) supports120 = true;
        }
        printf("Native adapter 0 enumerates 1920x1080@120: %s\n", supports120 ? "YES" : "NO");
        gd::wrap_factory(factory);
        HWND window = CreateWindowExW(0, L"STATIC", L"gd-arena-fix hidden smoke test", WS_OVERLAPPEDWINDOW,
            0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(window != nullptr, "hidden test window");
        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth = 3840; pp.BackBufferHeight = 2160;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = TRUE; pp.hDeviceWindow = window;
        IDirect3DDevice9Ex* device = nullptr;
        hr(factory->CreateDeviceEx(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr, &device), "hooked CreateDeviceEx");
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
        device->SetRenderTarget(0, physical); physical->Release();
        gd::release_canvas();
        device->Release(); factory->Release(); DestroyWindow(window);
        puts("ALL SMOKE TESTS PASSED (windowed; game timing and fullscreen remain untested)");
        return 0;
    } catch (const std::exception& e) { fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
