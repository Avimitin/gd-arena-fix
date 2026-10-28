#pragma once
#include "../src/note_clock.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

static void note_clock_test() {
    for (unsigned hz : {60U, 95U, 120U, 144U, 240U}) {
        gd::NoteClock clock;
        double time = 0, bias = 0, worst = 0;
        const unsigned frames = hz * 2;
        clock.advance(1, 0, 0);
        for (unsigned i = 1; i <= frames; ++i) {
            const double dt = (1.0 / hz) * (i % 3 == 0 ? 1.08 : (i % 3 == 1 ? 0.94 : 0.98));
            time += dt;
            const double raw = time * 300.0;
            const double out = clock.advance(i + 1, raw, dt);
            bias += out - raw;
            worst = std::max(worst, std::abs(out - raw));
            check(std::abs(out - raw) < 1e-7, "continuous note cursor has no steady phase lag");
        }
        check(std::abs(bias / frames) < 1e-8 && worst < 1e-7, "continuous note cursor preserves phase");
    }

    // Compare per-render-frame speed error against the unfiltered stepped cursor.
    // Include the measured 512-sample audio period (about 93.75 Hz) and a
    // 4 ms source alongside the game's 60 Hz and measured 1024-sample clocks.
    for (unsigned hz : {60U, 95U, 120U, 144U, 160U, 240U})
        for (double quantum : {1.0 / 60.0, 512.0 / 48000.0, 1024.0 / 48000.0, 0.004})
            for (bool jittered : {false, true}) {
                gd::NoteClock clock;
                double time = 0, direct = 0, filtered = 0, direct_sq = 0, smooth_sq = 0, bias = 0;
                unsigned count = 0;
                clock.advance(1, 0, 0);
                for (unsigned i = 1; i <= hz * 3; ++i) {
                    const double dt = (1.0 / hz) * (jittered ? (i % 3 == 0 ? 1.08 : (i % 3 == 1 ? 0.94 : 0.98)) : 1.0);
                    time += dt;
                    const double raw = std::floor(time / quantum + 1e-7) * quantum * 300.0;
                    const double out = clock.advance(i + 1, raw, dt);
                    const double de = (raw - direct) - 300.0 * dt;
                    const double se = (out - filtered) - 300.0 * dt;
                    direct_sq += de * de;
                    smooth_sq += se * se;
                    bias += out - raw;
                    ++count;
                    direct = raw;
                    filtered = out;
                    check(std::abs(out - raw) <= gd::NoteClock::max_correction + 1e-9,
                          "note clock visual phase correction stays within 25 ms");
                }
                if (direct_sq < 1e-10)
                    check(smooth_sq < 1e-10, "aligned 60 Hz cursor remains jitter free");
                else
                    check(smooth_sq < direct_sq * 0.90, "note clock reduces stepped-source speed jitter");
                check(std::abs(bias / count) < 0.25, "stepped-source mean phase drift stays below one millisecond");
            }

    gd::NoteClock clock;
    check(clock.advance(10, 0, 0) == 0, "note clock initializes at raw phase");
    const double once = clock.advance(11, 5, 1.0 / 60.0);
    check(clock.advance(11, 5, 1) == once, "same-frame note clock call never advances or changes phase");
    check(clock.advance(11, 99, 1) == 99, "same-frame source change reanchors instead of using stale phase");
    check(clock.advance(13, 6, 1.0 / 60.0) == 6, "skipped frame reanchors immediately");
    check(clock.advance(14, 5, 1.0 / 60.0) == 5, "backward seek reanchors immediately");
    check(clock.advance(15, 100, 1.0 / 60.0) == 100, "large forward seek reanchors immediately");
    check(clock.advance(16, 101, 0.100) == 101, "long render stall reanchors immediately");
    check(clock.advance(17, 0, 1.0 / 60.0) == 0, "zero seek reanchors immediately");
    check(clock.advance(18, 0, 1.0 / 60.0) == 0, "repeated startup zero does not freewheel");
    check(clock.advance(30, 500, 1.0 / 60.0) == 500, "inactive frames do not accumulate a catch-up ramp");

    gd::NoteClock correction;
    correction.advance(1, 0, 0);
    const double bounded_seek = correction.advance(2, 50, 1.0 / 60.0);
    check(std::abs((50 - bounded_seek) - gd::NoteClock::max_correction) < 1e-9,
          "sub-threshold seek cannot leave more than 25 ms visual phase error");

    clock.reset();
    clock.advance(1, 0, 0);
    for (unsigned i = 1; i <= 6; ++i)
        clock.advance(i + 1, 4, 1.0 / 60.0);
    const double before_expiry = clock.output;
    check(before_expiry > 4 && before_expiry <= 4 + gd::NoteClock::max_correction,
          "unchanged cursor may lead only within its bounded grace period");
    check(clock.advance(8, 4, 1.0 / 60.0) == 4, "100 ms held cursor reanchors and stops freewheeling");
    check(clock.advance(9, 4, 1.0 / 60.0) == 4, "held cursor remains reanchored");

    check(clock.advance(10, 10, std::nan("")) == 10, "invalid elapsed interval reanchors immediately");
    const double resumed = clock.advance(11, 12, 1.0 / 60.0);
    const double expected = 15.0 + (12.0 - 15.0) * -std::expm1(-(1.0 / 60.0) / gd::NoteClock::tau);
    check(std::abs(resumed - expected) < 1e-9, "valid sample predicts from the invalid-interval raw-phase reanchor");
    puts("PASS: note clock continuous phase, stepped-source smoothing, lead cap and discontinuities");
}
