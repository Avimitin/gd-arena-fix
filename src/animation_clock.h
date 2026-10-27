#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "timing_math.h"

namespace gd {
// An animation gets an immediate first update, then advances at 60 authored
// frames/second. Inactive objects do not accumulate a backlog.
struct AnimationClock {
    uint64_t seen = 0;
    double phase = 0;
    unsigned advance(uint64_t frame, double elapsed, bool restart = false) {
        if (seen == frame && !restart) return 0;
        if (restart || !seen || seen + 1 != frame) {
            seen = frame;
            phase = 0;
            return 1;
        }
        seen = frame;
        const double amount = std::min(phase + valid_elapsed(elapsed) * 60.0,double(UINT32_MAX));
        const auto count = static_cast<unsigned>(std::floor(amount + 1e-9));
        phase = std::max(0.0, amount - count);
        return count;
    }
};
inline int effect_counter(int value, unsigned ticks) {
    if (!ticks || !value) return value;
    return value>=8 || uint64_t(ticks)>=uint64_t(9-int64_t(value)) ? 0 : value+static_cast<int>(ticks);
}
// Number of native signed counter comparisons that pass after `ticks` INCs,
// including a possible 32-bit wrap. The atlas timer has no per-cell delay
// reset; once its startup delay expires, each passing tick advances a cell.
inline uint64_t sprite_steps(int32_t before,uint32_t ticks,int32_t threshold) {
    constexpr uint64_t cycle=uint64_t(1)<<32,half=cycle/2;
    const auto prefix=[&](uint64_t n) {
        if (threshold>=0) {
            const uint64_t start=static_cast<uint32_t>(threshold);
            return n<=start ? uint64_t(0) : std::min(n,half)-std::min(n,start);
        }
        const uint64_t start=static_cast<uint32_t>(threshold);
        return std::min(n,half)+(n>start?n-start:0);
    };
    const auto count=[&](uint64_t n) {return (n/cycle)*prefix(cycle)+prefix(n%cycle);};
    const uint64_t start=uint64_t(static_cast<uint32_t>(before))+1;
    return count(start+ticks)-count(start);
}
}
