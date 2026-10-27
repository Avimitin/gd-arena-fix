#include "mod.h"
#include "inline_hook.h"
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <algorithm>

namespace gd {
void require_disjoint_patches(std::vector<PatchRange> ranges) {
    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) { return a.address < b.address; });
    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& r = ranges[i];
        if (!r.size || r.size > UINTPTR_MAX - r.address)
            throw std::runtime_error("invalid patch range");
        if (i && r.address - ranges[i - 1].address < ranges[i - 1].size) {
            char message[128];
            snprintf(message, sizeof(message), "overlapping patches at %llX and %llX; no patches installed",
                     static_cast<unsigned long long>(ranges[i - 1].address),
                     static_cast<unsigned long long>(r.address));
            throw std::runtime_error(message);
        }
    }
}
static_assert(offsetof(InlineContext, rax) == 104 && offsetof(InlineContext, ip) == 128);
extern "C" void gd_inline_shim();
extern "C" void gd_inline_dispatch(InlineContext* context) {
    // The bridge's return slot points at its inline shim address. Immutable
    // descriptor words immediately follow it, so no map lookup/allocation is
    // needed on this hot path. The shim returns directly to the chosen game IP.
    const auto descriptor = reinterpret_cast<const uintptr_t*>(context->ip);
    const auto continuation = descriptor[1];
    const auto handler = reinterpret_cast<InlineHandler>(descriptor[2]);
    context->ip = continuation;
    handler(*context);
}
void FreeBridge::operator()(void* p) const {
    if (p)
        VirtualFree(p, 0, MEM_RELEASE);
}
InlineBridge prepare_inline_hook(const InlineHook& hook) {
    const auto& site = *hook.site;
    if (site.signature.length < 5)
        throw std::runtime_error("inline hook must cover complete instructions totaling at least five bytes");
    if (memcmp(at(site.address), site.signature.storage.data(), site.signature.length))
        throw std::runtime_error("inline hook instructions differ from selected game profile");
    // The game branch needs a rel32 destination. The bridge itself uses an
    // absolute call, so the mod DLL can be anywhere in the 64-bit address space.
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    const uintptr_t center = reinterpret_cast<uintptr_t>(at(site.address));
    const uintptr_t low = center > 0x70000000 ? center - 0x70000000 : info.dwAllocationGranularity;
    const uintptr_t high = center + 0x70000000;
    InlineBridge bridge;
    for (uintptr_t cursor = low; cursor < high;) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)))
            break;
        const auto next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (region.State == MEM_FREE) {
            const auto aligned =
                (cursor + info.dwAllocationGranularity - 1) & ~(uintptr_t(info.dwAllocationGranularity) - 1);
            if (aligned + 4096 <= next && aligned < high) {
                bridge.memory.reset(
                    VirtualAlloc(reinterpret_cast<void*>(aligned), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
                if (bridge.memory)
                    break;
            }
        }
        if (next <= cursor)
            break;
        cursor = next;
    }
    if (!bridge.memory)
        throw std::runtime_error("cannot allocate inline bridge in branch range");
    uint8_t code[30]{0xff, 0x15, 0, 0, 0, 0}; // call qword ptr [rip]; return is replaced by dispatcher
    const uintptr_t words[]{reinterpret_cast<uintptr_t>(&gd_inline_shim), center + site.signature.length,
                            reinterpret_cast<uintptr_t>(hook.handler)};
    memcpy(code + 6, words, sizeof(words));
    memcpy(bridge.memory.get(), code, sizeof(code));
    DWORD old;
    if (!VirtualProtect(bridge.memory.get(), 4096, PAGE_EXECUTE_READ, &old))
        throw std::runtime_error("cannot protect inline bridge");
    FlushInstructionCache(GetCurrentProcess(), bridge.memory.get(), sizeof(code));
    bridge.replacement.assign(site.signature.length, 0x90);
    bridge.replacement[0] = 0xe9;
    const auto displacement = reinterpret_cast<intptr_t>(bridge.memory.get()) - static_cast<intptr_t>(center + 5);
    if (displacement < INT32_MIN || displacement > INT32_MAX)
        throw std::runtime_error("inline bridge outside branch range");
    const auto rel = static_cast<int32_t>(displacement);
    memcpy(bridge.replacement.data() + 1, &rel, sizeof(rel));
    return bridge;
}
} // namespace gd
