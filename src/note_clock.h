#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gd {
// Render-only phase estimate for a 300-tick/s note clock. Call once per frame;
// reset when the source or playback mode changes. The output is in raw ticks.
struct NoteClock {
    static constexpr double ticks_per_second = 300.0;
    static constexpr double max_correction = ticks_per_second * 0.025;
    static constexpr double tau = 0.040;
    static constexpr double max_elapsed = 0.100;
    static constexpr double held_reanchor = 0.100;

    uint64_t frame = 0;
    bool has_frame = false, initialized = false;
    double output = 0, raw = 0, held = 0;

    void reset() {
        frame = 0;
        has_frame = initialized = false;
        output = raw = held = 0;
    }

    double advance(uint64_t serial, double raw_ticks, double elapsed_seconds) {
        if (has_frame && serial == frame && raw_ticks == raw)
            return output;
        const bool sequential = has_frame && frame != UINT64_MAX && serial == frame + 1;
        has_frame = true;
        frame = serial;
        if (!std::isfinite(raw_ticks) || raw_ticks < 0) {
            initialized = false;
            return output;
        }
        if (!initialized || !sequential || raw_ticks == 0 || !std::isfinite(elapsed_seconds) || elapsed_seconds <= 0 ||
            elapsed_seconds >= max_elapsed || raw_ticks < raw || raw_ticks - raw > 75.0) {
            output = raw = raw_ticks;
            held = 0;
            initialized = true;
            return output;
        }

        const double dt = elapsed_seconds;
        if (raw_ticks == raw)
            held += dt;
        else
            held = 0;
        raw = raw_ticks;
        if (held >= held_reanchor - 1e-9) {
            output = raw_ticks;
            return output;
        }

        const double predicted = output + ticks_per_second * dt;
        const double correction = -std::expm1(-dt / tau);
        output = predicted + (raw_ticks - predicted) * correction;
        output = std::max(std::max(0.0, raw_ticks - max_correction), std::min(output, raw_ticks + max_correction));
        return output;
    }
};
} // namespace gd
