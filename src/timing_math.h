#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace gd {
// Keep the original 60 Hz animation speed, including its small 16.6767 ms bias.
inline float afp_step(double elapsed) {
    return static_cast<float>(std::clamp(elapsed, 0.001, 0.100) * 60.0 * 0.01667666807770729);
}
struct FractionalTicks {
    double remainder = 0;
    uint32_t advance(double elapsed) {
        const double amount = remainder + std::clamp(elapsed, 0.0, 0.100) * 300.0;
        const auto whole = static_cast<uint32_t>(std::floor(amount + 1e-9));
        remainder = amount - whole;
        return whole;
    }
    void reset() { remainder = 0; }
};
}
