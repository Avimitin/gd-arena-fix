#pragma once
#include "game_profile.h"
#include <memory>
#include <bit>

namespace gd {
// Saved by inline_shim.asm. native_rsp() points at the stack before the bridge
// call, including existing locals and arguments at the intercepted instruction.
struct InlineContext {
    uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbx,rdx,rcx,rax,flags,rbp,ip;
    uintptr_t native_rsp() const { return reinterpret_cast<uintptr_t>(&ip) + sizeof(ip); }
    // The assembly frame stores the sixteen 32-byte vector registers directly
    // before this context. Access through bytes avoids aliasing typed locals.
    uint8_t* ymm(unsigned index) { return reinterpret_cast<uint8_t*>(this)-512+index*32; }
    void cmp32(uint32_t a,uint32_t b) {
        const uint32_t r=a-b;
        constexpr uint64_t arithmetic=0x8d5; // CF/PF/AF/ZF/SF/OF
        flags=(flags&~arithmetic) | uint64_t(a<b) | (std::popcount(r&255)%2==0 ? 4:0) |
            ((a^b^r)&16) | (r==0 ? 64:0) | ((r>>31)<<7) | ((((a^b)&(a^r))>>31)<<11);
    }
    // INC/DEC leave CF unchanged. 'result' is the final native-width value.
    void step32(uint32_t result,bool decrement=false) {
        const uint32_t before=decrement?result+1:result-1;
        constexpr uint64_t arithmetic=0x8d4;
        const bool overflow=before==(decrement?0x80000000U:0x7fffffffU);
        flags=(flags&~arithmetic) | (std::popcount(result&255)%2==0?4:0) |
            ((before^result)&16) | (result==0?64:0) | ((result>>31)<<7) | (uint64_t(overflow)<<11);
    }
};
using InlineHandler = void(*)(InlineContext&);
struct InlineHook { const target::Site* site; InlineHandler handler; };
struct FreeBridge { void operator()(void* p) const; };
struct InlineBridge {
    std::unique_ptr<void,FreeBridge> memory;
    std::vector<uint8_t> replacement;
};
InlineBridge prepare_inline_hook(const InlineHook& hook);
struct PatchRange { uintptr_t address; size_t size; };
void require_disjoint_patches(std::vector<PatchRange> ranges);
}
