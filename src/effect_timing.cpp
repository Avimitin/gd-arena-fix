#include "mod.h"
#include "scene_timing.h"
#include "effect_timing.h"
#include "scene_timeline.h"
#include "animation_clock.h"
#include <unordered_map>

namespace gd {
namespace {
template<class T> T& memory(uintptr_t va) {return *static_cast<T*>(at(va));}
thread_local std::unordered_map<void*,ElapsedNativeCounter> clocks;
thread_local AnimationClock gf_effect_clock;
thread_local int gf_effect_expected=0;
thread_local SceneTimeline ending;
thread_local SceneTimeline::Event ending_cue;
struct SpinnerAlpha {uint64_t serial=0;int expected=0;double value=0;bool initialized=false;};
thread_local SpinnerAlpha spinner_alpha;
struct EffectClock {AnimationClock clock;int32_t expected=0;};
thread_local std::unordered_map<void*,EffectClock> short_effects;
thread_local std::unordered_map<void*,uint64_t> float_frames;
template<size_t N> size_t index_of(const std::array<target::Site,N>& sites,const InlineContext& c) {
    for(size_t i=0;i<N;++i) if(c.ip==reinterpret_cast<uintptr_t>(at(sites[i].address))+sites[i].signature.length) return i;
    return N;
}
void scalar(InlineContext& c,unsigned index,float value) {
    memcpy(c.ymm(index),&value,sizeof(value));memset(c.ymm(index)+16,0,16);
}
uint32_t ticks(uint32_t& value,uint32_t identity=0) {
    const auto& f=scene_frame();auto& clock=clocks[&value];
    return clock.advance(value,identity,f.serial,f.elapsed);
}
void finish(uint32_t& value) {clocks[&value].finish(value);}
void decrement(uint32_t& value) {
    value-=std::min(value,scene_frame().active?ticks(value):1U);
    if(scene_frame().active) finish(value);
}
void overlay_age(InlineContext& c) {
    const auto& p=target::get();auto& age=memory<uint32_t>(p.overlay_age);const auto& f=scene_frame();
    const bool down=index_of(p.overlay_age_steps,c)==1;
    if(!f.active) {age=down?age-1:age+1;c.step32(age,down);return;}
    const auto kind=memory<uint32_t>(p.result_request_code);
    const auto identity=memory<uint32_t>(p.result_request_active)|(kind<<8);
    auto& clock=clocks[&age];const bool fresh=!clock.initialized||clock.expected!=age||
        clock.identity!=identity||clock.clock.seen+1<f.serial;
    auto amount=clock.advance(age,identity,f.serial,f.elapsed);
    // Opening consumes the native zero-entry marker immediately. Closing is
    // a countdown; retain its whole duration and its exact zero terminal state.
    if(fresh&&!down)++amount;
    const uint32_t limit=(kind==1||kind==4||kind==6||kind==7||kind==13)?INT32_MAX:45;
    age=down?age-std::min(age,amount):uint32_t(std::min(uint64_t(limit),uint64_t(age)+amount));
    clock.finish(age);c.step32(age,down);
}
void gf_effect(InlineContext& c) {
    const auto& p=target::get();auto& age=memory<int32_t>(p.gf_effect_age);const auto& f=scene_frame();
    const auto amount=f.active?gf_effect_clock.advance(f.serial,f.elapsed,age!=gf_effect_expected):1U;
    const auto before=age;age=effect_counter(age,amount);gf_effect_expected=age;
    c.rax=uint32_t(!f.active && before>=8?before:age);
    c.ip=reinterpret_cast<uintptr_t>(at(p.gf_effect_next));
}
void combo_effect(InlineContext& c) {
    auto& age=memory<uint32_t>(target::get().combo_effect_age);decrement(age);c.rax=age;
}
void gf_number(InlineContext& c) {
    auto& value=*reinterpret_cast<uint32_t*>(c.rbx);
    auto& step=*reinterpret_cast<int32_t*>(c.rbx+target::layout::gf_number_step);
    auto& age=*reinterpret_cast<uint32_t*>(c.rbx+target::layout::gf_number_age);
    c.rdx=age;c.r14=value;
    const auto& f=scene_frame();if(!f.active)return;
    const auto goal=uint32_t(c.rdi);auto& clock=clocks[&age];
    const bool fresh=clock.restarted(age,goal,f.serial);
    auto amount=clock.advance(age,goal,f.serial,f.elapsed);
    // The game's target-change branch still computes its own seven-step
    // slope. Start immediately, then retain that slope and final snap.
    if(fresh)amount=std::max(amount,1U);
    int32_t displayed_step=0;
    for(uint32_t tick=0;tick<std::min(amount,8U);++tick) {
        c.r14=value;c.rdx=age;
        if(int32_t(age)>=7) {value=goal;step=0;displayed_step=0;break;}
        displayed_step=step;
        value=step<0 && int64_t(int32_t(value))<-int64_t(step)?0:value+uint32_t(step);
        ++age;
    }
    clock.finish(age);c.rdi=value;
    *reinterpret_cast<int32_t*>(c.native_rsp()+target::layout::gf_number_stack_step)=displayed_step;
    c.ip=reinterpret_cast<uintptr_t>(at(target::get().gf_number_next));
}
void effect_age(InlineContext& c) {
    const auto& p=target::get();
    for(size_t i=0;i<p.effect_age_steps.size();++i) {
        const auto& site=p.effect_age_steps[i];
        if(c.ip!=reinterpret_cast<uintptr_t>(at(site.address))+site.signature.length) continue;
        auto& value=memory<uint32_t>(p.effect_ages[i]);const auto amount=scene_frame().active?ticks(value):1U;
        value=i==2?static_cast<uint32_t>(std::min(uint64_t(60),uint64_t(value)+amount)):value+amount;
        if(scene_frame().active) finish(value);
        // Native floating-point comparisons/ordinary arithmetic replace flags.
        return;
    }
}
void shutter_fade(InlineContext& c) {
    auto& state=*reinterpret_cast<int32_t*>(c.rsi);
    auto& age=*reinterpret_cast<uint32_t*>(c.rsi+target::layout::shutter_fade_age);
    const auto& f=scene_frame();float sample=static_cast<float>(age);
    if(f.active) {
        age=static_cast<uint32_t>(std::min(uint64_t(10),uint64_t(age)+ticks(age,uint32_t(state)^(uint32_t(c.r8)<<16))));
        finish(age);sample=static_cast<float>(age);
        if(config.animation_timing==2) sample=std::min(10.0f,sample+static_cast<float>(clocks[&age].clock.phase));
    }
    float alpha=sample/10.0f;if(state>0) alpha=1-alpha;
    *reinterpret_cast<float*>(c.rcx+target::layout::shutter_alpha)=alpha;
    c.rdx=age;if(!f.active) ++age;c.rax=age;
    memset(c.ymm(0),0,32);memcpy(c.ymm(0),&sample,sizeof(sample));
    memset(c.ymm(1),0,32);memcpy(c.ymm(1),&alpha,sizeof(alpha));
    c.cmp32(age,10);
}
void end_event(InlineContext& c) {
    const auto& p=target::get();auto& age=memory<uint32_t>(p.end_age);const auto point=uint32_t(c.rcx);
    const auto& f=scene_frame();bool due=age==point;
    if(f.active) {
        age=ending.begin(age,point,f.serial,f.elapsed);due=ending.event(ending_cue,point);
    }
    c.cmp32(due?point:point+1,point);
}
void end_step(InlineContext&) {
    if(!scene_frame().active) ++memory<uint32_t>(target::get().end_age);
}
void hud_phase(InlineContext& c) {
    auto& age=memory<uint32_t>(target::get().hud_age);
    age=(age+(scene_frame().active?ticks(age):1U))&4095;
    if(scene_frame().active) finish(age);c.rax=age;
}
void hud_countdown(InlineContext& c) {
    auto& remaining=memory<uint32_t>(target::get().hud_countdowns+c.r10);decrement(remaining);
}
void gf_overlay_hold(InlineContext& c) {
    const auto& p=target::get();auto& remaining=memory<uint32_t>(p.gf_overlay_hold_value+c.r10);
    auto& alpha=memory<float>(p.gf_overlay_hold_alpha+c.r10);const auto& f=scene_frame();
    if(!f.active) {
        c.rax=remaining;memset(c.ymm(0),0,32);scalar(c,0,alpha);return;
    }
    const auto identity=memory<uint32_t>(p.gf_overlay_hold_identity+c.r10);
    auto& clock=clocks[&remaining];const auto prior_phase=clock.clock.phase;
    const bool continuous=clock.initialized && clock.expected==remaining && clock.identity==identity && clock.clock.seen+1==f.serial;
    const double dt=continuous?f.elapsed:0;
    const double up=std::min(dt,std::max(0.0,(double(remaining)-prior_phase)/60));
    const double down=dt-up;
    const double rate=60.0*memory<float>(p.gf_overlay_step);
    remaining-=std::min(remaining,clock.advance(remaining,identity,f.serial,f.elapsed));clock.finish(remaining);
    alpha=static_cast<float>(std::clamp(std::min(1.0,double(alpha)+up*rate)-down*rate,0.0,1.0));
    c.rax=remaining;memset(c.ymm(0),0,32);scalar(c,0,alpha);
    c.ip=reinterpret_cast<uintptr_t>(at(p.gf_overlay_hold_next));
}
void gf_overlay_counter(InlineContext& c) {
    const auto& p=target::get();const auto i=index_of(p.gf_overlay_counters,c);if(i==p.gf_overlay_counters.size()) return;
    auto& value=memory<uint32_t>(p.gf_overlay_counter_values[i]+c.r10);
    const auto identity=p.gf_overlay_counter_identities[i]?memory<uint32_t>(p.gf_overlay_counter_identities[i]+c.r10):0;
    const auto amount=scene_frame().active?ticks(value,identity):1U;
    value-=std::min(value,amount);if(scene_frame().active) finish(value);
    c.rax=i==1?memory<uintptr_t>(p.gf_overlay_counter_object+c.rbx*sizeof(uintptr_t)):value;
    if(i==2) c.rcx=362; // Native following SUB computes its current atlas cell.
}
void gf_overlay_fade(InlineContext& c) {
    const auto& p=target::get();const auto i=index_of(p.gf_overlay_fades,c);if(i==p.gf_overlay_fades.size()) return;
    const auto address=p.gf_overlay_alphas[i/2]+c.r10;auto& value=memory<float>(address);
    const auto& f=scene_frame();float step;memcpy(&step,c.ymm(6),sizeof(step));
    float amount=step;
    if(f.active) {
        auto& seen=float_frames[&value];amount=static_cast<float>(seen+1==f.serial?f.elapsed*60*step:0);seen=f.serial;
    }
    value+=i%2?-amount:amount;if(f.active) value=std::clamp(value,0.0f,1.0f);
    scalar(c,0,value);
    if(i%2==0) c.ip=reinterpret_cast<uintptr_t>(at(p.gf_overlay_fade_next[i/2]));
}
void gf_overlay_hit(InlineContext& c) {
    const auto& p=target::get();auto& value=memory<int32_t>(p.gf_overlay_hit_value+c.r10);
    auto& s=short_effects[&value];const auto& f=scene_frame();
    const auto amount=f.active?s.clock.advance(f.serial,f.elapsed,value!=s.expected):1U;
    value=effect_counter(value,amount);s.expected=value;c.rax=uint32_t(value);
    c.ip=reinterpret_cast<uintptr_t>(at(p.gf_overlay_hit_next));
}
void spinner_fade(InlineContext& c) {
    const auto& p=target::get();auto& alpha=memory<int32_t>(p.spinner_alpha);const auto& f=scene_frame();
    const bool visible=memory<uint8_t>(p.spinner_visible)!=0;double amount=8;
    if(f.active) {
        auto& s=spinner_alpha;const bool fresh=!s.initialized||s.expected!=alpha||s.serial+1<f.serial;
        if(fresh)s.value=alpha;
        amount=fresh?8:(s.serial!=f.serial?valid_elapsed(f.elapsed)*480:0);
        s.value=std::clamp(s.value+(visible?amount:-amount),0.,255.);
        alpha=int32_t(s.value+1e-8);s.expected=alpha;s.serial=f.serial;s.initialized=true;
    }else alpha=std::clamp(alpha+(visible?8:-8),0,255);
    c.rdi=uint32_t(alpha);c.ip=reinterpret_cast<uintptr_t>(at(p.spinner_draw_next));
}
void spinner_step(InlineContext& c) {
    auto& age=memory<uint32_t>(target::get().spinner_age);const auto amount=scene_frame().active?ticks(age):1U;
    age=uint32_t(std::min(uint64_t(INT32_MAX),uint64_t(age)+amount));
    if(scene_frame().active)finish(age);c.step32(age);
}
}
std::vector<InlineHook> effect_inline_hooks() {
    const auto& p=target::get();
    std::vector<InlineHook> hooks{{&p.gf_effect_step,gf_effect},{&p.combo_effect_step,combo_effect},
        {&p.gf_number_step,gf_number},
        {&p.shutter_fade,shutter_fade},{&p.end_event,end_event},{&p.end_step,end_step},
        {&p.hud_phase_step,hud_phase},{&p.hud_countdown_step,hud_countdown}};
    hooks.push_back({&p.spinner_fade,spinner_fade});hooks.push_back({&p.spinner_step,spinner_step});
    for(const auto& site:p.overlay_age_steps)hooks.push_back({&site,overlay_age});
    for(const auto& site:p.effect_age_steps) hooks.push_back({&site,effect_age});
    hooks.push_back({&p.gf_overlay_hold,gf_overlay_hold});hooks.push_back({&p.gf_overlay_hit,gf_overlay_hit});
    for(const auto& site:p.gf_overlay_counters) hooks.push_back({&site,gf_overlay_counter});
    for(const auto& site:p.gf_overlay_fades) hooks.push_back({&site,gf_overlay_fade});
    return hooks;
}
}
