#pragma once
#include "timing_math.h"

namespace gd {
// Native state machines write their own reset values. Detect those writes,
// retain fractional time, and never decrement twice in one rendered frame.
struct ElapsedNativeCounter {
    ElapsedCounter clock;
    uint32_t expected = 0, identity = 0;
    bool initialized = false;
    bool restarted(uint32_t value, uint32_t state, uint64_t serial) const {
        return !initialized || expected != value || identity != state || clock.seen + 1 < serial;
    }
    void reset(uint32_t value, uint32_t state, uint64_t serial) {
        clock.reset(serial);
        initialized = true;
        expected = value;
        identity = state;
    }
    uint32_t advance(uint32_t value, uint32_t state, uint64_t serial, double elapsed) {
        if (restarted(value, state, serial))
            reset(value, state, serial);
        initialized = true;
        identity = state;
        return clock.advance(serial, elapsed, 60, false);
    }
    void finish(uint32_t value) {
        expected = value;
    }
};
// A native scene can change state or seek its age while executing. Keep an
// elapsed interval for arithmetic and an epoch for one-shot event crossings.
// Events not visited on a skipped branch remain eligible in the same epoch.
struct SceneTimeline {
    struct Event {
        uint64_t epoch = 0, call = 0;
        bool fired = false, result = false;
    };
    ElapsedCounter clock;
    uint64_t epoch = 0, call = 0;
    uint32_t value = 0, state = 0, origin = 0, previous = 0;
    bool initialized = false, first = false;

    void seek(uint32_t age, uint32_t identity, uint64_t serial) {
        value = previous = origin = age;
        state = identity;
        initialized = true;
        first = true;
        ++epoch;
        clock.reset(serial);
    }
    uint32_t begin(uint32_t age, uint32_t identity, uint64_t serial, double elapsed) {
        ++call;
        first = false;
        if (!initialized || age != value || identity != state || clock.seen + 1 < serial)
            seek(age, identity, serial);
        previous = age;
        value = static_cast<uint32_t>(
            std::min(uint64_t(UINT32_MAX), uint64_t(age) + clock.advance(serial, elapsed, 60, false)));
        return value;
    }
    void synchronize(uint32_t age, uint32_t identity, uint64_t serial) {
        if (age != value || identity != state)
            seek(age, identity, serial);
    }
    bool event(Event& gate, uint32_t point) const {
        if (gate.epoch != epoch)
            gate = {epoch, 0, false, false};
        if (gate.call == call)
            return gate.result; // Per-player loop in one call.
        gate.call = call;
        gate.result = !gate.fired && point >= origin && point <= value;
        gate.fired |= gate.result;
        return gate.result;
    }
    uint32_t steps_from(uint32_t start) const {
        const uint64_t low = first ? previous : uint64_t(previous) + 1;
        const uint64_t begin = std::max(low, uint64_t(start));
        return begin <= value ? static_cast<uint32_t>(uint64_t(value) - begin + 1) : 0;
    }
    uint32_t periods(uint32_t start, uint32_t period) const {
        const uint64_t low = first ? previous : uint64_t(previous) + 1;
        const uint64_t begin = std::max(low, uint64_t(start));
        if (!period || begin > value)
            return 0;
        const uint64_t last = (uint64_t(value) - start) / period;
        const uint64_t before = begin > start ? (begin - 1 - start) / period + 1 : 0;
        return static_cast<uint32_t>(last + 1 - before);
    }
};
} // namespace gd
