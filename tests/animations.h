#pragma once
#include "../src/animations.h"
#include "../src/animation_clock.h"
#include "../src/timers.h"
#include "../src/scene_timing.h"
#include <array>

static void animation_test_frame(double elapsed) {
    gd::begin_timing_frame(elapsed);gd::begin_animation_frame();
}

static void animation_clock_test() {
    for (unsigned hz : {60U, 90U, 120U, 144U, 240U}) {
        gd::AnimationClock clock;
        unsigned count = 0;
        // Include the initial immediate update and exactly ten seconds after it.
        for (unsigned i = 0; i <= hz * 10; ++i) {
            count += clock.advance(i + 1, 1.0 / hz);
            check(clock.advance(i + 1, 1.0 / hz) == 0, "duplicate update does not tick again");
        }
        check(count == 601, "animation time independent of render frequency");
        check(clock.advance(hz * 10 + 30, 1.0 / hz) == 1, "inactive animation has no catch-up burst");
    }
    gd::AnimationClock clock;
    check(clock.advance(1, 0.0) == 1, "new effects start immediately");
    unsigned count = 0;
    for (unsigned i = 0; i < 100; ++i) {
        count += clock.advance(i * 2 + 2, 0.004);
        count += clock.advance(i * 2 + 3, 0.016);
    }
    check(count == 120, "jittered frame durations preserve animation duration");
    check(clock.advance(202, 10.0) == 600, "animation clock retains the full stalled interval");
    check(clock.advance(203, std::numeric_limits<double>::quiet_NaN()) == 0,"invalid animation delta rejected");
    check(std::abs(gd::afp_step(10)-10.0060008f)<1e-5f,"AFP retains full elapsed time and authored rate bias");
    check(gd::effect_counter(1, 0) == 1 && gd::effect_counter(1, 7) == 8 &&
        gd::effect_counter(1, 8) == 0 && gd::effect_counter(0, 6) == 0, "eight-frame effect lifetime and inactive state");
    for (int32_t initial:{INT32_MIN,-20,0,20,INT32_MAX-10})
        for (int32_t threshold:{INT32_MIN,-5,0,5,INT32_MAX}) {
            uint64_t expected=0;uint32_t value=static_cast<uint32_t>(initial);
            for (unsigned i=1;i<=100;++i) {++value;expected+=static_cast<int32_t>(value)>=threshold;}
            check(gd::sprite_steps(initial,100,threshold)==expected,"bulk sprite arithmetic matches signed native counter wrap");
        }
    puts("PASS: animation clocks at 60/90/120/144/240 Hz, jitter, pauses and effect lifetimes");
}

template<class T> static T& anim_field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(object) + offset);
}
static unsigned animation_callbacks;
static std::array<float, gd::target::layout::pose_floats> drawn_pose;
static bool draw_writes_position;
static void __fastcall count_animation_callback(void*) { ++animation_callbacks; }
static void __fastcall observe_animation_draw(void* object) {
    memcpy(drawn_pose.data(), object, sizeof(drawn_pose));
    if (draw_writes_position) anim_field<float>(object, 0) = 1234;
}

static void native_animation_test(unsigned mode) {
    using Update = void(__fastcall*)(void*);
    using Draw = void(__fastcall*)(void*);
    const auto update = reinterpret_cast<Update>(gd::at(gd::target::get().animation_update.address));
    const auto draw = reinterpret_cast<Draw>(gd::at(gd::target::get().animation_draw.address));
    const auto hooks = gd::animation_hooks();
    // Run the real native evaluator and clock, replacing only GPU submission.
    auto draw_slot = hooks[2].original;
    void* saved_draw = *draw_slot;
    *draw_slot = reinterpret_cast<void*>(observe_animation_draw);
    alignas(16) std::array<unsigned char, gd::target::layout::animation_size> object{}, child{};
    alignas(8) std::array<unsigned char, gd::target::layout::track_size> track{};
    alignas(8) std::array<unsigned char, gd::target::layout::callback_size> callback{};
    struct PositionKey { int16_t unused, frame; float x, y, z; };
    struct ScalarKey { int16_t unused, frame; float value; };
    PositionKey positions[]{{0,0,0,0,0}, {0,1000,10000,0,0}, {0,-1,0,0,0}};
    ScalarKey alpha[]{{0,0,100}, {0,10,0}, {0,-1,0}};
    anim_field<void*>(track.data(), gd::target::layout::track_positions) = positions;
    anim_field<void*>(track.data(), gd::target::layout::track_alpha) = alpha;
    anim_field<void*>(object.data(), gd::target::layout::track) = track.data();
    anim_field<uint16_t>(object.data(), gd::target::layout::kind) = 1;
    anim_field<uint8_t>(object.data(), gd::target::layout::transform_flags) = 3;
    anim_field<void*>(object.data(), gd::target::layout::callbacks) = callback.data();
    anim_field<uint16_t>(object.data(), gd::target::layout::callback_count) = 1;
    anim_field<void*>(callback.data(), gd::target::layout::callback_function) = reinterpret_cast<void*>(count_animation_callback);
    anim_field<void*>(object.data(), gd::target::layout::child) = child.data();
    anim_field<void*>(child.data(), gd::target::layout::parent) = object.data();
    anim_field<uint16_t>(child.data(), gd::target::layout::kind) = 1;
    anim_field<uint8_t>(child.data(), gd::target::layout::transform_flags) = 3;
    animation_callbacks = 0;
    const unsigned hz = gd::config.fps;unsigned scene_cues=0;
    for (unsigned i = 0; i < hz; ++i) {
        animation_test_frame(1.0 / hz);
        update(object.data());
        const auto callbacks=animation_callbacks;
        gd::begin_animation_frame();update(object.data());
        check(animation_callbacks==callbacks,"reentering animation handling shares the main frame and cannot advance time twice");
        scene_cues+=gd::animation_event(object.data(),10,1);
        check(!gd::animation_event(object.data(),10,1),"held authored frame cannot repeat a scene sound cue");
        if(anim_field<int16_t>(object.data(),gd::target::layout::frame)==10) {
            check(gd::animation_predicate(object.data(),10,21)&&gd::animation_predicate(object.data(),10,21),
                "held-label predicates retain level semantics across repeated reads");
        }
        if (i == hz * 3 / 120) {
            const float tick = float(i) * 60.0f / hz;
            const float native_pose = std::floor(tick) * 10.0f;
            const float drawn = mode == 2 ? tick * 10.0f : native_pose;
            check(std::abs(anim_field<float>(object.data(), 0) - native_pose) < 1e-5, "native keyframe remains on integer tick");
            draw(object.data());
            check(std::abs(drawn_pose[0] - drawn) < 1e-5, "fractional native movement reaches renderer");
            check(std::abs(drawn_pose[gd::target::layout::pose_alpha_index] - (1.0f - drawn / 100.0f)) < 1e-5, "fractional native alpha reaches renderer");
            check(std::abs(anim_field<float>(object.data(), 0) - native_pose) < 1e-5, "draw restores logical pose");
            check(animation_callbacks == unsigned(std::floor(tick)) + 1, "interpolation invokes no timeline callbacks");
            // A manual position override must be left alone.
            anim_field<float>(object.data(), 0) = 99;
            draw(object.data());
            check(drawn_pose[0] == 99, "manual position is not interpolated away");
            anim_field<float>(object.data(), 0) = native_pose;
            anim_field<uint8_t>(object.data(), gd::target::layout::playback_flags) |= 2;
            draw(object.data());
            check(drawn_pose[0] == native_pose, "stopped animation is not interpolated");
            anim_field<uint8_t>(object.data(), gd::target::layout::playback_flags) &= ~2;
            draw_writes_position = true;
            draw(object.data());
            check(anim_field<float>(object.data(), 0) == 1234, "draw callback writes survive pose restoration");
            draw_writes_position = false;
            anim_field<float>(object.data(), 0) = native_pose;
        }
        gd::end_animation_frame();
    }
    check(anim_field<int16_t>(object.data(), gd::target::layout::frame) == 60, "native root advances 60 ticks per real second");
    check(anim_field<int16_t>(child.data(), gd::target::layout::frame) == 60, "recursive children follow root without a second rate division");
    check(animation_callbacks == 60, "native callbacks fire once per animation tick");
    check(scene_cues==1,"scene frame cue fires once across all rendered samples");
    animation_test_frame(2.25);update(object.data());
    check(gd::animation_event(object.data(),100,2)&&gd::animation_event(child.data(),100,3),"parent and child scene cues survive skipped authored frames");
    check(!gd::animation_event(object.data(),100,2),"stalled scene cue is consumed once");
    check(gd::animation_predicate(object.data(),100,22),"animation predicate observes a skipped authored label");
    check(!gd::animation_predicate(object.data(),100,22),"a crossed label does not become a permanently held predicate");
    const auto query_hooks=gd::animation_inline_hooks();
    for(const auto& query:gd::target::get().frame_queries) {
        const auto h=std::find_if(query_hooks.begin(),query_hooks.end(),[&](const auto& hook){return hook.site==&query.site;});
        check(h!=query_hooks.end(),"animation frame query is registered");
        auto invoke=[&](int label) {
            struct {gd::InlineContext c{};uint64_t stack[16]{};} fixture;
            auto& c=fixture.c;c.rbx=c.rdi=reinterpret_cast<uintptr_t>(object.data());
            c.rax=uint32_t(label);c.rsi=c.rdx=195;
            if(query.form==gd::target::FrameQueryForm::RestoreRsi)
                *reinterpret_cast<uint64_t*>(c.native_rsp()+query.restore_rsi)=0x12345678;
            else if(query.form==gd::target::FrameQueryForm::IndirectLabel) {
                c.rax=reinterpret_cast<uintptr_t>(object.data());c.rcx=195;
                c.r13=reinterpret_cast<uintptr_t>(&label)-query.label_offset;
            } else {c.rax=195;c.rcx=uint32_t(label);}
            c.ip=reinterpret_cast<uintptr_t>(gd::at(query.site.address))+query.site.signature.length;
            h->handler(c);
            if(query.form==gd::target::FrameQueryForm::RestoreRsi)
                check(c.rsi==0x12345678,"frame query restores displaced nonvolatile RSI");
            else check(c.ip==reinterpret_cast<uintptr_t>(gd::at(query.site.address))+query.site.signature.length ||
                c.ip==reinterpret_cast<uintptr_t>(gd::at(query.no)),"frame predicate resumes a native branch");
            if(query.no)check(bool(c.flags&64)==(c.ip!=reinterpret_cast<uintptr_t>(gd::at(query.no))),
                "frame predicate branch agrees with the adapted comparison");
            return bool(c.flags&64);
        };
        check(invoke(100)&&!invoke(100),"each frame-query bridge observes a skipped label once");
        check(invoke(195),"each frame-query bridge recognizes the current label");
        check(invoke(195),"frame-query bridge preserves held-label predicates");
    }
    check(gd::animation_predicate(nullptr,-1,23)&&!gd::animation_predicate(nullptr,1,23),
        "missing animation preserves native negative-label sentinel semantics");
    gd::end_animation_frame();
    check(anim_field<int16_t>(object.data(),gd::target::layout::frame)==195 &&
        anim_field<int16_t>(child.data(),gd::target::layout::frame)==195 && animation_callbacks==195,
        "native parent/child timelines and callbacks preserve the entire render stall");
    *draw_slot = saved_draw;
    puts("PASS: real native animation engine, recursive clock, callback rate, interpolated movement/alpha, pose restoration");
}

static void native_sprite_test() {
    auto records = static_cast<unsigned char*>(gd::at(gd::target::get().sprite_records));
    std::array<unsigned char, gd::target::layout::sprite_bank_size> saved{};
    check(VirtualAlloc(records, saved.size(), MEM_COMMIT, PAGE_READWRITE) != nullptr, "commit isolated sprite BSS");
    memcpy(saved.data(), records, saved.size());
    alignas(16) std::array<unsigned char, gd::target::layout::animation_size> object{};
    alignas(8) std::array<unsigned char, gd::target::layout::sprite_size> direct{};
    auto initialize = [&](void* record) {
        memset(record, 0, gd::target::layout::sprite_size);
        anim_field<void*>(record, gd::target::layout::sprite_object) = object.data();
        anim_field<uint8_t>(record, gd::target::layout::sprite_limit0) = 200;
        anim_field<uint8_t>(record, gd::target::layout::sprite_limit1) = 200;
        anim_field<uint8_t>(record, gd::target::layout::sprite_active) = 1;
        anim_field<int>(record, gd::target::layout::sprite_step) = 1;
    };
    using Batch = void(__fastcall*)();
    using Sprite = char(__fastcall*)(void*);
    for (const auto batch : {std::pair<uintptr_t, uintptr_t>{gd::target::get().dm_sprites.address, gd::target::get().sprite_records},
            {gd::target::get().gf_sprites.address, gd::target::get().gf_sprite_records}}) {
        memset(records, 0, saved.size());
        auto inlined = gd::at(batch.second);
        auto nested = gd::at(gd::target::get().nested_sprite_record);
        initialize(inlined); initialize(nested); initialize(direct.data());
        for (unsigned i = 0; i < gd::config.fps; ++i) {
            animation_test_frame(1.0 / gd::config.fps);
            reinterpret_cast<Batch>(gd::at(batch.first))();
            reinterpret_cast<Sprite>(gd::at(gd::target::get().sprite_update.address))(direct.data());
            gd::end_animation_frame();
        }
        check(anim_field<int>(inlined, gd::target::layout::sprite_frame) == 60, "inlined GF/DM sprite effects advance at 60 Hz");
        check(anim_field<int>(nested, gd::target::layout::sprite_frame) == 60, "nested sprite helper is not slowed twice");
        check(anim_field<int>(direct.data(), gd::target::layout::sprite_frame) == 60, "standalone sprite helper advances at 60 Hz");
        // A new atlas effect begins immediately even between authored ticks.
        anim_field<uint8_t>(inlined,gd::target::layout::sprite_active)=0;
        animation_test_frame(1.0/gd::config.fps);reinterpret_cast<Batch>(gd::at(batch.first))();gd::end_animation_frame();
        initialize(inlined);
        animation_test_frame(0);reinterpret_cast<Batch>(gd::at(batch.first))();gd::end_animation_frame();
        check(anim_field<int>(inlined,gd::target::layout::sprite_frame)==1,"fresh inlined atlas effect responds before the next shared tick");
        animation_test_frame(86400);reinterpret_cast<Batch>(gd::at(batch.first))();gd::end_animation_frame();
        check(!anim_field<uint8_t>(inlined,gd::target::layout::sprite_active) &&
            !anim_field<uint8_t>(nested,gd::target::layout::sprite_active),"whole-day sprite stall expires inlined and nested atlas effects");
    }
    const auto hooks=gd::animation_hooks();
    const auto native=reinterpret_cast<Sprite>(*hooks[3].original);
    const auto patched=reinterpret_cast<Sprite>(gd::at(gd::target::get().sprite_update.address));
    std::array<unsigned char,gd::target::layout::sprite_size> reference{};
    for (unsigned n:{1U,2U,5U,17U,59U,120U,700U})
        for (unsigned period:{0U,1U,3U,200U}) {
            initialize(direct.data());
            anim_field<uint8_t>(direct.data(),gd::target::layout::sprite_limit0)=static_cast<uint8_t>(period);
            anim_field<uint8_t>(direct.data(),gd::target::layout::sprite_limit1)=7;
            anim_field<int>(direct.data(),gd::target::layout::sprite_step)=5;
            reference=direct;
            for (unsigned i=0;i<n;++i) native(reference.data());
            // Observe the inactive state before starting a reused record.
            anim_field<uint8_t>(direct.data(),gd::target::layout::sprite_active)=0;
            animation_test_frame(0);patched(direct.data());gd::end_animation_frame();
            anim_field<uint8_t>(direct.data(),gd::target::layout::sprite_active)=1;
            animation_test_frame(0);patched(direct.data());gd::end_animation_frame();
            if (n>1) {animation_test_frame(double(n-1)/60);patched(direct.data());gd::end_animation_frame();}
            check(memcmp(reference.data(),direct.data(),direct.size())==0,"bulk sprite skip matches the original native sequence byte-for-byte");
        }
    memcpy(records, saved.data(), saved.size());
    puts("PASS: real GF/DM sprite batches and standalone effects; native R10/R11 loop convention retained");
}
