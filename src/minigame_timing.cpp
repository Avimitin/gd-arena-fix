#include "mod.h"
#include "minigame_timing.h"
#include "scene_timing.h"
#include "timing_math.h"
#include <unordered_map>

namespace gd {
namespace {
namespace L = target::layout::mini;
template <class T> T& field(uintptr_t base, ptrdiff_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}
float constant(uintptr_t va) {
    return *static_cast<float*>(at(va));
}
void jump(InlineContext& c, uintptr_t va) {
    c.ip = reinterpret_cast<uintptr_t>(at(va));
}
float scalar(InlineContext& c, unsigned r) {
    float x;
    memcpy(&x, c.ymm(r), 4);
    return x;
}
void scalar(InlineContext& c, unsigned dest, unsigned source, float x) {
    if (dest != source)
        memcpy(c.ymm(dest), c.ymm(source), 16);
    memcpy(c.ymm(dest), &x, 4);
    memset(c.ymm(dest) + 16, 0, 16);
}
struct Clock {
    uint64_t serial = 0;
    uint32_t expected = 0, identity = 0;
    double time = 0, previous = 0, dt = 0;
    bool initialized = false;
    double begin(uint32_t native, uint32_t state = 0) {
        const auto& f = scene_frame();
        if (!f.active) {
            previous = native;
            time = native + 1.;
            dt = 1;
            return time;
        }
        const bool fresh = !initialized || expected != native || identity != state || serial + 1 < f.serial;
        if (fresh)
            time = native;
        previous = time;
        dt = (!fresh && serial != f.serial) ? valid_elapsed(f.elapsed) * 60 : 0;
        time = std::min(double(INT32_MAX), time + dt);
        if (std::abs(time - std::round(time)) < 1e-8)
            time = std::round(time);
        serial = f.serial;
        identity = state;
        initialized = true;
        return time;
    }
    uint32_t whole() const {
        return uint32_t(std::floor(time + 1e-8));
    }
    void finish(uint32_t native) {
        expected = native;
    }
};
struct Delta {
    uint64_t serial = 0;
    uint32_t identity = 0;
    double dt = 0;
    double begin(uint32_t state) {
        const auto& f = scene_frame();
        dt =
            !f.active ? 1 : (serial && serial + 1 == f.serial && identity == state ? valid_elapsed(f.elapsed) * 60 : 0);
        serial = f.serial;
        identity = state;
        return dt;
    }
};
thread_local std::unordered_map<uintptr_t, Clock> clocks;
thread_local std::unordered_map<uintptr_t, Delta> motions;
thread_local std::unordered_map<uintptr_t, float> player_positions;

// Atlas progression is arithmetic; hundreds of missed cells need no update loop.
void atlas(uintptr_t base, ptrdiff_t tile_offset, ptrdiff_t step_offset, unsigned period, unsigned count,
           bool finite = false, ptrdiff_t active = 0) {
    auto& tile = field<uint32_t>(base, tile_offset);
    auto& step = field<uint32_t>(base, step_offset);
    auto& clock = clocks[base + tile_offset];
    clock.begin(tile * period + step);
    auto ticks = clock.whole();
    if (finite && ticks >= period * count) {
        tile = step = 0;
        field<uint8_t>(base, active) = 0;
        clock.initialized = false;
        return;
    }
    ticks %= period * count;
    tile = ticks / period;
    step = ticks % period;
    clock.time = std::fmod(clock.time, double(period * count));
    clock.finish(ticks);
}
void age(InlineContext& c) {
    auto& value = field<uint32_t>(c.rcx, L::age);
    auto& clock = clocks[c.rcx + L::age];
    clock.begin(value);
    value = clock.whole();
    clock.finish(value);
    c.step32(value);
}
void player_atlas(InlineContext& c) {
    const auto& p = target::get();
    atlas(c.rcx, L::player_tile, L::player_step, 3, 15);
    c.rsi = 0;
    c.rdi = c.r8;
    c.r12 = int64_t(int32_t(c.rdx));
    c.rbx = c.rcx;
    jump(c, p.mini_player_atlas_next);
}
void enemy_atlas(InlineContext& c) {
    atlas(c.rcx, L::enemy_tile, L::enemy_step, 15, 2);
    c.rsi = 0;
    c.rdi = c.rdx;
    c.rbx = c.rcx;
    jump(c, target::get().mini_enemy_atlas_next);
}
void debris_atlas(InlineContext& c) {
    atlas(c.r10, 0, L::debris_step, 5, 2);
    jump(c, target::get().mini_debris_atlas_next);
}
void debris_motion(InlineContext& c) {
    const double dt = clocks[c.r10].dt;
    scalar(c, 1, 6, float(field<float>(c.r10, L::debris_position) + scalar(c, 6) * dt));
}
void bursts(InlineContext& c) {
    for (size_t i = 0; i < L::burst_count; ++i) {
        const auto base = c.rbx + L::burst_begin + i * L::burst_stride;
        if (field<uint8_t>(base, L::burst_active))
            atlas(base, 0, L::burst_step, 1, 12, true, L::burst_active);
        else
            clocks.erase(base);
    }
    jump(c, target::get().mini_bursts_next);
}
void banner(InlineContext& c) {
    const auto& p = target::get();
    const auto b = c.rbx;
    auto& native = field<uint32_t>(b, L::banner_age);
    auto& clock = clocks[b + L::banner_age];
    clock.begin(native, field<uint32_t>(b, L::banner_kind));
    native = clock.whole();
    clock.finish(native);
    const auto before = std::max(0., std::min(clock.time, 120.) - std::min(clock.previous, 120.));
    const auto after = std::max(0., clock.time - std::max(clock.previous, 120.));
    // Byte alpha needs a fractional sidecar, otherwise 240+ Hz loses fade steps.
    auto& a = clocks[b + L::banner_alpha];
    auto& alpha = field<uint8_t>(b, L::banner_alpha);
    if (!a.initialized || a.expected != alpha || clock.previous == 0) {
        a.time = alpha;
        a.initialized = true;
    }
    if (field<uint32_t>(b, L::banner_kind) < 3)
        a.time = std::min(255., a.time + 10 * before);
    else {
        const auto resource = field<uintptr_t>(b, L::banner_resource);
        const float limit = constant(p.mini_banner_scale) / field<float>(resource, L::resource_height);
        auto& scale = field<float>(b, L::banner_scale);
        auto& angle = field<float>(b, L::banner_rotation);
        scale = float(std::min(double(limit), scale + field<float>(b, L::banner_vscale) * before));
        angle = float(
            std::min(double(constant(p.mini_banner_rotation)), angle + field<float>(b, L::banner_vrotation) * before));
    }
    a.time = std::max(0., a.time - 10 * after);
    alpha = uint8_t(a.time);
    a.expected = alpha;
    if (after > 0 && alpha == 0)
        field<uint8_t>(b, L::enabled) = 0;
    jump(c, p.mini_banner_next);
}
void gravity(uintptr_t b, double dt, float acceleration, float limit) {
    auto& v = field<float>(b, L::player_vy);
    auto& y = field<float>(b, L::player_y);
    y = float(std::min(double(limit), y + v * dt + acceleration * dt * (dt + 1) * .5));
    v = float(v + acceleration * dt);
}
void hurt(InlineContext& c) {
    const auto& p = target::get();
    const auto b = c.rbx;
    auto& native = field<uint32_t>(b, L::hurt_age);
    auto& clock = clocks[b + L::hurt_age];
    clock.begin(native);
    native = clock.whole();
    clock.finish(native);
    if (clock.previous < 60 && clock.time >= 60)
        reinterpret_cast<uint64_t(__fastcall*)(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned)>(
            at(p.keysound))(3, 127, 0, 127, 64, 0, 1);
    auto& state = field<uint32_t>(b, L::player_state);
    state = 2;
    if (clock.time <= 60) {
        jump(c, p.mini_hurt_wait);
        return;
    }
    gravity(b, std::max(0., std::min(clock.time, 180.) - std::max(clock.previous, 60.)), constant(p.mini_gravity),
            constant(p.mini_floor));
    field<uint8_t>(b, L::player_alpha) = native % 3 == 0 ? 26 : 255;
    c.rax = 2;
    if (clock.time >= 180) {
        native = 0;
        clock.initialized = false;
        field<uint8_t>(b, L::player_alpha) = 255;
        field<uint8_t>(b, L::hurt_flag) = 0;
        state = 3;
        c.rax = 3;
    }
    jump(c, p.mini_hurt_next);
}
void fall(InlineContext& c) {
    const auto& p = target::get();
    gravity(c.rbx, clocks[c.rbx + L::player_tile].dt, constant(p.mini_gravity), constant(p.mini_exit_floor));
    jump(c, p.mini_fall_next);
}
void player_velocity(InlineContext& c) {
    const auto b = c.rbx;
    const auto dt = clocks[b + L::player_tile].dt;
    const double acceleration = scalar(c, 0), v = field<float>(b, L::player_vx);
    player_positions[b] = float(field<float>(b, L::player_x) + v * dt + acceleration * dt * (dt + 1) * .5);
    scalar(c, 0, 0, float(v + acceleration * dt));
}
void player_motion(InlineContext& c) {
    scalar(c, 5, 9, player_positions[c.rbx]);
    field<float>(c.rbx, L::player_vx) = scalar(c, 0);
}
// Integral of a bounded triangular velocity. This handles any number of
// acceleration reversals in constant time, preserving fractional motion.
double oscillator(float& velocity, float& acceleration, double dt, double bound) {
    const double v = std::clamp(double(velocity), -bound, bound), a = std::abs(double(acceleration));
    if (a == 0)
        return v * dt;
    const double period = 4 * bound, phase = acceleration >= 0 ? v + bound : 3 * bound - v;
    auto primitive = [&](double x) {
        x = std::fmod(x, period);
        return x <= 2 * bound ? x * x * .5 - bound * x : 3 * bound * x - x * x * .5 - 4 * bound * bound;
    };
    const double end = std::fmod(phase + a * dt, period);
    const double next = end < 2 * bound ? end - bound : 3 * bound - end;
    const double distance = (primitive(phase + a * dt) - primitive(phase)) / a + .5 * (next - v);
    velocity = float(next);
    acceleration = float(end < 2 * bound ? a : -a);
    return distance;
}
void enemy_intro(InlineContext& c) {
    const auto& p = target::get();
    const auto b = c.rcx;
    const auto dt = clocks[b + L::enemy_tile].dt;
    const auto bound = constant(p.mini_enemy_bound);
    auto& x = field<float>(b, L::enemy_x);
    auto& y = field<float>(b, L::enemy_y);
    x += float(oscillator(field<float>(b, L::enemy_vx), field<float>(b, L::enemy_ax), dt, bound));
    y += float(oscillator(field<float>(b, L::enemy_vy), field<float>(b, L::enemy_ay), dt, bound) -
               constant(p.mini_enemy_drift) * dt);
    if (y <= constant(p.mini_enemy_target)) {
        x = constant(p.mini_enemy_reset_x);
        y = constant(p.mini_enemy_target);
        field<uint32_t>(b, L::enemy_state) = 2;
    }
    jump(c, p.mini_enemy_next);
}
void enemy_spawn(InlineContext& c) {
    const auto b = c.rcx;
    auto& age = field<uint32_t>(b, L::enemy_spawn_age);
    auto& clock = clocks[b + L::enemy_spawn_age];
    const auto period = std::max(1U, field<uint32_t>(b, L::enemy_interval));
    clock.begin(age);
    age = clock.whole();
    c.rax = age;
    c.step32(age);
    if (age >= period) {
        clock.time = std::fmod(clock.time, double(period));
        clock.finish(0);
    } else
        clock.finish(age);
    // Native spawn runs once with current positions; obsolete volleys are not replayed.
}
void enemy_death(InlineContext& c) {
    const auto& p = target::get();
    auto& native = field<uint32_t>(c.rcx, L::enemy_death_age);
    auto& clock = clocks[c.rcx + L::enemy_death_age];
    clock.begin(native);
    native = clock.whole();
    clock.finish(native);
    c.r8 = native;
    const bool due = std::floor(clock.previous / 10) != std::floor(clock.time / 10);
    jump(c, due ? p.mini_enemy_death_due : p.mini_enemy_death_next);
}
double particle_delta(uintptr_t b) {
    return motions[b].begin(field<uint32_t>(b, L::particle_state));
}
void particle0(InlineContext& c) {
    scalar(c, 3, 0, float(scalar(c, 0) - constant(target::get().mini_particle_speed) * particle_delta(c.rcx)));
}
void particle1(InlineContext& c) {
    scalar(c, 5, 0, float(scalar(c, 0) - constant(target::get().mini_particle_speed) * particle_delta(c.rcx)));
}
void particle_motion(InlineContext& c, bool turns) {
    const auto& p = target::get();
    const auto b = c.rcx;
    double dt = particle_delta(b);
    auto& x = field<float>(b, L::particle_x);
    auto& y = field<float>(b, L::particle_y);
    auto& vx = field<float>(b, L::particle_vx);
    auto& vy = field<float>(b, L::particle_vy);
    if (turns) {
        auto& native = field<uint32_t>(b, L::particle_age);
        auto& clock = clocks[b + L::particle_age];
        clock.begin(native);
        dt = clock.dt;
        motions[b].dt = dt;
        native = clock.whole();
        clock.finish(native);
        if (clock.previous < 20 && clock.time >= 20) {
            const double before = 20 - clock.previous;
            x -= float(vx * before);
            y -= float(vy * before);
            dt -= before;
            vx = (x - constant(p.mini_particle_target)) / constant(p.mini_particle_divisor);
            vy = (y - field<float>(c.rdx, L::player_x)) / constant(p.mini_particle_divisor);
        }
    }
    x -= float(vx * dt);
    y -= float(vy * dt);
    scalar(c, 3, 0, x);
    scalar(c, turns ? 7 : 4, 0, y);
    jump(c, turns ? p.mini_particle3_next : p.mini_particle2_next);
}
void particle2(InlineContext& c) {
    particle_motion(c, false);
}
void particle3(InlineContext& c) {
    particle_motion(c, true);
}
void spin(InlineContext& c) {
    const auto& p = target::get();
    const auto b = c.rbx;
    const auto dt = motions[b].dt;
    auto& angle = field<float>(b, L::particle_angle);
    auto& velocity = field<float>(b, L::particle_spin);
    const double low = constant(p.mini_spin_low), high = constant(p.mini_spin_high), width = high - low;
    if (width > 0 && velocity != 0 && dt > 0) {
        const double position = std::clamp(double(angle) - low, 0., width);
        const double phase = velocity >= 0 ? position : 2 * width - position;
        const double end = std::fmod(phase + std::abs(velocity) * dt, 2 * width);
        angle = float(low + (end < width ? end : 2 * width - end));
        velocity = std::copysign(velocity, end < width ? 1.f : -1.f);
    }
    jump(c, p.mini_spin_next);
}
} // namespace
std::vector<InlineHook> minigame_inline_hooks() {
    const auto& p = target::get();
    return {{&p.mini_age, age},
            {&p.mini_player_atlas, player_atlas},
            {&p.mini_enemy_atlas, enemy_atlas},
            {&p.mini_debris_atlas, debris_atlas},
            {&p.mini_debris_motion, debris_motion},
            {&p.mini_bursts, bursts},
            {&p.mini_banner, banner},
            {&p.mini_hurt, hurt},
            {&p.mini_fall, fall},
            {&p.mini_player_velocity, player_velocity},
            {&p.mini_player_motion, player_motion},
            {&p.mini_enemy_intro, enemy_intro},
            {&p.mini_enemy_spawn, enemy_spawn},
            {&p.mini_enemy_death, enemy_death},
            {&p.mini_particle0, particle0},
            {&p.mini_particle1, particle1},
            {&p.mini_particle2, particle2},
            {&p.mini_particle3, particle3},
            {&p.mini_spin, spin}};
}
} // namespace gd
