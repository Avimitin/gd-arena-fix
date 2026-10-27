#pragma once
#include "../src/inline_hook.h"
#include <array>

extern "C" void gd_inline_probe(void*,void*,const void*);
extern "C" void gd_inline_clobber();
extern "C" void gd_inline_shim();
extern "C" uint64_t gd_cmp_flags(uint32_t,uint32_t);
static gd::InlineContext inline_seen{};
static uintptr_t inline_destination;
static bool inline_unwind_ok;
__declspec(noinline) static void inline_observer(gd::InlineContext& c) {
    inline_seen = c;
    if (inline_destination) c.ip = inline_destination;
    const auto expected_ip = c.ip, expected_sp = c.native_rsp();
    CONTEXT unwind{}; RtlCaptureContext(&unwind);
    for (unsigned i=0; i<16; ++i) {
        DWORD64 base = 0;
        const auto fn = RtlLookupFunctionEntry(unwind.Rip,&base,nullptr);
        if (!fn) break;
        const bool shim = base+fn->BeginAddress == reinterpret_cast<uintptr_t>(&gd_inline_shim);
        PVOID handler = nullptr; DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,unwind.Rip,fn,&unwind,&handler,&establisher,nullptr);
        if (shim) { inline_unwind_ok = unwind.Rip == expected_ip && unwind.Rsp == expected_sp; break; }
    }
    gd_inline_clobber(); // Destroy public-ABI volatile registers and YMM upper halves.
}
static void inline_hook_test() {
    gd::require_disjoint_patches({{100,5},{105,8},{1,7}});
    for(const auto& conflict:std::vector<std::vector<gd::PatchRange>>{
        {{100,8},{100,8}},{{100,20},{104,5}},{{100,8},{107,8}}}) {
        bool rejected=false;
        try {gd::require_disjoint_patches(conflict);}catch(const std::runtime_error&){rejected=true;}
        check(rejected,"duplicate, contained and partially overlapping patches are rejected");
    }
    for(uint32_t a:{0U,1U,7U,8U,0x7fffffffU,0x80000000U,0xffffffffU})
        for(uint32_t b:{0U,1U,7U,8U,0x7fffffffU,0x80000000U,0xffffffffU}) {
            gd::InlineContext c{};c.flags=0x202;c.cmp32(a,b);
            check((c.flags&0x8d5)==(gd_cmp_flags(a,b)&0x8d5),"emulated comparison flags match native CMP including overflow");
        }
    struct Output { std::array<uint64_t,15> regs; uint64_t flags; uint8_t pad[128]; std::array<uint8_t,512> vectors; };
    static_assert(offsetof(Output,vectors)==256);
    alignas(32) std::array<uint8_t,512> vectors{};
    for (size_t i=0;i<vectors.size();++i) vectors[i] = static_cast<uint8_t>(i*17+i/32);
    const auto saved_game = gd::game;
    const auto saved_profile = gd::target::selected;
    gd::game = nullptr; gd::target::select(gd::target::supported[0]->sha256);
    auto memory = std::unique_ptr<void,gd::FreeBridge>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    check(memory!=nullptr,"allocate synthetic inline caller");
    auto code = static_cast<uint8_t*>(memory.get());
    for (bool aligned : {false,true}) {
        memset(code,0xcc,64);
        size_t offset = 0;
        if (aligned) { const uint8_t prolog[]{0x48,0x83,0xec,0x28}; memcpy(code,prolog,sizeof(prolog)); offset=4; }
        code[offset++]=0xf9; // STC; the bridge must preserve carry.
        memset(code+offset,0x90,5);
        if (aligned) {
            const uint8_t epilog[]{0x48,0x8d,0x64,0x24,0x28,0xc3}; // LEA preserves flags.
            memcpy(code+offset+5,epilog,sizeof(epilog));
        } else code[offset+5]=0xc3;
        const uint8_t alternate[]{0xb8,42,0,0,0,0xc3}; memcpy(code+32,alternate,sizeof(alternate));
        gd::target::Site site{gd::target::get().preferred_base+reinterpret_cast<uintptr_t>(code+offset),{0x90,0x90,0x90,0x90,0x90}};
        auto bridge = gd::prepare_inline_hook({&site,inline_observer});
        check(gd::write_memory(code+offset,bridge.replacement.data(),bridge.replacement.size()),"install synthetic inline bridge");
        Output output{}; inline_destination=0; inline_unwind_ok=false;
        gd_inline_probe(code,&output,vectors.data());
        for (size_t i=0;i<output.regs.size();++i) check(output.regs[i]==256+i,"inline bridge preserves all general registers");
        check(output.vectors==vectors,"inline bridge preserves all YMM lanes including upper halves");
        check((output.flags&1)!=0,"inline bridge restores original flags");
        check(inline_seen.rax==256 && inline_seen.rcx==257 && inline_seen.rbp==270,"inline context matches native register layout");
        check(inline_unwind_ok,"Windows virtual unwind reaches the selected continuation with original stack");
        if (!aligned) {
            inline_destination = reinterpret_cast<uintptr_t>(code+32);
            gd_inline_probe(code,&output,vectors.data());
            check(output.regs[0]==42,"inline handler can select a threshold branch destination");
        }
    }
    gd::game=saved_game; gd::target::selected=saved_profile;
    puts("PASS: inline hook GPR/YMM/flags preservation, both stack alignments, branch selection and Windows unwind");
}
