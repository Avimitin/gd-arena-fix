#pragma once
#include "../src/minigame_timing.h"
static unsigned mini_inputs, mini_sounds;
static uint64_t __fastcall mini_input_stub(unsigned) {
    ++mini_inputs;
    return 0;
}
static uint64_t __fastcall mini_sound_stub(unsigned, unsigned id) {
    if (id == 127)
        ++mini_sounds;
    return 0;
}
static void native_minigame_timing_test() {
    namespace L = gd::target::layout::mini;
    const auto& p = gd::target::get();
    timing_commit(p.mini_game_kind, 4);
    timing_commit(p.mini_input_enabled, 1);
    timing_memory<uint32_t>(p.mini_game_kind) = 1;
    timing_memory<uint8_t>(p.mini_input_enabled) = 0;
    TimingRedirect input(p.mini_shot_input, reinterpret_cast<void*>(mini_input_stub));
    TimingRedirect sound(p.keysound, reinterpret_cast<void*>(mini_sound_stub));
    const auto update = reinterpret_cast<void(__fastcall*)(void*)>(gd::at(p.mini_update));
    const auto player = reinterpret_cast<void(__fastcall*)(void*, int, void*)>(gd::at(p.mini_player_update));
    const auto particle = reinterpret_cast<void(__fastcall*)(void*, void*)>(gd::at(p.mini_particle_update));
    const auto enemy = reinterpret_cast<void(__fastcall*)(void*, void*)>(gd::at(p.mini_enemy_update));
    std::vector<uint8_t> object(L::size);
    auto base = object.data();
    auto u = [&](size_t offset) -> uint32_t& { return anim_field<uint32_t>(base, offset); };
    auto f = [&](size_t offset) -> float& { return anim_field<float>(base, offset); };
    auto reset = [&] {
        memset(base, 0, object.size());
        gd::begin_timing_frame(0);
        gd::begin_timing_frame(0);
    };
    const auto ground = timing_memory<float>(p.mini_floor);
    for (unsigned hz : {95U, 120U, 144U, 240U, 360U}) {
        reset();
        base[L::enabled] = 1;
        u(L::player_state) = 1;
        u(L::enemy + L::enemy_state) = 5;
        base[ptrdiff_t(L::burst_begin) + L::burst_active] = 1;
        base[ptrdiff_t(L::debris_begin) + L::debris_active] = 1;
        f(ptrdiff_t(L::debris_begin) + L::debris_position) = -10000;
        base[L::banner_active] = 1;
        u(L::banner_kind) = 0;
        update(base);
        for (unsigned i = 0; i < hz; ++i) {
            gd::begin_timing_frame(1.0 / hz);
            update(base);
            update(base);
        }
        check(u(L::age) == 60 && u(L::player_tile) == 5 && u(L::player_step) == 0,
              "mini-game score clock and player atlas follow wall time");
        check(u(L::enemy + L::enemy_tile) == 0 && u(L::enemy + L::enemy_step) == 0,
              "enemy atlas has its authored half-second cycle");
        check(base[ptrdiff_t(L::burst_begin) + L::burst_active] == 0,
              "mini-game burst expires after twelve authored ticks");
        check(std::abs(f(ptrdiff_t(L::debris_begin) + L::debris_position) -
                       (-10000 + 60 * timing_memory<float>(p.mini_debris_speed))) < .2f,
              "mini-game debris travels at the same speed across refresh rates");
        check(base[L::banner_alpha] == 255 && u(L::banner_age) == 60,
              "mini-game banner fade-in retains fractional alpha");
        gd::begin_timing_frame(2);
        update(base);
        check(base[L::enabled] == 0 && base[L::banner_alpha] == 0,
              "banner hold and fade-out both complete across a stall");

        reset();
        u(L::player_state) = 3;
        f(L::player_y) = ground;
        f(L::player_x) = 620;
        u(L::player_lane) = 2;
        f(L::player_accel) = 1.5f;
        u(L::enemy + L::enemy_state) = 2;
        mini_inputs = 0;
        player(base, 0, base);
        for (unsigned i = 0; i < hz; ++i) {
            gd::begin_timing_frame(1.0 / hz);
            player(base, 0, base);
        }
        check(mini_inputs == hz + 1, "mini-game input is polled at the full rendered refresh rate");
        check(std::abs(f(L::player_x) - timing_memory<float>(p.mini_enemy_target)) < .01f,
              "player motion reaches and clamps to its native lane target");

        reset();
        base[L::hurt_flag] = 1;
        u(L::player_state) = 2;
        f(L::player_y) = ground - 100;
        mini_sounds = 0;
        player(base, 0, base);
        for (unsigned i = 0; i < hz * 3; ++i) {
            gd::begin_timing_frame(1.0 / hz);
            player(base, 0, base);
            player(base, 0, base);
        }
        if (base[L::hurt_flag] || u(L::player_state) != 3 || mini_sounds != 1)
            printf("mini recovery: Hz=%u flag=%u state=%u age=%u sounds=%u\n", hz, base[L::hurt_flag],
                   u(L::player_state), u(L::hurt_age), mini_sounds);
        check(base[L::hurt_flag] == 0 && u(L::player_state) == 3 && mini_sounds == 1,
              "player recovers after three seconds with one recovery sound");
        check(f(L::player_y) == ground, "elapsed gravity preserves the player's landing position");

        reset();
        auto part = base + L::particle_begin;
        part[L::particle_active] = 1;
        anim_field<uint32_t>(part, L::particle_state) = 3;
        anim_field<float>(part, L::particle_x) = 750;
        anim_field<float>(part, L::particle_y) = 100;
        anim_field<float>(part, L::particle_vx) = 1;
        anim_field<float>(part, L::particle_vy) = 2;
        f(L::player_x) = 620;
        anim_field<float>(part, L::particle_angle) = timing_memory<float>(p.mini_spin_low);
        anim_field<float>(part, L::particle_spin) = .01f;
        particle(part, base);
        for (unsigned i = 0; i < hz; ++i) {
            gd::begin_timing_frame(1.0 / hz);
            particle(part, base);
            particle(part, base);
        }
        const auto turn_v = (730 - ground) / timing_memory<float>(p.mini_particle_divisor);
        if (std::abs(anim_field<float>(part, L::particle_x) - (730 - 40 * turn_v)) >= .1f)
            printf("mini particle: Hz=%u x=%f want=%f age=%u active=%u vx=%f\n", hz,
                   anim_field<float>(part, L::particle_x), 730 - 40 * turn_v,
                   anim_field<uint32_t>(part, L::particle_age), part[L::particle_active],
                   anim_field<float>(part, L::particle_vx));
        check(std::abs(anim_field<float>(part, L::particle_x) - (730 - 40 * turn_v)) < .1f,
              "particle turns exactly at the elapsed twenty-tick deadline");
        check(anim_field<uint32_t>(part, L::particle_age) == 60, "particle deadline clock is duplicate-safe");

        reset();
        auto foe = base + L::enemy;
        anim_field<uint32_t>(foe, L::enemy_state) = 1;
        anim_field<float>(foe, L::enemy_y) = 10000;
        anim_field<float>(foe, L::enemy_ax) = .1f;
        anim_field<float>(foe, L::enemy_ay) = .1f;
        enemy(foe, base);
        for (unsigned i = 0; i < hz; ++i) {
            gd::begin_timing_frame(1.0 / hz);
            enemy(foe, base);
            enemy(foe, base);
        }
        const auto velocity = anim_field<float>(foe, L::enemy_vx), position = anim_field<float>(foe, L::enemy_x);
        reset();
        foe = base + L::enemy;
        anim_field<uint32_t>(foe, L::enemy_state) = 1;
        anim_field<float>(foe, L::enemy_y) = 10000;
        anim_field<float>(foe, L::enemy_ax) = .1f;
        anim_field<float>(foe, L::enemy_ay) = .1f;
        enemy(foe, base);
        gd::begin_timing_frame(1);
        enemy(foe, base);
        check(std::abs(anim_field<float>(foe, L::enemy_vx) - velocity) < .001f &&
                  std::abs(anim_field<float>(foe, L::enemy_x) - position) < .02f,
              "enemy acceleration reversals integrate consistently across a one-second stall");
    }
    // A missed recovery threshold still plays the cue once and completes recovery.
    reset();
    base[L::hurt_flag] = 1;
    u(L::player_state) = 2;
    f(L::player_y) = ground - 100;
    mini_sounds = 0;
    player(base, 0, base);
    gd::begin_timing_frame(4);
    player(base, 0, base);
    player(base, 0, base);
    check(mini_sounds == 1 && base[L::hurt_flag] == 0, "player recovery survives a stall across both deadlines");
    gd::end_scene_frame();
    puts("PASS: native mini-game atlases, motion, recovery, banner, particle turn and full-rate input at "
         "95/120/144/240/360 Hz");
}
