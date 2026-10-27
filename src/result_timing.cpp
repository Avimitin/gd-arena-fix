#include "mod.h"
#include "result_timing.h"
#include "scene_timing.h"
#include "scene_timeline.h"
#include <unordered_map>

namespace gd {
namespace {
template<class T> T& memory(uintptr_t va) {return *static_cast<T*>(at(va));}
thread_local SceneTimeline result_clock,color_clock;
thread_local std::array<SceneTimeline::Event,13> result_events;
thread_local std::unordered_map<void*,ElapsedNativeCounter> countdowns;
bool active() {return scene_frame().active && result_clock.initialized && result_clock.clock.seen==scene_frame().serial;}
void synchronize() {
    const auto& p=target::get();
    result_clock.synchronize(memory<uint32_t>(p.result_age),memory<uint32_t>(p.result_state),scene_frame().serial);
    color_clock.synchronize(memory<uint32_t>(p.result_color),0,scene_frame().serial);
}
template<class T,size_t N> size_t site_index(const std::array<T,N>& sites,const InlineContext& c) {
    for(size_t i=0;i<N;++i) {
        const target::Site* site;
        if constexpr(std::is_same_v<T,target::Site>) site=&sites[i];else site=&sites[i].site;
        if(c.ip==reinterpret_cast<uintptr_t>(at(site->address))+site->signature.length) return i;
    }
    return N;
}
void result_begin(InlineContext& c) {
    const auto& p=target::get();
    const auto state=memory<uint32_t>(p.result_state);
    if(scene_frame().active) {
        auto& age=memory<uint32_t>(p.result_age);
        age=result_clock.begin(age,state,scene_frame().serial,scene_frame().elapsed);
        auto& color=memory<uint32_t>(p.result_color);
        color=color_clock.begin(color,0,scene_frame().serial,scene_frame().elapsed);
    }
    c.rax=static_cast<uint64_t>(int64_t(int32_t(state))); // MOVSXD RAX,[state].
}
void result_end(InlineContext&) {
    const auto& p=target::get();
    if(active()) synchronize();
    else {++memory<uint32_t>(p.result_color);++memory<uint32_t>(p.result_age);}
    // The immediately following color helper replaces flags before reading them.
}
void result_event(InlineContext& c) {
    const auto& p=target::get();
    const auto index=site_index(p.result_events,c);
    if(index==p.result_events.size()) return;
    const auto age=memory<uint32_t>(p.result_age),point=p.result_events[index].tick;
    bool due=age==point;
    if(active()) {synchronize();due=result_clock.event(result_events[index],point);}
    if(index<9) c.cmp32(due?point:point+1,point);
    else {
        // The request helper's SETG also uses the real comparison's flags.
        c.cmp32(age,point);
        c.ip=reinterpret_cast<uintptr_t>(at(due?p.result_event_yes[index-9]:p.result_event_no[index-9]));
    }
}
void result_period(InlineContext& c) {
    const auto& p=target::get();const auto i=site_index(p.result_periods,c);
    if(i==p.result_periods.size()) return;
    if(i<2) {
        if(!active()) {c.cmp32(memory<uint32_t>(p.result_age),uint32_t(c.rax));return;}
        synchronize();c.cmp32(result_clock.periods(0,3)?0:1,0);
    } else {
        bool due=uint32_t(c.rcx)==15;
        if(active()) {synchronize();due=color_clock.periods(15,40)!=0;}
        c.cmp32(uint32_t(c.rcx),15);
        c.ip=reinterpret_cast<uintptr_t>(at(due?p.result_color_yes:p.result_color_no));
    }
}
uint32_t countdown(uint32_t& value) {
    if(!active()) return --value;
    auto& clock=countdowns[&value];
    const auto ticks=clock.advance(value,0,scene_frame().serial,scene_frame().elapsed);
    value-=std::min(value,ticks);clock.finish(value);return value;
}
void result_sound_step(InlineContext& c) {
    const auto value=countdown(*reinterpret_cast<uint32_t*>(c.rbx));
    c.rax=value;c.cmp32(value,0); // Native SUB's ZF controls sound dispatch.
}
void result_queue(InlineContext& c) {
    auto& left=*reinterpret_cast<uint32_t*>(c.rbx);const uint32_t old=uint32_t(c.rax);
    const auto& f=scene_frame();
    if(f.active) {
        auto& clock=countdowns[&left];
        left=old-std::min(old,clock.advance(old,0,f.serial,f.elapsed));clock.finish(left);c.rax=left;
    } else {c.rax=old-uint32_t(c.r14);left=uint32_t(c.rax);}
    c.cmp32(uint32_t(c.rax),0);
}
void result_countdown(InlineContext& c) {
    const auto& sites=target::get().result_countdowns;const auto i=site_index(sites,c);
    if(i==sites.size()) return;
    auto& value=*reinterpret_cast<uint32_t*>(c.r12+c.rsi*sizeof(uint32_t)+target::layout::result_countdown_offsets[i]);
    const auto before=value;countdown(value);c.rax=value;
    // A render stall can jump directly past 1 to 0. Deliver the transition
    // once; holding age 1 across multiple renders must not replay it.
    const bool due=active()?before>1 && value<=1:value==1;
    c.cmp32(due?1:2,1);
}
int32_t advance_number(int32_t value,int32_t step,int32_t goal,uint32_t start) {
    uint32_t ticks=1;
    if(active()) {synchronize();ticks=result_clock.steps_from(start);}
    return static_cast<int32_t>(std::clamp(int64_t(value)+int64_t(step)*ticks,int64_t(INT32_MIN),int64_t(goal)));
}
void result_countup(InlineContext& c) {
    const auto& sites=target::get().result_countups;const auto i=site_index(sites,c);
    if(i>=sites.size()) return;
    if(i<2) {
        const auto step=*reinterpret_cast<int32_t*>(i==0?c.rbx+target::layout::result_countup_step:
            c.rbx+c.r9+target::layout::result_countup1_step);
        c.rcx=uint32_t(advance_number(int32_t(c.rcx),step,int32_t(c.rdx),i==0?6:21));
        c.cmp32(uint32_t(c.rcx),uint32_t(c.rdx));
    } else if(i==2) {
        c.rax=uint32_t(advance_number(int32_t(c.r8),int32_t(c.rdx),int32_t(c.rcx),0));
        c.cmp32(uint32_t(c.rax),uint32_t(c.rcx));
    } else {
        c.rax=uint32_t(advance_number(int32_t(c.rcx),int32_t(c.rdx),int32_t(c.rdi),0));
        memory<uint32_t>(target::get().result_gauges+2*sizeof(uint32_t)+c.rbp*sizeof(uint32_t))=uint32_t(c.rax);
        // Native CMP EAX,EDI follows this store.
    }
}
thread_local SceneTimeline battle_clock;
thread_local std::array<SceneTimeline::Event,10> battle_events;
thread_local std::array<SceneTimeline::Event,5> battle_sequence;
thread_local std::array<bool,3> battle_cross20;
bool battle_active() {return scene_frame().active && battle_clock.initialized && battle_clock.clock.seen==scene_frame().serial;}
void battle_sync() {
    const auto& p=target::get();battle_clock.synchronize(memory<uint32_t>(p.battle_age),memory<uint32_t>(p.battle_state),scene_frame().serial);
}
void battle_begin(InlineContext& c) {
    const auto& p=target::get();
    if(scene_frame().active) {
        auto& age=memory<uint32_t>(p.battle_age);
        age=battle_clock.begin(age,memory<uint32_t>(p.battle_state),scene_frame().serial,scene_frame().elapsed);
    }
    c.cmp32(uint32_t(c.rdi),11);
    c.ip=reinterpret_cast<uintptr_t>(at(uint32_t(c.rdi)<=11?p.battle_dispatch:p.battle_dispatch_default));
}
void battle_step(InlineContext& c) {
    if(!battle_active()) c.r8=uint32_t(c.r8)+1;
    memory<uint32_t>(target::get().battle_age)=uint32_t(c.r8);
}
void battle_end(InlineContext& c) {
    if(battle_active()) battle_sync();
    c.rcx=memory<uint32_t>(target::get().battle_scores+target::layout::battle_score_value);
}
void battle_event(InlineContext& c) {
    const auto& p=target::get();const auto i=site_index(p.battle_events,c);
    if(i==p.battle_events.size()) return;
    const auto& event=p.battle_events[i];const auto age=memory<uint32_t>(p.battle_age);
    bool due=age==event.tick;
    if(battle_active()) {battle_sync();due=battle_clock.event(battle_events[i],event.tick);}
    // Both sites dispatch the same click; acknowledge the later one when a
    // stall crosses both, so it cannot replay on the following render.
    if(battle_active() && i==7 && age>=32) (void)battle_clock.event(battle_events[8],32);
    c.cmp32(age,event.tick); // A following range branch may also need these flags.
    auto next=due?event.yes:event.no;
    if(i==4 && age>=20) {
        // This cue used to sit inside age<20. Preserve it across a stall
        // without postponing the later roulette phase to another render.
        if(due) reinterpret_cast<void(__fastcall*)(void*,const char*)>(at(p.animation_label))(
            memory<void*>(p.battle_intro_object),static_cast<const char*>(at(p.label_in)));
        next=p.battle_intro_late;
    }
    c.ip=reinterpret_cast<uintptr_t>(at(next));
}
void battle_cues(InlineContext& c) {
    constexpr uint32_t points[]{8,21,34,47,78};
    const auto& p=target::get();const auto age=memory<uint32_t>(p.battle_age);
    bool due=false;
    if(battle_active()) {
        battle_sync();for(size_t i=0;i<battle_sequence.size();++i) due=battle_clock.event(battle_sequence[i],points[i])||due;
    } else for(auto point:points) due|=point==age;
    c.ip=reinterpret_cast<uintptr_t>(at(due?p.battle_cue_yes:p.battle_cue_no));
}
void battle_numbers(InlineContext& c) {
    const auto& p=target::get();const auto i=site_index(p.battle_numbers,c);
    if(i==p.battle_numbers.size()) return;
    const uintptr_t category=p.battle_scores+i*target::layout::battle_score_category_stride;
    memory<uint32_t>(category)=uint32_t(c.r14);c.r9=uint32_t(c.rsi);
    if(!battle_active()) return;
    battle_sync();bool counting=false;
    const auto early=battle_clock.steps_from(0)-battle_clock.steps_from(40);
    const auto late=battle_clock.steps_from(40)-battle_clock.steps_from(60);
    for(size_t player=0;player<2;++player) {
        const auto base=category+player*target::layout::battle_player_stride;
        memory<uint32_t>(base)=1;
        const auto initial=memory<int32_t>(base+target::layout::battle_score_initial);
        const auto goal=memory<int32_t>(base+target::layout::battle_score_goal);
        auto& value=memory<int32_t>(base+target::layout::battle_score_value);
        bool initial_complete=false;
        if(early) {
            const int64_t raw=int64_t(value)+int64_t(std::max(initial/31,1))*early;
            initial_complete=raw>initial;
            value=static_cast<int32_t>(std::min(raw,int64_t(initial)));
            if(battle_clock.value<40) counting|=!initial_complete;
        }
        if(late) {
            const int64_t raw=int64_t(value)+int64_t(std::max(goal-initial/20,1))*late;
            value=static_cast<int32_t>(std::min(raw,int64_t(goal)));
            if(battle_clock.value<60) counting|=raw<=goal;
        }
        if(battle_clock.value>=60) value=goal;
        auto copy=[&](size_t offset) {memory<int32_t>(base+offset+target::layout::battle_score_property_display)=memory<int32_t>(base+offset);};
        if(initial_complete || battle_clock.value>=40) copy(target::layout::battle_score_property0);
        if(battle_clock.value>=40) copy(target::layout::battle_score_property1);
        if(battle_clock.value>=60) copy(target::layout::battle_score_property2);
    }
    c.r9=counting?1:0;c.ip=reinterpret_cast<uintptr_t>(at(p.battle_number_next[i]));
}
void battle_number_cue(InlineContext& c) {
    const auto& p=target::get();const auto i=site_index(p.battle_number_cues,c);
    if(i==p.battle_number_cues.size()) return;
    bool due=uint32_t(c.rax)==uint32_t(c.r14);
    if(battle_active()) {battle_sync();due=battle_clock.periods(1,4)!=0;}
    c.ip=reinterpret_cast<uintptr_t>(at(due?p.battle_number_cue_yes[i]:p.battle_number_cue_no[i]));
}
void battle_roulette(InlineContext& c) {
    const auto& p=target::get();
    if(!battle_active()) {
        c.ip=reinterpret_cast<uintptr_t>(at((c.r14&c.r9&0xff)?p.battle_roulette_no:p.battle_roulette_yes));return;
    }
    battle_sync();const auto steps=battle_clock.periods(32,2);
    if(!steps) {c.ip=reinterpret_cast<uintptr_t>(at(p.battle_roulette_no));return;}
    const auto goal=memory<int32_t>(p.battle_roulette_goal),start=memory<int32_t>(p.battle_roulette_start);
    auto& value=memory<int32_t>(p.battle_roulette_value);
    const int64_t difference=int64_t(goal)-start;
    const int64_t step=difference>0?std::max(difference/30,int64_t(1)):std::min(difference/30,int64_t(-1));
    if(difference>0) value=static_cast<int32_t>(std::min(int64_t(goal),int64_t(value)+step*steps));
    if(difference<0) value=static_cast<int32_t>(std::max(int64_t(goal),int64_t(value)+step*steps));
    c.r8=uint32_t(goal);c.r10=uint32_t(start);
    c.ip=reinterpret_cast<uintptr_t>(at(p.battle_roulette_after));
}
void battle_awards(InlineContext& c) {
    const auto& p=target::get();
    if(!battle_active()) {
        c.cmp32(uint32_t(c.r8),60);
        c.ip=reinterpret_cast<uintptr_t>(at(int32_t(c.r8)>60?p.battle_awards_stock:p.battle_awards_next));return;
    }
    battle_sync();const auto steps=battle_clock.periods(79,20);
    const auto offset=memory<uint32_t>(p.battle_player)*target::layout::battle_player_stride;
    bool transferred=false;
    for(size_t column=0;column<target::layout::battle_award_columns;++column) {
        auto& pending=memory<int32_t>(p.battle_awards_pending+offset+column*sizeof(int32_t));
        auto& value=memory<int32_t>(p.battle_awards_values+offset+column*sizeof(int32_t));
        const auto count=static_cast<int32_t>(std::min(uint32_t(std::max(pending,0)),steps));
        value+=count;pending-=count;transferred|=count!=0;
    }
    c.ip=reinterpret_cast<uintptr_t>(at(transferred?p.battle_awards_sound:p.battle_awards_next));
}
uint32_t battle_countdown(uint32_t& value,bool enabled,uint32_t state=0) {
    if(!enabled) return --value;
    auto& clock=countdowns[&value];const auto ticks=clock.advance(value,state,scene_frame().serial,scene_frame().elapsed);
    value-=std::min(value,ticks);clock.finish(value);return value;
}
void battle_countdown_step(InlineContext& c) {
    const auto& p=target::get();const auto i=site_index(p.battle_countdowns,c);
    if(i==p.battle_countdowns.size()) return;
    auto& value=memory<uint32_t>(p.battle_counter0+(2-i)*sizeof(uint32_t));const auto before=value;
    battle_countdown(value,battle_active());
    battle_cross20[i]=before>20 && value<=20 && value>0;
    if(i==2) c.rax=value;
    c.cmp32(value,0);
}
void battle_countdown_cue(InlineContext& c) {
    const auto& p=target::get();const auto i=site_index(p.battle_countdown_cues,c);
    if(i==p.battle_countdown_cues.size()) return;
    const bool due=battle_active()?battle_cross20[i]:memory<uint32_t>(p.battle_counter0+(2-i)*sizeof(uint32_t))==20;
    c.cmp32(due?20:21,20);
    if(i==2) c.ip=reinterpret_cast<uintptr_t>(at(due?p.battle_countdown_last_yes:p.battle_countdown_last_no));
}
void battle_exit_step(InlineContext& c) {
    const auto& p=target::get();auto& value=memory<uint32_t>(p.battle_exit_age);const auto before=value;
    battle_countdown(value,scene_frame().active,memory<uint32_t>(p.battle_outer_state));c.rax=value;
    const bool due=scene_frame().active?before>30 && value<=30:value==30;
    c.cmp32(due?30:31,30);
}
}
std::vector<InlineHook> result_inline_hooks() {
    const auto& p=target::get();
    std::vector<InlineHook> hooks{{&p.result_begin,result_begin},{&p.result_end,result_end},
        {&p.result_sound_step,result_sound_step},{&p.result_panel_queue,result_queue}};
    for(const auto& event:p.result_events) hooks.push_back({&event.site,result_event});
    for(const auto& site:p.result_periods) hooks.push_back({&site,result_period});
    for(const auto& site:p.result_countdowns) hooks.push_back({&site,result_countdown});
    for(const auto& site:p.result_countups) hooks.push_back({&site,result_countup});
    hooks.insert(hooks.end(),{{&p.battle_begin,battle_begin},{&p.battle_step,battle_step},{&p.battle_end,battle_end},
        {&p.battle_cue_sequence,battle_cues},{&p.battle_roulette,battle_roulette},{&p.battle_awards,battle_awards},
        {&p.battle_exit_step,battle_exit_step}});
    for(const auto& event:p.battle_events) hooks.push_back({&event.site,battle_event});
    for(const auto& site:p.battle_numbers) hooks.push_back({&site,battle_numbers});
    for(const auto& site:p.battle_number_cues) hooks.push_back({&site,battle_number_cue});
    for(const auto& site:p.battle_countdowns) hooks.push_back({&site,battle_countdown_step});
    for(const auto& site:p.battle_countdown_cues) hooks.push_back({&site,battle_countdown_cue});
    return hooks;
}
}
