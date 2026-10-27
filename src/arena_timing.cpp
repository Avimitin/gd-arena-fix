#include "mod.h"
#include "arena_timing.h"
#include "scene_timing.h"
#include "scene_timeline.h"
#include "animations.h"
#include <unordered_map>
namespace gd {
namespace {
thread_local SceneTimeline timeline;
thread_local SceneTimeline::Event reveal;
thread_local std::unordered_map<uint32_t*,ElapsedNativeCounter> countdowns;
uint32_t& memory(uintptr_t va) {return *static_cast<uint32_t*>(at(va));}
void step(InlineContext& c) {
    const auto& p=target::get();const auto& f=scene_frame();auto& age=memory(p.arena_age);
    size_t index=0;for(;index<p.arena_steps.size();++index)if(c.ip==reinterpret_cast<uintptr_t>(at(p.arena_steps[index].address))+p.arena_steps[index].signature.length)break;
    if(index==p.arena_steps.size())return;
    age=f.active?timeline.begin(age,memory(p.arena_state),f.serial,f.elapsed):age+1;
    if(p.arena_step_eax[index])c.rax=age;c.step32(age);
}
void event(InlineContext& c) {
    const bool due=scene_frame().active?timeline.event(reveal,300):uint32_t(c.rax)==300;
    c.cmp32(uint32_t(c.rax),300);c.flags=(c.flags&~uint64_t(64))|(due?64:0);
}
uint32_t countdown(uint32_t& value,bool signed_end) {
    const auto& f=scene_frame();auto& clock=countdowns[&value];
    const auto amount=f.active?clock.advance(value,0,f.serial,f.elapsed):1;
    if(signed_end)value=uint32_t(std::max(int64_t(-1),int64_t(int32_t(value))-amount));
    else value-=std::min(value,amount);
    clock.finish(value);return value;
}
void gesture0(InlineContext& c) {
    const auto value=countdown(memory(target::get().arena_gesture_age0),true);
    c.cmp32(value+1,1); // Native CMOVS consumes the deadline's sign.
}
void gesture1(InlineContext& c) {
    const auto& p=target::get();c.rcx=countdown(memory(p.arena_gesture_age1),true);
    memory(p.arena_gesture_count0)=uint32_t(c.rax); // Displaced MOV, not a timer.
}
void voice(InlineContext& c) {
    auto& value=*reinterpret_cast<uint32_t*>(c.rsi);c.rax=countdown(value,false);c.cmp32(value+1,1);
}
void frame_event(InlineContext& c) {
    const auto& p=target::get();const auto found=std::find_if(p.frame_events.begin(),p.frame_events.end(),[&](const auto& e){
        return c.ip==reinterpret_cast<uintptr_t>(at(e.site.address))+e.site.signature.length;
    });
    if(found==p.frame_events.end())return;
    int32_t label=0;
    switch(found->label) {
    case target::FrameLabelRegister::Rax:label=int32_t(c.rax);break;
    case target::FrameLabelRegister::Rsi:label=int32_t(c.rsi);break;
    case target::FrameLabelRegister::Rdi:label=int32_t(c.rdi);break;
    case target::FrameLabelRegister::R14:label=int32_t(c.r14);break;
    }
    const bool due=animation_event(*static_cast<void**>(at(found->object)),label,found->site.address);
    c.cmp32(uint32_t(c.rbp),uint32_t(label));
    if(!due) {c.ip=reinterpret_cast<uintptr_t>(at(found->no));return;}
    switch(found->tail) {
    case target::FrameEventTail::None:break;
    case target::FrameEventTail::StackRbx:*reinterpret_cast<uint32_t*>(c.native_rsp()+found->stack_offset)=uint32_t(c.rbx);break;
    case target::FrameEventTail::ZeroR8:c.r8=0;c.cmp32(0,0);break;
    case target::FrameEventTail::Call:c.rax=reinterpret_cast<uint64_t(__fastcall*)()>(at(found->call))();break;
    }
}
}
std::vector<InlineHook> arena_inline_hooks() {
    const auto& p=target::get();std::vector<InlineHook> result{{&p.arena_event,event},{&p.arena_gesture0,gesture0},
        {&p.arena_gesture1,gesture1},{&p.arena_voice_delay,voice}};
    for(const auto& site:p.arena_steps)result.push_back({&site,step});
    for(const auto& event:p.frame_events)result.push_back({&event.site,frame_event});return result;
}
}
