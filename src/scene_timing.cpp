#include "mod.h"
#include "scene_timing.h"
#include "timing_math.h"
#include "scene_timeline.h"
#include "result_timing.h"
#include "effect_timing.h"
#include "song_timing.h"
#include "service_timing.h"
#include "system_timing.h"
#include "tutorial_timing.h"
#include "drum_tutorial_timing.h"
#include "summary_timing.h"
#include "minigame_timing.h"
#include "arena_timing.h"
#include "options_timing.h"
#include "timers.h"
#include <intrin.h>
#include <unordered_map>

namespace gd {
namespace {
template<class T> T& memory(uintptr_t va) { return *static_cast<T*>(at(va)); }
thread_local SceneFrame frame;

// Track resets made by the native state machine. Tick units describe authored
// duration, not a limit on how often the surrounding function processes input.
struct NativeCounter : ElapsedNativeCounter {
    uint32_t advance(uint32_t value, uint32_t state = 0) {
        return ElapsedNativeCounter::advance(value,state,frame.serial,frame.elapsed);
    }
    void finish(uint32_t value, uint32_t state = 0) {
        expected = value; identity = state;
    }
    void reset(uint32_t value, uint32_t state = 0) {
        ElapsedNativeCounter::reset(value,state,frame.serial);
    }
    void increment(uint32_t& value, uint32_t state = 0) {
        value += advance(value, state); finish(value, state);
    }
    void reconcile(uint32_t before, uint32_t& after, uint32_t modulus = 0) {
        const uint32_t native_next = modulus ? (before + 1) % modulus : before + 1;
        if (after != native_next) { reset(after); return; }
        const auto ticks = advance(before);
        after = modulus ? static_cast<uint32_t>((uint64_t(before) + ticks) % modulus) : before + ticks;
        finish(after);
    }
};
thread_local NativeCounter song_delay, shared[3], light_phase[3];
thread_local ElapsedCounter bookkeeping_clock;
thread_local ElapsedCounter pattern_clock;
thread_local uint32_t pattern_expected = 0;
thread_local bool pattern_active = false;
using SongStart = int64_t(__fastcall*)(unsigned char);
using Light = uint64_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);
using Bookkeeping = int64_t(__fastcall*)();
SongStart original_song_start;
Light original_light;
Bookkeeping original_bookkeeping;
using Pattern = void(__fastcall*)();
Pattern original_pattern;
using Secondary = int64_t(__fastcall*)(int,void*);
Secondary original_secondary;
using Network = void(__fastcall*)();
Network original_lobby, original_match;
thread_local NativeCounter lobby_clock, match_clock, chat_clock, send_clock;
using Tally = uint64_t(__fastcall*)();
Tally original_tally;
thread_local SceneTimeline tally_clock;
thread_local std::array<SceneTimeline::Event,6> tally_events;
void tally_sync() {
    const auto& p=target::get();
    tally_clock.synchronize(memory<uint32_t>(p.tally_age),memory<uint32_t>(p.tally_state),frame.serial);
}
uint64_t __fastcall tally() {
    const auto& p=target::get();
    if (!frame.active || !memory<uint8_t>(p.tally_enabled)) return original_tally();
    auto& age=memory<uint32_t>(p.tally_age);
    age=tally_clock.begin(age,memory<uint32_t>(p.tally_state),frame.serial,frame.elapsed);
    const auto result=original_tally();
    tally_sync();
    return result;
}
void tally_step(InlineContext& c) {
    if (!frame.active) c.rax=uint32_t(c.rax)+1;
    c.cmp32(uint32_t(c.r13),memory<uint32_t>(target::get().tally_state));
}
void tally_event(InlineContext& c) {
    const auto& p=target::get();
    for (size_t i=0;i<p.tally_events.size();++i) {
        const auto& event=p.tally_events[i];
        if (c.ip!=reinterpret_cast<uintptr_t>(at(event.site.address))+event.site.signature.length) continue;
        if (!frame.active) {c.cmp32(memory<uint32_t>(p.tally_age),event.tick);return;}
        tally_sync();
        bool due=tally_clock.event(tally_events[i],event.tick);
        // Earlier narration cues become obsolete after a stall. Keep the
        // newest cue and the separate final animation transition.
        if (i+1<p.tally_events.size()-1 && tally_clock.value>=p.tally_events[i+1].tick) due=false;
        c.cmp32(due?event.tick:event.tick+1,event.tick);
        return;
    }
}
void tally_numbers(InlineContext& c) {
    const auto& p=target::get();
    if (!frame.active) {c.cmp32(memory<uint32_t>(p.tally_age),uint32_t(c.r9));return;}
    tally_sync();
    const auto steps=tally_clock.steps_from(uint32_t(c.r9));
    if (tally_clock.value>=uint32_t(c.r9)) {
        auto values=reinterpret_cast<int32_t*>(c.rcx+target::layout::tally_value_offset);
        const auto goals=reinterpret_cast<const int32_t*>(c.rcx+target::layout::tally_target_offset);
        for (size_t i=0;i<target::layout::tally_columns;++i) {
            const int64_t increment=std::max(goals[i]/60,1);
            values[i]=static_cast<int32_t>(std::min(int64_t(goals[i]),int64_t(values[i])+increment*steps));
        }
    }
    c.ip=reinterpret_cast<uintptr_t>(at(p.tally_number_next));
    // The loop epilogue overwrites flags and temporary arithmetic registers.
}
void network_step(NativeCounter& clock, uintptr_t address, uint32_t state, bool running) {
    auto& value = memory<uint32_t>(address);
    if (!frame.active) { if (running) ++value; return; }
    if (running) clock.increment(value,state);
    else clock.reset(value,state);
}
void network_finish(NativeCounter& clock, uintptr_t address, uint32_t state) {
    const auto value = memory<uint32_t>(address);
    if (frame.active && (value != clock.expected || state != clock.identity)) clock.reset(value,state);
}
void __fastcall lobby() {
    const auto& p = target::get();
    const auto state = memory<uint32_t>(p.lobby_state);
    network_step(lobby_clock,p.lobby_age,state,memory<uint8_t>(p.lobby_active) && (state==4 || state==5));
    original_lobby();
    network_finish(lobby_clock,p.lobby_age,memory<uint32_t>(p.lobby_state));
}
void __fastcall match() {
    const auto& p = target::get();
    const bool active = memory<uint8_t>(p.match_active) != 0;
    const auto state = memory<uint32_t>(p.match_state);
    network_step(match_clock,p.match_age,state,active && state==5);
    network_step(send_clock,p.chat_send_age,state,active && state==3 && memory<uint32_t>(p.network_connection)!=4);
    // This is a cooldown timestamp, unlike the adjacent sent-message sequence
    // and retry-attempt counters, which remain tied to actual events.
    network_step(chat_clock,p.chat_age,0,active);
    original_match();
    network_finish(match_clock,p.match_age,memory<uint32_t>(p.match_state));
    network_finish(send_clock,p.chat_send_age,memory<uint32_t>(p.match_state));
    network_finish(chat_clock,p.chat_age,0);
}
template<class T> T& field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object)+offset);
}
struct SecondaryClock {
    NativeCounter age, remaining;
    uint32_t displayed_age = 0;
    bool cross20 = false;
};
thread_local std::unordered_map<void*,SecondaryClock> secondary_clocks;
thread_local std::unordered_map<void*,NativeCounter> preview_clocks;
thread_local std::unordered_map<void*,NativeCounter> hit_clocks;
using OptionRepeat = void*(__fastcall*)();
OptionRepeat original_repeat;
struct RepeatClock { ElapsedCounter clock; uint32_t age=0, output=0; bool held=false; };
thread_local std::unordered_map<void*,RepeatClock> repeat_clocks;
thread_local std::unordered_map<void*,NativeCounter> speed_clocks;
struct ControllerClock {
    ElapsedCounter clock;
    uintptr_t table=0;
    uint32_t index=0,phase=0,ticks=0,update=0,draw=0;
};
thread_local std::unordered_map<void*,ControllerClock> controller_clocks;
uint32_t controller_time(void* object) {
    track_scene_controller(object);
    return controller_clocks[object].ticks;
}
void controller_sample(InlineContext& c) {
    const auto& sites=target::get().controller_time_reads;
    size_t index=0;
    while (index<sites.size() && c.ip!=reinterpret_cast<uintptr_t>(at(sites[index].address))+sites[index].signature.length) ++index;
    if (index==sites.size()) return;
    const uintptr_t base=index==0 ? c.rdx : index==1 ? c.rcx : index==2 ? c.rbx : c.r15;
    const auto native=field<uint32_t>(reinterpret_cast<void*>(base),index==3 ? target::layout::controller_draw_age : target::layout::controller_update_age);
    const auto ticks=frame.active ? controller_time(reinterpret_cast<void*>(base)) : native;
    if (index<2) {
        (index==0 ? c.r15 : c.rdi)=ticks;c.r9=target::layout::service_render_height;
    } else {
        const auto product=uint64_t(uint32_t(c.rax))*ticks;
        c.rax=uint32_t(product);c.rdx=uint32_t(product>>32)>>(index==2?5:3);
        // Native AND/TEST immediately following replaces the arithmetic flags.
    }
}
void* __fastcall option_repeat() {
    if (!frame.active) return original_repeat();
    void* object=memory<void*>(target::get().option_object);
    auto held=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(object)+target::layout::option_held);
    std::array<uint32_t,target::layout::option_repeat_count> before;
    std::copy_n(held,before.size(),before.begin());
    auto result=original_repeat(); // Poll both physical inputs/modifiers every render.
    for (size_t i=0;i<before.size();++i) {
        auto& s=repeat_clocks[held+i];
        if (!held[i]) {s.held=false;s.age=s.output=0;s.clock.reset(frame.serial);continue;}
        if (!s.held || before[i]!=s.output || s.clock.seen+1<frame.serial) {
            s.clock.reset(frame.serial);s.age=1;s.held=true;held[i]=1;
        } else {
            const auto ticks=s.clock.advance(frame.serial,frame.elapsed,60,false);
            s.age=static_cast<uint32_t>(std::min(uint64_t(UINT32_MAX),uint64_t(s.age)+ticks));
            // Consumers recognize 1 as an edge and >=15 as repeat. Value 2
            // means held without an action. Coalesce repeats after a stall.
            held[i]=ticks && s.age>=15 ? 15 : 2;
        }
        s.output=held[i];
    }
    return result;
}
void speed_step(InlineContext& c) {
    const auto& sites=target::get().speed_steps;
    const unsigned player=c.ip==reinterpret_cast<uintptr_t>(at(sites[2].address))+sites[2].signature.length ? 1:0;
    void* object=reinterpret_cast<void*>(c.r11);
    auto& age=field<uint8_t>(object,target::layout::option_speed_age+player);
    if (!frame.active) {++age;return;}
    const uint32_t identity=field<uint8_t>(object,target::layout::option_speed+player) |
        uint32_t(field<uint8_t>(object,target::layout::option_speed_previous+player))<<8;
    auto& clock=speed_clocks[&age];
    age=static_cast<uint8_t>(std::min(16U,unsigned(age)+clock.advance(age,identity)));
    clock.finish(age,identity);
    // The next TEST overwrites flags before any branch.
}
void speed_sample(InlineContext& c) {
    float age=static_cast<float>(uint32_t(c.rax));
    if (frame.active && config.animation_timing==2) {
        auto ptr=reinterpret_cast<uint8_t*>(c.rdx+c.r8+target::layout::option_speed_age);
        const auto found=speed_clocks.find(ptr);
        if (found!=speed_clocks.end() && found->second.clock.seen==frame.serial && found->second.expected==*ptr)
            age+=static_cast<float>(found->second.clock.phase);
    }
    float difference;memcpy(&difference,c.ymm(1),sizeof(difference));
    const float product=difference*age;
    memcpy(c.ymm(0),&age,sizeof(age));memset(c.ymm(0)+16,0,16);
    memcpy(c.ymm(1),&product,sizeof(product));memset(c.ymm(1)+16,0,16);
}
void hit_step(uint32_t& value,uint32_t terminal) {
    if(!frame.active) {++value;return;}
    auto& clock=hit_clocks[&value];
    const bool fresh=!clock.initialized || clock.expected!=value || clock.clock.seen+1<frame.serial;
    const auto ticks=clock.advance(value);
    value=static_cast<uint32_t>(std::min(uint64_t(terminal),uint64_t(value)+(fresh?1:ticks)));
    clock.finish(value);
}
void hit_short(InlineContext& c) {
    auto& value=*reinterpret_cast<uint32_t*>(c.rbx);
    hit_step(value,8);c.cmp32(value,7);
}
void hit_long(InlineContext& c) {
    auto& value=field<uint32_t>(reinterpret_cast<void*>(c.r8),target::layout::hit_long_age);
    hit_step(value,13);c.rax=value;
}
void preview_position(InlineContext& c) {
    const auto& p=target::get();
    for (size_t i=0;i<p.preview_steps.size();++i) {
        const auto& site=p.preview_steps[i];
        if (c.ip!=reinterpret_cast<uintptr_t>(at(site.address))+site.signature.length) continue;
        auto& value=*reinterpret_cast<int32_t*>(c.r8+c.rcx*sizeof(int32_t)+target::layout::preview_positions+i*sizeof(int32_t));
        if (!frame.active) { --value; return; }
        auto& clock=preview_clocks[&value];
        const auto ticks=clock.advance(static_cast<uint32_t>(value));
        if (ticks) {
            int64_t phase=int64_t(target::layout::preview_wrap)-value+ticks;
            phase=(phase%target::layout::preview_period+target::layout::preview_period)%target::layout::preview_period;
            value=target::layout::preview_wrap-static_cast<int32_t>(phase);
        }
        clock.finish(static_cast<uint32_t>(value));
        // The following native MOV/CMP/CMOV evaluates the corrected position.
        return;
    }
}
void preview_countdown(InlineContext& c) {
    auto& value=*reinterpret_cast<int32_t*>(c.r8+c.r9);
    bool due=false;
    if (!frame.active) { due=--value<=0; if(due)value=target::layout::preview_event_period; }
    else {
        auto& clock=preview_clocks[&value];
        const auto ticks=clock.advance(static_cast<uint32_t>(value));
        const auto remaining=int64_t(value)-ticks;
        due=remaining<=0;
        // A missed interval advances phase, but dispatches only the current
        // preview effect. Preserve the remainder through the native reset.
        value=due ? target::layout::preview_event_period-static_cast<int32_t>((-remaining)%target::layout::preview_event_period)
                  : static_cast<int32_t>(remaining);
        clock.finish(static_cast<uint32_t>(value));
    }
    const auto& p=target::get();
    c.ip=reinterpret_cast<uintptr_t>(at(due?p.preview_event:p.preview_next));
    // Both destinations overwrite arithmetic flags before using them.
}
void preview_sample(InlineContext& c) {
    const auto& sites=target::get().preview_samples;
    const bool countdown=c.ip==reinterpret_cast<uintptr_t>(at(sites[2].address))+sites[2].signature.length;
    const unsigned dest=c.ip==reinterpret_cast<uintptr_t>(at(sites[1].address))+sites[1].signature.length ? 1:0;
    const auto ptr=reinterpret_cast<int32_t*>(countdown ? c.r9+c.rax : c.rcx+c.rax*sizeof(int32_t)+target::layout::preview_positions);
    float value=static_cast<float>(*ptr);
    if (frame.active && config.animation_timing==2) {
        const auto found=preview_clocks.find(ptr);
        if (found!=preview_clocks.end() && found->second.clock.seen==frame.serial && found->second.expected==static_cast<uint32_t>(*ptr))
            value-=static_cast<float>(found->second.clock.phase);
    }
    // VCVTSI2SS keeps the source's upper three XMM lanes and clears the upper
    // YMM half. Source and destination are identical at all three sites.
    memcpy(c.ymm(dest),&value,sizeof(value));
    memset(c.ymm(dest)+16,0,16);
}

int64_t __fastcall secondary(int player, void* object) {
    if (!frame.active || !object) return original_secondary(player,object);
    auto& age = field<uint32_t>(object,target::layout::secondary_age);
    const auto state = field<uint32_t>(object,target::layout::secondary_state);
    auto& clock = secondary_clocks[object];
    const auto before = age;
    const bool reset = !clock.age.initialized || clock.age.expected != age || clock.age.identity != state;
    clock.age.increment(age,state);
    clock.displayed_age = age;
    clock.cross20 = age >= 20 && (before < 20 || (reset && before == 20));
    const auto result = original_secondary(player,object);
    const auto next = field<uint32_t>(object,target::layout::secondary_state);
    if (next != state || age != clock.age.expected) clock.age.reset(age,next);
    return result;
}
void secondary_cross20(InlineContext& c) {
    bool crossed = uint32_t(c.rax) == 20;
    if (frame.active) {
        const auto found = secondary_clocks.find(reinterpret_cast<void*>(c.rbx));
        crossed = found != secondary_clocks.end() && found->second.cross20 && uint32_t(c.rax) == found->second.displayed_age;
        if (found != secondary_clocks.end()) found->second.cross20 = false;
    }
    const auto& p = target::get();
    c.ip = reinterpret_cast<uintptr_t>(at(crossed ? p.secondary_at20_yes : p.secondary_at20_no));
    // Both destinations overwrite flags before reading them.
}
void secondary_age(InlineContext& c) {
    auto& age = field<uint32_t>(reinterpret_cast<void*>(c.rbx),target::layout::secondary_age);
    if (!frame.active) ++age;
    c.rax = age; // Replaces inc [rbx+age]; mov eax,[rbx+age].
}
void secondary_countdown(InlineContext& c) {
    void* object = reinterpret_cast<void*>(c.rbx);
    auto& value = field<uint32_t>(object,target::layout::secondary_countdown);
    if (!frame.active) --value;
    else {
        auto& clock = secondary_clocks[object].remaining;
        const auto state = field<uint32_t>(object,target::layout::secondary_state);
        const auto ticks = clock.advance(value,state);
        value -= std::min(value,ticks); clock.finish(value,state);
    }
    c.rax = value; // Native signed-positive guard already ran before this site.
}

int64_t __fastcall song_start(unsigned char alternate) {
    auto& delay = memory<uint32_t>(target::get().song_start_delay);
    const auto state = memory<uint32_t>(target::get().song_start_state);
    if (!frame.active) {
        // The instruction patch also affects non-render callers. Preserve
        // their original one-step behavior when no elapsed frame is available.
        if (state == 3) ++delay;
        return original_song_start(alternate);
    }
    if (state == 3) song_delay.increment(delay, state);
    else song_delay.reset(delay, state);
    const auto result = original_song_start(alternate);
    const auto next = memory<uint32_t>(target::get().song_start_state);
    if (next != state || delay != song_delay.expected) song_delay.reset(delay, next);
    return result;
}
uint64_t __fastcall light(uintptr_t a, uintptr_t b, uintptr_t c) {
    if (!frame.active) return original_light(a,b,c);
    const auto& p = target::get();
    auto& age = memory<uint32_t>(p.light_age);
    auto& color = memory<uint32_t>(p.light_color);
    auto& brightness = memory<uint32_t>(p.light_brightness);
    const std::array<uint32_t,3> before{age,color,brightness};
    const auto result = original_light(a,b,c);
    light_phase[0].reconcile(before[0], age);
    light_phase[1].reconcile(before[1], color);
    light_phase[2].reconcile(before[2], brightness, target::layout::light_period);
    return result;
}
void __fastcall pattern() {
    const auto& p = target::get();
    const bool animated = memory<uint8_t>(p.light_pattern_enabled) && memory<uint32_t>(p.light_pattern_mode) == 1;
    if (!frame.active || !animated) {
        pattern_active = false; original_pattern(); return;
    }
    auto& index = memory<uint32_t>(p.light_pattern_index);
    uint32_t due = 0;
    if (!pattern_active || index != pattern_expected || pattern_clock.seen + 1 < frame.serial) {
        pattern_clock.reset(frame.serial); due = 1;
    } else {
        due = pattern_clock.advance(frame.serial, frame.elapsed, 60.0 / target::layout::light_pattern_step, false);
    }
    pattern_active = true;
    if (due) {
        // Select the latest phase once. Native output formerly used (age & 7)
        // == 0, which can repeat on held ages or be skipped during a stall.
        index += due - 1;
        auto& age = memory<uint32_t>(p.light_color);
        const auto saved = age; age = 0;
        original_pattern();
        age = saved;
    }
    pattern_expected = index;
}
__declspec(noinline) int64_t __fastcall bookkeeping() {
    if (!frame.active || _ReturnAddress() != at(target::get().bookkeeping_return)) return original_bookkeeping();
    // One call coalesces a stall: the native routine measures its own elapsed
    // timestamp. Replaying it for every missed interval would repeat I/O.
    if (!bookkeeping_clock.seen) bookkeeping_clock.reset(frame.serial);
    if (!bookkeeping_clock.advance(frame.serial, frame.elapsed, 1.0 / 60.0, false)) return 0;
    return original_bookkeeping();
}
}
const SceneFrame& scene_frame() {return frame;}
void begin_scene_frame(uint64_t serial, double elapsed) {
    frame = {true, serial, valid_elapsed(elapsed)};
}
void end_scene_frame() { frame.active = false; }
void track_scene_controller(void* controller) {
    if (!frame.active || !controller) return;
    auto& s=controller_clocks[controller];
    const auto table=field<uintptr_t>(controller,target::layout::controller_table);
    const auto index=field<uint32_t>(controller,target::layout::controller_index);
    const auto phase=field<uint32_t>(controller,target::layout::controller_phase);
    const auto update=field<uint32_t>(controller,target::layout::controller_update_age);
    const auto draw=field<uint32_t>(controller,target::layout::controller_draw_age);
    if (!s.clock.seen || s.clock.seen+1<frame.serial || table!=s.table || index!=s.index || phase!=s.phase ||
        update<s.update || draw<s.draw) {
        s.ticks=0;s.clock.reset(frame.serial);
    }
    s.table=table;s.index=index;s.phase=phase;s.update=update;s.draw=draw;
    s.ticks+=s.clock.advance(frame.serial,frame.elapsed,60,false);
}
std::array<uint32_t, 3> shared_counter_snapshot() {
    const auto& p = target::get();
    return {memory<uint32_t>(p.session_age), memory<uint32_t>(p.report_age), memory<uint32_t>(p.color_phase)};
}
void finish_shared_counters(const std::array<uint32_t, 3>& before) {
    if (!frame.active) return;
    const auto& p = target::get();
    const uintptr_t addresses[]{p.session_age, p.report_age, p.color_phase};
    for (size_t i = 0; i < before.size(); ++i) shared[i].reconcile(before[i], memory<uint32_t>(addresses[i]));
}
std::vector<DiagnosticHook> scene_timing_hooks() {
    const auto& p = target::get();
    return {
        bind_hook(p.song_start, reinterpret_cast<void*>(song_start), reinterpret_cast<void**>(&original_song_start)),
        bind_hook(p.lights, reinterpret_cast<void*>(light), reinterpret_cast<void**>(&original_light)),
        bind_hook(p.bookkeeping, reinterpret_cast<void*>(bookkeeping), reinterpret_cast<void**>(&original_bookkeeping)),
        bind_hook(p.light_pattern, reinterpret_cast<void*>(pattern), reinterpret_cast<void**>(&original_pattern)),
        bind_hook(p.lobby_update, reinterpret_cast<void*>(lobby), reinterpret_cast<void**>(&original_lobby)),
        bind_hook(p.match_update, reinterpret_cast<void*>(match), reinterpret_cast<void**>(&original_match)),
        bind_hook(p.tally_update, reinterpret_cast<void*>(tally), reinterpret_cast<void**>(&original_tally)),
        bind_hook(p.option_repeat, reinterpret_cast<void*>(option_repeat), reinterpret_cast<void**>(&original_repeat)),
        bind_hook(p.secondary_update, reinterpret_cast<void*>(secondary), reinterpret_cast<void**>(&original_secondary))
    };
}
std::vector<const target::Patch*> scene_timing_patches() {
    const auto& p = target::get();
    std::vector<const target::Patch*> result{&p.song_delay_increment,&p.bookkeeping_cadence};
    for (const auto& patch : p.network_steps) result.push_back(&patch);
    return result;
}
std::vector<InlineHook> scene_inline_hooks() {
    const auto& p = target::get();
    std::vector<InlineHook> result{{&p.secondary_at20,secondary_cross20}, {&p.secondary_age_step,secondary_age},
        {&p.secondary_countdown0,secondary_countdown}, {&p.secondary_countdown1,secondary_countdown}};
    for (const auto& site : p.preview_steps) result.push_back({&site,preview_position});
    result.push_back({&p.preview_countdown,preview_countdown});
    for (const auto& site : p.preview_samples) result.push_back({&site,preview_sample});
    result.push_back({&p.hit_effect_short,hit_short});result.push_back({&p.hit_effect_long,hit_long});
    result.push_back({&p.tally_step,tally_step});result.push_back({&p.tally_numbers,tally_numbers});
    for (const auto& event:p.tally_events) result.push_back({&event.site,tally_event});
    for (const auto& site:p.speed_steps) result.push_back({&site,speed_step});
    result.push_back({&p.speed_sample,speed_sample});
    for (const auto& site:p.controller_time_reads) result.push_back({&site,controller_sample});
    const auto results=result_inline_hooks();result.insert(result.end(),results.begin(),results.end());
    const auto effects=effect_inline_hooks();result.insert(result.end(),effects.begin(),effects.end());
    const auto song=song_inline_hooks();result.insert(result.end(),song.begin(),song.end());
    const auto service=service_inline_hooks();result.insert(result.end(),service.begin(),service.end());
    const auto system=system_inline_hooks();result.insert(result.end(),system.begin(),system.end());
    const auto timers=timer_inline_hooks();result.insert(result.end(),timers.begin(),timers.end());
    const auto tutorial=tutorial_inline_hooks();result.insert(result.end(),tutorial.begin(),tutorial.end());
    const auto drum=drum_tutorial_inline_hooks();result.insert(result.end(),drum.begin(),drum.end());
    const auto summary=summary_inline_hooks();result.insert(result.end(),summary.begin(),summary.end());
    const auto mini=minigame_inline_hooks();result.insert(result.end(),mini.begin(),mini.end());
    const auto arena=arena_inline_hooks();result.insert(result.end(),arena.begin(),arena.end());
    const auto options=options_inline_hooks();result.insert(result.end(),options.begin(),options.end());
    return result;
}
}
