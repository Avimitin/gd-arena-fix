#include "mod.h"
#include "service_timing.h"
#include "scene_timing.h"
#include "scene_timeline.h"
#include "animation_clock.h"
#include "animations.h"
#include <unordered_map>
namespace gd {
namespace {
thread_local std::unordered_map<void*,ElapsedNativeCounter> clocks;
thread_local AnimationClock poll_clock;
thread_local uint32_t poll_expected=0;
thread_local SceneTimeline qc_clock,card_clock;
template<class T> T& memory(uintptr_t va) {return *static_cast<T*>(at(va));}
template<class T> T& field(uintptr_t p,size_t offset=0) {return *reinterpret_cast<T*>(p+offset);}
template<class T> uint32_t elapsed(T& value,uint32_t identity=0) {
    const auto& f=scene_frame();return f.active?clocks[&value].advance(value,identity,f.serial,f.elapsed):1;
}
template<class T> void finish(T& value) {if(scene_frame().active) clocks[&value].finish(value);}
uint32_t increment(uint32_t& value,uint32_t identity=0) {
    value=static_cast<uint32_t>(std::min(uint64_t(INT32_MAX),uint64_t(value)+elapsed(value,identity)));finish(value);return value;
}
template<class T> uint32_t decrement(T& value) {value=static_cast<T>(value-std::min(uint32_t(value),elapsed(value)));finish(value);return value;}
template<size_t N> size_t index_of(const std::array<target::Site,N>& sites,const InlineContext& c) {
    for(size_t i=0;i<N;++i) if(c.ip==reinterpret_cast<uintptr_t>(at(sites[i].address))+sites[i].signature.length) return i;
    return N;
}
void cooldown(InlineContext&) {increment(memory<uint32_t>(target::get().judgment_cooldown));}
void held(InlineContext& c) {increment(memory<uint32_t>(target::get().judgment_hold+c.r15*sizeof(uint32_t)));}
void feedback(InlineContext& c) {increment(field<uint32_t>(c.r14,target::layout::judgment_feedback));}
void light(InlineContext&) {increment(memory<uint32_t>(target::get().service_light_age));}
void dialog(InlineContext& c) {
    const auto& p=target::get();auto& age=memory<uint32_t>(p.service_dialog_age);const auto old=age;
    const auto& f=scene_frame();const auto identity=memory<uint32_t>(p.service_dialog_state);
    auto& clock=clocks[&age];
    const bool first=!clock.initialized || clock.expected!=age || clock.identity!=identity || clock.clock.seen+1<f.serial;
    increment(age,identity);
    // Callers use zero as an initialization sentinel. Preserve the immediate
    // first increment, while comparisons sample elapsed authored time.
    if(f.active && first) {++age;finish(age);}
    c.rax=age;const float sample=static_cast<float>(f.active?age-1:old);
    memset(c.ymm(0),0,32);memcpy(c.ymm(0),&sample,sizeof(sample));
}
void word_countdown(InlineContext& c) {
    const auto& p=target::get();const auto i=index_of(p.service_word_steps,c);if(i==p.service_word_steps.size()) return;
    c.rax=decrement(memory<uint16_t>(p.service_words[i]));
}
void io(InlineContext& c) {
    const auto& p=target::get();const auto i=index_of(p.service_io_steps,c);
    if(i==0) {c.rax=decrement(field<uint32_t>(c.rbx));field<uint32_t>(c.rbx,target::layout::service_io_pending)=uint32_t(c.r15);}
    else if(i==1) c.rax=decrement(field<uint32_t>(c.rbx,target::layout::service_io_pending));
    else if(i==2) {
        c.rax=increment(field<uint32_t>(c.rbx,target::layout::service_io_hold));
        c.ip=reinterpret_cast<uintptr_t>(at(p.service_io_next));
    }
}
void poll(InlineContext& c) {
    const auto& p=target::get();const auto i=index_of(p.service_poll_steps,c);if(i==p.service_poll_steps.size()) return;
    auto& value=memory<uint32_t>(p.service_poll_age);const auto& f=scene_frame();const auto old=value;
    const auto amount=f.active?poll_clock.advance(f.serial,f.elapsed,value!=poll_expected):1U;
    value=static_cast<uint32_t>(std::min(uint64_t(INT32_MAX),uint64_t(value)+amount));poll_expected=value;c.rax=value;
    const bool due=f.active?((uint64_t(value)+1)/2!=(uint64_t(old)+1)/2):(value&1)!=0;
    c.ip=reinterpret_cast<uintptr_t>(at(due?p.service_poll_yes[i]:p.service_poll_no[i]));
}
void qc(InlineContext& c) {
    if(!scene_frame().active) {c.rax=0x88888889;return;} // Native reciprocal division setup.
    const auto& p=target::get();const auto& f=scene_frame();auto& age=memory<uint32_t>(p.qc_age);
    age=qc_clock.begin(age,0,f.serial,f.elapsed);const auto pulses=qc_clock.periods(0,60);
    if(pulses) {
        auto& brightness=memory<uint8_t>(p.qc_brightness);
        if(pulses&1)brightness=brightness==1?127:1;
        memory<uint32_t>(p.qc_phase)=(memory<uint32_t>(p.qc_phase)+pulses)%8;
    }
    c.ip=reinterpret_cast<uintptr_t>(at(pulses?p.qc_due:p.qc_idle));
}
void qc_native_step(InlineContext& c) {
    if(!scene_frame().active) {auto& age=memory<uint32_t>(target::get().qc_age);++age;c.step32(age);}
}
void card_step(InlineContext& c) {
    auto& age=memory<uint32_t>(target::get().card_error_age);const auto& f=scene_frame();
    age=f.active?card_clock.begin(uint32_t(c.r8),0,f.serial,f.elapsed):uint32_t(c.r8)+1;
    c.r8=age;c.step32(age);
}
void card_period(InlineContext& c) {
    const auto& p=target::get();const bool due=scene_frame().active?
        (card_clock.value<600&&card_clock.periods(180,180)!=0):uint32_t(c.r8)==uint32_t(c.rcx);
    c.cmp32(uint32_t(c.r8),uint32_t(c.rcx));c.ip=reinterpret_cast<uintptr_t>(at(due?p.card_error_due:p.card_error_idle));
}
void card_alternate(InlineContext& c) {
    auto& age=memory<uint32_t>(target::get().card_error_age);const auto& f=scene_frame();
    age=f.active?card_clock.begin(uint32_t(c.rax),0,f.serial,f.elapsed):uint32_t(c.rax)+1;
    c.rax=age;c.step32(age);
}
void card_timeout(InlineContext& c) {
    auto& value=*reinterpret_cast<uint32_t*>(c.rbx+target::get().card_timeout_offset);
    static thread_local std::unordered_map<uint32_t*,ElapsedNativeCounter> deadlines;
    const auto& f=scene_frame();auto& clock=deadlines[&value];
    const auto amount=f.active?clock.advance(value,0,f.serial,f.elapsed):1;
    value-=std::min(value,amount);clock.finish(value);c.cmp32(value,uint32_t(c.rbp));
}
void card_release(InlineContext& c) {
    const auto& p=target::get();auto& age=field<uint32_t>(c.rbx);increment(age);
    if(index_of(p.card_release_steps,c)==0)c.cmp32(memory<uint32_t>(p.card_release_enabled),0);
    else {c.step32(age);*reinterpret_cast<uint32_t*>(c.rbx+p.card_release_state_offset)=2;}
}
void card_release_end(InlineContext& c) {
    const auto& p=target::get();const auto age=memory<uint32_t>(p.card_release_age);
    const bool due=scene_frame().active&&animation_event(memory<void*>(p.card_release_object),int32_t(c.rdi),p.card_release_end.address);
    c.cmp32(due?601:age,600);
}
}
std::vector<InlineHook> service_inline_hooks() {
    const auto& p=target::get();std::vector<InlineHook> hooks{{&p.judgment_cooldown_step,cooldown},
        {&p.judgment_hold_step,held},{&p.judgment_feedback_step,feedback},{&p.service_light_step,light}};
    for(const auto& site:p.service_dialog_steps) hooks.push_back({&site,dialog});
    for(const auto& site:p.service_io_steps) hooks.push_back({&site,io});
    for(const auto& site:p.service_word_steps) hooks.push_back({&site,word_countdown});
    for(const auto& site:p.service_poll_steps) hooks.push_back({&site,poll});
    hooks.push_back({&p.qc_schedule,qc});hooks.push_back({&p.card_error_step,card_step});hooks.push_back({&p.card_error_period,card_period});
    hooks.push_back({&p.card_timeout_step,card_timeout});hooks.push_back({&p.card_alternate_step,card_alternate});
    for(const auto& site:p.qc_native_steps) hooks.push_back({&site,qc_native_step});
    for(const auto& site:p.card_release_steps)hooks.push_back({&site,card_release});
    hooks.push_back({&p.card_release_end,card_release_end});
    return hooks;
}
}
