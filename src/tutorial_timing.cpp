#include "mod.h"
#include "tutorial_timing.h"
#include "scene_timing.h"
#include "scene_timeline.h"
#include <unordered_map>
namespace gd {
namespace {
thread_local ElapsedNativeCounter remaining_clock;
thread_local ElapsedCounter position_clock;
thread_local uint32_t position_expected=0,previous=0,current=0;
thread_local bool first=false;
thread_local uint32_t practice_previous=0,practice_current=0;
thread_local uint64_t practice_epoch=0;
struct PracticeCue {uint64_t epoch=0;uint32_t note=0;bool fired=false;};
thread_local std::unordered_map<uintptr_t,PracticeCue> practice_cues;
thread_local std::unordered_map<void*,ElapsedNativeCounter> clocks;
template<class T> T& memory(uintptr_t va) {return *static_cast<T*>(at(va));}
template<class T> T& field(uintptr_t p,ptrdiff_t offset=0) {return *reinterpret_cast<T*>(p+offset);}
void begin(InlineContext& c) {
    const auto& p=target::get();auto& remaining=memory<uint32_t>(p.tutorial_remaining);
    const auto state=memory<uint32_t>(p.tutorial_state);const auto& f=scene_frame();
    c.rdx=uint64_t(int64_t(int32_t(state)));
    if(!f.active)return;
    first=remaining_clock.restarted(remaining,state,f.serial);previous=remaining;
    const double dt=!first&&remaining_clock.clock.seen!=f.serial?valid_elapsed(f.elapsed):0;
    const double from=double(remaining)-remaining_clock.clock.phase;
    remaining-=std::min(remaining,remaining_clock.advance(remaining,state,f.serial,f.elapsed));
    remaining_clock.finish(remaining);current=remaining;
    auto& position=memory<uint32_t>(p.tutorial_position);
    if(first||position!=position_expected)++practice_epoch;
    if(position!=position_expected)position_clock.reset(f.serial);
    practice_previous=position;
    double running=memory<uint8_t>(p.tutorial_pause)?0:dt;
    // Scripted pause windows may be entered and exited within a single stall.
    // Integrate the whole interval, including boundaries the renderer skipped.
    if(state==p.tutorial_pause_states[0]) {
        const double paused=std::max(0.0,std::min(from,double(p.tutorial_pause_boundaries[0]))-
            std::max(from-dt*60,double(p.tutorial_pause_boundaries[1])))/60;
        running=std::max(0.0,dt-paused);
    } else if(state==p.tutorial_pause_states[1]) {
        running=std::clamp((from-p.tutorial_pause_boundaries[1])/60,0.0,dt);
    }
    position=static_cast<uint32_t>(std::min(uint64_t(INT32_MAX),uint64_t(position)+
        position_clock.advance(f.serial,running,300,false)));
    position_expected=position;practice_current=position;
}
void practice_cue(InlineContext& c) {
    const auto& p=target::get();c.cmp32(uint32_t(c.rdx),uint32_t(c.rax));
    if(!scene_frame().active) {
        c.ip=reinterpret_cast<uintptr_t>(at(int32_t(c.rdx)>int32_t(c.rax)?p.tutorial_practice_cue_native:p.tutorial_practice_cue_no));return;
    }
    auto& gate=practice_cues[c.rdi];const auto note=uint32_t(c.rcx);
    if(gate.epoch!=practice_epoch||gate.note!=note)gate={practice_epoch,note,false};
    // Native 60 Hz visits the centre of this ten-position window once.
    // A crossing remains valid if the current frame has passed the far edge.
    const int64_t point=int64_t(int32_t(note))-145;
    const bool due=!gate.fired&&int64_t(practice_previous)<point&&int64_t(practice_current)>=point;
    if(due) {
        gate.fired=true;c.r13=field<uint64_t>(c.native_rsp(),p.tutorial_practice_saved_r13);
    }
    c.ip=reinterpret_cast<uintptr_t>(at(due?p.tutorial_practice_cue_yes:p.tutorial_practice_cue_no));
}
void end(InlineContext& c) {
    const auto& p=target::get();auto& remaining=memory<uint32_t>(p.tutorial_remaining);const auto& f=scene_frame();
    if(!f.active) {remaining=uint32_t(c.rax)-1;c.rax=remaining;c.step32(remaining,true);return;}
    // Native scene changes can load a new countdown inside this invocation.
    // Establish its epoch now; time for the old stage cannot shorten the new one.
    remaining_clock.advance(remaining,memory<uint32_t>(p.tutorial_state),f.serial,0);
    remaining_clock.finish(remaining);c.rax=remaining;
}
void phase(InlineContext&) {
    if(!scene_frame().active)memory<uint32_t>(target::get().tutorial_position)+=5;
}
void cue(InlineContext& c) {
    const auto& p=target::get();
    for(size_t i=0;i<p.tutorial_events.size();++i) {
        const auto& e=p.tutorial_events[i];
        if(c.ip!=reinterpret_cast<uintptr_t>(at(e.site.address))+e.site.signature.length)continue;
        const auto value=memory<uint32_t>(p.tutorial_remaining);c.cmp32(value,e.tick);
        if(scene_frame().active) {
            const bool due=(first&&current==e.tick)||(previous>e.tick&&current<=e.tick);
            // One comparison also feeds CMOVL. Keep its signed-order flags;
            // only the equality branch represents a one-shot cue.
            c.flags=(c.flags&~uint64_t(64))|(due?64:0);
        }
        return;
    }
}
void countdown(InlineContext& c) {
    const auto& p=target::get();size_t i=0;
    for(;i<p.tutorial_countdowns.size();++i) {
        const auto& s=p.tutorial_countdowns[i];
        if(c.ip==reinterpret_cast<uintptr_t>(at(s.address))+s.signature.length)break;
    }
    if(i==p.tutorial_countdowns.size())return;
    const uintptr_t pointer=i==2?reinterpret_cast<uintptr_t>(at(p.tutorial_input_delay)):
        (i==0?c.rbx:i==1||i==5?c.rdi:c.r14)+p.tutorial_countdown_offsets[i];
    uint32_t old=0;memcpy(&old,reinterpret_cast<void*>(pointer),4);
    if(i==0||i==1||i==2||i==4)old=uint32_t(c.rax);
    const auto& f=scene_frame();auto& clock=clocks[reinterpret_cast<void*>(pointer)];
    const uint32_t amount=f.active?clock.advance(old,0,f.serial,f.elapsed):1;
    // The hit branch deliberately supplies zero to store the native -1 marker.
    const uint32_t value=int32_t(old)<=0?old-1:old-std::min(old,amount);
    memcpy(reinterpret_cast<void*>(pointer),&value,4);if(f.active)clock.finish(value);
    c.step32(value,true);
    if(i==3)c.rax=field<uint32_t>(c.r14,p.tutorial_next_effect_distance);
    else if(i==5)c.rdi+=sizeof(uint32_t); // Next instruction replaces ADD flags.
    else {c.rax=value;if(i==4)c.rcx=180;}
}
void gauge(InlineContext& c) {
    const auto& p=target::get();auto& value=field<int32_t>(c.r14);
    const auto goal=field<int32_t>(c.r14,-ptrdiff_t(p.tutorial_gauge_target_distance));
    const auto& f=scene_frame();auto& clock=clocks[&value];
    auto amount=f.active?clock.advance(uint32_t(value),uint32_t(goal),f.serial,f.elapsed):1;
    // Native integer easing reaches a fixed point within a few hundred steps.
    // Stop there, so a long stall has bounded cost without dropping elapsed time.
    while(amount--) {const auto next=value+(goal-value)/10;if(next==value)break;value=next;}
    if(f.active)clock.finish(uint32_t(value));
    double sample=value;
    if(f.active&&config.animation_timing==2)sample+=((goal-value)/10)*clock.clock.phase;
    c.rcx=uint32_t(int32_t(sample));c.r8=uint32_t(goal);
    c.ip=reinterpret_cast<uintptr_t>(at(p.tutorial_gauge_next));
}
}
std::vector<InlineHook> tutorial_inline_hooks() {
    const auto& p=target::get();std::vector<InlineHook> hooks{{&p.tutorial_begin,begin},{&p.tutorial_end,end},
        {&p.tutorial_phase_step,phase},{&p.tutorial_gauge,gauge},{&p.tutorial_practice_cue,practice_cue}};
    for(const auto& e:p.tutorial_events)hooks.push_back({&e.site,cue});
    for(const auto& s:p.tutorial_countdowns)hooks.push_back({&s,countdown});
    return hooks;
}
}
