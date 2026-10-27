#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace gd {
// Wall-clock consumers must not silently discard time during a render stall.
inline double valid_elapsed(double elapsed) { return std::isfinite(elapsed) && elapsed > 0 ? elapsed : 0; }
// Keep the authored speed, including its small 16.6767 ms bias, across stalls.
inline float afp_step(double elapsed) {
    return static_cast<float>(std::min(valid_elapsed(elapsed)*60.0*0.01667666807770729,
        double(std::numeric_limits<float>::max())));
}
struct FractionalTicks {
    double remainder = 0;
    uint32_t advance(double elapsed) {
        const double amount = std::min(remainder + valid_elapsed(elapsed) * 300.0,
            double(std::numeric_limits<uint32_t>::max()));
        const auto whole = static_cast<uint32_t>(std::floor(amount + 1e-9));
        remainder = std::max(0.0, amount - whole);
        return whole;
    }
    void reset() { remainder = 0; }
};
// A shared consumer advances once per main update, irrespective of how many
// aliases call it. Pauses freeze its phase; inactive scenes accrue no backlog.
struct ElapsedCounter {
    uint64_t seen = 0;
    double phase = 0;
    void reset(uint64_t frame) { seen = frame; phase = 0; }
    uint32_t advance(uint64_t frame, double elapsed, double rate, bool paused) {
        if (seen == frame) return 0;
        const bool continuous = seen && seen + 1 == frame;
        seen = frame;
        if (!continuous || paused) return 0;
        const double amount = std::min(phase + valid_elapsed(elapsed) * rate,
            double(std::numeric_limits<uint32_t>::max()));
        const auto whole = static_cast<uint32_t>(std::floor(amount + 1e-9));
        phase = std::max(0.0, amount - whole);
        return whole;
    }
};
}
