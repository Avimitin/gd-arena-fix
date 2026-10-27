#include "mod.h"
#include "system_timing.h"
#include "scene_timing.h"
#include "scene_timeline.h"
#include <unordered_map>
namespace gd {
namespace {
thread_local std::unordered_map<uint32_t*,ElapsedNativeCounter> clocks;
thread_local ElapsedCounter wait_clock;
thread_local uint32_t wait_expected=0;
thread_local SceneTimeline init_clock;
thread_local SceneTimeline::Event init_cue;
uint32_t& memory(uintptr_t va) {return *static_cast<uint32_t*>(at(va));}
void wait_begin(InlineContext& c) {
    const auto& p=target::get();c.rax=memory(p.lifecycle_wait_state);
    const auto& f=scene_frame();if(!f.active)return;
    auto& age=memory(p.lifecycle_wait_age);
    if(age!=wait_expected)wait_clock.reset(f.serial);
    // This native prologue increments twice per update during its ten-count
    // wait: its real duration is five authored ticks, not ten.
    age=uint32_t(std::min(uint64_t(INT32_MAX),uint64_t(age)+wait_clock.advance(f.serial,f.elapsed,120,false)));
    wait_expected=age;
}
void wait_step(InlineContext& c) {
    if(!scene_frame().active) {auto& age=memory(target::get().lifecycle_wait_age);++age;c.step32(age);}
}
void init_step(InlineContext& c) {
    const auto& f=scene_frame();auto& age=memory(target::get().lifecycle_init_age);
    age=f.active?init_clock.begin(age,0,f.serial,f.elapsed):age+1;c.step32(age);
}
void init_event(InlineContext& c) {
    bool due=uint32_t(c.rax)==4320;
    if(scene_frame().active&&init_clock.initialized) {
        // This is a single global cue, unlike a per-player event loop.
        due=(!init_cue.fired||init_cue.epoch!=init_clock.epoch)&&init_clock.event(init_cue,4320);
    }
    c.cmp32(uint32_t(c.rax),4320);c.flags=(c.flags&~uint64_t(64))|(due?64:0);
}
void counter(InlineContext& c) {
    using F=target::CounterForm;
    const auto& p=target::get();
    const auto it=std::find_if(p.system_counters.begin(),p.system_counters.end(),[&](const auto& x){
        return c.ip==reinterpret_cast<uintptr_t>(at(x.site.address))+x.site.signature.length;
    });
    if(it==p.system_counters.end()) return;
    uintptr_t base=0;
    switch(it->base) {
    case target::CounterBase::Global:break;
    case target::CounterBase::Rdi:base=c.rdi;break;
    case target::CounterBase::Rbx:base=c.rbx;break;
    case target::CounterBase::R13:base=c.r13;break;
    case target::CounterBase::R14:base=c.r14;break;
    }
    auto& value=it->base==target::CounterBase::Global?memory(it->value):*reinterpret_cast<uint32_t*>(base+it->value);
    const auto form=it->form;
    uint32_t old=value;
    switch(form) {
    case F::IncEax:case F::IncEaxSetEbx:case F::IncEaxCmpEsi2:case F::DecEax:old=uint32_t(c.rax);break;
    case F::IncEcx:case F::IncEcxStoreR8:case F::IncEcxCmp60:case F::LeaEaxEcx:case F::DecEaxEcx:old=uint32_t(c.rcx);break;
    case F::LeaEaxEdx:old=uint32_t(c.rdx);break;
    case F::IncR8:case F::IncR8LoadRdx:case F::LeaEaxR8:old=uint32_t(c.r8);break;
    default:break;
    }
    const bool down=form==F::DecEax||form==F::DecEaxEcx;
    const auto& f=scene_frame();uint32_t amount=1;
    if(f.active) {
        auto& clock=clocks[&value];const auto identity=it->identity?
            (it->base==target::CounterBase::Global?memory(it->identity):*reinterpret_cast<uint32_t*>(base+it->identity)):0;
        const bool fresh=clock.restarted(old,identity,f.serial);
        amount=clock.advance(old,identity,f.serial,f.elapsed);
        // Consume the native zero-entry sentinel immediately, once per epoch.
        // Countdown duration starts when first observed, without removing a tick.
        if(fresh&&!down) ++amount;
    }
    if(!f.active) value=down?old-1:old+1;
    else if(down) value=old-std::min(old,amount);
    else value=uint32_t(std::min(int64_t(INT32_MAX),int64_t(int32_t(old))+amount));
    if(f.active) clocks[&value].finish(value);
    switch(form) {
    case F::IncMemory:c.step32(value);break;
    case F::IncEcxStoreR8:memory(it->auxiliary)=uint32_t(c.r8);[[fallthrough]];
    case F::IncEcx:c.rcx=value;c.step32(value);break;
    case F::IncR8LoadRdx:c.rdx=uint64_t(int64_t(int32_t(memory(it->auxiliary))));[[fallthrough]];
    case F::IncR8:c.r8=value;c.step32(value);break;
    case F::IncEcxCmp60:c.rcx=value;c.cmp32(value,60);break;
    case F::IncEaxCmpEsi2:c.rax=value;c.cmp32(uint32_t(c.rsi),2);break;
    case F::IncEaxSetEbx:c.rbx=1;[[fallthrough]];
    case F::IncEax:c.rax=value;c.step32(value);break;
    case F::DecEax:c.rax=value;c.step32(value,true);break;
    case F::LeaEaxEcx:c.rax=value;if(f.active)c.rcx=value-1;break;
    case F::LeaEaxEdx:c.rax=value;if(f.active)c.rdx=value-1;break;
    case F::LeaEaxR8:c.rax=value;if(f.active)c.r8=value-1;break;
    case F::DecEaxEcx:c.rax=value;break;
    case F::IncMemoryLoadEax:c.step32(value);c.rax=*reinterpret_cast<uint32_t*>(base+it->auxiliary);break;
    case F::IncMemoryRestoreRbp:c.step32(value);c.rbp=*reinterpret_cast<uint64_t*>(c.native_rsp()+it->auxiliary);break;
    case F::IncMemoryCall:c.step32(value);c.rax=reinterpret_cast<uint64_t(__fastcall*)()>(at(it->auxiliary))();break;
    case F::IncMemoryCmp:c.cmp32(value,uint32_t(it->auxiliary));break;
    }
}
}
std::vector<InlineHook> system_inline_hooks() {
    const auto& p=target::get();
    std::vector<InlineHook> hooks{{&p.lifecycle_wait_begin,wait_begin},{&p.lifecycle_init_step,init_step},{&p.lifecycle_init_event,init_event}};
    for(const auto& step:target::get().system_counters) hooks.push_back({&step.site,counter});
    for(const auto& s:p.lifecycle_wait_steps)hooks.push_back({&s,wait_step});
    return hooks;
}
}
